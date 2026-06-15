// gbp_pe_compute_subsystem.cc
// Functional tests for the new descriptor-driven compute subsystem.
// V0 supported path: OP_BELIEF with degree == 0 (prior-only SPM read).

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vgbp_pe_compute_subsystem_top.h"
#include "Vgbp_pe_compute_subsystem_top___024root.h"

static int error_count = 0;
static int test_count  = 0;

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

static void tick(Vgbp_pe_compute_subsystem_top* d) {
  d->clk = 0; d->eval();
  d->clk = 1; d->eval();
}

static void reset_dut(Vgbp_pe_compute_subsystem_top* d) {
  d->rst_n = 0;
  d->cmd_valid_i = 0;
  d->spm_rd0_ready_i = 1;
  d->spm_backdoor_wr_valid_i = 0;
  for (int i = 0; i < 5; i++) tick(d);
  d->rst_n = 1;
  for (int i = 0; i < 3; i++) tick(d);
}

static void spm_write_word(Vgbp_pe_compute_subsystem_top* d,
                           uint32_t addr, uint32_t data) {
  d->spm_backdoor_wr_addr_i  = addr;
  d->spm_backdoor_wr_data_i  = data;
  d->spm_backdoor_wr_valid_i = 1;
  tick(d);
  d->spm_backdoor_wr_valid_i = 0;
}

static uint32_t spm_read_word(Vgbp_pe_compute_subsystem_top* d, uint32_t addr) {
  d->spm_backdoor_rd_addr_i = addr;
  d->eval();
  uint64_t beat = d->spm_backdoor_rd_data_o;
  return (addr & 1) ? uint32_t(beat >> 32) : uint32_t(beat);
}

static int packed_count(int d) { return d * (d + 1) / 2; }
static int packed_index(int row, int col, int d) {
  return row * d - row * (row - 1) / 2 + (col - row);
}

static void identity_L_packed(int d, float* L) {
  int p = packed_count(d);
  for (int i = 0; i < p; i++) L[i] = 0.0f;
  for (int i = 0; i < d; i++) L[packed_index(i, i, d)] = 1.0f;
}

static void write_belief_prior(Vgbp_pe_compute_subsystem_top* d,
                               uint32_t base, int dim,
                               const float* eta,
                               const float* L_packed,
                               const float* mu_old) {
  int p = packed_count(dim);
  // Clear the first 16-word operand beat to avoid X propagation.
  for (int i = 0; i < 16; i++) spm_write_word(d, base + i, 0);
  uint32_t addr = base;
  for (int i = 0; i < dim; i++) spm_write_word(d, addr++, f2u(eta[i]));
  for (int i = 0; i < p;    i++) spm_write_word(d, addr++, f2u(L_packed[i]));
  for (int i = 0; i < dim; i++) spm_write_word(d, addr++, f2u(mu_old[i]));
}

static void issue_command(Vgbp_pe_compute_subsystem_top* d,
                          uint32_t node_id, int dof, int adj_count,
                          uint32_t state_words, uint32_t state_base) {
  d->cmd_valid_i        = 1;
  d->cmd_node_id_i      = node_id;
  d->cmd_is_factor_i    = 0;
  d->cmd_dof_i          = dof;
  d->cmd_adj_count_i    = adj_count;
  d->cmd_state_words_i  = state_words;
  d->cmd_state_base_i   = state_base;
  d->cmd_neighbor_dofs_i= 0;
  while (!d->cmd_ready_o) tick(d);
  tick(d);
  d->cmd_valid_i = 0;
}

static bool wait_done(Vgbp_pe_compute_subsystem_top* d, int max_cycles) {
  int cycles = 0;
  while (!d->done_valid_o && cycles < max_cycles) {
    tick(d);
    cycles++;
  }
  return d->done_valid_o;
}

static bool float_eq(float a, float b, float eps = 0.02f) {
  return std::fabs(a - b) <= eps;
}

