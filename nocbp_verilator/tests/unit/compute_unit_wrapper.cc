// compute_unit_wrapper.cc
// Unit tests for the compute_unit_wrapper top-level.

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>

#include "verilated.h"
#include "Vcompute_unit_wrapper_top.h"
#include "Vcompute_unit_wrapper_top___024root.h"

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

static void tick(Vcompute_unit_wrapper_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vcompute_unit_wrapper_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);

  d->cmd_valid_i = 0;
  d->rd_req_ready_i = 0;
  d->operand_valid_i = 0;
  d->wb_ready_i = 0;
  d->done_ready_i = 0;
  for (int i = 0; i < 8; i++) {
    d->cmd_operand_desc_valid_i[i] = 0;
    d->cmd_operand_desc_kind_i[i] = 0;
    d->cmd_operand_desc_base_addr_i[i] = 0;
    d->cmd_operand_desc_nbeats_i[i] = 0;
  }
}

enum {
  OP_MSG_F2V      = 0,
  OP_BELIEF       = 1,
  OP_RELIN_CHECK  = 2,
  OP_ROBUST_SCALE = 3
};

enum {
  FACTOR_SCALAR = 0,
  FACTOR_SE2    = 1,
  FACTOR_BA     = 2,
  FACTOR_SE3    = 3
};

enum {
  DIM_1 = 0,
  DIM_3 = 1,
  DIM_6 = 2
};

enum {
  OST_MSG_STATIC       = 0,
  OST_CAV_FACTOR_O     = 1,
  OST_CAV_BELIEF_O     = 2,
  OST_CAV_OLD_TO_O     = 3,
  OST_BELIEF_PRIOR     = 4,
  OST_BELIEF_MSG       = 5
};

enum {
  WB_MSG    = 0,
  WB_BELIEF = 1
};

static void clear_operand(Vcompute_unit_wrapper_top* d) {
  d->operand_valid_i = 0;
  d->operand_kind_i  = 0;
  d->operand_ctx_id_i = 0;
  d->operand_op_id_i  = 0;
  d->operand_beat_idx_i = 0;
  d->operand_last_i   = 0;
  for (int i = 0; i < 16; i++)
    d->operand_data_flat_i[i] = 0;
}

static void send_operand_beat(Vcompute_unit_wrapper_top* d, int kind,
                              uint32_t data[16], int last,
                              uint32_t op_id, uint16_t beat_idx) {
  d->operand_valid_i    = 1;
  d->operand_kind_i     = kind;
  d->operand_op_id_i    = op_id;
  d->operand_beat_idx_i = beat_idx;
  d->operand_last_i     = last;
  for (int i = 0; i < 16; i++)
    d->operand_data_flat_i[i] = data[i];

  int cnt = 0;
  while (!d->operand_ready_o) {
    tick(d);
    cnt++;
    if (cnt > 1000) {
      std::fprintf(stderr, "  [FAIL] operand_ready_o stuck low (kind=%d)\n", kind);
      clear_operand(d);
      return;
    }
  }
  tick(d);
  clear_operand(d);
}

static void issue_wrapper_cmd(Vcompute_unit_wrapper_top* d,
                              int op, int factor_type, int dim_i, int dim_o,
                              int direction, uint32_t op_id,
                              uint32_t node_id, uint32_t factor_id,
                              uint32_t dst_addr,
                              float damping, float diag_lambda, float pivot_eps,
                              int regularize_en, uint16_t degree) {
  d->cmd_valid_i          = 1;
  d->cmd_op_i             = op;
  d->cmd_factor_type_i    = factor_type;
  d->cmd_dim_i_i          = dim_i;
  d->cmd_dim_o_i          = dim_o;
  d->cmd_direction_i      = direction;
  d->cmd_ctx_id_i         = 0;
  d->cmd_op_id_i          = op_id;
  d->cmd_node_id_i        = node_id;
  d->cmd_factor_id_i      = factor_id;
  d->cmd_dst_addr_i       = dst_addr;
  d->cmd_aux_addr_i       = 0;
  d->cmd_degree_i         = degree;
  d->cmd_damping_i        = f2u(damping);
  d->cmd_diag_lambda_i    = f2u(diag_lambda);
  d->cmd_pivot_eps_i      = f2u(pivot_eps);
  d->cmd_regularize_en_i  = regularize_en;

  while (!d->cmd_ready_o) {
    tick(d);
  }
  tick(d);
  d->cmd_valid_i = 0;
}

