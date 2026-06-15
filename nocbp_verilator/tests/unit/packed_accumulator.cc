// packed_accumulator.cc
// Unit test for packed_accumulator

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vpacked_accumulator_top.h"

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

static void tick(Vpacked_accumulator_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vpacked_accumulator_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

static void set_eta(Vpacked_accumulator_top* d, int idx, float v) {
  d->prior_eta_flat_i[idx] = f2u(v);
}
static void set_L(Vpacked_accumulator_top* d, int idx, float v) {
  d->prior_L_flat_i[idx] = f2u(v);
}
static void set_msg_eta(Vpacked_accumulator_top* d, int idx, float v) {
  d->msg_eta_flat_i[idx] = f2u(v);
}
static void set_msg_L(Vpacked_accumulator_top* d, int idx, float v) {
  d->msg_L_flat_i[idx] = f2u(v);
}

static void start(Vpacked_accumulator_top* d, int dim, int degree) {
  d->dim_i = dim;
  d->degree_i = degree;
  d->start_valid_i = 1;
  int cnt = 0;
  while (!d->start_ready_o && cnt < 100) { tick(d); cnt++; }
  tick(d);
  d->start_valid_i = 0;
}

static void send_msg(Vpacked_accumulator_top* d, int last) {
  d->msg_valid_i = 1;
  d->msg_last_i = last;
  int cnt = 0;
  while (!d->msg_ready_o && cnt < 5000) {
    tick(d);
    cnt++;
  }
  if (!d->msg_ready_o) {
    std::fprintf(stderr, "  [FAIL] msg_ready_o stuck low\n");
    d->msg_valid_i = 0;
    d->msg_last_i = 0;
    return;
  }
  tick(d);
  d->msg_valid_i = 0;
  d->msg_last_i = 0;
}

// ============================================================
static int test_scalar(Vpacked_accumulator_top* d) {
  std::printf("\n--- scalar ---\n");
  reset(d);

  // prior: eta=1, L=2
  set_eta(d, 0, 1.0f);
  set_L(d, 0, 2.0f);
  start(d, 0, 2);

  // msg1: eta=3, L=4
  set_msg_eta(d, 0, 3.0f);
  set_msg_L(d, 0, 4.0f);
  send_msg(d, 0);

  // msg2: eta=5, L=6
  set_msg_eta(d, 0, 5.0f);
  set_msg_L(d, 0, 6.0f);
  send_msg(d, 1);

  d->acc_ready_i = 1;
  int cycles = 0;
  while (!d->acc_valid_o && cycles < 5000) {
    tick(d);
    cycles++;
  }
  if (cycles >= 5000)
    std::fprintf(stderr, "  [TIMEOUT] scalar: acc_valid_o not asserted\n");

  check(d->acc_valid_o == 1, "scalar: acc_valid_o");
  check(d->msg_count_o == 2, "scalar: msg_count=2");
  check(d->degree_mismatch_o == 0, "scalar: no degree mismatch");
  float eta = u2f(d->acc_eta_flat_o[0]);
  float L   = u2f(d->acc_L_flat_o[0]);
  std::printf("  scalar: eta=%g L=%g\n", eta, L);
  check(std::fabs(eta - 9.0f) < 0.01f, "scalar: eta=9");
  check(std::fabs(L - 12.0f) < 0.01f, "scalar: L=12");
  return error_count;
}

// ============================================================
static int test_3x3(Vpacked_accumulator_top* d) {
  std::printf("\n--- 3x3 ---\n");
  reset(d);

  // prior: eta=[1,2,3], L=[1,0,0,1,0,1]
  for (int i = 0; i < 3; i++) set_eta(d, i, float(i + 1));
  set_L(d, 0, 1.0f); set_L(d, 1, 0.0f); set_L(d, 2, 0.0f);
  set_L(d, 3, 1.0f); set_L(d, 4, 0.0f); set_L(d, 5, 1.0f);
  start(d, 1, 1);

  // msg: eta=[1,1,1], L=[1,0,0,1,0,1]
  for (int i = 0; i < 3; i++) set_msg_eta(d, i, 1.0f);
  set_msg_L(d, 0, 1.0f); set_msg_L(d, 1, 0.0f); set_msg_L(d, 2, 0.0f);
  set_msg_L(d, 3, 1.0f); set_msg_L(d, 4, 0.0f); set_msg_L(d, 5, 1.0f);
  send_msg(d, 1);

  d->acc_ready_i = 1;
  int cycles = 0;
  while (!d->acc_valid_o && cycles < 5000) {
    tick(d);
    cycles++;
  }
  if (cycles >= 5000)
    std::fprintf(stderr, "  [TIMEOUT] 3x3: acc_valid_o not asserted\n");

  check(d->acc_valid_o == 1, "3x3: acc_valid_o");
  check(d->msg_count_o == 1, "3x3: msg_count=1");
  check(d->degree_mismatch_o == 0, "3x3: no degree mismatch");
  std::printf("  3x3: eta=[%g,%g,%g]\n",
              u2f(d->acc_eta_flat_o[0]), u2f(d->acc_eta_flat_o[1]), u2f(d->acc_eta_flat_o[2]));
  check(std::fabs(u2f(d->acc_eta_flat_o[0]) - 2.0f) < 0.01f, "3x3: eta0=2");
  check(std::fabs(u2f(d->acc_eta_flat_o[1]) - 3.0f) < 0.01f, "3x3: eta1=3");
  check(std::fabs(u2f(d->acc_eta_flat_o[2]) - 4.0f) < 0.01f, "3x3: eta2=4");
  check(std::fabs(u2f(d->acc_L_flat_o[0]) - 2.0f) < 0.01f, "3x3: L00=2");
  check(std::fabs(u2f(d->acc_L_flat_o[3]) - 2.0f) < 0.01f, "3x3: L11=2");
  check(std::fabs(u2f(d->acc_L_flat_o[5]) - 2.0f) < 0.01f, "3x3: L22=2");
  return error_count;
}

// ============================================================
static int test_dim6_identity(Vpacked_accumulator_top* d) {
  std::printf("\n--- dim6 identity ---\n");
  reset(d);

  const int dim = 6;
  const int p   = dim * (dim + 1) / 2;

  // prior: eta=[1..6], L=I
  for (int i = 0; i < dim; i++) set_eta(d, i, float(i + 1));
  for (int row = 0; row < dim; row++) {
    for (int col = row; col < dim; col++) {
      int idx = row * dim - row * (row - 1) / 2 + (col - row);
      set_L(d, idx, (row == col) ? 1.0f : 0.0f);
    }
  }
  start(d, 2, 1);   // dim enum 2 = DIM_6, degree 1

  // msg: same as prior
  for (int i = 0; i < dim; i++) set_msg_eta(d, i, float(i + 1));
  for (int row = 0; row < dim; row++) {
    for (int col = row; col < dim; col++) {
      int idx = row * dim - row * (row - 1) / 2 + (col - row);
      set_msg_L(d, idx, (row == col) ? 1.0f : 0.0f);
    }
  }
  send_msg(d, 1);

  d->acc_ready_i = 1;
  int cycles = 0;
  while (!d->acc_valid_o && cycles < 5000) { tick(d); cycles++; }
  if (cycles >= 5000)
    std::fprintf(stderr, "  [TIMEOUT] dim6: acc_valid_o not asserted\n");

  check(d->acc_valid_o == 1, "dim6: acc_valid_o");
  check(d->msg_count_o == 1, "dim6: msg_count=1");
  check(d->degree_mismatch_o == 0, "dim6: no degree mismatch");

  std::printf("  dim6: eta=[%g,%g,%g,%g,%g,%g]\n",
              u2f(d->acc_eta_flat_o[0]), u2f(d->acc_eta_flat_o[1]),
              u2f(d->acc_eta_flat_o[2]), u2f(d->acc_eta_flat_o[3]),
              u2f(d->acc_eta_flat_o[4]), u2f(d->acc_eta_flat_o[5]));

  for (int i = 0; i < dim; i++)
    check(std::fabs(u2f(d->acc_eta_flat_o[i]) - 2.0f * float(i + 1)) < 0.05f,
          "dim6: eta doubled");

  for (int row = 0; row < dim; row++) {
    for (int col = row; col < dim; col++) {
      int idx = row * dim - row * (row - 1) / 2 + (col - row);
      float exp = (row == col) ? 2.0f : 0.0f;
      check(std::fabs(u2f(d->acc_L_flat_o[idx]) - exp) < 0.05f,
            "dim6: L doubled identity");
    }
  }
  return error_count;
}

// ============================================================
static int test_degree_mismatch(Vpacked_accumulator_top* d) {
  std::printf("\n--- degree mismatch ---\n");
  reset(d);

  set_eta(d, 0, 1.0f);
  set_L(d, 0, 2.0f);
  start(d, 0, 1);

  // degree=1 but msg_last_i=0 -> mismatch expected
  set_msg_eta(d, 0, 3.0f);
  set_msg_L(d, 0, 4.0f);
  send_msg(d, 0);

  d->acc_ready_i = 1;
  int cycles = 0;
  while (!d->acc_valid_o && cycles < 5000) {
    tick(d);
    cycles++;
  }
  if (cycles >= 5000)
    std::fprintf(stderr, "  [TIMEOUT] mismatch: acc_valid_o not asserted\n");

  check(d->acc_valid_o == 1, "mismatch: acc_valid_o");
  check(d->degree_mismatch_o == 1, "mismatch: degree_mismatch=1");
  return error_count;
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vpacked_accumulator_top;

  std::printf("========================================\n");
  std::printf("packed_accumulator unit tests\n");
  std::printf("========================================\n");

  int start_errors = error_count;
  test_scalar(dut);
  test_3x3(dut);
  test_dim6_identity(dut);
  test_degree_mismatch(dut);

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
