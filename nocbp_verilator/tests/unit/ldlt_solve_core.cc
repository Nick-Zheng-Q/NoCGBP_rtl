// ldlt_solve_core.cc
// Unit test for ldlt_solve_core
// Uses ldlt_golden.hpp as the step-by-step reference model.

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "debug.h"
#include "verilated.h"
#include "Vldlt_solve_core_top.h"
#include "Vldlt_solve_core_top___024root.h"
#include "ldlt_golden.hpp"

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

static void tick(Vldlt_solve_core_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vldlt_solve_core_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

// Read B matrix from DUT inputs (row-major, stride GBP_MAX_RHS=7).
static std::vector<std::vector<float>> read_B_input(Vldlt_solve_core_top* d,
                                                    int d_dim, int nrhs) {
  std::vector<std::vector<float>> B(d_dim, std::vector<float>(nrhs, 0.0f));
  for (int r = 0; r < d_dim; ++r) {
    for (int c = 0; c < nrhs; ++c) {
      B[r][c] = u2f(d->ldlt_req_B_flat_i[r * 7 + c]);
    }
  }
  return B;
}

// Read X output from DUT (row-major, stride GBP_MAX_RHS=7).
static std::vector<std::vector<float>> read_X_output(Vldlt_solve_core_top* d,
                                                     int d_dim, int nrhs) {
  std::vector<std::vector<float>> X(d_dim, std::vector<float>(nrhs, 0.0f));
  for (int r = 0; r < d_dim; ++r) {
    for (int c = 0; c < nrhs; ++c) {
      X[r][c] = u2f(d->ldlt_rsp_X_flat_o[r * 7 + c]);
    }
  }
  return X;
}

// Read internal B/L/D arrays from the Verilated model.
static std::vector<std::vector<float>> read_internal_B(Vldlt_solve_core_top* d,
                                                       int d_dim, int nrhs) {
  std::vector<std::vector<float>> B(d_dim, std::vector<float>(nrhs, 0.0f));
  for (int r = 0; r < d_dim; ++r) {
    for (int c = 0; c < nrhs; ++c) {
      B[r][c] = u2f(d->rootp->ldlt_solve_core_top__DOT__u_ldlt__DOT__B[r][c]);
    }
  }
  return B;
}

static std::vector<std::vector<float>> read_internal_L(Vldlt_solve_core_top* d,
                                                       int d_dim) {
  std::vector<std::vector<float>> L(d_dim, std::vector<float>(d_dim, 0.0f));
  for (int r = 0; r < d_dim; ++r) {
    for (int c = 0; c < d_dim; ++c) {
      L[r][c] = u2f(d->rootp->ldlt_solve_core_top__DOT__u_ldlt__DOT__L[r][c]);
    }
  }
  return L;
}

static std::vector<float> read_internal_D(Vldlt_solve_core_top* d, int d_dim) {
  std::vector<float> D(d_dim, 0.0f);
  for (int i = 0; i < d_dim; ++i) {
    D[i] = u2f(d->rootp->ldlt_solve_core_top__DOT__u_ldlt__DOT__D[i]);
  }
  return D;
}

static void dump_matrix(const char* label,
                        const std::vector<std::vector<float>>& M) {
  NOCBP_DBG("[RTL] %s:\n", label);
  for (size_t r = 0; r < M.size(); ++r) {
    NOCBP_DBG("  row%zu:", r);
    for (size_t c = 0; c < M[r].size(); ++c) NOCBP_DBG(" %g", M[r][c]);
    NOCBP_DBG("\n");
  }
}

static void dump_vector(const char* label, const std::vector<float>& V) {
  NOCBP_DBG("[RTL] %s:", label);
  for (float v : V) NOCBP_DBG(" %g", v);
  NOCBP_DBG("\n");
}

// Generic runner: drive the DUT with given A/B, compare against golden model.
static std::vector<std::vector<float>> run_ldlt_case(
    Vldlt_solve_core_top* d,
    const std::string& name,
    int dim_enum,             // 0=DIM_1, 1=DIM_3, 2=DIM_6
    int nrhs,
    const std::vector<ldlt::fp32_t>& A_flat,
    const std::vector<std::vector<float>>& B,
    int d_dim,
    int timeout_cycles = 5000) {

  reset(d);

  d->ldlt_req_dim_i  = dim_enum;
  d->ldlt_req_nrhs_i = nrhs;
  for (size_t i = 0; i < A_flat.size(); ++i)
    d->ldlt_req_A_flat_i[i] = A_flat[i];
  for (int r = 0; r < d_dim; ++r)
    for (int c = 0; c < nrhs; ++c)
      d->ldlt_req_B_flat_i[r * 7 + c] = f2u(B[r][c]);

  d->ldlt_req_diag_lambda_i   = f2u(0.0f);
  d->ldlt_req_pivot_eps_i     = f2u(1e-12f);
  d->ldlt_req_regularize_en_i = 0;
  d->ldlt_rsp_ready_i         = 1;

  // Golden reference, step-by-step.
  ldlt::Result golden = ldlt::solve(A_flat, B, d_dim, nrhs, NOCBP_DEBUG);

  d->ldlt_req_valid_i = 1;
  tick(d);
  d->ldlt_req_valid_i = 0;

  int cycles = 0;
  int prev_state = -1;
  while (!d->ldlt_rsp_valid_o && cycles < timeout_cycles) {
    tick(d);
    int state = d->rootp->ldlt_solve_core_top__DOT__u_ldlt__DOT__state_r;
    if (NOCBP_DEBUG) {
      // Dump matrices at interesting phase boundaries.
      // ST_FORWARD_FINISH(28) means forward solve just finished -> B holds y.
      if (prev_state != 28 && state == 28) {
        dump_matrix("B after forward", read_internal_B(d, d_dim, nrhs));
      }
      // ST_BACKWARD_INIT(32) means diagonal scale just finished -> B holds z.
      if (prev_state != 32 && state == 32) {
        dump_matrix("B after diag scale", read_internal_B(d, d_dim, nrhs));
        dump_vector("D", read_internal_D(d, d_dim));
        dump_matrix("L", read_internal_L(d, d_dim));
      }
      prev_state = state;
    }
    cycles++;
  }

  check(d->ldlt_rsp_valid_o == 1, (name + ": rsp_valid").c_str());
  if (!d->ldlt_rsp_valid_o) {
    std::printf("  %s: TIMEOUT after %d cycles\n", name.c_str(), cycles);
    return {};
  }
  check(d->ldlt_rsp_fail_o == 0, (name + ": no fail").c_str());

  std::vector<std::vector<float>> X = read_X_output(d, d_dim, nrhs);

  NOCBP_DBG("[RTL] %s x output:\n", name.c_str());
  for (int r = 0; r < d_dim; ++r) {
    for (int c = 0; c < nrhs; ++c) {
      NOCBP_DBG("  X[%d][%d]=%g (golden=%g)\n", r, c, X[r][c], golden.x[r][c]);
    }
  }

  for (int r = 0; r < d_dim; ++r) {
    for (int c = 0; c < nrhs; ++c) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%s: X[%d][%d]", name.c_str(), r, c);
      check(std::fabs(X[r][c] - golden.x[r][c]) < 0.01f, buf);
    }
  }

  return X;
}