static void set_operand_desc(Vcompute_unit_wrapper_top* d, int idx,
                             int valid, int kind, uint32_t base, uint16_t nbeats) {
  d->cmd_operand_desc_valid_i[idx]     = valid;
  d->cmd_operand_desc_kind_i[idx]      = kind;
  d->cmd_operand_desc_base_addr_i[idx] = base;
  d->cmd_operand_desc_nbeats_i[idx]    = nbeats;
}

static int packed_count(int d) { return d * (d + 1) / 2; }
static int packed_index(int row, int col, int d) {
  return row * d - row * (row - 1) / 2 + (col - row);
}

static bool expect_rd_req(Vcompute_unit_wrapper_top* d, int expected_kind,
                          uint32_t expected_op_id, uint16_t expected_nbeats = 1) {
  int cnt = 0;
  while (!d->rd_req_valid_o && cnt < 100) {
    tick(d);
    cnt++;
  }
  if (!d->rd_req_valid_o) {
    std::fprintf(stderr, "  [FAIL] rd_req_valid_o never asserted for kind=%d\n", expected_kind);
    return false;
  }
  check(d->rd_req_kind_o == expected_kind, "wrapper: rd_req kind");
  check(d->rd_req_nbeats_o == expected_nbeats, "wrapper: rd_req nbeats");
  check(d->rd_req_op_id_o == expected_op_id, "wrapper: rd_req op_id echoed");
  tick(d); // consume the request (rd_req_ready_i is high)
  return true;
}

static void send_operand_stream(Vcompute_unit_wrapper_top* d, int kind,
                                const std::vector<uint32_t>& data,
                                uint32_t op_id) {
  int n = (int)data.size();
  int idx = 0;
  uint16_t beat = 0;
  while (idx < n) {
    uint32_t beat_data[16] = {0};
    int words = (n - idx > 16) ? 16 : (n - idx);
    for (int i = 0; i < words; i++)
      beat_data[i] = data[idx + i];
    int last = (idx + words >= n) ? 1 : 0;
    send_operand_beat(d, kind, beat_data, last, op_id, beat);
    idx += words;
    beat++;
  }
}

static bool wait_wb(Vcompute_unit_wrapper_top* d, int max_cycles) {
  int cycles = 0;
  while (!d->wb_valid_o && cycles < max_cycles) {
    tick(d);
    cycles++;
  }
  return d->wb_valid_o;
}

static void wait_wrapper_state(Vcompute_unit_wrapper_top* d, unsigned target, int max_cycles) {
  int cycles = 0;
  while ((unsigned)d->rootp->compute_unit_wrapper_top__DOT__u_wrapper__DOT__state_r != target
         && cycles < max_cycles) {
    tick(d);
    cycles++;
  }
}

static bool wait_done(Vcompute_unit_wrapper_top* d, int max_cycles) {
  int cycles = 0;
  while (!d->done_valid_o && cycles < max_cycles) {
    tick(d);
    cycles++;
  }
  return d->done_valid_o;
}

