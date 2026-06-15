// schur_update_unit.cc
// Unit test for schur_update_unit

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vschur_update_unit_top.h"

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

static void tick(Vschur_update_unit_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vschur_update_unit_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

void test_schur_update_scalar(Vschur_update_unit_top* d) {
  std::printf("\n=== schur_update_unit scalar ===\n");
  reset(d);

  d->sch_dim_i_i = 0; // DIM_1
  d->sch_dim_o_i = 0; // DIM_1
  d->sch_factor_eta_flat_i[0] = f2u(5.0f);
  d->sch_factor_L_ii_flat_i[0] = f2u(4.0f);
  d->sch_L_io_dense_flat_i[0] = f2u(2.0f);
  // solve_X is d_o x (d_i+1) = 1 x 2 row-major
  // X_Lambda = solve_X[0][0] = 1.5, X_eta = solve_X[0][1] = 1.0
  d->sch_solve_X_flat_i[0] = f2u(1.5f);
  d->sch_solve_X_flat_i[1] = f2u(1.0f);
  d->sch_ready_i = 1;

  d->sch_valid_i = 1;
  tick(d);
  d->sch_valid_i = 0;

  int cycles = 0;
  while (!d->sch_valid_o && cycles < 2000) {
    tick(d);
    cycles++;
  }

  check(d->sch_valid_o == 1, "schur scalar: valid_o");
  // msg_eta = 5 - 2*1.0 = 3.0
  // msg_L = 4 - 2*1.5 = 1.0
  float eta = u2f(d->sch_msg_eta_flat_o[0]);
  float L   = u2f(d->sch_msg_L_flat_o[0]);
  std::printf("  schur scalar: eta=%g L=%g\n", eta, L);
  check(std::fabs(eta - 3.0f) < 0.01f, "schur scalar: eta=3");
  check(std::fabs(L - 1.0f) < 0.01f, "schur scalar: L=1");
}

void test_schur_update_3x3(Vschur_update_unit_top* d) {
  std::printf("\n=== schur_update_unit 3x3 ===\n");
  reset(d);

  d->sch_dim_i_i = 1; // DIM_3
  d->sch_dim_o_i = 1; // DIM_3

  // factor_eta = [1,2,3]
  d->sch_factor_eta_flat_i[0] = f2u(1.0f);
  d->sch_factor_eta_flat_i[1] = f2u(2.0f);
  d->sch_factor_eta_flat_i[2] = f2u(3.0f);

  // factor_L_ii = identity (packed upper triangular)
  d->sch_factor_L_ii_flat_i[0] = f2u(1.0f);
  d->sch_factor_L_ii_flat_i[1] = f2u(0.0f);
  d->sch_factor_L_ii_flat_i[2] = f2u(0.0f);
  d->sch_factor_L_ii_flat_i[3] = f2u(1.0f);
  d->sch_factor_L_ii_flat_i[4] = f2u(0.0f);
  d->sch_factor_L_ii_flat_i[5] = f2u(1.0f);

  // L_io = identity 3x3 in max-size row-major (stride GBP_MAX_VAR_DIM=6)
  for (int i = 0; i < 36; i++) d->sch_L_io_dense_flat_i[i] = f2u(0.0f);
  d->sch_L_io_dense_flat_i[0]  = f2u(1.0f); // (0,0)
  d->sch_L_io_dense_flat_i[7]  = f2u(1.0f); // (1,1)
  d->sch_L_io_dense_flat_i[14] = f2u(1.0f); // (2,2)

  // solve_X = 0.5*I for Lambda part, eta part = [1,0,0]
  // d_o x (d_i+1) = 3 x 4 row-major, RTL stride GBP_MAX_RHS=7
  for (int i = 0; i < 42; i++)
    d->sch_solve_X_flat_i[i] = f2u(0.0f);
  d->sch_solve_X_flat_i[0]  = f2u(0.5f); // X[0][0]
  d->sch_solve_X_flat_i[8]  = f2u(0.5f); // X[1][1]
  d->sch_solve_X_flat_i[16] = f2u(0.5f); // X[2][2]
  d->sch_solve_X_flat_i[3]  = f2u(1.0f); // X[0][3] = eta col

  d->sch_ready_i = 1;
  d->sch_valid_i = 1;
  tick(d);
  d->sch_valid_i = 0;

  int cycles = 0;
  while (!d->sch_valid_o && cycles < 5000) {
    tick(d);
    cycles++;
  }

  check(d->sch_valid_o == 1, "schur 3x3: valid_o");
  // factor_eta=[1,2,3], L_io=I, X_Lambda=0.5*I, X_eta=[1,0,0]
  // msg_eta[p] = factor_eta[p] - L_io[p][p] * X[p][3]
  //   = [1-1*1, 2-1*0, 3-1*0] = [0,2,3]
  // msg_L[p][q] = factor_L_ii[p][q] - L_io[p][p] * X[p][q] (p<=q)
  //   diag = 1 - 0.5 = 0.5, off-diag = 0
  check(std::fabs(u2f(d->sch_msg_eta_flat_o[0]) - 0.0f) < 0.01f, "schur 3x3: eta0=0");
  check(std::fabs(u2f(d->sch_msg_eta_flat_o[1]) - 2.0f) < 0.01f, "schur 3x3: eta1=2");
  check(std::fabs(u2f(d->sch_msg_eta_flat_o[2]) - 3.0f) < 0.01f, "schur 3x3: eta2=3");
  check(std::fabs(u2f(d->sch_msg_L_flat_o[0]) - 0.5f) < 0.01f, "schur 3x3: L00=0.5");
  check(std::fabs(u2f(d->sch_msg_L_flat_o[1]) - 0.0f) < 0.01f, "schur 3x3: L01=0");
  check(std::fabs(u2f(d->sch_msg_L_flat_o[2]) - 0.0f) < 0.01f, "schur 3x3: L02=0");
  check(std::fabs(u2f(d->sch_msg_L_flat_o[3]) - 0.5f) < 0.01f, "schur 3x3: L11=0.5");
  check(std::fabs(u2f(d->sch_msg_L_flat_o[4]) - 0.0f) < 0.01f, "schur 3x3: L12=0");
  check(std::fabs(u2f(d->sch_msg_L_flat_o[5]) - 0.5f) < 0.01f, "schur 3x3: L22=0.5");
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vschur_update_unit_top;

  std::printf("========================================\n");
  std::printf("schur_update_unit unit tests\n");
  std::printf("========================================\n");

  test_schur_update_scalar(dut);
  test_schur_update_3x3(dut);

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