// ============================================================
static int test_belief_dim1(Vgbp_pe_compute_subsystem_top* d) {
  std::printf("\n=== Compute Subsystem: belief dim=1 ===\n");
  reset_dut(d);
  int dim = 1;
  uint32_t node_id = 0x10;
  uint32_t base    = 0x100;
  float eta[1]     = {2.0f};
  float L[1]       = {3.0f};
  float mu_old[1]  = {0.0f};
  write_belief_prior(d, base, dim, eta, L, mu_old);

  issue_command(d, node_id, dim, 0, 3, base);

  bool ok = wait_done(d, 5000);
  check(ok, "dim1: done_valid_o asserted");
  if (!ok) return 1;

  check(d->done_node_id_o == node_id, "dim1: done_node_id");
  check(d->done_is_factor_o == 0,     "dim1: done_is_factor=0");
  check(d->batch_done_o,              "dim1: batch_done_o asserted");

  // Allow write_stream_engine to finish writing the result.
  for (int i = 0; i < 200; i++) tick(d);

  std::printf("  dim1 SPM @ 0x%03X:", base);
  for (int i = 0; i < 4; i++) std::printf(" %f", u2f(spm_read_word(d, base + i)));
  std::printf("\n");



  float mu_out = u2f(spm_read_word(d, base + 2));
  std::printf("  dim1: mu=%f (expected %f)\n", mu_out, 2.0f / 3.0f);
  check(float_eq(mu_out, 2.0f / 3.0f), "dim1: mu = eta / L");
  return error_count > 0 ? 1 : 0;
}

