// phase_controller.cc
// Unit test for phase_controller
// Test cases from docs/gbp_pe/verification/unit_tests/01_phase_controller.md

#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vphase_controller_top.h"

static void tick(Vphase_controller_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vphase_controller_top* dut, int cycles = 3) {
  dut->rst_n = 0;
  dut->no_schedulable_nodes_i = 0;
  dut->sched_valid_i = 0;
  dut->sched_node_id_i = 0;
  dut->wb_done_i = 0;
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
}

// ── Test Case 1: Normal Phase Switch ──
// Factor phase completes -> switch to variable -> switch back to factor
static int test_normal_phase_switch(Vphase_controller_top* dut) {
  printf("  Test Case 1: Normal Phase Switch...");
  reset_dut(dut);
  int pass = 1;

  // T+0: Reset deasserted, should be in FACTOR_PHASE
  if (!dut->phase_factor_first_o) {
    fprintf(stderr, "\n    FAIL at T+0: phase_factor_first=0, expected 1 (FACTOR_PHASE)");
    pass = 0;
  }
  if (dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL at T+0: phase_switch_pulse=1, expected 0");
    pass = 0;
  }

  // T+1: Assert no_schedulable_nodes -> first switch
  dut->no_schedulable_nodes_i = 1;
  tick(dut);
  // phase_switch_pulse = 1 (combinational from no_schedulable_nodes)
  // phase_factor_first still 1 (register updates next cycle)
  if (!dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL at T+1: phase_switch_pulse=0, expected 1");
    pass = 0;
  }

  // T+2: Clear no_schedulable_nodes, register updates
  dut->no_schedulable_nodes_i = 0;
  tick(dut);
  // phase_factor_first = 0 (VARIABLE_PHASE)
  if (dut->phase_factor_first_o) {
    fprintf(stderr, "\n    FAIL at T+2: phase_factor_first=1, expected 0 (VARIABLE_PHASE)");
    pass = 0;
  }
  if (dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL at T+2: phase_switch_pulse=1, expected 0");
    pass = 0;
  }

  // T+3: Assert no_schedulable_nodes again -> second switch
  dut->no_schedulable_nodes_i = 1;
  tick(dut);
  if (!dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL at T+3: phase_switch_pulse=0, expected 1");
    pass = 0;
  }

  // T+4: Clear, register updates
  dut->no_schedulable_nodes_i = 0;
  tick(dut);
  // phase_factor_first = 1 (back to FACTOR_PHASE)
  if (!dut->phase_factor_first_o) {
    fprintf(stderr, "\n    FAIL at T+4: phase_factor_first=0, expected 1 (FACTOR_PHASE)");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Continuous Scheduling ──
// Multiple nodes schedulable, no switch until exhausted
static int test_continuous_scheduling(Vphase_controller_top* dut) {
  printf("  Test Case 2: Continuous Scheduling...");
  reset_dut(dut);
  int pass = 1;

  // T+0: FACTOR_PHASE
  if (!dut->phase_factor_first_o) {
    fprintf(stderr, "\n    FAIL at T+0: phase_factor_first=0, expected 1");
    pass = 0;
  }

  // T+1..T+3: sched_valid is active (no_schedulable_nodes = 0)
  // No switch should occur
  for (int i = 1; i <= 3; ++i) {
    dut->no_schedulable_nodes_i = 0;
    tick(dut);
    if (dut->phase_switch_pulse_o) {
      fprintf(stderr, "\n    FAIL at T+%d: unexpected phase_switch_pulse=1", i);
      pass = 0;
    }
    if (!dut->phase_factor_first_o) {
      fprintf(stderr, "\n    FAIL at T+%d: phase changed unexpectedly", i);
      pass = 0;
    }
  }

  // T+4: Phase exhausted
  dut->no_schedulable_nodes_i = 1;
  tick(dut);
  if (!dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL at T+4: phase_switch_pulse=0, expected 1");
    pass = 0;
  }

  // T+5: Switch should have occurred
  dut->no_schedulable_nodes_i = 0;
  tick(dut);
  if (dut->phase_factor_first_o) {
    fprintf(stderr, "\n    FAIL at T+5: still in FACTOR_PHASE, expected VARIABLE_PHASE");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Corner Case: Reset during switch ──
static int test_reset_during_switch(Vphase_controller_top* dut) {
  printf("  Corner Case: Reset during switch...");
  int pass = 1;

  // Start normal, trigger switch
  reset_dut(dut);
  dut->no_schedulable_nodes_i = 1;
  tick(dut);
  // Now in switch state

  // Reset mid-switch
  dut->rst_n = 0;
  tick(dut);
  dut->rst_n = 1;
  dut->no_schedulable_nodes_i = 0;
  tick(dut);

  // Should be back in FACTOR_PHASE
  if (!dut->phase_factor_first_o) {
    fprintf(stderr, "\n    FAIL: not in FACTOR_PHASE after reset");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: Immediate exhaustion ──
// No schedulable nodes asserted immediately after reset.
static int test_immediate_exhaustion(Vphase_controller_top* dut) {
  printf("  Test Case 4: Immediate exhaustion...");
  reset_dut(dut);
  int pass = 1;

  dut->no_schedulable_nodes_i = 1;
  tick(dut);
  if (!dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL: expected phase_switch_pulse=1 on immediate exhaustion");
    pass = 0;
  }

  dut->no_schedulable_nodes_i = 0;
  tick(dut);
  if (dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL: expected phase_switch_pulse=0 after clearing no_sched");
    pass = 0;
  }
  if (dut->phase_factor_first_o) {
    fprintf(stderr, "\n    FAIL: expected phase to switch to VARIABLE_PHASE");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: Continuous back-to-back switches ──
// Switch factor->variable and immediately back to factor with no idle gap.
static int test_back_to_back_switches(Vphase_controller_top* dut) {
  printf("  Test Case 5: Continuous back-to-back switches...");
  reset_dut(dut);
  int pass = 1;

  // First switch: FACTOR -> VARIABLE
  dut->no_schedulable_nodes_i = 1;
  tick(dut);
  if (!dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL first pulse: expected phase_switch_pulse=1");
    pass = 0;
  }

  dut->no_schedulable_nodes_i = 0;
  tick(dut);
  if (dut->phase_factor_first_o) {
    fprintf(stderr, "\n    FAIL first switch: expected VARIABLE_PHASE");
    pass = 0;
  }

  // Second switch immediately: VARIABLE -> FACTOR
  dut->no_schedulable_nodes_i = 1;
  tick(dut);
  if (!dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL second pulse: expected phase_switch_pulse=1");
    pass = 0;
  }

  dut->no_schedulable_nodes_i = 0;
  tick(dut);
  if (!dut->phase_factor_first_o) {
    fprintf(stderr, "\n    FAIL second switch: expected FACTOR_PHASE");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 6: phase_switch_pulse single-cycle width check ──
// Assert no_schedulable_nodes for exactly one cycle and verify the pulse
// is high for only that cycle.
static int test_pulse_single_cycle_width(Vphase_controller_top* dut) {
  printf("  Test Case 6: phase_switch_pulse single-cycle width...");
  reset_dut(dut);
  int pass = 1;

  // Let a node be visited first so the visited-mask logic is exercised.
  dut->sched_valid_i = 1;
  dut->sched_node_id_i = 3;
  tick(dut);
  dut->sched_valid_i = 0;

  // Assert no_sched for exactly one cycle
  dut->no_schedulable_nodes_i = 1;
  tick(dut);
  if (!dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL: expected phase_switch_pulse=1 while no_sched asserted");
    pass = 0;
  }

  dut->no_schedulable_nodes_i = 0;
  tick(dut);
  if (dut->phase_switch_pulse_o) {
    fprintf(stderr, "\n    FAIL: expected phase_switch_pulse=0 one cycle after deasserting no_sched");
    pass = 0;
  }
  if (dut->phase_factor_first_o) {
    fprintf(stderr, "\n    FAIL: expected phase to have toggled");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vphase_controller_top;

  int failures = 0;

  printf("phase_controller unit tests (from 01_phase_controller.md):\n");
  failures += test_normal_phase_switch(dut);
  failures += test_continuous_scheduling(dut);
  failures += test_reset_during_switch(dut);
  failures += test_immediate_exhaustion(dut);
  failures += test_back_to_back_switches(dut);
  failures += test_pulse_single_cycle_width(dut);

  if (failures == 0) {
    printf("\nAll 6 tests PASSED\n");
  } else {
    printf("\n%d of 6 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