// ============================================================
static void test_msg_scalar(Vcompute_unit_wrapper_top* d) {
  std::printf("\n=== wrapper OP_MSG_F2V scalar ===\n");
  reset(d);

  d->rd_req_ready_i = 1;
  d->wb_ready_i     = 0;
  d->done_ready_i   = 0;

  // Read plan for a scalar factor-to-variable message
  set_operand_desc(d, 0, 1, OST_MSG_STATIC,   0x100, 1);
  set_operand_desc(d, 1, 1, OST_CAV_FACTOR_O, 0x200, 1);
  set_operand_desc(d, 2, 1, OST_CAV_BELIEF_O, 0x300, 1);
  set_operand_desc(d, 3, 1, OST_CAV_OLD_TO_O, 0x400, 1);

  issue_wrapper_cmd(d, OP_MSG_F2V, FACTOR_SCALAR, DIM_1, DIM_1, 0,
                    0xAABB, 0x1111, 0x2222, 0x1000,
                    0.0f, 0.0f, 1e-12f, 0, 0);

  // Expect four read requests in index order
  bool rd_ok = true;
  rd_ok &= expect_rd_req(d, OST_MSG_STATIC,   0xAABB);
  rd_ok &= expect_rd_req(d, OST_CAV_FACTOR_O, 0xAABB);
  rd_ok &= expect_rd_req(d, OST_CAV_BELIEF_O, 0xAABB);
  rd_ok &= expect_rd_req(d, OST_CAV_OLD_TO_O, 0xAABB);
  if (!rd_ok) return;

  // Wait for wrapper to finish scanning invalid descriptors and enter W_STREAM_OPERANDS
  wait_wrapper_state(d, 2, 100);

  // Feed operand beats
  uint32_t static_data[16] = {0};
  static_data[0] = f2u(3.0f);
  static_data[1] = f2u(2.0f);
  static_data[2] = f2u(1.0f);
  static_data[3] = f2u(0.0f);
  static_data[4] = f2u(0.0f);
  send_operand_beat(d, OST_MSG_STATIC, static_data, 1, 0xAABB, 0);

  uint32_t cav_factor[16] = {0};
  cav_factor[0] = f2u(1.0f);
  cav_factor[1] = f2u(1.0f);
  send_operand_beat(d, OST_CAV_FACTOR_O, cav_factor, 1, 0xAABB, 0);

  uint32_t cav_belief[16] = {0};
  cav_belief[0] = f2u(1.0f);
  cav_belief[1] = f2u(1.0f);
  send_operand_beat(d, OST_CAV_BELIEF_O, cav_belief, 1, 0xAABB, 0);

  uint32_t cav_old[16] = {0};
  cav_old[0] = f2u(0.0f);
  cav_old[1] = f2u(0.0f);
  send_operand_beat(d, OST_CAV_OLD_TO_O, cav_old, 1, 0xAABB, 0);

  // Wait for writeback
  bool ok = wait_wb(d, 5000);
  check(ok, "wrapper: wb_valid_o");
  if (!ok) return;

  check(d->wb_addr_o == 0x1000, "wrapper: wb_addr=dst_addr");
  check(d->wb_kind_o == WB_MSG, "wrapper: wb_kind=WB_MSG");
  check(d->wb_nwords_o == 2, "wrapper: wb_nwords=E(1)=2");
  check(d->wb_fail_o == 0, "wrapper: wb_fail=0");

  float eta = u2f(d->wb_payload_flat_o[0]);
  float L   = u2f(d->wb_payload_flat_o[1]);
  std::printf("  wrapper wb: eta=%g L=%g\n", eta, L);
  check(std::fabs(eta - 2.0f) < 0.05f, "wrapper: wb eta=2");
  check(std::fabs(L   - 1.5f) < 0.05f, "wrapper: wb L=1.5");

  // Consume writeback
  d->wb_ready_i = 1;
  tick(d);
  d->wb_ready_i = 0;

  // Wait for done
  ok = wait_done(d, 1000);
  check(ok, "wrapper: done_valid_o");
  if (!ok) return;

  check(d->done_node_id_o == 0x1111, "wrapper: done node_id");
  check(d->done_factor_id_o == 0x2222, "wrapper: done factor_id");
  check(d->done_op_o == OP_MSG_F2V, "wrapper: done op");
  check(d->done_success_o == 1, "wrapper: done success");
  check(d->done_fail_o == 0, "wrapper: done fail=0");
  check(d->done_stream_error_o == 0, "wrapper: done stream_error=0");

  d->done_ready_i = 1;
  tick(d);
  d->done_ready_i = 0;
}

