// gbp_pe_control_subsystem.cc
// Functional test for control subsystem.

#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vgbp_pe_control_subsystem_top.h"

static void tick(Vgbp_pe_control_subsystem_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void set_all_nodes_ready(Vgbp_pe_control_subsystem_top* dut) {
  for (int i = 0; i < 32; ++i) dut->node_ready_i[i] = 0xFFFFFFFFu;
}

static void set_no_nodes_ready(Vgbp_pe_control_subsystem_top* dut) {
  for (int i = 0; i < 32; ++i) dut->node_ready_i[i] = 0;
}

static void reset_dut(Vgbp_pe_control_subsystem_top* dut) {
  dut->rst_n = 0;
  dut->wb_done_i = 0;
  dut->comp_cmd_ready_i = 1;
  dut->adj_ready_i = 1;
  dut->local_ready_i = 1;
  set_all_nodes_ready(dut);
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  // Note: post-reset ticks kept at 0 to avoid spurious scheduler advances.
  // Tests that need the pipeline to progress explicitly wait for outputs.
}

// ── Test 1: Verify pipeline produces comp_cmd_valid ──
static int test_cmd_production(Vgbp_pe_control_subsystem_top* dut) {
  printf("  Test 1: Command Production...");
  reset_dut(dut);
  int pass = 1;

  // Wait for first command (node_scheduler auto-selects node 0)
  int cycles = 0;
  while (!dut->comp_cmd_valid_o && cycles < 100) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 100) {
    fprintf(stderr, "\n    FAIL: comp_cmd_valid never asserted");
    pass = 0;
  }

  if (pass) {
    if (dut->comp_cmd_node_id_o != 0) {
      fprintf(stderr, "\n    FAIL: node_id=%d, expected 0", dut->comp_cmd_node_id_o);
      pass = 0;
    }
    if (dut->comp_cmd_dof_o != 3) {
      fprintf(stderr, "\n    FAIL: dof=%d, expected 3", dut->comp_cmd_dof_o);
      pass = 0;
    }
    if (dut->comp_cmd_adj_count_o != 2) {
      fprintf(stderr, "\n    FAIL: adj_count=%d, expected 2", dut->comp_cmd_adj_count_o);
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 2: Backpressure on compute cmd_ready ──
static int test_backpressure(Vgbp_pe_control_subsystem_top* dut) {
  printf("  Test 2: Backpressure...");
  reset_dut(dut);
  int pass = 1;

  dut->comp_cmd_ready_i = 0; // stall compute

  // Run a few cycles, pipeline should still advance to metadata scan
  for (int i = 0; i < 20; ++i) tick(dut);

  // Release backpressure
  dut->comp_cmd_ready_i = 1;
  tick(dut);

  // comp_cmd_valid should now be seen
  int cycles = 0;
  while (!dut->comp_cmd_valid_o && cycles < 100) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 100) {
    fprintf(stderr, "\n    FAIL: comp_cmd_valid never asserted after backpressure release");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 3: Phase switch after all nodes visited ──
static int test_phase_switch(Vgbp_pe_control_subsystem_top* dut) {
  printf("  Test 3: Phase Switch...");
  reset_dut(dut);
  int pass = 1;

  // Auto-send wb_done for each node until phase switch
  int cycles = 0;
  int wb_count = 0;
  while (!dut->phase_switch_pulse_o && cycles < 10000) {
    if (dut->comp_cmd_valid_o) {
      tick(dut); // accept cmd
      dut->wb_done_i = 1;
      tick(dut);
      dut->wb_done_i = 0;
      wb_count++;
    } else {
      tick(dut);
    }
    cycles++;
  }
  if (cycles >= 10000) {
    fprintf(stderr, "\n    FAIL: phase_switch_pulse never asserted (wb_count=%d)", wb_count);
    pass = 0;
  }

  if (pass) {
    tick(dut);
    if (dut->phase_factor_first_o != 0) {
      fprintf(stderr, "\n    FAIL: phase_factor_first=%d, expected 0", dut->phase_factor_first_o);
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 4: Multi-Edge Sequence ──
// The control subsystem has a local neighbor state reader that consumes
// local adjacencies internally. Only remote adjacencies appear on adj_valid_o.
static int test_multi_edge_sequence(Vgbp_pe_control_subsystem_top* dut) {
  printf("  Test 4: Multi-Edge Sequence...");
  reset_dut(dut);
  int pass = 1;

  // Wait for command valid
  int cycles = 0;
  while (!dut->comp_cmd_valid_o && cycles < 100) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 100) {
    fprintf(stderr, "\n    FAIL: comp_cmd_valid never asserted");
    pass = 0;
  }

  // After command, local reader should process local adj (neighbor=2)
  // and remote adj (neighbor=3) should appear on adj_valid_o.
  // Wait for adj_valid_o (remote only)
  cycles = 0;
  while (!dut->adj_valid_o && cycles < 100) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 100) {
    fprintf(stderr, "\n    FAIL: remote adj_valid never asserted");
    pass = 0;
  }

  // Remote adj: neighbor_id=3, x=2, y=1
  if (dut->adj_neighbor_id_o != 3) {
    fprintf(stderr, "\n    FAIL: remote adj_neighbor_id=%d, expected 3", dut->adj_neighbor_id_o);
    pass = 0;
  }
  if (dut->adj_is_local_o != 0) {
    fprintf(stderr, "\n    FAIL: remote adj_is_local=%d, expected 0", dut->adj_is_local_o);
    pass = 0;
  }
  if (dut->adj_edge_idx_o != 1) {
    fprintf(stderr, "\n    FAIL: remote adj_edge_idx=%d, expected 1", dut->adj_edge_idx_o);
    pass = 0;
  }
  if (!dut->adj_last_o) {
    fprintf(stderr, "\n    FAIL: remote adj_last=0, expected 1");
    pass = 0;
  }

  // Also verify local state was streamed to accumulator
  // The local reader reads neighbor 2's state (state_base=8, state_words=2)
  // We should have seen local_valid_o for 2 cycles
  // (This is hard to verify after-the-fact; we verify the remote path is correct.)

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 5: Local State Reader ──
// Verify that local adjacencies trigger SPM reads and stream data to
// the accumulator via local_valid_o / local_data_o / local_last_o.
static int test_local_state_reader(Vgbp_pe_control_subsystem_top* dut) {
  printf("  Test 5: Local State Reader...");
  reset_dut(dut);
  int pass = 1;

  // Make only node 0 ready so the scan is deterministic
  set_no_nodes_ready(dut);
  dut->node_ready_i[0] = 1;

  int local_cycles = 0;
  int saw_data8 = 0;
  int saw_data9 = 0;
  int saw_last = 0;
  int cycles = 0;

  // Watch the local stream until command is produced (indicates scan done)
  while (!dut->comp_cmd_valid_o && cycles < 200) {
    if (dut->local_valid_o) {
      // DEBUG
      // printf("\n    DEBUG cycle=%d local_data=%u last=%d spm_addr=%u", cycles, dut->local_data_o, dut->local_last_o, dut->debug_spm_rd_addr_o);
      local_cycles++;
      if (dut->local_data_o == 8) saw_data8 = 1;
      if (dut->local_data_o == 9) saw_data9 = 1;
      if (dut->local_last_o) saw_last = 1;
    }
    tick(dut);
    cycles++;
  }
  if (cycles >= 200) {
    fprintf(stderr, "\n    FAIL: comp_cmd_valid never asserted");
    pass = 0;
  }

  // Drain any trailing local_valid
  for (int i = 0; i < 10; ++i) {
    if (dut->local_valid_o) {
      local_cycles++;
      if (dut->local_data_o == 8) saw_data8 = 1;
      if (dut->local_data_o == 9) saw_data9 = 1;
      if (dut->local_last_o) saw_last = 1;
    }
    tick(dut);
  }

  if (local_cycles != 2) {
    fprintf(stderr, "\n    FAIL: local_valid asserted %d cycles, expected 2", local_cycles);
    pass = 0;
  }
  if (!saw_data8) { fprintf(stderr, "\n    FAIL: never saw local_data=8"); pass = 0; }
  if (!saw_data9) { fprintf(stderr, "\n    FAIL: never saw local_data=9"); pass = 0; }
  if (!saw_last)  { fprintf(stderr, "\n    FAIL: never saw local_last=1"); pass = 0; }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 6: Empty Queue Idle / no_schedulable_nodes ──
// With no nodes ready and all nodes eventually visited, the scheduler
// should assert no_schedulable_nodes and the phase controller should
// emit a phase_switch_pulse and toggle phase_factor_first.
static int test_empty_queue_idle(Vgbp_pe_control_subsystem_top* dut) {
  printf("  Test 6: Empty Queue Idle...");
  reset_dut(dut);
  int pass = 1;

  // No nodes ready -> scheduler will visit all nodes in discovery mode
  set_no_nodes_ready(dut);

  int cycles = 0;
  while (!dut->phase_switch_pulse_o && cycles < 3000) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 3000) {
    fprintf(stderr, "\n    FAIL: phase_switch_pulse never asserted (cycles=%d)", cycles);
    pass = 0;
  }

  if (pass) {
    if (!dut->no_schedulable_nodes_o) {
      fprintf(stderr, "\n    FAIL: no_schedulable_nodes=0 when phase_switch_pulse fired");
      pass = 0;
    }
    tick(dut);
    // Phase should have toggled from 1 (factor) to 0 (variable)
    if (dut->phase_factor_first_o != 0) {
      fprintf(stderr, "\n    FAIL: phase_factor_first=%d, expected 0", dut->phase_factor_first_o);
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 7: Visited Mask Clear ──
// After a node is scheduled and a phase switch occurs, visited_mask
// must be cleared so the node can be scheduled again.
static int test_visited_mask_clear(Vgbp_pe_control_subsystem_top* dut) {
  printf("  Test 7: Visited Mask Clear...");
  reset_dut(dut);
  int pass = 1;

  // With all nodes ready, wait for the first command and accept it.
  int cycles = 0;
  while (!dut->comp_cmd_valid_o && cycles < 100) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 100) {
    fprintf(stderr, "\n    FAIL: comp_cmd_valid never asserted");
    pass = 0;
  }

  // Accept command and pulse wb_done so the scheduler can advance.
  tick(dut);
  dut->wb_done_i = 1;
  tick(dut);
  dut->wb_done_i = 0;

  // Node 0 should now be marked visited.
  if (((dut->visited_mask_o[0] >> 0) & 1u) == 0) {
    fprintf(stderr, "\n    FAIL: visited_mask[0]=0 after scheduling");
    pass = 0;
  }

  // Drop all node readiness and let the phase drain.
  set_no_nodes_ready(dut);

  // Wait for phase switch.
  cycles = 0;
  while (!dut->phase_switch_pulse_o && cycles < 3000) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 3000) {
    fprintf(stderr, "\n    FAIL: phase_switch_pulse never asserted");
    pass = 0;
  }

  // Verify the previously visited node (node 0) is no longer marked.
  // The mask may still hold the in-flight node (node 1) if the phase switch
  // occurred while the scheduler was stalled waiting for the scanner.
  if (((dut->visited_mask_o[0] >> 0) & 1u) != 0) {
    fprintf(stderr, "\n    FAIL: visited_mask[0] still set after phase switch");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 8: Factor/Variable Priority ──
// The scheduler's sched_is_factor output follows phase_factor_first.
// Verify that in factor phase commands are marked factor and after a
// phase switch they are marked variable.
static int test_factor_variable_priority(Vgbp_pe_control_subsystem_top* dut) {
  printf("  Test 8: Factor/Variable Priority...");
  reset_dut(dut);
  int pass = 1;

  // Start: factor phase (phase_factor_first=1)
  set_all_nodes_ready(dut);

  // Wait for first command and verify it is factor
  int cycles = 0;
  while (!dut->comp_cmd_valid_o && cycles < 100) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 100) {
    fprintf(stderr, "\n    FAIL: first comp_cmd_valid never asserted");
    pass = 0;
  }
  if (pass && !dut->comp_cmd_is_factor_o) {
    fprintf(stderr, "\n    FAIL: first cmd_is_factor=0 in factor phase");
    pass = 0;
  }

  // Accept command and pulse wb_done for a few nodes to trigger phase switch quickly
  tick(dut);
  set_no_nodes_ready(dut);
  for (int i = 0; i < 5; ++i) {
    dut->wb_done_i = 1;
    tick(dut);
    dut->wb_done_i = 0;
    tick(dut);
  }

  // Wait for phase switch
  cycles = 0;
  while (!dut->phase_switch_pulse_o && cycles < 1500) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 1500) {
    fprintf(stderr, "\n    FAIL: phase_switch_pulse never asserted");
    pass = 0;
  }

  // After switch, phase_factor_first should be 0 (variable phase)
  tick(dut);
  if (dut->phase_factor_first_o != 0) {
    fprintf(stderr, "\n    FAIL: phase_factor_first=%d after switch, expected 0", dut->phase_factor_first_o);
    pass = 0;
  }

  // Make a node ready and wait for next command; it should be variable
  set_all_nodes_ready(dut);
  cycles = 0;
  while (!dut->comp_cmd_valid_o && cycles < 100) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 100) {
    fprintf(stderr, "\n    FAIL: post-switch comp_cmd_valid never asserted");
    pass = 0;
  }
  if (pass && dut->comp_cmd_is_factor_o) {
    fprintf(stderr, "\n    FAIL: post-switch cmd_is_factor=1 in variable phase");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_pe_control_subsystem_top;

  int failures = 0;
  printf("gbp_pe_control_subsystem functional tests:\n");
  failures += test_cmd_production(dut);
  failures += test_backpressure(dut);
  failures += test_phase_switch(dut);
  failures += test_multi_edge_sequence(dut);
  failures += test_local_state_reader(dut);
  failures += test_empty_queue_idle(dut);
  failures += test_visited_mask_clear(dut);
  failures += test_factor_variable_priority(dut);

  if (failures == 0) {
    printf("\nAll 8 tests PASSED\n");
  } else {
    printf("\n%d of 8 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
