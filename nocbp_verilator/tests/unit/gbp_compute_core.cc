// gbp_compute_core.cc
// Unit tests for the GBP compute core top-level.

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>

#include "verilated.h"
#include "Vgbp_compute_core_top.h"
#include "Vgbp_compute_core_top___024root.h"

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

static void tick(Vgbp_compute_core_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vgbp_compute_core_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

// Local copies of gbp_op_pkg enums for convenience
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

static void consume_rsp(Vgbp_compute_core_top* d) {
  d->rsp_ready_i = 1;
  tick(d);
  d->rsp_ready_i = 0;
}

static void clear_operand(Vgbp_compute_core_top* d) {
  d->operand_valid_i = 0;
  d->operand_kind_i  = 0;
  d->operand_ctx_id_i = 0;
  d->operand_op_id_i  = 0;
  d->operand_beat_idx_i = 0;
  d->operand_last_i   = 0;
  for (int i = 0; i < 16; i++)
    d->operand_data_flat_i[i] = 0;
}

static void send_operand_beat(Vgbp_compute_core_top* d, int kind,
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

static void send_operand_stream(Vgbp_compute_core_top* d, int kind,
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

static void issue_cmd(Vgbp_compute_core_top* d,
                      int op, int factor_type, int dim_i, int dim_o,
                      int direction, uint32_t op_id, uint32_t dst_addr,
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
  d->cmd_node_id_i        = 0x1111;
  d->cmd_factor_id_i      = 0x2222;
  d->cmd_dst_addr_i       = dst_addr;
  d->cmd_aux_addr_i       = 0;
  d->cmd_damping_i        = f2u(damping);
  d->cmd_diag_lambda_i    = f2u(diag_lambda);
  d->cmd_pivot_eps_i      = f2u(pivot_eps);
  d->cmd_regularize_en_i  = regularize_en;
  d->cmd_degree_i         = degree;

  while (!d->cmd_ready_o) {
    tick(d);
  }
  tick(d);
  d->cmd_valid_i = 0;
}

static bool wait_rsp(Vgbp_compute_core_top* d, int max_cycles) {
  int cycles = 0;
  while (!d->rsp_valid_o && cycles < max_cycles) {
    tick(d);
    cycles++;
  }
  return d->rsp_valid_o;
}

// ============================================================
static void test_msg_scalar(Vgbp_compute_core_top* d) {
  std::printf("\n=== OP_MSG_F2V scalar ===\n");
  reset(d);
  d->rsp_ready_i = 0;

  issue_cmd(d, OP_MSG_F2V, FACTOR_SCALAR, DIM_1, DIM_1, 0,
            0xAABB, 0x1000, 0.0f, 0.0f, 1e-12f, 0, 0);

  // OST_MSG_STATIC: eta_i=3, L_ii=2, L_io=1, old_eta=0, old_L=0
  uint32_t static_data[16] = {0};
  static_data[0] = f2u(3.0f);
  static_data[1] = f2u(2.0f);
  static_data[2] = f2u(1.0f);
  static_data[3] = f2u(0.0f);
  static_data[4] = f2u(0.0f);
  send_operand_beat(d, OST_MSG_STATIC, static_data, 1, 0xAABB, 0);

  // Cavity: factor_o (eta=1,L=1), belief_o (eta=1,L=1), old_to_o (eta=0,L=0)
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

  bool ok = wait_rsp(d, 5000);
  check(ok, "msg scalar: rsp_valid_o");
  if (!ok) return;

  check(d->rsp_op_id_o == 0xAABB, "msg scalar: op_id echoed");
  check(d->rsp_dst_addr_o == 0x1000, "msg scalar: dst_addr echoed");
  check(d->rsp_fail_o == 0, "msg scalar: fail=0");
  check(d->rsp_stream_error_o == 0, "msg scalar: stream_error=0");
  check(d->rsp_msg_dim_o == DIM_1, "msg scalar: msg_dim=DIM_1");

  float eta = u2f(d->rsp_msg_eta_flat_o[0]);
  float L   = u2f(d->rsp_msg_L_flat_o[0]);
  std::printf("  msg scalar: eta=%g L=%g\n", eta, L);
  check(std::fabs(eta - 2.0f) < 0.05f, "msg scalar: eta=2");
  check(std::fabs(L   - 1.5f) < 0.05f, "msg scalar: L=1.5");

  consume_rsp(d);
}

// ============================================================
static void test_belief_scalar(Vgbp_compute_core_top* d) {
  std::printf("\n=== OP_BELIEF scalar ===\n");
  reset(d);
  d->rsp_ready_i = 0;

  issue_cmd(d, OP_BELIEF, FACTOR_SCALAR, DIM_1, DIM_1, 0,
            0xBEEF, 0x2000, 0.0f, 0.0f, 1e-12f, 0, 1);

  // OST_BELIEF_PRIOR: eta=1, L=2, mu_old=3
  uint32_t prior[16] = {0};
  prior[0] = f2u(1.0f);
  prior[1] = f2u(2.0f);
  prior[2] = f2u(3.0f);
  send_operand_beat(d, OST_BELIEF_PRIOR, prior, 1, 0xBEEF, 0);

  // One incoming message: eta=10, L=11
  uint32_t msg[16] = {0};
  msg[0] = f2u(10.0f);
  msg[1] = f2u(11.0f);
  send_operand_beat(d, OST_BELIEF_MSG, msg, 1, 0xBEEF, 0);

  bool ok = wait_rsp(d, 5000);
  check(ok, "belief scalar: rsp_valid_o");
  if (!ok) return;

  check(d->rsp_op_id_o == 0xBEEF, "belief scalar: op_id echoed");
  check(d->rsp_fail_o == 0, "belief scalar: fail=0");
  check(d->rsp_stream_error_o == 0, "belief scalar: stream_error=0");
  check(d->rsp_bel_dim_o == DIM_1, "belief scalar: bel_dim=DIM_1");

  float eta = u2f(d->rsp_bel_eta_flat_o[0]);
  float L   = u2f(d->rsp_bel_L_flat_o[0]);
  float mu  = u2f(d->rsp_bel_mu_flat_o[0]);
  float residual = u2f(d->rsp_bel_residual_o);
  std::printf("  belief scalar: eta=%g L=%g mu=%g residual=%g\n", eta, L, mu, residual);
  check(std::fabs(eta - 11.0f) < 0.05f, "belief scalar: eta=11");
  check(std::fabs(L   - 13.0f) < 0.05f, "belief scalar: L=13");
  check(std::fabs(mu  - (11.0f / 13.0f)) < 0.05f, "belief scalar: mu=11/13");
  check(std::fabs(residual - std::pow((11.0f / 13.0f) - 3.0f, 2.0f)) < 0.05f,
        "belief scalar: residual");

  consume_rsp(d);
}

// ============================================================
static void test_illegal_cmd(Vgbp_compute_core_top* d) {
  std::printf("\n=== illegal command ===\n");
  reset(d);
  d->rsp_ready_i = 0;

  // OP_RELIN_CHECK is illegal when ENABLE_RELIN_P=0
  issue_cmd(d, OP_RELIN_CHECK, FACTOR_SCALAR, DIM_1, DIM_1, 0,
            0xDEAD, 0x3000, 0.0f, 0.0f, 1e-12f, 0, 0);

  bool ok = wait_rsp(d, 100);
  check(ok, "illegal: rsp_valid_o");
  if (!ok) return;
  check(d->rsp_fail_o == 1, "illegal: fail=1");
  check(d->rsp_stream_error_o == 0, "illegal: stream_error=0");
  consume_rsp(d);
}

// ============================================================
static void test_stream_error_msg(Vgbp_compute_core_top* d) {
  std::printf("\n=== message stream error ===\n");
  reset(d);
  d->rsp_ready_i = 0;

  issue_cmd(d, OP_MSG_F2V, FACTOR_SCALAR, DIM_1, DIM_1, 0,
            0xCAFE, 0x4000, 0.0f, 0.0f, 1e-12f, 0, 0);

  uint32_t static_data[16] = {0};
  static_data[0] = f2u(3.0f);
  static_data[1] = f2u(2.0f);
  static_data[2] = f2u(1.0f);
  send_operand_beat(d, OST_MSG_STATIC, static_data, 1, 0xCAFE, 0);

  // Send CAV_BELIEF_O before CAV_FACTOR_O -> stream error
  uint32_t bad[16] = {0};
  bad[0] = f2u(1.0f);
  bad[1] = f2u(1.0f);
  send_operand_beat(d, OST_CAV_BELIEF_O, bad, 1, 0xCAFE, 0);

  bool ok = wait_rsp(d, 500);
  check(ok, "msg stream error: rsp_valid_o");
  if (!ok) return;
  check(d->rsp_stream_error_o == 1, "msg stream error: stream_error=1");
  consume_rsp(d);
}

// ============================================================
static void test_pivot_fail_msg(Vgbp_compute_core_top* d) {
  std::printf("\n=== message pivot fail ===\n");
  reset(d);
  d->rsp_ready_i = 0;

  issue_cmd(d, OP_MSG_F2V, FACTOR_SCALAR, DIM_1, DIM_1, 0,
            0xBAD0, 0x5000, 0.0f, 0.0f, 0.0f, 0, 0);

  // Static: eta_i=3, L_ii=2, L_io=1
  uint32_t static_data[16] = {0};
  static_data[0] = f2u(3.0f);
  static_data[1] = f2u(2.0f);
  static_data[2] = f2u(1.0f);
  send_operand_beat(d, OST_MSG_STATIC, static_data, 1, 0xBAD0, 0);

  // Cavity with zero precision -> A_cav = 0, pivot will fail
  uint32_t zero[16] = {0};
  send_operand_beat(d, OST_CAV_FACTOR_O, zero, 1, 0xBAD0, 0);
  send_operand_beat(d, OST_CAV_BELIEF_O, zero, 1, 0xBAD0, 0);
  send_operand_beat(d, OST_CAV_OLD_TO_O, zero, 1, 0xBAD0, 0);

  bool ok = wait_rsp(d, 5000);
  check(ok, "pivot fail: rsp_valid_o");
  if (!ok) return;
  check(d->rsp_fail_o == 1, "pivot fail: fail=1");
  consume_rsp(d);
}

// ============================================================
static void test_msg_3x3_identity(Vgbp_compute_core_top* d) {
  std::printf("\n=== OP_MSG_F2V 3x3 identity ===\n");
  reset(d);
  d->rsp_ready_i = 0;

  issue_cmd(d, OP_MSG_F2V, FACTOR_SE2, DIM_3, DIM_3, 0,
            0xD1D1, 0x3000, 0.0f, 0.0f, 1e-12f, 0, 0);

  // Static layout: eta_i(3), L_ii_packed(6), L_io_dense(9), old_eta(3), old_L_packed(6)
  std::vector<uint32_t> stat;
  // eta_i = [1,0,0]
  stat.push_back(f2u(1.0f)); stat.push_back(f2u(0.0f)); stat.push_back(f2u(0.0f));
  // L_ii = I upper tri
  stat.push_back(f2u(1.0f)); stat.push_back(f2u(0.0f)); stat.push_back(f2u(0.0f));
  stat.push_back(f2u(1.0f)); stat.push_back(f2u(0.0f)); stat.push_back(f2u(1.0f));
  // L_io = I row-major
  for (int i = 0; i < 9; i++)
    stat.push_back(f2u((i % 4 == 0) ? 1.0f : 0.0f));
  // old_eta = [0,0,0]
  stat.push_back(f2u(0.0f)); stat.push_back(f2u(0.0f)); stat.push_back(f2u(0.0f));
  // old_L = 0
  for (int i = 0; i < 6; i++) stat.push_back(f2u(0.0f));
  send_operand_stream(d, OST_MSG_STATIC, stat, 0xD1D1);

  // Cavities: eta=[1,0,0], L=I upper tri
  std::vector<uint32_t> cav;
  cav.push_back(f2u(1.0f)); cav.push_back(f2u(0.0f)); cav.push_back(f2u(0.0f));
  cav.push_back(f2u(1.0f)); cav.push_back(f2u(0.0f)); cav.push_back(f2u(0.0f));
  cav.push_back(f2u(1.0f)); cav.push_back(f2u(0.0f)); cav.push_back(f2u(1.0f));

  send_operand_stream(d, OST_CAV_FACTOR_O, cav, 0xD1D1);
  send_operand_stream(d, OST_CAV_BELIEF_O, cav, 0xD1D1);

  std::vector<uint32_t> cav_old(9, f2u(0.0f));
  send_operand_stream(d, OST_CAV_OLD_TO_O, cav_old, 0xD1D1);

  bool ok = wait_rsp(d, 5000);
  check(ok, "msg 3x3: rsp_valid_o");
  if (!ok) return;

  check(d->rsp_fail_o == 0, "msg 3x3: fail=0");
  check(d->rsp_stream_error_o == 0, "msg 3x3: stream_error=0");
  check(d->rsp_msg_dim_o == DIM_3, "msg 3x3: msg_dim=DIM_3");

  // Expected: eta_raw = [0,0,0], L_raw = 0.5*I upper tri
  float eta0 = u2f(d->rsp_msg_eta_flat_o[0]);
  float eta1 = u2f(d->rsp_msg_eta_flat_o[1]);
  float eta2 = u2f(d->rsp_msg_eta_flat_o[2]);
  float L00  = u2f(d->rsp_msg_L_flat_o[0]);
  float L01  = u2f(d->rsp_msg_L_flat_o[1]);
  float L02  = u2f(d->rsp_msg_L_flat_o[2]);
  float L11  = u2f(d->rsp_msg_L_flat_o[3]);
  float L12  = u2f(d->rsp_msg_L_flat_o[4]);
  float L22  = u2f(d->rsp_msg_L_flat_o[5]);
  std::printf("  msg 3x3: eta=[%g,%g,%g] L=[%g,%g,%g,%g,%g,%g]\n",
              eta0, eta1, eta2, L00, L01, L02, L11, L12, L22);
  check(std::fabs(eta0) < 0.05f, "msg 3x3: eta0=0");
  check(std::fabs(eta1) < 0.05f, "msg 3x3: eta1=0");
  check(std::fabs(eta2) < 0.05f, "msg 3x3: eta2=0");
  check(std::fabs(L00 - 0.5f) < 0.05f, "msg 3x3: L00=0.5");
  check(std::fabs(L11 - 0.5f) < 0.05f, "msg 3x3: L11=0.5");
  check(std::fabs(L22 - 0.5f) < 0.05f, "msg 3x3: L22=0.5");
  check(std::fabs(L01) < 0.05f, "msg 3x3: L01=0");
  check(std::fabs(L02) < 0.05f, "msg 3x3: L02=0");
  check(std::fabs(L12) < 0.05f, "msg 3x3: L12=0");

  consume_rsp(d);
}

// ============================================================
static void test_belief_3x3(Vgbp_compute_core_top* d) {
  std::printf("\n=== OP_BELIEF 3x3 ===\n");
  reset(d);
  d->rsp_ready_i = 0;

  issue_cmd(d, OP_BELIEF, FACTOR_SE2, DIM_3, DIM_3, 0,
            0xD2D2, 0x4000, 0.0f, 0.0f, 1e-12f, 0, 2);

  // Prior: eta=[1,0,0], L=I upper tri, mu_old=[0,0,0] -> 12 words
  std::vector<uint32_t> prior;
  prior.push_back(f2u(1.0f)); prior.push_back(f2u(0.0f)); prior.push_back(f2u(0.0f));
  prior.push_back(f2u(1.0f)); prior.push_back(f2u(0.0f)); prior.push_back(f2u(0.0f));
  prior.push_back(f2u(1.0f)); prior.push_back(f2u(0.0f)); prior.push_back(f2u(1.0f));
  prior.push_back(f2u(0.0f)); prior.push_back(f2u(0.0f)); prior.push_back(f2u(0.0f));
  send_operand_stream(d, OST_BELIEF_PRIOR, prior, 0xD2D2);

  // Two incoming messages, each in its own beat.
  // Only the final beat has last=1 so the unpacker marks the final message.
  uint32_t msg_beat[16] = {0};

  // Message 1: eta=[1,0,0], L=I upper tri
  msg_beat[0] = f2u(1.0f); msg_beat[1] = f2u(0.0f); msg_beat[2] = f2u(0.0f);
  msg_beat[3] = f2u(1.0f); msg_beat[4] = f2u(0.0f); msg_beat[5] = f2u(0.0f);
  msg_beat[6] = f2u(1.0f); msg_beat[7] = f2u(0.0f); msg_beat[8] = f2u(1.0f);
  send_operand_beat(d, OST_BELIEF_MSG, msg_beat, 0, 0xD2D2, 0);

  // Message 2: eta=[0,1,0], L=I upper tri
  for (int i = 0; i < 16; i++) msg_beat[i] = 0;
  msg_beat[0] = f2u(0.0f); msg_beat[1] = f2u(1.0f); msg_beat[2] = f2u(0.0f);
  msg_beat[3] = f2u(1.0f); msg_beat[4] = f2u(0.0f); msg_beat[5] = f2u(0.0f);
  msg_beat[6] = f2u(1.0f); msg_beat[7] = f2u(0.0f); msg_beat[8] = f2u(1.0f);
  send_operand_beat(d, OST_BELIEF_MSG, msg_beat, 1, 0xD2D2, 1);

  bool ok = wait_rsp(d, 5000);
  check(ok, "belief 3x3: rsp_valid_o");
  if (!ok) return;

  check(d->rsp_fail_o == 0, "belief 3x3: fail=0");
  check(d->rsp_stream_error_o == 0, "belief 3x3: stream_error=0");
  check(d->rsp_bel_dim_o == DIM_3, "belief 3x3: bel_dim=DIM_3");

  float eta0 = u2f(d->rsp_bel_eta_flat_o[0]);
  float eta1 = u2f(d->rsp_bel_eta_flat_o[1]);
  float eta2 = u2f(d->rsp_bel_eta_flat_o[2]);
  float mu0  = u2f(d->rsp_bel_mu_flat_o[0]);
  float mu1  = u2f(d->rsp_bel_mu_flat_o[1]);
  float mu2  = u2f(d->rsp_bel_mu_flat_o[2]);
  float residual = u2f(d->rsp_bel_residual_o);
  std::printf("  belief 3x3: eta=[%g,%g,%g] mu=[%g,%g,%g] residual=%g\n",
              eta0, eta1, eta2, mu0, mu1, mu2, residual);
  // prior L = I, two incoming messages each L = I => accumulated L = 3*I
  // mu = L^{-1} * eta = [2/3, 1/3, 0]
  // residual = ||mu - mu_old||^2 = (2/3)^2 + (1/3)^2 = 5/9
  check(std::fabs(eta0 - 2.0f) < 0.05f, "belief 3x3: eta0=2");
  check(std::fabs(eta1 - 1.0f) < 0.05f, "belief 3x3: eta1=1");
  check(std::fabs(eta2 - 0.0f) < 0.05f, "belief 3x3: eta2=0");
  check(std::fabs(mu0 - (2.0f/3.0f)) < 0.05f, "belief 3x3: mu0=2/3");
  check(std::fabs(mu1 - (1.0f/3.0f)) < 0.05f, "belief 3x3: mu1=1/3");
  check(std::fabs(mu2 - 0.0f) < 0.05f, "belief 3x3: mu2=0");
  check(std::fabs(residual - (5.0f/9.0f)) < 0.05f, "belief 3x3: residual=5/9");

  consume_rsp(d);
}

// ============================================================
static void test_belief_6x6_identity(Vgbp_compute_core_top* d) {
  std::printf("\n=== OP_BELIEF 6x6 identity ===\n");
  reset(d);
  d->rsp_ready_i = 0;

  // dim_i=2 -> DIM_6; factor type does not matter for OP_BELIEF.
  issue_cmd(d, OP_BELIEF, FACTOR_SE3, DIM_6, DIM_6, 0,
            0xD6D6, 0x5000, 0.0f, 0.0f, 1e-12f, 0, 0);

  // Prior: eta=[1..6], L=I upper tri, mu_old=[0..0] -> 33 words, 3 beats.
  const int dim = 6;
  const int p   = dim * (dim + 1) / 2;
  std::vector<uint32_t> prior;
  for (int i = 0; i < dim; i++) prior.push_back(f2u(float(i + 1)));
  for (int row = 0; row < dim; row++) {
    for (int col = row; col < dim; col++) {
      prior.push_back(f2u((row == col) ? 1.0f : 0.0f));
    }
  }
  for (int i = 0; i < dim; i++) prior.push_back(f2u(0.0f));
  send_operand_stream(d, OST_BELIEF_PRIOR, prior, 0xD6D6);

  bool ok = wait_rsp(d, 5000);
  check(ok, "belief 6x6: rsp_valid_o");
  if (!ok) return;

  check(d->rsp_op_id_o == 0xD6D6, "belief 6x6: op_id echoed");
  check(d->rsp_fail_o == 0, "belief 6x6: fail=0");
  check(d->rsp_stream_error_o == 0, "belief 6x6: stream_error=0");
  check(d->rsp_bel_dim_o == DIM_6, "belief 6x6: bel_dim=DIM_6");

  float eta[6], mu[6], L[21];
  for (int i = 0; i < dim; i++) eta[i] = u2f(d->rsp_bel_eta_flat_o[i]);
  for (int i = 0; i < p;    i++) L[i]   = u2f(d->rsp_bel_L_flat_o[i]);
  for (int i = 0; i < dim; i++) mu[i]  = u2f(d->rsp_bel_mu_flat_o[i]);
  float residual = u2f(d->rsp_bel_residual_o);

  std::printf("  belief 6x6: eta=[%g,%g,%g,%g,%g,%g] mu=[%g,%g,%g,%g,%g,%g] residual=%g\n",
              eta[0], eta[1], eta[2], eta[3], eta[4], eta[5],
              mu[0], mu[1], mu[2], mu[3], mu[4], mu[5], residual);

  for (int i = 0; i < dim; i++)
    check(std::fabs(eta[i] - float(i + 1)) < 0.05f, "belief 6x6: eta");
  for (int i = 0; i < p; i++) {
    bool diag = (i == 0 || i == 6 || i == 11 || i == 15 || i == 18 || i == 20);
    float exp = diag ? 1.0f : 0.0f;
    check(std::fabs(L[i] - exp) < 0.05f, "belief 6x6: L identity");
  }
  for (int i = 0; i < dim; i++)
    check(std::fabs(mu[i] - float(i + 1)) < 0.05f, "belief 6x6: mu=eta");
  check(std::fabs(residual - 91.0f) < 0.5f, "belief 6x6: residual=91");

  consume_rsp(d);
}

// ============================================================
static void test_msg_damped(Vgbp_compute_core_top* d) {
  std::printf("\n=== OP_MSG_F2V scalar damped ===\n");
  reset(d);
  d->rsp_ready_i = 0;

  issue_cmd(d, OP_MSG_F2V, FACTOR_SCALAR, DIM_1, DIM_1, 0,
            0xD3D3, 0x5000, 0.25f, 0.0f, 1e-12f, 0, 0);

  // Static: eta_i=3, L_ii=2, L_io=1, old_eta=10, old_L=20
  uint32_t static_data[16] = {0};
  static_data[0] = f2u(3.0f);
  static_data[1] = f2u(2.0f);
  static_data[2] = f2u(1.0f);
  static_data[3] = f2u(10.0f);
  static_data[4] = f2u(20.0f);
  send_operand_beat(d, OST_MSG_STATIC, static_data, 1, 0xD3D3, 0);

  uint32_t cav_factor[16] = {0};
  cav_factor[0] = f2u(1.0f);
  cav_factor[1] = f2u(1.0f);
  send_operand_beat(d, OST_CAV_FACTOR_O, cav_factor, 1, 0xD3D3, 0);

  uint32_t cav_belief[16] = {0};
  cav_belief[0] = f2u(1.0f);
  cav_belief[1] = f2u(1.0f);
  send_operand_beat(d, OST_CAV_BELIEF_O, cav_belief, 1, 0xD3D3, 0);

  uint32_t cav_old[16] = {0};
  cav_old[0] = f2u(0.0f);
  cav_old[1] = f2u(0.0f);
  send_operand_beat(d, OST_CAV_OLD_TO_O, cav_old, 1, 0xD3D3, 0);

  bool ok = wait_rsp(d, 5000);
  check(ok, "msg damped: rsp_valid_o");
  if (!ok) return;

  check(d->rsp_fail_o == 0, "msg damped: fail=0");
  float eta = u2f(d->rsp_msg_eta_flat_o[0]);
  float L   = u2f(d->rsp_msg_L_flat_o[0]);
  std::printf("  msg damped: eta=%g L=%g\n", eta, L);
  // raw eta=2, raw L=1.5; damped = 0.75*raw + 0.25*old
  check(std::fabs(eta - 4.0f) < 0.05f, "msg damped: eta=4");
  check(std::fabs(L   - 6.125f) < 0.05f, "msg damped: L=6.125");

  consume_rsp(d);
}

// ============================================================
static void test_belief_degree_mismatch(Vgbp_compute_core_top* d) {
  std::printf("\n=== OP_BELIEF degree mismatch ===\n");
  reset(d);
  d->rsp_ready_i = 0;

  issue_cmd(d, OP_BELIEF, FACTOR_SCALAR, DIM_1, DIM_1, 0,
            0xD4D4, 0x6000, 0.0f, 0.0f, 1e-12f, 0, 2);

  uint32_t prior[16] = {0};
  prior[0] = f2u(1.0f);
  prior[1] = f2u(2.0f);
  prior[2] = f2u(0.0f);
  send_operand_beat(d, OST_BELIEF_PRIOR, prior, 1, 0xD4D4, 0);

  // Send two messages but neither is marked last, while degree=2.
  // This triggers the "msg_count == degree && !msg_last" mismatch case.
  uint32_t msg[16] = {0};
  msg[0] = f2u(10.0f);
  msg[1] = f2u(11.0f);
  send_operand_beat(d, OST_BELIEF_MSG, msg, 0, 0xD4D4, 0);
  msg[0] = f2u(0.0f);
  msg[1] = f2u(0.0f);
  send_operand_beat(d, OST_BELIEF_MSG, msg, 0, 0xD4D4, 1);

  bool ok = wait_rsp(d, 5000);
  check(ok, "belief mismatch: rsp_valid_o");
  if (!ok) return;

  check(d->rsp_degree_mismatch_o == 1, "belief mismatch: degree_mismatch=1");
  consume_rsp(d);
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_compute_core_top;

  std::printf("========================================\n");
  std::printf("gbp_compute_core unit tests\n");
  std::printf("========================================\n");

  test_msg_scalar(dut);
  test_belief_scalar(dut);
  test_illegal_cmd(dut);
  test_stream_error_msg(dut);
  test_pivot_fail_msg(dut);
  test_msg_3x3_identity(dut);
  test_belief_3x3(dut);
  test_belief_6x6_identity(dut);
  test_msg_damped(dut);
  test_belief_degree_mismatch(dut);

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