// ============================================================
static int test_belief_dim6(Vgbp_pe_compute_subsystem_top* d) {
  std::printf("\n=== Compute Subsystem: belief dim=6 ===\n");
  reset_dut(d);
  int dim = 6;
  uint32_t node_id = 0x12;
  uint32_t base    = 0x120;
  float eta[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  float L_packed[packed_count(6)];
  identity_L_packed(dim, L_packed);
  float mu_old[6] = {0};
  write_belief_prior(d, base, dim, eta, L_packed, mu_old);

  issue_command(d, node_id, dim, 0, 33, base);
  bool ok = wait_done(d, 10000);
  check(ok, "dim6: done_valid_o asserted");
  if (!ok) return 1;

  check(d->done_node_id_o == node_id, "dim6: done_node_id");

  for (int i = 0; i < 200; i++) tick(d);

  int p = packed_count(dim);
  for (int i = 0; i < dim; i++) {
    float mu_out = u2f(spm_read_word(d, base + p + dim + i));
    char msg[64]; snprintf(msg, sizeof(msg), "dim6: mu[%d]=%g", i, mu_out);
    check(float_eq(mu_out, eta[i]), msg);
  }
  float residual = u2f(spm_read_word(d, base + p + dim + dim));
  std::printf("  dim6: residual=%f (expected 91.0)\n", residual);
  check(float_eq(residual, 91.0f, 0.5f), "dim6: residual = 91");
  return error_count > 0 ? 1 : 0;
}

// ============================================================
static int test_spm_read_backpressure(Vgbp_pe_compute_subsystem_top* d) {
  std::printf("\n=== Compute Subsystem: SPM read backpressure ===\n");
  reset_dut(d);
  int dim = 1;
  uint32_t node_id = 0x20;
  uint32_t base    = 0x200;
  float eta[1]     = {2.0f};
  float L[1]       = {3.0f};
  float mu_old[1]  = {0.0f};
  write_belief_prior(d, base, dim, eta, L, mu_old);

  // Stall the SPM read port around command issue.
  d->spm_rd0_ready_i = 0;
  issue_command(d, node_id, dim, 0, 3, base);
  for (int i = 0; i < 10; i++) tick(d);

  // Release stall; compute should still complete.
  d->spm_rd0_ready_i = 1;
  bool ok = wait_done(d, 5000);
  check(ok, "backpressure: done_valid_o after stall release");
  if (ok) check(d->done_node_id_o == node_id, "backpressure: done_node_id");
  return error_count > 0 ? 1 : 0;
}

// ============================================================
static int test_reset_during_compute(Vgbp_pe_compute_subsystem_top* d) {
  std::printf("\n=== Compute Subsystem: reset during compute ===\n");
  reset_dut(d);
  int dim = 1;
  uint32_t node_id = 0x30;
  uint32_t base    = 0x300;
  float eta[1]     = {2.0f};
  float L[1]       = {3.0f};
  float mu_old[1]  = {0.0f};
  write_belief_prior(d, base, dim, eta, L, mu_old);

  issue_command(d, node_id, dim, 0, 3, base);
  for (int i = 0; i < 20; i++) tick(d);

  // Assert reset in flight
  d->rst_n = 0;
  for (int i = 0; i < 3; i++) tick(d);
  check(!d->done_valid_o, "reset: done cleared during reset");

  // Recover with a fresh command
  d->rst_n = 1;
  for (int i = 0; i < 3; i++) tick(d);

  uint32_t node_id2 = 0x31;
  uint32_t base2    = 0x310;
  write_belief_prior(d, base2, dim, eta, L, mu_old);
  issue_command(d, node_id2, dim, 0, 3, base2);
  bool ok = wait_done(d, 5000);
  check(ok, "reset: done after recovery");
  if (ok) check(d->done_node_id_o == node_id2, "reset: recovered node_id");
  return error_count > 0 ? 1 : 0;
}

// ============================================================
static int test_belief_degree1_dim1(Vgbp_pe_compute_subsystem_top* d) {
  std::printf("\n=== Compute Subsystem: belief degree-1 dim=1 ===\n");
  reset_dut(d);
  int dim = 1;
  uint32_t node_id = 0x13;
  uint32_t base    = 0x130;
  float eta[1]     = {2.0f};
  float L[1]       = {3.0f};
  float mu_old[1]  = {0.0f};
  write_belief_prior(d, base, dim, eta, L, mu_old);

  // Write message data at base + E(1) = base + 2
  int E_d = dim + dim * (dim + 1) / 2;  // E(1) = 2
  spm_write_word(d, base + E_d + 0, f2u(10.0f));  // message eta
  spm_write_word(d, base + E_d + 1, f2u(5.0f));   // message L

  // adj_count=1 => degree-1 belief, requires OST_BELIEF_MSG stream
  issue_command(d, node_id, dim, 1, 3, base);

  bool ok = wait_done(d, 5000);
  check(ok, "belief_deg1: done_valid_o asserted");
  if (!ok) {
    std::fprintf(stderr, "  BUG: degree-1 belief hangs — OST_BELIEF_MSG descriptor missing\n");
    return 1;
  }

  for (int i = 0; i < 200; i++) tick(d);

  // Combined: eta = 2 + 10 = 12, lam = 3 + 5 = 8, mu = 12/8 = 1.5
  float mu_out = u2f(spm_read_word(d, base + 2));
  std::printf("  belief_deg1: mu=%f (expected 1.5)\n", mu_out);
  check(float_eq(mu_out, 1.5f, 0.05f), "belief_deg1: mu = (eta_prior+eta_msg) / (L_prior+L_msg)");
  return 0;
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_pe_compute_subsystem_top;

  std::printf("========================================\n");
  std::printf("gbp_pe_compute_subsystem functional tests\n");
  std::printf("========================================\n");

  int failures = 0;
  int start_err;

  start_err = error_count;
  test_belief_dim1(dut);
  failures += (error_count > start_err);

  start_err = error_count;
  test_belief_dim6(dut);
  failures += (error_count > start_err);

  start_err = error_count;
  test_spm_read_backpressure(dut);
  failures += (error_count > start_err);

  start_err = error_count;
  test_reset_during_compute(dut);
  failures += (error_count > start_err);

  start_err = error_count;
  test_belief_degree1_dim1(dut);
  failures += (error_count > start_err);

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