// ============================================================
static void test_belief_scalar(Vcompute_unit_wrapper_top* d) {
  std::printf("\n=== wrapper OP_BELIEF scalar ===\n");
  reset(d);

  d->rd_req_ready_i = 1;
  d->wb_ready_i     = 0;
  d->done_ready_i   = 0;

  set_operand_desc(d, 0, 1, OST_BELIEF_PRIOR, 0x500, 1);
  set_operand_desc(d, 1, 1, OST_BELIEF_MSG,   0x600, 1);

  issue_wrapper_cmd(d, OP_BELIEF, FACTOR_SCALAR, DIM_1, DIM_1, 0,
                    0xBEEF, 0x3333, 0x4444, 0x2000,
                    0.0f, 0.0f, 1e-12f, 0, 1);

  bool rd_ok = true;
  rd_ok &= expect_rd_req(d, OST_BELIEF_PRIOR, 0xBEEF);
  rd_ok &= expect_rd_req(d, OST_BELIEF_MSG,   0xBEEF);
  if (!rd_ok) return;

  wait_wrapper_state(d, 2, 100);

  uint32_t prior[16] = {0};
  prior[0] = f2u(1.0f);
  prior[1] = f2u(2.0f);
  prior[2] = f2u(3.0f);
  send_operand_beat(d, OST_BELIEF_PRIOR, prior, 1, 0xBEEF, 0);

  uint32_t msg[16] = {0};
  msg[0] = f2u(10.0f);
  msg[1] = f2u(11.0f);
  send_operand_beat(d, OST_BELIEF_MSG, msg, 1, 0xBEEF, 0);

  bool ok = wait_wb(d, 5000);
  check(ok, "wrapper belief: wb_valid_o");
  if (!ok) return;

  check(d->wb_addr_o == 0x2000, "wrapper belief: wb_addr");
  check(d->wb_kind_o == WB_BELIEF, "wrapper belief: wb_kind=WB_BELIEF");
  check(d->wb_nwords_o == 4, "wrapper belief: wb_nwords=E(1)+1+1=4");
  check(d->wb_fail_o == 0, "wrapper belief: wb_fail=0");

  float eta = u2f(d->wb_payload_flat_o[0]);
  float L   = u2f(d->wb_payload_flat_o[1]);
  float mu  = u2f(d->wb_payload_flat_o[2]);
  float residual = u2f(d->wb_payload_flat_o[3]);
  std::printf("  wrapper belief wb: eta=%g L=%g mu=%g residual=%g\n",
              eta, L, mu, residual);
  check(std::fabs(eta - 11.0f) < 0.05f, "wrapper belief: wb eta=11");
  check(std::fabs(L   - 13.0f) < 0.05f, "wrapper belief: wb L=13");
  check(std::fabs(mu  - (11.0f / 13.0f)) < 0.05f, "wrapper belief: wb mu");
  check(std::fabs(residual - std::pow((11.0f / 13.0f) - 3.0f, 2.0f)) < 0.05f,
        "wrapper belief: wb residual");

  d->wb_ready_i = 1;
  tick(d);
  d->wb_ready_i = 0;

  ok = wait_done(d, 1000);
  check(ok, "wrapper belief: done_valid_o");
  if (!ok) return;

  check(d->done_node_id_o == 0x3333, "wrapper belief: done node_id");
  check(d->done_factor_id_o == 0x4444, "wrapper belief: done factor_id");
  check(d->done_op_o == OP_BELIEF, "wrapper belief: done op");
  check(d->done_success_o == 1, "wrapper belief: done success");

  d->done_ready_i = 1;
  tick(d);
  d->done_ready_i = 0;
}

