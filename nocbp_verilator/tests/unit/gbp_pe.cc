// gbp_pe.cc - Unit test for gbp_pe (top-level whitebox)
// Rewritten for the new descriptor-driven compute core (compute_unit_wrapper).
//
// The new core supports dimensions {1,3,6}.  This test focuses on the
// degree-0 belief path (prior -> solve -> writeback), which is the V0
// supported end-to-end datapath.  The remote-notification path is still
// exercised with the whitebox force-done override.

#include "../common/test_utils.hpp"
#include "Vgbp_pe_top.h"
#include "Vgbp_pe_top___024root.h"
#include "debug.h"
#include "ldlt_golden.hpp"
#include "verilated.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace test_utils;

static uint32_t f2u(float f) {
  union {
    float f;
    uint32_t u;
  } c;
  c.f = f;
  return c.u;
}
static float u2f(uint32_t u) {
  union {
    float f;
    uint32_t u;
  } c;
  c.u = u;
  return c.f;
}

// NoC link_sif helpers (VlWide<5>, 133 bits)
static inline void link_sif_set_fwd_ready(Vgbp_pe_top *dut) {
  dut->link_sif_i.m_storage[4] |= (1u << 3);
}
static inline void link_sif_clear(Vgbp_pe_top *dut) {
  for (int i = 0; i < 5; i++)
    dut->link_sif_i.m_storage[i] = 0;
}
static inline bool link_sif_fwd_v(const Vgbp_pe_top *dut) {
  return (dut->link_sif_o.m_storage[4] >> 4) & 1u;
}

// SPM backdoor helpers.  addr is a 32-bit word address.
static uint64_t *spm_bank_ptr(Vgbp_pe_top *dut, uint32_t addr) {
  uint32_t bank_id = (addr >> 1) & 0x7;
  uint32_t row = (addr >> 4) & 0x3FFF;
  auto *r = dut->rootp;
  switch (bank_id) {
  case 0:
    return &r->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__banks__BRA__0__KET____DOT__u_bank__DOT__mem_r
                .m_storage[row];
  case 1:
    return &r->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__banks__BRA__1__KET____DOT__u_bank__DOT__mem_r
                .m_storage[row];
  case 2:
    return &r->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__banks__BRA__2__KET____DOT__u_bank__DOT__mem_r
                .m_storage[row];
  case 3:
    return &r->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__banks__BRA__3__KET____DOT__u_bank__DOT__mem_r
                .m_storage[row];
  case 4:
    return &r->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__banks__BRA__4__KET____DOT__u_bank__DOT__mem_r
                .m_storage[row];
  case 5:
    return &r->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__banks__BRA__5__KET____DOT__u_bank__DOT__mem_r
                .m_storage[row];
  case 6:
    return &r->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__banks__BRA__6__KET____DOT__u_bank__DOT__mem_r
                .m_storage[row];
  case 7:
    return &r->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__banks__BRA__7__KET____DOT__u_bank__DOT__mem_r
                .m_storage[row];
  }
  return nullptr;
}

static void spm_write_word(Vgbp_pe_top *dut, uint32_t addr, uint32_t data) {
  uint64_t *p = spm_bank_ptr(dut, addr);
  if (!p) {
    printf("ERROR: invalid SPM addr 0x%08X\n", addr);
    return;
  }
  uint64_t beat = *p;
  if ((addr & 1) == 0) {
    beat = (beat & 0xFFFFFFFF00000000ULL) | (uint64_t)data;
  } else {
    beat = (beat & 0x00000000FFFFFFFFULL) | ((uint64_t)data << 32);
  }
  *p = beat;
}

static uint32_t spm_read_word(Vgbp_pe_top *dut, uint32_t addr) {
  uint64_t *p = spm_bank_ptr(dut, addr);
  if (!p)
    return 0xDEADBEEF;
  uint64_t beat = *p;
  return ((addr & 1) == 0) ? (uint32_t)(beat & 0xFFFFFFFFULL)
                           : (uint32_t)(beat >> 32);
}

static void spm_write_float(Vgbp_pe_top *dut, uint32_t addr, float f) {
  spm_write_word(dut, addr, f2u(f));
}

static float spm_read_float(Vgbp_pe_top *dut, uint32_t addr) {
  return u2f(spm_read_word(dut, addr));
}

static bool float_eq(float a, float b, float eps = 0.01f) {
  return std::fabs(a - b) <= eps;
}

// Number of packed upper-triangular elements for dimension d
static int packed_count(int d) { return d * (d + 1) / 2; }

// Number of scalars in a belief prior stream: eta(d) + L_packed(P(d)) +
// mu_old(d)
static int belief_prior_words(int d) { return d + packed_count(d) + d; }

// Packed upper-triangular row-major index for (row, col), row <= col
static int packed_index(int row, int col, int d) {
  return row * d - row * (row - 1) / 2 + (col - row);
}

