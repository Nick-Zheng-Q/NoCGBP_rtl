// belief_operand_unpacker.cc
// Unit test for belief_operand_unpacker

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vbelief_operand_unpacker_top.h"

static int error_count = 0;
static int test_count = 0;

static void check(bool condition, const char* msg) {
  test_count++;
  if (!condition) {
    error_count++;
    std::fprintf(stderr, "  [FAIL] %s\n", msg);
  } else {
    std::fprintf(stdout, "  [PASS] %s\n", msg);
  }
}

static uint32_t f2u(float f) {
  union { float f; uint32_t u; } c; c.f = f; return c.u;
}
static float u2f(uint32_t u) {
  union { float f; uint32_t u; } c; c.u = u; return c.f;
}

static void tick(Vbelief_operand_unpacker_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vbelief_operand_unpacker_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

enum {
  OST_BELIEF_PRIOR = 4,
  OST_BELIEF_MSG   = 5
};

static void clear_beat(Vbelief_operand_unpacker_top* d) {
  d->beat_valid_i = 0;
  d->beat_kind_i = 0;
  d->beat_last_i = 0;
  for (int i = 0; i < 16; i++) d->beat_data_flat_i[i] = 0;
}

static void send_beat(Vbelief_operand_unpacker_top* d, int kind,
                      uint32_t data[16], int last, uint32_t op_id, uint16_t beat_idx) {
  d->beat_valid_i = 1;
  d->beat_kind_i = kind;
  d->beat_last_i = last;
  d->beat_op_id_i = op_id;
  d->beat_beat_idx_i = beat_idx;
  for (int i = 0; i < 16; i++) d->beat_data_flat_i[i] = data[i];

  d->eval();
  int cnt = 0;
  while (!d->beat_ready_o && cnt < 100) {
    tick(d);
    cnt++;
  }
  if (!d->beat_ready_o) {
    std::fprintf(stderr, "  [FAIL] beat_ready_o stuck low\n");
    clear_beat(d);
    return;
  }
  tick(d); // accept the beat
  clear_beat(d);
}

// ============================================================
static int test_scalar(Vbelief_operand_unpacker_top* d) {
  std::printf("\n--- scalar ---\n");
  reset(d);

  d->dim_i = 0;      // DIM_1
  d->degree_i = 2;
  d->op_id_i = 0x1234;
  d->prior_ready_i = 1;
  d->msg_ready_i = 1;

  // prior: eta=1, L=2, mu_old=3  (E(1)+1 = 3 scalars)
  uint32_t prior[16] = {0};
  prior[0] = f2u(1.0f);
  prior[1] = f2u(2.0f);
  prior[2] = f2u(3.0f);
  send_beat(d, OST_BELIEF_PRIOR, prior, 0, 0x1234, 0);

  int cycles = 0;
  while (!d->prior_valid_o && cycles < 100) { tick(d); cycles++; }
  check(d->prior_valid_o == 1, "scalar: prior_valid_o");
  check(u2f(d->prior_eta_flat_o[0]) == 1.0f, "scalar: prior_eta=1");
  check(u2f(d->prior_L_flat_o[0]) == 2.0f, "scalar: prior_L=2");
  check(u2f(d->prior_mu_old_flat_o[0]) == 3.0f, "scalar: prior_mu_old=3");

  // consume prior
  tick(d);

  // two messages, each E(1)=2 scalars: (10,11) and (20,21)
  // V0: one message per OST_BELIEF_MSG beat, last=1 on final beat.
  uint32_t msg0[16] = {0};
  msg0[0] = f2u(10.0f); msg0[1] = f2u(11.0f);
  send_beat(d, OST_BELIEF_MSG, msg0, 0, 0x1234, 0);

  // message 1
  cycles = 0;
  while (!d->msg_valid_o && cycles < 100) { tick(d); cycles++; }
  check(d->msg_valid_o == 1, "scalar: msg1_valid_o");
  check(d->msg_last_o == 0, "scalar: msg1_not_last");
  check(u2f(d->msg_eta_flat_o[0]) == 10.0f, "scalar: msg1_eta=10");
  check(u2f(d->msg_L_flat_o[0]) == 11.0f, "scalar: msg1_L=11");
  tick(d); // consume msg1

  uint32_t msg1[16] = {0};
  msg1[0] = f2u(20.0f); msg1[1] = f2u(21.0f);
  send_beat(d, OST_BELIEF_MSG, msg1, 1, 0x1234, 1);

  // message 2
  cycles = 0;
  while (!d->msg_valid_o && cycles < 100) { tick(d); cycles++; }
  check(d->msg_valid_o == 1, "scalar: msg2_valid_o");
  check(d->msg_last_o == 1, "scalar: msg2_last");
  check(u2f(d->msg_eta_flat_o[0]) == 20.0f, "scalar: msg2_eta=20");
  check(u2f(d->msg_L_flat_o[0]) == 21.0f, "scalar: msg2_L=21");
  tick(d); // consume msg2

  check(d->stream_error_o == 0, "scalar: no stream error");
  return error_count;
}

// ============================================================
static int test_3x3(Vbelief_operand_unpacker_top* d) {
  std::printf("\n--- 3x3 ---\n");
  reset(d);

  d->dim_i = 1;      // DIM_3
  d->degree_i = 1;
  d->op_id_i = 0xABCD;
  d->prior_ready_i = 1;
  d->msg_ready_i = 1;

  // prior: eta=[1,2,3], L=[1,0,0,1,0,1], mu_old=[4,5,6] -> 12 scalars
  uint32_t prior[16] = {0};
  for (int i = 0; i < 3; i++) prior[i] = f2u(float(i + 1));
  prior[3] = f2u(1.0f); prior[4] = f2u(0.0f); prior[5] = f2u(0.0f);
  prior[6] = f2u(1.0f); prior[7] = f2u(0.0f); prior[8] = f2u(1.0f);
  for (int i = 0; i < 3; i++) prior[9 + i] = f2u(float(i + 4));
  send_beat(d, OST_BELIEF_PRIOR, prior, 0, 0xABCD, 0);

  int cycles = 0;
  while (!d->prior_valid_o && cycles < 100) { tick(d); cycles++; }
  check(d->prior_valid_o == 1, "3x3: prior_valid_o");
  check(std::fabs(u2f(d->prior_eta_flat_o[0]) - 1.0f) < 0.001f, "3x3: prior_eta0");
  check(std::fabs(u2f(d->prior_L_flat_o[0]) - 1.0f) < 0.001f, "3x3: prior_L00");
  check(std::fabs(u2f(d->prior_mu_old_flat_o[2]) - 6.0f) < 0.001f, "3x3: prior_mu_old2");
  tick(d);

  // msg: eta=[1,1,1], L=[2,0,0,2,0,2] -> 9 scalars
  uint32_t msg[16] = {0};
  for (int i = 0; i < 3; i++) msg[i] = f2u(1.0f);
  msg[3] = f2u(2.0f); msg[4] = f2u(0.0f); msg[5] = f2u(0.0f);
  msg[6] = f2u(2.0f); msg[7] = f2u(0.0f); msg[8] = f2u(2.0f);
  send_beat(d, OST_BELIEF_MSG, msg, 1, 0xABCD, 0);

  cycles = 0;
  while (!d->msg_valid_o && cycles < 100) { tick(d); cycles++; }
  check(d->msg_valid_o == 1, "3x3: msg_valid_o");
  check(d->msg_last_o == 1, "3x3: msg_last");
  check(std::fabs(u2f(d->msg_eta_flat_o[0]) - 1.0f) < 0.001f, "3x3: msg_eta0");
  check(std::fabs(u2f(d->msg_L_flat_o[0]) - 2.0f) < 0.001f, "3x3: msg_L00");
  check(std::fabs(u2f(d->msg_L_flat_o[3]) - 2.0f) < 0.001f, "3x3: msg_L11");
  tick(d);

  check(d->stream_error_o == 0, "3x3: no stream error");
  return error_count;
}

// ============================================================
static int test_6x6(Vbelief_operand_unpacker_top* d) {
  std::printf("\n--- 6x6 ---\n");
  reset(d);

  d->dim_i = 2;      // DIM_6
  d->degree_i = 1;
  d->op_id_i = 0xBEEF;
  d->prior_ready_i = 1;
  d->msg_ready_i = 1;

  // prior: E(6)+6 = 33 scalars -> 3 beats
  uint32_t prior0[16] = {0}, prior1[16] = {0}, prior2[16] = {0};
  for (int i = 0; i < 16; i++) prior0[i] = f2u(float(i));
  for (int i = 0; i < 16; i++) prior1[i] = f2u(float(i + 16));
  prior2[0] = f2u(32.0f);
  send_beat(d, OST_BELIEF_PRIOR, prior0, 0, 0xBEEF, 0);
  send_beat(d, OST_BELIEF_PRIOR, prior1, 0, 0xBEEF, 1);
  send_beat(d, OST_BELIEF_PRIOR, prior2, 0, 0xBEEF, 2);

  int cycles = 0;
  while (!d->prior_valid_o && cycles < 200) { tick(d); cycles++; }
  check(d->prior_valid_o == 1, "6x6: prior_valid_o");
  check(std::fabs(u2f(d->prior_eta_flat_o[0]) - 0.0f) < 0.001f, "6x6: prior_eta0");
  check(std::fabs(u2f(d->prior_mu_old_flat_o[5]) - 32.0f) < 0.001f, "6x6: prior_mu_old5");
  check(std::fabs(u2f(d->prior_L_flat_o[5]) - 11.0f) < 0.001f, "6x6: prior_L5");
  check(std::fabs(u2f(d->prior_L_flat_o[6]) - 12.0f) < 0.001f, "6x6: prior_L6");
  check(std::fabs(u2f(d->prior_L_flat_o[15]) - 21.0f) < 0.001f, "6x6: prior_L15");
  tick(d);

  // msg: E(6)=27 scalars -> 2 beats
  uint32_t msg0[16] = {0}, msg1[16] = {0};
  for (int i = 0; i < 16; i++) msg0[i] = f2u(float(i + 100));
  for (int i = 0; i < 11; i++) msg1[i] = f2u(float(i + 116));
  send_beat(d, OST_BELIEF_MSG, msg0, 0, 0xBEEF, 0);
  send_beat(d, OST_BELIEF_MSG, msg1, 1, 0xBEEF, 1);

  cycles = 0;
  while (!d->msg_valid_o && cycles < 200) { tick(d); cycles++; }
  check(d->msg_valid_o == 1, "6x6: msg_valid_o");
  check(d->msg_last_o == 1, "6x6: msg_last");
  check(std::fabs(u2f(d->msg_eta_flat_o[0]) - 100.0f) < 0.001f, "6x6: msg_eta0");
  check(std::fabs(u2f(d->msg_L_flat_o[20]) - 126.0f) < 0.001f, "6x6: msg_L20");
  tick(d);

  check(d->stream_error_o == 0, "6x6: no stream error");
  return error_count;
}

// ============================================================
static int test_stream_error(Vbelief_operand_unpacker_top* d) {
  std::printf("\n--- stream error ---\n");
  reset(d);

  d->dim_i = 0;
  d->degree_i = 1;
  d->op_id_i = 0x1234;
  d->prior_ready_i = 1;
  d->msg_ready_i = 1;

  // First beat is MSG instead of PRIOR -> error
  uint32_t data[16] = {0};
  data[0] = f2u(1.0f);
  send_beat(d, OST_BELIEF_MSG, data, 0, 0x1234, 0);

  check(d->stream_error_o == 1, "error: unexpected kind");

  reset(d);
  d->dim_i = 0;
  d->degree_i = 1;
  d->op_id_i = 0x1234;
  d->prior_ready_i = 1;
  d->msg_ready_i = 1;

  // op_id mismatch
  send_beat(d, OST_BELIEF_PRIOR, data, 0, 0xDEAD, 0);
  check(d->stream_error_o == 1, "error: op_id mismatch");

  return error_count;
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vbelief_operand_unpacker_top;

  std::printf("========================================\n");
  std::printf("belief_operand_unpacker unit tests\n");
  std::printf("========================================\n");

  int start_errors = error_count;
  test_scalar(dut);
  test_3x3(dut);
  test_6x6(dut);
  test_stream_error(dut);

  std::printf("\n========================================\n");
  std::printf("Test Summary: %d tests, %d errors\n", test_count, error_count - start_errors);
  if (error_count == start_errors)
    std::printf("ALL TESTS PASSED!\n");
  else
    std::printf("SOME TESTS FAILED!\n");
  std::printf("========================================\n");

  delete dut;
  return error_count > start_errors ? 1 : 0;
}