// ============================================================
void test_ldlt_solve_core_scalar(Vldlt_solve_core_top* d) {
  std::printf("\n=== ldlt_solve_core scalar ===\n");
  std::vector<ldlt::fp32_t> A = { f2u(4.0f) };
  std::vector<std::vector<float>> B = { { 8.0f } };
  auto X = run_ldlt_case(d, "ldlt scalar", 0, 1, A, B, 1);
  if (!X.empty()) {
    std::printf("  ldlt scalar: fail=%d min_pivot=%g x=%g\n",
                d->ldlt_rsp_fail_o, u2f(d->ldlt_rsp_min_pivot_o), X[0][0]);
  }
}

void test_ldlt_solve_core_3x3_identity(Vldlt_solve_core_top* d) {
  std::printf("\n=== ldlt_solve_core 3x3 identity ===\n");
  std::vector<ldlt::fp32_t> A(6);
  A[0] = f2u(1.0f); A[1] = f2u(0.0f); A[2] = f2u(0.0f);
  A[3] = f2u(1.0f); A[4] = f2u(0.0f); A[5] = f2u(1.0f);
  std::vector<std::vector<float>> B = { { 1.0f }, { 2.0f }, { 3.0f } };
  auto X = run_ldlt_case(d, "ldlt 3x3 I", 1, 1, A, B, 3);
  if (!X.empty()) {
    std::printf("  ldlt 3x3 I: fail=%d min_pivot=%g x=[%g,%g,%g]\n",
                d->ldlt_rsp_fail_o, u2f(d->ldlt_rsp_min_pivot_o),
                X[0][0], X[1][0], X[2][0]);
  }
}

