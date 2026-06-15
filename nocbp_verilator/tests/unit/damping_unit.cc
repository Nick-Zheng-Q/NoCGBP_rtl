// damping_unit.cc
// Unit test for damping_unit

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vdamping_unit_top.h"

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

static void tick(Vdamping_unit_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vdamping_unit_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

void test_damping_unit(Vdamping_unit_top* d) {
  std::printf("\n=== damping_unit ===\n");

  reset(d);

  d->damp_valid_i = 1;
  d->damp_ready_i = 1;
  d->damp_dim_i = 0;
  d->damp_damping_i = f2u(0.3f);
  d->damp_eta_raw_flat_i[0] = f2u(10.0f);
  d->damp_L_raw_flat_i[0] = f2u(20.0f);
  d->damp_old_eta_flat_i[0] = f2u(2.0f);
  d->damp_old_L_flat_i[0] = f2u(4.0f);
  tick(d);
  // Pipeline: 3 stages × 3 cycles + FSM overhead per element, 2 elements
  for (int i = 0; i < 50; i++) tick(d);

  // Wait for valid output
  for (int i = 0; i < 100; i++) {
    tick(d);
    if (d->damp_valid_o) break;
  }

  check(d->damp_valid_o == 1, "damp: valid");

  float eta = u2f(d->damp_eta_flat_o[0]);
  float L = u2f(d->damp_L_flat_o[0]);
  check(std::fabs(eta - 7.6f) < 0.01f, "damp: eta=7.6");
  check(std::fabs(L - 15.2f) < 0.01f, "damp: L=15.2");

  d->damp_ready_i = 0;
  tick(d);
  check(d->damp_ready_o == 0, "damp: stalled");
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vdamping_unit_top;

  std::printf("========================================\n");
  std::printf("damping_unit unit tests\n");
  std::printf("========================================\n");

  test_damping_unit(dut);

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
