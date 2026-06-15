// rhs_builder_for_message.cc
// Unit test for rhs_builder_for_message

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vrhs_builder_for_message_top.h"

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

static void tick(Vrhs_builder_for_message_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vrhs_builder_for_message_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

void test_rhs_builder(Vrhs_builder_for_message_top* d) {
  std::printf("\n=== rhs_builder_for_message ===\n");

  // SE2: dim_i=3, dim_o=3
  d->rhs_dim_i_i = 1; // DIM_3 (enum value 1)
  d->rhs_dim_o_i = 1; // DIM_3 (enum value 1)

  // L_io = identity 3x3
  // L_io_dense_flat_i is VlWide<36> = 36 uint32_t words
  for (int i = 0; i < 36; i++) d->rhs_L_io_flat_i[i] = 0;
  d->rhs_L_io_flat_i[0] = f2u(1.0f);
  d->rhs_L_io_flat_i[4] = f2u(1.0f);
  d->rhs_L_io_flat_i[8] = f2u(1.0f);

  // cav_eta = [1, 2, 3]
  d->rhs_cav_eta_flat_i[0] = f2u(1.0f);
  d->rhs_cav_eta_flat_i[1] = f2u(2.0f);
  d->rhs_cav_eta_flat_i[2] = f2u(3.0f);
  d->eval();

  check(d->rhs_nrhs_o == 4, "SE2: nrhs=4");

  // B flat: stride = GBP_MAX_RHS = 7
  // B[0,0]=B[0], B[0,1]=B[1], ..., B[0,3]=B[3]
  // B[1,0]=B[7], B[1,1]=B[8], ..., B[1,3]=B[10]
  // B[2,0]=B[14], B[2,1]=B[15], ..., B[2,3]=B[17]
  check(u2f(d->rhs_B_flat_o[0]) == 1.0f, "SE2: B[0,0]=1");
  check(u2f(d->rhs_B_flat_o[1]) == 0.0f, "SE2: B[0,1]=0");
  check(u2f(d->rhs_B_flat_o[3]) == 1.0f, "SE2: B[0,3]=cav[0]=1");
  check(u2f(d->rhs_B_flat_o[10]) == 2.0f, "SE2: B[1,3]=cav[1]=2");
  check(u2f(d->rhs_B_flat_o[17]) == 3.0f, "SE2: B[2,3]=cav[2]=3");

  // Scalar
  d->rhs_dim_i_i = 0;
  d->rhs_dim_o_i = 0;
  d->rhs_L_io_flat_i[0] = f2u(5.0f);
  d->rhs_cav_eta_flat_i[0] = f2u(7.0f);
  d->eval();

  check(d->rhs_nrhs_o == 2, "scalar: nrhs=2");
  check(u2f(d->rhs_B_flat_o[0]) == 5.0f, "scalar: B[0,0]=5");
  check(u2f(d->rhs_B_flat_o[1]) == 7.0f, "scalar: B[0,1]=7");
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vrhs_builder_for_message_top;

  std::printf("========================================\n");
  std::printf("rhs_builder_for_message unit tests\n");
  std::printf("========================================\n");

  test_rhs_builder(dut);

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