void test_ldlt_solve_core_3x3(Vldlt_solve_core_top* d) {
  std::printf("\n=== ldlt_solve_core 3x3 SPD ===\n");
  // A = [[4,1,1],[1,3,1],[1,1,2]]
  std::vector<ldlt::fp32_t> A(6);
  A[0] = f2u(4.0f); A[1] = f2u(1.0f); A[2] = f2u(1.0f);
  A[3] = f2u(3.0f); A[4] = f2u(1.0f); A[5] = f2u(2.0f);
  std::vector<std::vector<float>> B = { { 9.0f }, { 7.0f }, { 5.0f } };
  auto X = run_ldlt_case(d, "ldlt 3x3", 1, 1, A, B, 3);
  if (!X.empty()) {
    std::printf("  ldlt 3x3: x=[%g,%g,%g]\n", X[0][0], X[1][0], X[2][0]);
  }
}

void test_ldlt_solve_core_3x3_nrhs4(Vldlt_solve_core_top* d) {
  std::printf("\n=== ldlt_solve_core 3x3 nrhs=4 ===\n");
  // A = 2*I
  std::vector<ldlt::fp32_t> A(6);
  A[0] = f2u(2.0f); A[1] = f2u(0.0f); A[2] = f2u(0.0f);
  A[3] = f2u(2.0f); A[4] = f2u(0.0f); A[5] = f2u(2.0f);

  // B = [I | [2,0,0]^T]
  std::vector<std::vector<float>> B(3, std::vector<float>(4, 0.0f));
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 4; ++c) {
      if (c < 3 && r == c) B[r][c] = 1.0f;
      if (c == 3 && r == 0) B[r][c] = 2.0f;
    }
  }

  auto X = run_ldlt_case(d, "ldlt 3x3 nrhs4", 1, 4, A, B, 3);
  if (!X.empty()) {
    std::printf("  ldlt 3x3 nrhs4: X =\n");
    for (int r = 0; r < 3; r++) {
      std::printf("    row%d: ", r);
      for (int c = 0; c < 4; c++) {
        std::printf("%g ", X[r][c]);
      }
      std::printf("\n");
    }
  }
}

void test_ldlt_solve_core_6x6_identity(Vldlt_solve_core_top* d) {
  std::printf("\n=== ldlt_solve_core 6x6 identity ===\n");
  // A = I_6 packed upper-triangular row-major
  std::vector<ldlt::fp32_t> A(21, f2u(0.0f));
  for (int row = 0; row < 6; row++) {
    int idx = row * 6 - row * (row - 1) / 2;
    A[idx] = f2u(1.0f);
  }

  std::vector<std::vector<float>> B(6, std::vector<float>(1, 0.0f));
  for (int r = 0; r < 6; r++) B[r][0] = (float)(r + 1);

  auto X = run_ldlt_case(d, "ldlt 6x6 I", 2, 1, A, B, 6);
  if (!X.empty()) {
    std::printf("  ldlt 6x6 I: x=[");
    for (int r = 0; r < 6; r++) {
      std::printf("%g%s", X[r][0], (r == 5) ? "]\n" : ", ");
    }
  }
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vldlt_solve_core_top;

  std::printf("========================================\n");
  std::printf("ldlt_solve_core unit tests\n");
  std::printf("========================================\n");

  test_ldlt_solve_core_scalar(dut);
  test_ldlt_solve_core_3x3_identity(dut);
  test_ldlt_solve_core_3x3(dut);
  test_ldlt_solve_core_3x3_nrhs4(dut);
  test_ldlt_solve_core_6x6_identity(dut);

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
