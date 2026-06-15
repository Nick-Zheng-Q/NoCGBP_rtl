// cavity_builder.cc
// Unit test for cavity_builder

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vcavity_builder_top.h"

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

static void tick(Vcavity_builder_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vcavity_builder_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

static void send_beat(Vcavity_builder_top* d, int kind, uint32_t data[16], int last) {
  d->beat_valid_i = 1;
  d->beat_kind_i = kind;
  d->beat_last_i = last;
  for (int i = 0; i < 16; i++) d->beat_data_flat_i[i] = data[i];

  int cnt = 0;
  while (!d->beat_ready_o) {
    tick(d);
    cnt++;
    if (cnt > 100) {
      std::fprintf(stderr, "  [FAIL] beat_ready_o stuck low\n");
      return;
    }
  }
  // beat_ready_o is high: accept on this tick
  tick(d);
  d->beat_valid_i = 0;
  d->beat_last_i = 0;
}

// ============================================================
static int test_scalar(Vcavity_builder_top* d) {
  std::printf("\n--- scalar ---\n");
  reset(d);

  d->dim_o_i = 0; // DIM_1 -> d_o=1, e_o=2
  d->cav_ready_i = 1;
  d->start_valid_i = 1;
  tick(d);
  d->start_valid_i = 0;

  // OST_CAV_FACTOR_O: eta=3, L=5
  uint32_t factor[16] = {0};
  factor[0] = f2u(3.0f);
  factor[1] = f2u(5.0f);
  send_beat(d, 1, factor, 1);

  // OST_CAV_BELIEF_O: eta=1, L=2
  uint32_t belief[16] = {0};
  belief[0] = f2u(1.0f);
  belief[1] = f2u(2.0f);
  send_beat(d, 2, belief, 1);

  // OST_CAV_OLD_TO_O: eta=2, L=1
  uint32_t old[16] = {0};
  old[0] = f2u(2.0f);
  old[1] = f2u(1.0f);
  send_beat(d, 3, old, 1);

  int cycles = 0;
  while (!d->cav_valid_o && cycles < 2000) {
    tick(d);
    cycles++;
  }

  check(d->cav_valid_o == 1, "scalar: valid_o");
  check(d->stream_error_o == 0, "scalar: no stream error");
  float eta = u2f(d->cav_eta_flat_o[0]);
  float L   = u2f(d->cav_L_flat_o[0]);
  std::printf("  scalar: eta=%g L=%g\n", eta, L);
  check(std::fabs(eta - 2.0f) < 0.01f, "scalar: eta=2");
  check(std::fabs(L - 6.0f) < 0.01f, "scalar: L=6");
  return error_count;
}

// ============================================================
static int test_3x3(Vcavity_builder_top* d) {
  std::printf("\n--- 3x3 ---\n");
  reset(d);

  d->dim_o_i = 1; // DIM_3 -> d_o=3, e_o=9
  d->cav_ready_i = 1;
  d->start_valid_i = 1;
  tick(d);
  d->start_valid_i = 0;

  // OST_CAV_FACTOR_O: eta=[1,2,3], L_ii packed=[1,0,0,1,0,1]
  uint32_t factor[16] = {0};
  factor[0] = f2u(1.0f); factor[1] = f2u(2.0f); factor[2] = f2u(3.0f);
  factor[3] = f2u(1.0f); factor[4] = f2u(0.0f); factor[5] = f2u(0.0f);
  factor[6] = f2u(1.0f); factor[7] = f2u(0.0f); factor[8] = f2u(1.0f);
  send_beat(d, 1, factor, 1);

  // OST_CAV_BELIEF_O: eta=[1,1,1], L_ii packed=[1,0,0,1,0,1]
  uint32_t belief[16] = {0};
  belief[0] = f2u(1.0f); belief[1] = f2u(1.0f); belief[2] = f2u(1.0f);
  belief[3] = f2u(1.0f); belief[4] = f2u(0.0f); belief[5] = f2u(0.0f);
  belief[6] = f2u(1.0f); belief[7] = f2u(0.0f); belief[8] = f2u(1.0f);
  send_beat(d, 2, belief, 1);

  // OST_CAV_OLD_TO_O: eta=[0,1,0], L_ii packed=[0,0,0,0,0,0]
  uint32_t old[16] = {0};
  old[0] = f2u(0.0f); old[1] = f2u(1.0f); old[2] = f2u(0.0f);
  send_beat(d, 3, old, 1);

  int cycles = 0;
  while (!d->cav_valid_o && cycles < 2000) {
    tick(d);
    cycles++;
  }

  check(d->cav_valid_o == 1, "3x3: valid_o");
  check(d->stream_error_o == 0, "3x3: no stream error");
  std::printf("  3x3: eta=[%g,%g,%g]\n",
              u2f(d->cav_eta_flat_o[0]), u2f(d->cav_eta_flat_o[1]), u2f(d->cav_eta_flat_o[2]));
  check(std::fabs(u2f(d->cav_eta_flat_o[0]) - 2.0f) < 0.01f, "3x3: eta0=2");
  check(std::fabs(u2f(d->cav_eta_flat_o[1]) - 2.0f) < 0.01f, "3x3: eta1=2");
  check(std::fabs(u2f(d->cav_eta_flat_o[2]) - 4.0f) < 0.01f, "3x3: eta2=4");
  check(std::fabs(u2f(d->cav_L_flat_o[0]) - 2.0f) < 0.01f, "3x3: L00=2");
  check(std::fabs(u2f(d->cav_L_flat_o[1]) - 0.0f) < 0.01f, "3x3: L01=0");
  check(std::fabs(u2f(d->cav_L_flat_o[2]) - 0.0f) < 0.01f, "3x3: L02=0");
  check(std::fabs(u2f(d->cav_L_flat_o[3]) - 2.0f) < 0.01f, "3x3: L11=2");
  check(std::fabs(u2f(d->cav_L_flat_o[4]) - 0.0f) < 0.01f, "3x3: L12=0");
  check(std::fabs(u2f(d->cav_L_flat_o[5]) - 2.0f) < 0.01f, "3x3: L22=2");
  return error_count;
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vcavity_builder_top;

  std::printf("========================================\n");
  std::printf("cavity_builder unit tests\n");
  std::printf("========================================\n");

  int start_errors = error_count;
  test_scalar(dut);
  test_3x3(dut);

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