// Write a belief prior into SPM starting at base_addr.
// eta[0..d-1], L_packed (upper-triangular row-major), mu_old[0..d-1].
static void write_belief_prior(Vgbp_pe_top *dut, uint32_t base_addr, int d,
                               const float *eta, const float *L_packed,
                               const float *mu_old) {
  int p = packed_count(d);
  uint32_t addr = base_addr;
  for (int i = 0; i < d; i++)
    spm_write_float(dut, addr++, eta[i]);
  for (int i = 0; i < p; i++)
    spm_write_float(dut, addr++, L_packed[i]);
  for (int i = 0; i < d; i++)
    spm_write_float(dut, addr++, mu_old[i]);
  // Clear remaining words in the first 16-word operand beat so the assembler
  // does not feed X values from uninitialized SPM.
  for (int i = 0; i < 16; i++)
    spm_write_word(dut, base_addr + i, 0);
  // Rewrite the prior on top of the cleared region.
  addr = base_addr;
  for (int i = 0; i < d; i++)
    spm_write_float(dut, addr++, eta[i]);
  for (int i = 0; i < p; i++)
    spm_write_float(dut, addr++, L_packed[i]);
  for (int i = 0; i < d; i++)
    spm_write_float(dut, addr++, mu_old[i]);
}

// Build an identity L_packed for dimension d.
static void identity_L_packed(int d, float *L_packed) {
  int p = packed_count(d);
  for (int i = 0; i < p; i++)
    L_packed[i] = 0.0f;
  for (int i = 0; i < d; i++) {
    int idx = packed_index(i, i, d);
    L_packed[idx] = 1.0f;
  }
}

// -----------------------------------------------------------------------------
// C++ probes for the degree-0 belief solve datapath.
// These dump internal RTL state and compare against the golden LDLT model.
// All output is gated by NOCBP_DEBUG and goes to stderr.
// -----------------------------------------------------------------------------

