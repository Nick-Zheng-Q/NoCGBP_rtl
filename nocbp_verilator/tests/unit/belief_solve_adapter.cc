// belief_solve_adapter.cc
// Unit test for belief_solve_adapter

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vbelief_solve_adapter_top.h"

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

static void tick(Vbelief_solve_adapter_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vbelief_solve_adapter_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

static int packed_count(int d) { return d * (d + 1) / 2; }
static int packed_index(int row, int col, int d) {
  return row * d - row * (row - 1) / 2 + (col - row);
}

void test_belief_solve_adapter(Vbelief_solve_adapter_top* d) {
  std::printf("\n=== belief_solve_adapter scalar ===\n");

  reset(d);

  d->bsa_valid_i = 1;
  d->bsa_dim_i = 0;
  d->bsa_acc_eta_flat_i[0] = f2u(5.0f);
  d->bsa_acc_L_flat_i[0] = f2u(3.0f);
  d->bsa_diag_lambda_i = f2u(1e-9f);
  d->bsa_pivot_eps_i = f2u(1e-12f);
  d->bsa_regularize_en_i = 1;
  d->bsa_req_ready_i = 1;
  tick(d);
  for (int i = 0; i < 5; i++) tick(d);

  check(d->bsa_req_valid_o == 1, "BSA scalar: valid");
  check(d->bsa_req_dim_o == 0, "BSA scalar: dim=DIM_1");
  check(d->bsa_req_nrhs_o == 1, "BSA scalar: nrhs=1");
  check(u2f(d->bsa_req_A_flat_o[0]) == 3.0f, "BSA scalar: A[0]=3");
  check(u2f(d->bsa_req_B_flat_o[0]) == 5.0f, "BSA scalar: B[0,0]=5");
  check(d->bsa_req_regularize_en_o == 1, "BSA scalar: reg_en=1");
}

// ============================================================
// Dim6 test: verifies that the packed identity L and eta[1..6]
// map correctly into the LDLT request A/B flats.
// ============================================================
void test_belief_solve_adapter_dim6(Vbelief_solve_adapter_top* d) {
  std::printf("\n=== belief_solve_adapter dim6 ===\n");

  reset(d);

  const int dim = 6;
  const int p   = packed_count(dim);
  float eta[6]  = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  float L[21]   = {0};
  for (int i = 0; i < dim; i++)
    L[packed_index(i, i, dim)] = 1.0f;

  for (int i = 0; i < dim; i++) d->bsa_acc_eta_flat_i[i] = f2u(eta[i]);
  for (int i = 0; i < p;    i++) d->bsa_acc_L_flat_i[i]   = f2u(L[i]);
  for (int i = dim; i < 6; i++) d->bsa_acc_eta_flat_i[i] = 0;

  d->bsa_valid_i = 1;
  d->bsa_dim_i = 2;            // DIM_6
  d->bsa_diag_lambda_i = f2u(1e-9f);
  d->bsa_pivot_eps_i = f2u(1e-12f);
  d->bsa_regularize_en_i = 0;
  d->bsa_req_ready_i = 1;
  tick(d);
  for (int i = 0; i < 5; i++) tick(d);

  check(d->bsa_req_valid_o == 1, "BSA dim6: valid");
  check(d->bsa_req_dim_o == 2, "BSA dim6: dim=DIM_6");
  check(d->bsa_req_nrhs_o == 1, "BSA dim6: nrhs=1");

  bool A_ok = true;
  for (int i = 0; i < p; i++) {
    if (u2f(d->bsa_req_A_flat_o[i]) != L[i]) { A_ok = false; break; }
  }
  check(A_ok, "BSA dim6: A_flat matches identity L");

  bool B_ok = true;
  for (int i = 0; i < dim; i++) {
    // solve_req_B_flat_o is row-major with GBP_MAX_RHS=7 columns.
    if (u2f(d->bsa_req_B_flat_o[i * 7 + 0]) != eta[i]) { B_ok = false; break; }
  }
  check(B_ok, "BSA dim6: B_flat matches eta[1..6]");
  check(d->bsa_req_regularize_en_o == 0, "BSA dim6: reg_en=0");
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vbelief_solve_adapter_top;

  std::printf("========================================\n");
  std::printf("belief_solve_adapter unit tests\n");
  std::printf("========================================\n");

  test_belief_solve_adapter(dut);
  test_belief_solve_adapter_dim6(dut);

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
