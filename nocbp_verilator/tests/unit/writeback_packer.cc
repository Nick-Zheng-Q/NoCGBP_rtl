// writeback_packer.cc
// Unit test for writeback_packer

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Vwriteback_packer_top.h"

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

static void tick(Vwriteback_packer_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Vwriteback_packer_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

void test_writeback_packer(Vwriteback_packer_top* d) {
  std::printf("\n=== writeback_packer ===\n");

  reset(d);

  d->wbp_valid_i = 1;
  d->wbp_wb_ready_i = 1;
  d->wbp_rsp_op_i = 0;
  d->wbp_rsp_dst_addr_i = 0x100;
  d->wbp_msg_dim_i = 0;
  d->wbp_msg_eta_flat_i[0] = f2u(5.0f);
  d->wbp_msg_L_flat_i[0] = f2u(3.0f);
  d->wbp_msg_fail_i = 0;
  d->wbp_bel_dim_i = 0;
  d->wbp_bel_fail_i = 0;
  d->wbp_bel_residual_i = 0;
  tick(d);
  for (int i = 0; i < 5; i++) tick(d);

  check(d->wbp_wb_valid_o == 1, "WBP: valid");
  check(d->wbp_wb_addr_o == 0x100, "WBP: addr=0x100");
  check(d->wbp_wb_nwords_o == 2, "WBP: nwords=E(1)=2");
  check(d->wbp_wb_kind_o == 0, "WBP: kind=WB_MSG");
  check(u2f(d->wbp_wb_payload_flat_o[0]) == 5.0f, "WBP: payload[0]=5");
  check(u2f(d->wbp_wb_payload_flat_o[1]) == 3.0f, "WBP: payload[1]=3");
  check(d->wbp_wb_fail_o == 0, "WBP: fail=0");

  d->wbp_wb_ready_i = 0;
  tick(d);
  check(d->wbp_ready_o == 0, "WBP: stalled");
}

void test_writeback_packer_dim6_belief(Vwriteback_packer_top* d) {
  std::printf("\n=== writeback_packer: dim6 belief ===\n");

  reset(d);

  // Build identity L_packed for dim6: 21 scalars.
  float L_packed[21] = {0};
  int idx = 0;
  for (int r = 0; r < 6; r++) {
    for (int c = r; c < 6; c++) {
      L_packed[idx++] = (c == r) ? 1.0f : 0.0f;
    }
  }

  float eta[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  float mu[6]  = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  float residual = 0.125f;

  d->wbp_valid_i = 1;
  d->wbp_wb_ready_i = 1;
  d->wbp_rsp_op_i = 1;            // OP_BELIEF
  d->wbp_rsp_dst_addr_i = 0x120;
  d->wbp_bel_dim_i = 2;           // DIM_6 in gbp_dim_e
  for (int i = 0; i < 6; i++) {
    d->wbp_bel_eta_flat_i[i] = f2u(eta[i]);
    d->wbp_bel_mu_flat_i[i]  = f2u(mu[i]);
  }
  for (int i = 0; i < 21; i++) {
    d->wbp_bel_L_flat_i[i] = f2u(L_packed[i]);
  }
  d->wbp_bel_residual_i = f2u(residual);
  d->wbp_bel_fail_i = 0;

  tick(d);
  d->wbp_valid_i = 0;

  // Wait for the residual pipeline to finish.
  int cycles = 0;
  while (!d->wbp_wb_valid_o && cycles < 200) {
    tick(d);
    cycles++;
  }

  // E(6) = 6 + 21 = 27; nwords = E + dim + residual = 34.
  check(d->wbp_wb_valid_o == 1, "WBP dim6: valid");
  check(d->wbp_wb_addr_o == 0x120, "WBP dim6: addr=0x120");
  check(d->wbp_wb_nwords_o == 34, "WBP dim6: nwords=34");
  check(d->wbp_wb_kind_o == 1, "WBP dim6: kind=WB_BELIEF");

  bool layout_ok = true;
  for (int i = 0; i < 6 && layout_ok; i++) {
    if (u2f(d->wbp_wb_payload_flat_o[i]) != eta[i]) layout_ok = false;
  }
  for (int i = 0; i < 21 && layout_ok; i++) {
    if (u2f(d->wbp_wb_payload_flat_o[6 + i]) != L_packed[i]) layout_ok = false;
  }
  for (int i = 0; i < 6 && layout_ok; i++) {
    if (u2f(d->wbp_wb_payload_flat_o[27 + i]) != mu[i]) layout_ok = false;
  }
  check(layout_ok, "WBP dim6: eta/L/mu layout");
  check(u2f(d->wbp_wb_payload_flat_o[33]) == residual, "WBP dim6: residual");
  check(d->wbp_wb_fail_o == 0, "WBP dim6: fail=0");
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vwriteback_packer_top;

  std::printf("========================================\n");
  std::printf("writeback_packer unit tests\n");
  std::printf("========================================\n");

  test_writeback_packer(dut);
  test_writeback_packer_dim6_belief(dut);

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