static void probe_belief_pipeline(Vgbp_pe_top *dut, const char *tc_name, int d,
                                  uint32_t base) {
  auto *r = dut->rootp;

  // ---------------------------------------------------------------------------
  // 1) LDLT inputs (from belief_solve_adapter registers)
  // ---------------------------------------------------------------------------
  int p = packed_count(d);
  std::vector<ldlt::fp32_t> A_flat(p);
  std::vector<std::vector<float>> B(d, std::vector<float>(1, 0.0f));
  NOCBP_DBG("[%s] packed_accumulator acc[0..%d] (eta):", tc_name, d - 1);
  for (int i = 0; i < d; i++) {
    uint32_t u =
        r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_acc__DOT__acc
            [i];
    NOCBP_DBG(" %g", u2f(u));
  }
  NOCBP_DBG("\n");
  NOCBP_DBG("[%s] BSA A_r (packed L, %d scalars):", tc_name, p);
  for (int i = 0; i < p; i++) {
    uint32_t u =
        r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bsa__DOT__A_r
            [i];
    A_flat[i] = u;
    NOCBP_DBG(" %g", u2f(u));
  }
  NOCBP_DBG("\n");
  NOCBP_DBG("[%s] BSA B_r (RHS eta):\n", tc_name);
  for (int row = 0; row < 6; row++) {
    uint32_t u =
        r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bsa__DOT__B_r
            [row][0];
    if (row < d)
      B[row][0] = u2f(u);
    NOCBP_DBG("  row%d: 0x%08x (%g)%s\n", row, u, u2f(u),
              (row < d) ? " used" : " unused");
  }
  NOCBP_DBG("[%s] bsa_B_flat_o (RHS to ldlt, first %d rows):\n", tc_name, d);
  for (int row = 0; row < d; row++) {
    uint32_t u =
        r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__bsa_B_flat
            .m_storage[row * 7 + 0];
    NOCBP_DBG("  row%d: 0x%08x (%g)\n", row, u, u2f(u));
  }

  // ---------------------------------------------------------------------------
  // 2) LDLT output (held in the compute core flat wire)
  // ---------------------------------------------------------------------------
  std::vector<std::vector<float>> X(d, std::vector<float>(1, 0.0f));
  NOCBP_DBG("[%s] ldlt_X_flat (solution x):\n", tc_name);
  for (int row = 0; row < d; row++) {
    uint32_t u =
        r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__ldlt_X_flat
            .m_storage[row * 7 + 0];
    X[row][0] = u2f(u);
    NOCBP_DBG("  row%d: %g (0x%08x)\n", row, X[row][0], u);
  }

  // Golden reference for the observed inputs.
  ldlt::Result golden = ldlt::solve(A_flat, B, d, 1, false);
  NOCBP_DBG("[%s] Golden x (for observed A/B):\n", tc_name);
  for (int row = 0; row < d; row++) {
    NOCBP_DBG("  row%d: %g\n", row, golden.x[row][0]);
  }
  for (int row = 0; row < d; row++) {
    if (std::fabs(X[row][0] - golden.x[row][0]) > 0.01f) {
      NOCBP_DBG("[%s] MISMATCH: ldlt_X[%d]=%g, golden=%g\n", tc_name, row,
                X[row][0], golden.x[row][0]);
    }
  }

  // ---------------------------------------------------------------------------
  // 3) Belief result builder output
  // ---------------------------------------------------------------------------
  NOCBP_DBG("[%s] brb mu_r:\n", tc_name);
  for (int i = 0; i < d; i++) {
    uint32_t u =
        r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_brb__DOT__mu_r
            [i];
    float mu = u2f(u);
    NOCBP_DBG("  mu[%d]: %g (0x%08x) expected %g\n", i, mu, u, X[i][0]);
  }

  // ---------------------------------------------------------------------------
  // 4) Writeback packer payload
  // ---------------------------------------------------------------------------
  int e = d + p; // E(d) = eta_count + packed_L_count
  uint16_t nwords_r =
      (uint16_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_wbp__DOT__nwords_r;
  uint16_t wse_count =
      (uint16_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_wse__DOT__desc_count_r;
  uint16_t wse_word_cnt =
      (uint16_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_wse__DOT__word_cnt_r;
  uint16_t wb_word_cnt =
      (uint16_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_wb_adapter__DOT__word_cnt_r;
  uint16_t adapter_nwords =
      (uint16_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_wb_adapter__DOT__wb_r
          .nwords;
  uint8_t wse_active =
      (uint8_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_wse__DOT__active_r;
  uint8_t wse_pending =
      (uint8_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_wse__DOT__write_pending_r;
  uint8_t agu_active =
      (uint8_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_wse__DOT__u_agu__DOT__active_r;
  uint16_t agu_cnt =
      (uint16_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_wse__DOT__u_agu__DOT__cnt_r;
  uint32_t agu_addr =
      (uint32_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_wse__DOT__agu_addr;
  uint8_t arb_wr_grant3 =
      (uint8_t)((r->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__u_spm_arbiter__DOT__wr_grant_oh >>
                 3) &
                1);
  uint8_t arb_wr_grant5 =
      (uint8_t)((r->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__u_spm_arbiter__DOT__wr_grant_oh >>
                 5) &
                1);
  uint8_t arb_bank_wr =
      (uint8_t)r
          ->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__u_spm_arbiter__DOT__bank_wr_en_tmp;
  NOCBP_DBG("[%s] wbp nwords_r=%d adapter_nwords=%d wse_desc_count=%d "
            "wse_word_cnt=%d wb_word_cnt=%d wse_active=%d pending=%d "
            "agu_active=%d agu_cnt=%d agu_addr=0x%x wr_grant3=%d wr_grant5=%d "
            "bank_wr=0x%x payload_r (d=%d, E=%d, P=%d):\n",
            tc_name, nwords_r, adapter_nwords, wse_count, wse_word_cnt,
            wb_word_cnt, wse_active, wse_pending, agu_active, agu_cnt, agu_addr,
            arb_wr_grant3, arb_wr_grant5, arb_bank_wr, d, e, p);
  for (int i = 0; i < d; i++) {
    uint32_t u =
        r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_wbp__DOT__payload_r
            [i];
    NOCBP_DBG("  payload[%2d] eta[%d]: %g\n", i, i, u2f(u));
  }
  for (int i = 0; i < p; i++) {
    uint32_t u =
        r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_wbp__DOT__payload_r
            [d + i];
    NOCBP_DBG("  payload[%2d] L[%d]: %g\n", d + i, i, u2f(u));
  }
  for (int i = 0; i < d; i++) {
    uint32_t u =
        r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_wbp__DOT__payload_r
            [e + i];
    NOCBP_DBG("  payload[%2d] mu[%d]: %g\n", e + i, i, u2f(u));
  }
  uint32_t residual_u =
      r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_wbp__DOT__payload_r
          [e + d];
  NOCBP_DBG("  payload[%2d] residual: %g\n", e + d, u2f(residual_u));

  // ---------------------------------------------------------------------------
  // 5) Core latched result registers and final SPM contents
  // ---------------------------------------------------------------------------
  int final_core =
      (int)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__state_r;
  int final_brb =
      (int)r
          ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_brb__DOT__state_r;
  NOCBP_DBG("[%s] final core state=%d brb state=%d\n", tc_name, final_core,
            final_brb);
  NOCBP_DBG("[%s] core bel_mu_r:\n", tc_name);
  for (int i = 0; i < d; i++) {
    uint32_t u =
        r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__bel_mu_r
            [i];
    NOCBP_DBG("  bel_mu[%d]: %g\n", i, u2f(u));
  }
  NOCBP_DBG("[%s] SPM result @ base=0x%03X:\n", tc_name, base);
  for (int i = 0; i < d; i++) {
    float mu = spm_read_float(dut, base + e + i);
    NOCBP_DBG("  addr+%2d mu[%d]: %g expected %g\n", e + i, i, mu, B[i][0]);
  }
}

// Issue a whitebox belief command and wait for completion.
// Returns 0 on success, non-zero on failure.
static int run_belief_command(Vgbp_pe_top *dut, uint32_t node_id, int dof,
                              int state_words, const char *tc_name,
                              bool expect_reset = true) {
  if (!dut->wb_cmd_ready_o) {
    return fail(tc_name, "wb_cmd_ready_o not high before command injection");
  }

  dut->wb_cmd_adj_is_local_i = 0xFF; // all local -> no remote notifications
  dut->wb_cmd_valid_i = 1;
  dut->wb_cmd_node_id_i = node_id & 0x3FF;
  dut->wb_cmd_is_factor_i = 0; // belief
  dut->wb_cmd_dof_i = dof & 0xF;
  dut->wb_cmd_adj_count_i = 0;
  dut->wb_cmd_state_words_i = state_words & 0x1FF;
  dut->wb_cmd_neighbor_dofs_i = 0;
  tick(dut);
  dut->wb_cmd_valid_i = 0;

  bool done_seen = false;
  bool reset_seen = false;
  int cycle;
  const int max_cycles = 10000;
  for (cycle = 0; cycle < max_cycles; cycle++) {
    tick(dut);
    if (dut->wb_done_valid_o && !done_seen)
      done_seen = true;
    if (dut->reset_valid_o && !reset_seen)
      reset_seen = true;
    if (NOCBP_DEBUG) {
      auto *r = dut->rootp;

      // Per-cycle trace for first 10 cycles of each command
      if (cycle < 10) {
        int wr_ptr =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_dispatcher__DOT__wr_ptr;
        int rd_ptr =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_dispatcher__DOT__rd_ptr;
        int asm_state =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__state_r;
        int rse_active =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__active_r;
        int rse_desc_valid =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__rse_desc_valid;
        int rse_desc_ready =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__rse_desc_ready;
        uint32_t agu_addr =
            (uint32_t)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__agu_addr;
        int desc_valid =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__rse_desc_valid;
        int desc_ready =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__rse_desc_ready;
        // Read FIFO entry at rd_ptr
        int fifo_rd_idx = rd_ptr & 0x7;
        uint64_t w0 =
            r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_dispatcher__DOT__fifo
                [fifo_rd_idx]
                    .m_storage[0];
        uint64_t w1 =
            r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_dispatcher__DOT__fifo
                [fifo_rd_idx]
                    .m_storage[1];
        uint32_t fifo_base =
            (uint32_t)(((w0 >> 16) & 0x3FFFF) | ((w1 & 0xF) << 18));
        uint32_t fifo_nbeats = (uint32_t)(w0 & 0xFFFF);
        NOCBP_DBG("[%s] c%d wr=%d rd=%d empty=%d asm=%d rse=%d "
                  "d_val=%d d_rdy=%d d_hsk=%d "
                  "fifo[%d]=0x%x/%d agu=0x%x\n",
                  tc_name, cycle, wr_ptr, rd_ptr, (wr_ptr == rd_ptr), asm_state,
                  rse_active, desc_valid, desc_ready, desc_valid && desc_ready,
                  fifo_rd_idx, fifo_base, fifo_nbeats, agu_addr);
      }
      static int last_wrap = -1, last_core = -1, last_ldlt = -1;
      int wrap =
          (int)r
              ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__state_r;
      int core =
          (int)r
              ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__state_r;
      int ldlt =
          (int)r
              ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_ldlt__DOT__state_r;
      int prev_core = last_core;
      if (wrap != last_wrap || core != last_core || ldlt != last_ldlt) {
        int acc_state =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_acc__DOT__state_r;
        NOCBP_DBG(
            "%s c%4d wrap=%d core=%d(dim=%d) acc=%d ldlt=%d(k=%d i=%d j=%d "
            "r=%d)\n",
            tc_name, cycle, wrap, core,
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__cmd_r
                .dim_i,
            acc_state, ldlt,
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_ldlt__DOT__k_r,
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_ldlt__DOT__i_r,
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_ldlt__DOT__j_r,
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_ldlt__DOT__r_r);
        last_wrap = wrap;
        last_core = core;
        last_ldlt = ldlt;
      }
      // Trace assembler beat completion (data_buffer_r after each 16-word
      // beat).
      {
        static int last_asm_valid = 0;
        int asm_valid =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__operand_valid_r;
        if (asm_valid && !last_asm_valid) {
          int beat_idx =
              (int)r
                  ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__beat_idx_r;
          NOCBP_DBG("[%s] asm_beat c%d beat_idx=%d data_buffer=[", tc_name,
                    cycle, beat_idx);
          for (int i = 0; i < 16; i++) {
            uint32_t u =
                r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__data_buffer_r
                    .m_storage[i];
            NOCBP_DBG(" %g", u2f(u));
          }
          NOCBP_DBG(" ]\n");
        }
        last_asm_valid = asm_valid;
      }

      // Detect RSE descriptor acceptance handshake (rse_desc_valid &&
      // rse_desc_ready) At this moment, rd_ptr has already advanced, so the
      // consumed entry is at rd_ptr-1.
      {
        static int last_desc_hsk = 0;
        int rse_desc_valid =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__rse_desc_valid;
        int rse_desc_ready =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__rse_desc_ready;
        int desc_hsk = rse_desc_valid && rse_desc_ready;
        if (desc_hsk && !last_desc_hsk) {
          int wr_ptr =
              (int)r
                  ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_dispatcher__DOT__wr_ptr;
          int rd_ptr =
              (int)r
                  ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_dispatcher__DOT__rd_ptr;
          int consumed_idx = (rd_ptr - 1) & 0x7;
          uint64_t w0 =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_dispatcher__DOT__fifo
                  [consumed_idx]
                      .m_storage[0];
          uint64_t w1 =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_dispatcher__DOT__fifo
                  [consumed_idx]
                      .m_storage[1];
          uint32_t nbeats = (uint32_t)(w0 & 0xFFFF);
          uint32_t base_addr =
              (uint32_t)(((w0 >> 16) & 0x3FFFF) | ((w1 & 0xF) << 18));
          uint32_t kind = (uint32_t)((w0 >> 34) & 0xF);
          uint32_t op_id =
              (uint32_t)(((w0 >> 38) & 0x3FFFFFFF) | ((w1 & 0x3F) << 26));
          int asm_state =
              (int)r
                  ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__state_r;
          uint32_t agu_addr =
              (uint32_t)r
                  ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__agu_addr;
          int rse_active =
              (int)r
                  ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__active_r;
          NOCBP_DBG("[%s] DESC_HSK c%d asm_state=%d rse_active=%d "
                    "wr_ptr=%d rd_ptr=%d consumed_idx=%d "
                    "fifo_entry: op_id=0x%x kind=%d base_addr=0x%x nbeats=%d "
                    "agu_addr=0x%x desc_base_r=0x%x\n",
                    tc_name, cycle, asm_state, rse_active, wr_ptr, rd_ptr,
                    consumed_idx, op_id, kind, base_addr, nbeats, agu_addr);
        }
        last_desc_hsk = desc_hsk;
      }

      // -----------------------------------------------------------------
      // Trace RSE (read_stream_engine) SPM read activity
      // -----------------------------------------------------------------
      {
        static int last_rse_active = 0;
        int rse_active =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__active_r;
        if (rse_active && !last_rse_active) {
          uint32_t agu_addr =
              (uint32_t)r
                  ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__agu_addr;
          NOCBP_DBG("[%s] RSE START c%d agu_addr=0x%x\n", tc_name, cycle,
                    agu_addr);
        }
        if (!rse_active && last_rse_active) {
          NOCBP_DBG("[%s] RSE DONE c%d\n", tc_name, cycle);
        }
        last_rse_active = rse_active;
      }

      // Trace RSE response events (arbiter returning data)
      {
        static int last_rsp_valid = 0;
        int rsp_valid =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__rsp_valid;
        int rd_req_active =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__rd_req_active_r;
        int retrying =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__retrying;
        uint32_t issued_addr =
            (uint32_t)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__issued_addr_r;
        uint32_t agu_addr =
            (uint32_t)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__agu_addr;
        int arb_grant_oh =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__u_spm_arbiter__DOT__rd_grant_oh;
        int arb_grant_r =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_memory_subsystem__DOT__u_spm_arbiter__DOT__rd_grant_r;
        int mem_rd_valid = (int)r->gbp_pe_top__DOT__dut__DOT__mem_rd_valid;
        // Print on rsp_valid rising edge or when retrying
        if ((rsp_valid && !last_rsp_valid) || retrying) {
          // Read the actual 64-bit data from arbiter to compute subsystem
          uint64_t rd_data_c1 = r->gbp_pe_top__DOT__dut__DOT__comp_spm_rd0_data;
          uint32_t lo = (uint32_t)(rd_data_c1 & 0xFFFFFFFF);
          uint32_t hi = (uint32_t)(rd_data_c1 >> 32);
          NOCBP_DBG("[%s] rse_rd c%d rsp=%d retry=%d req_act=%d "
                    "issued_addr=0x%x agu_addr=0x%x "
                    "arb_grant_oh=0x%x arb_grant_r=0x%x "
                    "mem_rd_valid=0x%x data=[%g %g] raw=0x%016llx\n",
                    tc_name, cycle, rsp_valid, retrying, rd_req_active,
                    issued_addr, agu_addr, arb_grant_oh, arb_grant_r,
                    mem_rd_valid, u2f(lo), u2f(hi),
                    (unsigned long long)rd_data_c1);
        }
        last_rsp_valid = rsp_valid;
      }

      // Trace skid buffer state (RSE backpressure indicator)
      {
        static int last_skid_valid = 0;
        int skid_valid =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__skid_valid_r;
        if (skid_valid && !last_skid_valid) {
          uint64_t skid_data =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_op_asm__DOT__u_rse__DOT__skid_data_r;
          uint32_t lo = (uint32_t)(skid_data & 0xFFFFFFFF);
          uint32_t hi = (uint32_t)(skid_data >> 32);
          NOCBP_DBG("[%s] rse_skid c%d FILL data=[%g %g] raw=0x%016llx\n",
                    tc_name, cycle, u2f(lo), u2f(hi),
                    (unsigned long long)skid_data);
        }
        if (!skid_valid && last_skid_valid) {
          NOCBP_DBG("[%s] rse_skid c%d DRAIN\n", tc_name, cycle);
        }
        last_skid_valid = skid_valid;
      }

      // Trace operand beat data while core is consuming the prior stream.
      if (core == 8 || core == 9) {
        static int last_bop_wr = -1;
        int bop_wr =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bop__DOT__wr_ptr_r;
        int bop_state =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bop__DOT__state_r;
        int bop_pcnt =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bop__DOT__prior_cnt_r;
        if (bop_wr != last_bop_wr) {
          last_bop_wr = bop_wr;
          NOCBP_DBG("[%s] bop_write c%d state=%d wr=%d pcnt=%d buf=[", tc_name,
                    cycle, bop_state, bop_wr, bop_pcnt);
          for (int i = 0; i < 33; i++) {
            uint32_t u =
                r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bop__DOT__buf_data
                    .m_storage[i];
            NOCBP_DBG(" %g", u2f(u));
          }
          NOCBP_DBG(" ]\n");
        }
      }

      // Snapshot the belief-solve-adapter latch point.
      if (prev_core != 10 && core == 10) {
        int core_dim =
            (int)r
                ->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__cmd_r
                .dim_i;
        uint8_t bsa_valid_r =
            r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bsa__DOT__valid_r;
        NOCBP_DBG("[%s] snapshot ST_BEL_B3_ADAPT c%d core_dim=%d prev_core=%d "
                  "bsa_valid_r=%d acc_reg=[",
                  tc_name, cycle, core_dim, prev_core, bsa_valid_r);
        for (int i = 0; i < 6; i++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_acc__DOT__acc
                  [i];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ] acc_eta_flat=[");
        for (int i = 0; i < 6; i++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__acc_eta_flat
                  .m_storage[i];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ] acc_L_flat=[");
        for (int i = 0; i < 21; i++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__acc_L_flat
                  .m_storage[i];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ] bop_prior_L_flat=[");
        for (int i = 0; i < 21; i++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__bop_prior_L_flat
                  .m_storage[i];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ] bop_buf=[");
        for (int i = 0; i < 33; i++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bop__DOT__buf_data
                  .m_storage[i];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ] B_r[row][0]=[");
        for (int row = 0; row < 6; row++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bsa__DOT__B_r
                  [row][0];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ] B_r[0][col]=[");
        for (int col = 0; col < 6; col++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bsa__DOT__B_r
                  [0][col];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ]\n");
      }
      // Snapshot after result-builder latch.
      if (prev_core != 12 && core == 12) {
        NOCBP_DBG("[%s] snapshot ST_BEL_B5_RESULT c%d ldlt_X=[", tc_name,
                  cycle);
        for (int row = 0; row < 6; row++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__ldlt_X_flat
                  .m_storage[row * 7 + 0];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ] brb_mu_r=[");
        for (int row = 0; row < 6; row++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_brb__DOT__mu_r
                  [row];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ]\n");
      }
      // Snapshot one cycle into solve (B_r should now hold latched values).
      if (prev_core != 11 && core == 11) {
        NOCBP_DBG("[%s] snapshot ST_BEL_B4_SOLVE c%d B_r=[", tc_name, cycle);
        for (int row = 0; row < 6; row++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bsa__DOT__B_r
                  [row][0];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ]\n");
      }
      // Snapshot when core consumes brb output (ST_RSP entry).
      if (prev_core != 1 && core == 1) {
        NOCBP_DBG("[%s] snapshot ST_RSP c%d brb_mu_r=[", tc_name, cycle);
        for (int row = 0; row < 6; row++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_brb__DOT__mu_r
                  [row];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ] bel_mu_r=[");
        for (int row = 0; row < 6; row++) {
          uint32_t u =
              r->gbp_pe_top__DOT__dut__DOT__u_compute_subsystem__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__bel_mu_r
                  [row];
          NOCBP_DBG(" %g", u2f(u));
        }
        NOCBP_DBG(" ]\n");
      }
    }
    if (dut->wb_cmd_ready_o)
      break;
  }

  if (cycle >= max_cycles) {
    printf("TIMEOUT after %d cycles (done=%d reset=%d)\n", max_cycles,
           done_seen, reset_seen);
    return fail(tc_name, "belief compute did not complete within timeout");
  }
  if (!done_seen) {
    return fail(tc_name, "wb_done_valid_o never asserted");
  }
  if (expect_reset && !reset_seen) {
    return fail(tc_name, "reset_valid_o never asserted (adj_count=0 path)");
  }
  // Allow write_stream_engine to flush any residual SPM writes.
  for (int i = 0; i < 200; i++)
    tick(dut);
  return 0;
}

// =============================================================================
// TC1: degree-0 belief solve, dim=1 (numerical)
// Prior: eta=2.0, L=3.0, mu_old=0.0  =>  mu = eta / L = 0.6667
// =============================================================================
static int tc1_belief_dim1(Vgbp_pe_top *dut) {
  printf("\n=== TC1: Belief solve dim=1 (degree-0) ===\n");
  uint32_t node_id = 0x10;
  uint32_t base = (node_id << 4); // matches whitebox state_base mapping
  float eta[1] = {2.0f};
  float L_packed[1] = {3.0f};
  float mu_old[1] = {0.0f};
  write_belief_prior(dut, base, 1, eta, L_packed, mu_old);
  printf("  prior: eta=%f L=%f mu_old=%f @ base=0x%03X\n", eta[0], L_packed[0],
         mu_old[0], base);

  int rc = run_belief_command(dut, node_id, 1, belief_prior_words(1), "TC1");
  if (rc)
    return rc;
  probe_belief_pipeline(dut, "TC1", 1, base);

  // Belief writeback layout for dim=1: word0=eta, word1=L, word2=mu,
  // word3=residual
  float eta_out = spm_read_float(dut, base + 0);
  float L_out = spm_read_float(dut, base + 1);
  float mu_out = spm_read_float(dut, base + 2);
  printf("  result: eta=%f L=%f mu=%f (expected mu=%f)\n", eta_out, L_out,
         mu_out, 2.0f / 3.0f);

  if (!float_eq(eta_out, 2.0f))
    return fail("TC1", "eta mismatch");
  if (!float_eq(L_out, 3.0f))
    return fail("TC1", "L mismatch");
  if (!float_eq(mu_out, 2.0f / 3.0f, 0.02f))
    return fail("TC1", "mu mismatch");

  printf("TC1 PASS\n");
  return 0;
}

// =============================================================================
// TC2: Remote Consumer Notification Path
// Force a compute completion while adj_is_local is all-zeros and verify that
// the writeback_controller drives a NoC notification packet.
// =============================================================================
static int tc2_remote_edge(Vgbp_pe_top *dut) {
  printf("\n=== TC2: Remote Consumer Notification Path ===\n");

  link_sif_clear(dut);
  link_sif_set_fwd_ready(dut);

  if (!dut->wb_cmd_ready_o) {
    return fail("TC2", "wb_cmd_ready_o not high before injection");
  }

  dut->wb_cmd_adj_is_local_i = 0x00; // all remote
  dut->wb_cmd_valid_i = 1;
  dut->wb_cmd_node_id_i = 0x10;
  dut->wb_cmd_is_factor_i = 0; // belief
  dut->wb_cmd_dof_i = 1;
  dut->wb_cmd_adj_count_i = 1;
  dut->wb_cmd_state_words_i = belief_prior_words(1);
  tick(dut);
  dut->wb_cmd_valid_i = 0;

  // Force done to trigger the writeback_controller without waiting for compute.
  dut->wb_force_done_valid_i = 1;
  dut->clk = 0;
  dut->eval();
  bool reset_seen = dut->reset_valid_o;
  dut->clk = 1;
  dut->eval();
  dut->wb_force_done_valid_i = 0;

  bool notif_seen = false;
  bool noc_pkt_seen = false;
  int notif_cycle = -1;
  int noc_pkt_cycle = -1;
  uint32_t noc_pkt_words[5] = {0};

  const int max_cycles = 50;
  int cycle;
  for (cycle = 0; cycle < max_cycles; cycle++) {
    dut->clk = 0;
    dut->eval();
    if (dut->tx_notif_valid_o && !notif_seen) {
      notif_seen = true;
      notif_cycle = cycle;
    }
    bool noc_v = link_sif_fwd_v(dut);
    if (noc_v && !noc_pkt_seen) {
      noc_pkt_seen = true;
      noc_pkt_cycle = cycle;
      for (int i = 0; i < 5; i++)
        noc_pkt_words[i] = dut->link_sif_o.m_storage[i];
    }
    dut->clk = 1;
    dut->eval();

    auto *r = dut->rootp;
    uint8_t wb_state =
        r->gbp_pe_top__DOT__dut__DOT__u_writeback_controller__DOT__state_r;
    if (wb_state == 0 && cycle > 2)
      break;
  }

  if (cycle >= max_cycles) {
    auto *r = dut->rootp;
    uint8_t wb_state =
        r->gbp_pe_top__DOT__dut__DOT__u_writeback_controller__DOT__state_r;
    printf(
        "TIMEOUT after %d cycles (reset=%d notif=%d noc_pkt=%d) wb_state=%d\n",
        max_cycles, reset_seen, notif_seen, noc_pkt_seen, wb_state);
    return fail("TC2", "writeback did not complete within timeout");
  }

  if (!reset_seen)
    return fail("TC2", "reset_valid_o never asserted on force-done cycle");
  if (!notif_seen)
    return fail("TC2", "tx_notif_valid_o never asserted");
  if (!noc_pkt_seen)
    return fail("TC2", "NoC fwd packet never observed");

  printf("  reset_valid = 1 (force-done cycle)\n");
  printf("  notif_valid @ cycle %d\n", notif_cycle);
  printf("  NoC pkt out @ cycle %d\n", noc_pkt_cycle);

  unsigned __int128 pkt_val =
      (unsigned __int128)(noc_pkt_words[1] >> 20) |
      ((unsigned __int128)noc_pkt_words[2] << 12) |
      ((unsigned __int128)noc_pkt_words[3] << 44) |
      ((unsigned __int128)(noc_pkt_words[4] & 0x7) << 76);
  uint32_t pkt_op = (pkt_val >> 59) & 0xF;
  if (pkt_op != 0x2) {
    return fail("TC2", "NoC packet op mismatch (expected e_remote_sw=2)");
  }
  printf("TC2 PASS\n");
  return 0;
}

// =============================================================================
// TC3: degree-0 belief solve, dim=3 (identity L => mu = eta)
// =============================================================================
static int tc3_belief_dim3(Vgbp_pe_top *dut) {
  printf("\n=== TC3: Belief solve dim=3 (degree-0, identity L) ===\n");
  uint32_t node_id = 0x11;
  uint32_t base = (node_id << 4);
  float eta[3] = {1.0f, 2.0f, 3.0f};
  float L_packed[packed_count(3)];
  identity_L_packed(3, L_packed);
  float mu_old[3] = {0.0f, 0.0f, 0.0f};
  write_belief_prior(dut, base, 3, eta, L_packed, mu_old);
  printf("  prior: eta=[%f,%f,%f] with L=I @ base=0x%03X\n", eta[0], eta[1],
         eta[2], base);

  int rc = run_belief_command(dut, node_id, 3, belief_prior_words(3), "TC3");
  if (rc)
    return rc;
  probe_belief_pipeline(dut, "TC3", 3, base);

  // Belief writeback layout: word0..2=eta, word3..8=L_packed, word9..11=mu
  for (int i = 0; i < 3; i++) {
    float eta_out = spm_read_float(dut, base + i);
    if (!float_eq(eta_out, eta[i])) {
      char msg[128];
      snprintf(msg, sizeof(msg), "eta[%d]=%f expected %f", i, eta_out, eta[i]);
      return fail("TC3", msg);
    }
  }
  for (int i = 0; i < packed_count(3); i++) {
    float L_out = spm_read_float(dut, base + 3 + i);
    if (!float_eq(L_out, L_packed[i])) {
      char msg[128];
      snprintf(msg, sizeof(msg), "L_packed[%d]=%f expected %f", i, L_out,
               L_packed[i]);
      return fail("TC3", msg);
    }
  }
  for (int i = 0; i < 3; i++) {
    float mu_out = spm_read_float(dut, base + 9 + i);
    if (!float_eq(mu_out, eta[i])) {
      char msg[128];
      snprintf(msg, sizeof(msg), "mu[%d]=%f expected %f", i, mu_out, eta[i]);
      return fail("TC3", msg);
    }
  }
  printf("TC3 PASS\n");
  return 0;
}

// =============================================================================
// TC4: degree-0 belief solve, dim=6 (multi-beat operand stream, identity L)
// prior_total = 6 + 21 + 6 = 33 scalars => 3 operand beats.
// =============================================================================
static int tc4_belief_dim6(Vgbp_pe_top *dut) {
  printf(
      "\n=== TC4: Belief solve dim=6 (degree-0, identity L, multi-beat) ===\n");
  uint32_t node_id = 0x12;
  uint32_t base = (node_id << 4);
  float eta[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  float L_packed[packed_count(6)];
  identity_L_packed(6, L_packed);
  float mu_old[6] = {0};
  write_belief_prior(dut, base, 6, eta, L_packed, mu_old);
  printf("  prior: eta=[1..6] with L=I @ base=0x%03X (%d scalars)\n", base,
         belief_prior_words(6));
  printf("  SPM readback before command:");
  for (int i = 0; i < belief_prior_words(6); i++) {
    printf(" %g", spm_read_float(dut, base + i));
  }
  printf("\n");

  int rc = run_belief_command(dut, node_id, 6, belief_prior_words(6), "TC4");
  if (rc)
    return rc;
  probe_belief_pipeline(dut, "TC4", 6, base);

  printf("  SPM readback after command:");
  for (int i = 0; i < 34; i++) {
    printf(" %g", spm_read_float(dut, base + i));
  }
  printf("\n");

  // Writeback layout: word0..5=eta, word6..26=L_packed, word27..32=mu
  for (int i = 0; i < 6; i++) {
    float eta_out = spm_read_float(dut, base + i);
    if (!float_eq(eta_out, eta[i])) {
      char msg[128];
      snprintf(msg, sizeof(msg), "eta[%d]=%f expected %f", i, eta_out, eta[i]);
      return fail("TC4", msg);
    }
  }
  for (int i = 0; i < packed_count(6); i++) {
    float L_out = spm_read_float(dut, base + 6 + i);
    if (!float_eq(L_out, L_packed[i])) {
      char msg[128];
      snprintf(msg, sizeof(msg), "L_packed[%d]=%f expected %f", i, L_out,
               L_packed[i]);
      return fail("TC4", msg);
    }
  }
  for (int i = 0; i < 6; i++) {
    float mu_out = spm_read_float(dut, base + 27 + i);
    if (!float_eq(mu_out, eta[i])) {
      char msg[128];
      snprintf(msg, sizeof(msg), "mu[%d]=%f expected %f", i, mu_out, eta[i]);
      return fail("TC4", msg);
    }
  }
  printf("TC4 PASS\n");
  return 0;
}

// =============================================================================
// Main
// =============================================================================
int run_test(int argc, char **argv) {
  auto *dut = new Vgbp_pe_top;

  trace_init(dut, "gbp_pe.vcd");

  dut->wb_cmd_valid_i = 0;
  dut->wb_cmd_adj_is_local_i = 0xFF;
  dut->wb_force_done_valid_i = 0;
  dut->wb_lr_valid_i = 0;
  link_sif_clear(dut);
  reset_dut(dut, 10);
  for (int i = 0; i < 5; i++)
    tick(dut);

  int rc = 0;
  rc |= tc1_belief_dim1(dut);
  rc |= tc2_remote_edge(dut);
  // TC2 leaves the compute unit running a degree-1 command (only notification
  // path was verified). Reset the PE so that subsequent tests start from idle.
  reset_dut(dut, 10);
  for (int i = 0; i < 5; i++)
    tick(dut);
  rc |= tc3_belief_dim3(dut);
  rc |= tc4_belief_dim6(dut);

  if (rc == 0) {
    print_test_pass("gbp_pe");
  }
  trace_close();
  delete dut;
  return rc;
}