// ============================================================
static void test_belief_6x6_identity(Vcompute_unit_wrapper_top* d) {
  std::printf("\n=== wrapper OP_BELIEF 6x6 identity ===\n");
  reset(d);

  d->rd_req_ready_i = 1;
  d->wb_ready_i     = 0;
  d->done_ready_i   = 0;

  // Only the prior descriptor is valid; degree=0 -> no messages.
  set_operand_desc(d, 0, 1, OST_BELIEF_PRIOR, 0x500, 3);

  issue_wrapper_cmd(d, OP_BELIEF, FACTOR_SE3, DIM_6, DIM_6, 0,
                    0xC6C6, 0x3333, 0x4444, 0x6000,
                    0.0f, 0.0f, 1e-12f, 0, 0);

  bool rd_ok = expect_rd_req(d, OST_BELIEF_PRIOR, 0xC6C6, 3);
  if (!rd_ok) return;

  wait_wrapper_state(d, 2, 100);

  // Build 33-word prior: eta(6) + L_packed(21) + mu_old(6).
  const int dim = 6;
  const int p   = packed_count(dim);
  std::vector<uint32_t> prior;
  for (int i = 0; i < dim; i++) prior.push_back(f2u(float(i + 1)));
  for (int row = 0; row < dim; row++) {
    for (int col = row; col < dim; col++) {
      prior.push_back(f2u((row == col) ? 1.0f : 0.0f));
    }
  }
  for (int i = 0; i < dim; i++) prior.push_back(f2u(0.0f));
  send_operand_stream(d, OST_BELIEF_PRIOR, prior, 0xC6C6);

  bool ok = wait_wb(d, 5000);
  check(ok, "wrapper 6x6: wb_valid_o");
  if (!ok) return;

  check(d->wb_addr_o == 0x6000, "wrapper 6x6: wb_addr");
  check(d->wb_kind_o == WB_BELIEF, "wrapper 6x6: wb_kind=WB_BELIEF");
  // E(6)=27 + mu(6) + residual(1) = 34
  check(d->wb_nwords_o == 34, "wrapper 6x6: wb_nwords=34");
  check(d->wb_fail_o == 0, "wrapper 6x6: wb_fail=0");

  float eta[6], L[21], mu[6];
  for (int i = 0; i < dim; i++) eta[i] = u2f(d->wb_payload_flat_o[i]);
  for (int i = 0; i < p;    i++) L[i]   = u2f(d->wb_payload_flat_o[dim + i]);
  for (int i = 0; i < dim; i++) mu[i]  = u2f(d->wb_payload_flat_o[dim + p + i]);
  float residual = u2f(d->wb_payload_flat_o[dim + p + dim]);

  std::printf("  wrapper 6x6: eta=[%g,%g,%g,%g,%g,%g] mu=[%g,%g,%g,%g,%g,%g] residual=%g\n",
              eta[0], eta[1], eta[2], eta[3], eta[4], eta[5],
              mu[0], mu[1], mu[2], mu[3], mu[4], mu[5], residual);

  for (int i = 0; i < dim; i++)
    check(std::fabs(eta[i] - float(i + 1)) < 0.05f, "wrapper 6x6: eta");
  for (int i = 0; i < p; i++) {
    bool diag = (i == 0 || i == 6 || i == 11 || i == 15 || i == 18 || i == 20);
    check(std::fabs(L[i] - (diag ? 1.0f : 0.0f)) < 0.05f, "wrapper 6x6: L identity");
  }
  for (int i = 0; i < dim; i++)
    check(std::fabs(mu[i] - float(i + 1)) < 0.05f, "wrapper 6x6: mu=eta");
  check(std::fabs(residual - 91.0f) < 0.5f, "wrapper 6x6: residual=91");

  d->wb_ready_i = 1;
  tick(d);
  d->wb_ready_i = 0;

  ok = wait_done(d, 1000);
  check(ok, "wrapper 6x6: done_valid_o");
  if (!ok) return;

  check(d->done_node_id_o == 0x3333, "wrapper 6x6: done node_id");
  check(d->done_factor_id_o == 0x4444, "wrapper 6x6: done factor_id");
  check(d->done_op_o == OP_BELIEF, "wrapper 6x6: done op");
  check(d->done_success_o == 1, "wrapper 6x6: done success");

  d->done_ready_i = 1;
  tick(d);
  d->done_ready_i = 0;
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vcompute_unit_wrapper_top;

  std::printf("========================================\n");
  std::printf("compute_unit_wrapper unit tests\n");
  std::printf("========================================\n");

  test_msg_scalar(dut);
  test_belief_scalar(dut);
  test_belief_6x6_identity(dut);

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
