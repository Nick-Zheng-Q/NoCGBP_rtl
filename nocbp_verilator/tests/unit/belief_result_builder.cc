// belief_result_builder.cc
// Unit test for belief_result_builder

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vbelief_result_builder_top.h"

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

static void tick(Vbelief_result_builder_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vbelief_result_builder_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

void test_belief_result_builder(Vbelief_result_builder_top* d) {
  std::printf("\n=== belief_result_builder ===\n");

  reset(d);

  d->brb_valid_i = 1;
  d->brb_dim_i = 0;
  d->brb_acc_eta_flat_i[0] = f2u(10.0f);
  d->brb_acc_L_flat_i[0] = f2u(4.0f);
  d->brb_mu_old_flat_i[0] = f2u(1.0f);
  d->brb_solve_dim_i = 0;
  d->brb_solve_X_flat_i[0] = f2u(2.5f);
  d->brb_solve_fail_i = 0;
  d->brb_solve_regularized_i = 0;
  d->brb_solve_nan_guard_i = 0;
  d->brb_solve_min_pivot_i = f2u(4.0f);
  d->brb_ready_i = 1;
  // Wait for valid output
  for (int i = 0; i < 100; i++) {
    tick(d);
    if (d->brb_valid_o) break;
  }

  check(d->brb_valid_o == 1, "BRB: valid");
  check(u2f(d->brb_result_eta_flat_o[0]) == 10.0f, "BRB: eta=10");
  check(u2f(d->brb_result_L_flat_o[0]) == 4.0f, "BRB: L=4");
  check(u2f(d->brb_result_mu_flat_o[0]) == 2.5f, "BRB: mu=2.5");

  float res = u2f(d->brb_result_residual_o);
  check(std::fabs(res - 2.25f) < 0.01f, "BRB: residual=2.25");
  check(d->brb_result_fail_o == 0, "BRB: fail=0");
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vbelief_result_builder_top;

  std::printf("========================================\n");
  std::printf("belief_result_builder unit tests\n");
  std::printf("========================================\n");

  test_belief_result_builder(dut);

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
