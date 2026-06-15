// op_decoder.cc
// Unit test for op_decoder

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vop_decoder_top.h"

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

static void tick(Vop_decoder_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vop_decoder_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

void test_op_decoder(Vop_decoder_top* d) {
  std::printf("\n=== op_decoder ===\n");
  std::fflush(stdout);

  // Scalar factor
  d->od_cmd_op_i = 0;
  d->od_cmd_factor_type_i = 0;
  d->od_cmd_dim_i_i = 0;
  d->od_cmd_dim_o_i = 0;
  d->od_cmd_direction_i = 0;
  d->eval();

  check(d->od_is_msg_o == 1, "scalar: is_msg");
  check(d->od_is_belief_o == 0, "scalar: not belief");
  check(d->od_legal_o == 1, "scalar: legal");
  check(d->od_dim_i_val_o == 1, "scalar: dim_i=1");
  check(d->od_e_i_o == 2, "scalar: E(1)=2");
  check(d->od_nrhs_o == 2, "scalar: nrhs=2");

  // SE3
  d->od_cmd_factor_type_i = 3;
  d->od_cmd_dim_i_i = 2;
  d->od_cmd_dim_o_i = 2;
  d->eval();
  check(d->od_legal_o == 1, "SE3: legal");
  check(d->od_e_i_o == 27, "SE3: E(6)=27");

  // BA dir=0
  d->od_cmd_factor_type_i = 2;
  d->od_cmd_dim_i_i = 2;
  d->od_cmd_dim_o_i = 1;
  d->od_cmd_direction_i = 0;
  d->eval();
  check(d->od_legal_o == 1, "BA dir=0: legal");

  // BA dir=0 wrong dims
  d->od_cmd_dim_i_i = 1;
  d->od_cmd_dim_o_i = 2;
  d->eval();
  check(d->od_legal_o == 0, "BA wrong: illegal");

  // BA dir=1
  d->od_cmd_dim_i_i = 1;
  d->od_cmd_dim_o_i = 2;
  d->od_cmd_direction_i = 1;
  d->eval();
  check(d->od_legal_o == 1, "BA dir=1: legal");

  // Belief
  d->od_cmd_op_i = 1;
  d->od_cmd_dim_i_i = 2;
  d->eval();
  check(d->od_is_belief_o == 1, "belief: is_belief");
  check(d->od_legal_o == 1, "belief: legal");

  // Relin V0 disabled
  d->od_cmd_op_i = 2;
  d->eval();
  check(d->od_legal_o == 0, "relin: illegal");
  check(d->od_illegal_op_o == 1, "relin: illegal_op");
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vop_decoder_top;

  std::printf("========================================\n");
  std::printf("op_decoder unit tests\n");
  std::printf("========================================\n");

  test_op_decoder(dut);

  std::printf("\n========================================\n");
  std::printf("Test Summary: %d tests, %d errors\n", test_count, error_count);
  if (error_count == 0)
    std::printf("ALL TESTS PASSED!\n");
  else
    std::printf("SOME TESTS FAILED!\n");
  std::printf("========================================\n");

  delete dut;
  return error_count > 0 ? 1 : 0;
}
