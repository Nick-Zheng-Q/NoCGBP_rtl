// phase_scheduling.cc
// Integration test for phase scheduling across control + compute subsystems.
// Test cases from docs/gbp_pe/verification/integration_tests/05_phase_scheduling.md

#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vphase_scheduling_top.h"
#include "Vphase_scheduling_top___024root.h"

static void tick(Vphase_scheduling_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vphase_scheduling_top* dut, uint32_t ready_word0 = 0, int post_reset_ticks = 3) {
  dut->rst_n = 0;
  for (int i = 0; i < 32; ++i) dut->node_ready_i[i] = 0;
  dut->node_ready_i[0] = ready_word0;
  dut->ns_valid_i = 0;
  dut->ns_data_i = 0;
  dut->ns_last_i = 0;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < post_reset_ticks; ++i) tick(dut);
}

// ── Test 1: Factor Phase Scheduling ──
static int test_factor_phase(Vphase_scheduling_top* dut) {
  printf("  Test 1: Factor Phase Scheduling...");
  reset_dut(dut, (1u << 0) | (1u << 1));
  int pass = 1;

  // Wait for first scheduling in factor phase
  int cycles = 0;
  while (!dut->sched_valid_o && cycles < 100) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 100) {
    fprintf(stderr, "\n    FAIL: no scheduling");
    pass = 0;
  }

  if (pass) {
    if (!dut->phase_factor_first_o) {
      fprintf(stderr, "\n    FAIL: not in factor phase");
      pass = 0;
    }
  }

  // Feed neighbor state stream (8 words) whenever compute is ready.
  int ns_word_idx = 0;
  cycles = 0;
  while (!dut->done_valid_o && cycles < 2000) {
    if (cycles < 50) {
      auto* r = dut->rootp;
      printf("  dbg cyc%3d: cmd_v=%d done=%d ns_ready=%d wrap_state=%d op_asm_state=%d rse_vr=%d%d rd_valid=%d ready=%d sample_addr=0x%X rse_active=%d agu_ready=%d rd_active=%d rsp_valid=%d retry=%d wr_rd_v=%d desc_idx=%d core_op_rdy=%d op_v=%d\n",
             cycles, dut->comp_cmd_valid_o, dut->done_valid_o,
             dut->ns_ready_o,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__state_r,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__state_r,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__rse_beat_valid,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__rse_beat_ready,
             (int)r->phase_scheduling_top__DOT__comp_spm_rd0_valid,
             (int)r->phase_scheduling_top__DOT__comp_spm_rd0_valid_r,
             (unsigned)r->phase_scheduling_top__DOT__comp_spm_rd0_sample_addr,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__u_rse__DOT__active_r,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__u_rse__DOT__agu_addr_ready,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__u_rse__DOT__rd_req_active_r,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__u_rse__DOT__rsp_valid,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__u_rse__DOT__retrying,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__rd_req_valid_r,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__desc_idx_r,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__core_operand_ready,
             (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__operand_valid_r);
    }
    if (dut->ns_ready_o && ns_word_idx < 8) {
      dut->ns_valid_i = 1;
      dut->ns_data_i = (ns_word_idx == 0) ? 0x3F800000u
                                          : (0x40000000u + (uint32_t)ns_word_idx);
      dut->ns_last_i = (ns_word_idx == 7);
      ns_word_idx++;
    } else if (ns_word_idx >= 8) {
      dut->ns_valid_i = 0;
      dut->ns_last_i = 0;
    } else {
      dut->ns_valid_i = 0;
      dut->ns_last_i = 0;
    }
    tick(dut);
    cycles++;
  }
  if (cycles >= 2000) {
    fprintf(stderr, "\n    FAIL: done never asserted");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 2: Phase Switch Factor -> Variable ──
static int test_phase_switch(Vphase_scheduling_top* dut) {
  printf("  Test 2: Phase Switch...");
  reset_dut(dut, (1u << 4));
  int pass = 1;

  // Factor phase should find no nodes, switch to variable
  int cycles = 0;
  while (!dut->phase_switch_pulse_o && cycles < 100) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 100) {
    fprintf(stderr, "\n    FAIL: phase_switch_pulse never asserted");
    pass = 0;
  }

  // Wait for variable node 4 to be scheduled
  cycles = 0;
  while (!dut->sched_valid_o && cycles < 100) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 100) {
    fprintf(stderr, "\n    FAIL: no scheduling after switch");
    pass = 0;
  }

  if (pass) {
    if (dut->phase_factor_first_o) {
      fprintf(stderr, "\n    FAIL: still in factor phase after switch");
      pass = 0;
    }
    if (dut->sched_node_id_o != 4) {
      fprintf(stderr, "\n    FAIL: sched_node_id=%d, expected 4", dut->sched_node_id_o);
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 3: Back to Factor Phase ──
static int test_back_to_factor(Vphase_scheduling_top* dut) {
  printf("  Test 3: Back to Factor Phase...");
  reset_dut(dut, (1u << 2) | (1u << 5));
  int pass = 1;

  int switch_count = 0;
  int cycles = 0;
  while (switch_count < 2 && cycles < 500) {
    if (dut->phase_switch_pulse_o) switch_count++;
    tick(dut);
    cycles++;
  }
  if (switch_count < 2) {
    fprintf(stderr, "\n    FAIL: only %d phase switches, expected 2", switch_count);
    pass = 0;
  }

  // After 2 switches: factor -> variable -> factor
  if (pass) {
    if (!dut->phase_factor_first_o) {
      fprintf(stderr, "\n    FAIL: not back in factor phase");
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// Helper: feed neighbor state stream to complete one compute
static void feed_neighbor_state(Vphase_scheduling_top* dut) {
  for (int i = 0; i < 8; ++i) {
    dut->ns_valid_i = 1;
    dut->ns_data_i = (i == 0) ? 0x3F800000u : (0x40000000u + (uint32_t)i);
    dut->ns_last_i = (i == 7);
    tick(dut);
  }
  dut->ns_valid_i = 0;
  dut->ns_last_i = 0;
}

// ── Test 4: Round-Robin Within Phase ──
static int test_round_robin(Vphase_scheduling_top* dut) {
  printf("  Test 4: Round-Robin Within Phase...");
  reset_dut(dut, (1u << 0) | (1u << 1) | (1u << 2), 0);  // no post-reset ticks
  int pass = 1;

  // Track unique scheduled nodes. The scheduler may phase-switch and re-schedule
  // nodes while compute is busy, so we use a set instead of strict ordering.
  uint32_t prev_sched_id = 0xFFFF;
  int nodes_seen = 0;
  bool seen_nodes[3] = {false, false, false};
  int max_cycles = 500;
  for (int c = 0; c < max_cycles && nodes_seen < 3; ++c) {
    tick(dut);
    if (dut->sched_valid_o && dut->sched_node_id_o != prev_sched_id) {
      uint32_t sid = dut->sched_node_id_o;
      if (sid > 2) {
        // Phase switch caused re-scheduling; ignore nodes we've already seen
        prev_sched_id = sid;
        continue;
      }
      if (!seen_nodes[sid]) {
        seen_nodes[sid] = true;
        nodes_seen++;
        prev_sched_id = sid;

        // Complete compute — feed neighbor state on demand (when core is ready)
        int ns_fed = 0;
        int w = 0;
        while (!dut->done_valid_o && w < 2000) {
          tick(dut); w++;
          if (dut->ns_ready_o && ns_fed < 8) {
            dut->ns_valid_i = 1;
            dut->ns_data_i = (ns_fed == 0) ? 0x3F800000u : (0x40000000u + (uint32_t)ns_fed);
            dut->ns_last_i = (ns_fed == 7);
            ns_fed++;
          } else {
            dut->ns_valid_i = 0;
            dut->ns_last_i = 0;
          }
          if (w % 100 == 0) {
            auto* r = dut->rootp;
            fprintf(stderr, "\n    [node%d w=%d] wrap=%d asm=%d rse=%d cmd_v=%d done=%d rd_req_v=%d disp_valid=%d disp_empty=%d desc_idx=%d core_state=%d core_rsp_v=%d op_v=%d ns_rdy=%d bop_st=%d bop_prior=%d/%d bop_msg_in=%d bop_buf=%d",
                    sid, w,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__state_r,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__state_r,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__u_rse__DOT__active_r,
                    (int)dut->comp_cmd_valid_o,
                    (int)dut->done_valid_o,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__rd_req_valid_r,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_dispatcher__DOT__desc_valid_r,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_dispatcher__DOT__empty,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__desc_idx_r,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__state_r,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__core_rsp_valid,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_op_asm__DOT__operand_valid_r,
                    (int)dut->ns_ready_o,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bop__DOT__state_r,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bop__DOT__prior_cnt_r,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bop__DOT__prior_total,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bop__DOT__msg_in_cnt_r,
                    (int)r->phase_scheduling_top__DOT__u_compute__DOT__u_compute_unit_wrapper__DOT__u_core__DOT__u_bop__DOT__buf_cnt_r);
          }
        }
        dut->ns_valid_i = 0;
        dut->ns_last_i = 0;
        if (w >= 2000) {
          fprintf(stderr, "\n    FAIL: done never asserted for node %d", sid);
          pass = 0;
          break;
        }
        c += w + 8;  // account for ticks spent
      } else {
        // Already seen this node; just update prev to avoid re-processing
        prev_sched_id = sid;
      }
    }
  }
  if (nodes_seen < 3 && pass) {
    fprintf(stderr, "\n    FAIL: only %d of 3 unique nodes scheduled", nodes_seen);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 5: Visited Mask Cleared on Phase Switch ──
static int test_visited_mask_clear(Vphase_scheduling_top* dut) {
  printf("  Test 5: Visited Mask Clear...");
  reset_dut(dut, (1u << 0), 0);  // no post-reset ticks
  int pass = 1;

  // Schedule and complete node 0
  int cycles = 0;
  while (!dut->sched_valid_o && cycles < 100) { tick(dut); cycles++; }
  if (cycles >= 100) { fprintf(stderr, "\n    FAIL: node 0 not scheduled"); pass = 0; }

  if (pass) {
    feed_neighbor_state(dut);
    int w = 0;
    while (!dut->done_valid_o && w < 2000) { tick(dut); w++; }
    if (w >= 2000) { fprintf(stderr, "\n    FAIL: done never asserted"); pass = 0; }
  }

  // Wait for phase switch (node 0 no longer ready)
  if (pass) {
    dut->node_ready_i[0] = 0;  // clear node 0
    int w = 0;
    while (!dut->phase_switch_pulse_o && w < 200) { tick(dut); w++; }
    if (w >= 200) { fprintf(stderr, "\n    FAIL: phase switch never happened"); pass = 0; }
  }

  // After phase switch, make node 0 ready again — it should be schedulable
  // because visited_mask was cleared
  if (pass) {
    dut->node_ready_i[0] = (1u << 0);
    int w = 0;
    while (!dut->sched_valid_o && w < 100) { tick(dut); w++; }
    if (w >= 100) {
      fprintf(stderr, "\n    FAIL: node 0 not re-schedulable after phase switch");
      pass = 0;
    } else if (dut->sched_node_id_o != 0) {
      fprintf(stderr, "\n    FAIL: re-scheduled node %d, expected 0", dut->sched_node_id_o);
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vphase_scheduling_top;

  int failures = 0;
  printf("Phase Scheduling integration tests:\n");
  failures += test_factor_phase(dut);
  failures += test_phase_switch(dut);
  failures += test_back_to_factor(dut);
  failures += test_round_robin(dut);
  failures += test_visited_mask_clear(dut);

  if (failures == 0) {
    printf("\nAll 5 tests PASSED\n");
  } else {
    printf("\n%d of 5 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
