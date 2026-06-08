// gbp_pe_compute_subsystem.cc
// Functional test for compute subsystem.
// Simplified: verifies command acceptance and done assertion.

#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vgbp_pe_compute_subsystem_top.h"

static void tick(Vgbp_pe_compute_subsystem_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe_compute_subsystem_top* dut) {
  dut->rst_n = 0;
  dut->cmd_valid_i = 0;
  dut->ns_valid_i = 0;
  dut->spm_rd0_ready_i = 1;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

// ── Test 1: Simple command -> done ──
static int test_simple_compute(Vgbp_pe_compute_subsystem_top* dut) {
  printf("  Test 1: Simple Compute...");
  reset_dut(dut);
  int pass = 1;

  // Issue command: variable node, dof=1
  // state_words must be 8 FP32 words (= 4 x 64b beats = 1 x 256b engine word)
  // so that the read_stream_engine issues enough beats for the assembler.
  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x10;
  dut->cmd_is_factor_i = 0;
  dut->cmd_dof_i = 1;
  dut->cmd_adj_count_i = 1;
  dut->cmd_state_words_i = 8;
  dut->cmd_state_base_i = 0x100;
  dut->clk = 0;
  dut->eval();
  int cmd_ready_seen = dut->cmd_ready_o;
  dut->clk = 1;
  dut->eval();
  dut->cmd_valid_i = 0;

  if (!cmd_ready_seen) {
    fprintf(stderr, "\n    FAIL: cmd_ready not asserted in issue cycle");
    pass = 0;
  }

  // Feed neighbor state stream (8 words = 1 engine word)
  for (int i = 0; i < 8; ++i) {
    dut->ns_valid_i = 1;
    dut->ns_data_i = 0x3F800000 + i; // FP32 1.0 + epsilon
    dut->ns_last_i = (i == 7);
    tick(dut);
  }
  dut->ns_valid_i = 0;

  // Wait for done (GBP computation may take many cycles)
  int cycles = 0;
  while (!dut->done_valid_o && cycles < 2000) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 2000) {
    fprintf(stderr, "\n    FAIL: done_valid never asserted");
    pass = 0;
  }

  if (pass) {
    if (dut->done_node_id_o != 0x10) {
      fprintf(stderr, "\n    FAIL: done_node_id=0x%x, expected 0x10", dut->done_node_id_o);
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 2: Backpressure on ns_ready ──
static int test_backpressure(Vgbp_pe_compute_subsystem_top* dut) {
  printf("  Test 2: Backpressure...");
  reset_dut(dut);
  int pass = 1;

  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x10;
  dut->cmd_is_factor_i = 0;
  dut->cmd_dof_i = 1;
  dut->cmd_adj_count_i = 1;
  dut->cmd_state_words_i = 8;
  dut->cmd_state_base_i = 0x100;
  tick(dut);
  dut->cmd_valid_i = 0;

  // Stall neighbor stream (only 4 words, not enough to complete)
  dut->ns_valid_i = 1;
  dut->ns_data_i = 0x3F800000;
  dut->ns_last_i = 0;
  for (int i = 0; i < 5; ++i) {
    if (dut->ns_ready_o) {
      // only advance if ready
      tick(dut);
    } else {
      tick(dut);
    }
  }
  dut->ns_valid_i = 0;

  // done should not be asserted yet
  if (dut->done_valid_o) {
    fprintf(stderr, "\n    FAIL: done_valid asserted too early");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 3: Factor Node Command ──
static int test_factor_node(Vgbp_pe_compute_subsystem_top* dut) {
  printf("  Test 3: Factor Node...");
  reset_dut(dut);
  int pass = 1;

  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x20;
  dut->cmd_is_factor_i = 1;  // factor node
  dut->cmd_dof_i = 2;
  dut->cmd_adj_count_i = 1;
  dut->cmd_state_words_i = 8;
  dut->cmd_state_base_i = 0x200;
  tick(dut);
  dut->cmd_valid_i = 0;

  // Feed neighbor state stream
  for (int i = 0; i < 8; ++i) {
    dut->ns_valid_i = 1;
    dut->ns_data_i = 0x3F800000 + i;
    dut->ns_last_i = (i == 7);
    tick(dut);
  }
  dut->ns_valid_i = 0;

  // Wait for done
  int cycles = 0;
  while (!dut->done_valid_o && cycles < 2000) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 2000) {
    fprintf(stderr, "\n    FAIL: done_valid never asserted");
    pass = 0;
  }

  if (pass) {
    if (dut->done_node_id_o != 0x20) {
      fprintf(stderr, "\n    FAIL: done_node_id=0x%x, expected 0x20", dut->done_node_id_o);
      pass = 0;
    }
    if (!dut->done_is_factor_o) {
      fprintf(stderr, "\n    FAIL: done_is_factor=0, expected 1");
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 4: Multiple Adjacent Nodes ──
static int test_multiple_adjacent(Vgbp_pe_compute_subsystem_top* dut) {
  printf("  Test 4: Multiple Adjacent Nodes...");
  reset_dut(dut);
  int pass = 1;

  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x30;
  dut->cmd_is_factor_i = 0;
  dut->cmd_dof_i = 1;
  dut->cmd_adj_count_i = 2;  // 2 neighbors
  dut->cmd_state_words_i = 8;
  dut->cmd_state_base_i = 0x300;
  tick(dut);
  dut->cmd_valid_i = 0;

  // Feed neighbor state stream for 2 neighbors (8 words each)
  for (int n = 0; n < 2; ++n) {
    for (int i = 0; i < 8; ++i) {
      dut->ns_valid_i = 1;
      dut->ns_data_i = 0x3F800000 + (n * 8) + i;
      dut->ns_last_i = (i == 7);
      tick(dut);
    }
  }
  dut->ns_valid_i = 0;

  // Wait for done
  int cycles = 0;
  while (!dut->done_valid_o && cycles < 2000) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 2000) {
    fprintf(stderr, "\n    FAIL: done_valid never asserted");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 5: Batch Done Assertion ──
static int test_batch_done(Vgbp_pe_compute_subsystem_top* dut) {
  printf("  Test 5: Batch Done...");
  reset_dut(dut);
  int pass = 1;

  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x40;
  dut->cmd_is_factor_i = 0;
  dut->cmd_dof_i = 1;
  dut->cmd_adj_count_i = 1;
  dut->cmd_state_words_i = 8;
  dut->cmd_state_base_i = 0x400;
  tick(dut);
  dut->cmd_valid_i = 0;

  for (int i = 0; i < 8; ++i) {
    dut->ns_valid_i = 1;
    dut->ns_data_i = 0x3F800000 + i;
    dut->ns_last_i = (i == 7);
    tick(dut);
  }
  dut->ns_valid_i = 0;

  int cycles = 0;
  while (!dut->done_valid_o && cycles < 2000) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 2000) {
    fprintf(stderr, "\n    FAIL: done_valid never asserted");
    pass = 0;
  }

  // batch_done may assert on same cycle or shortly after done_valid
  int wait_cycles = 0;
  while (!dut->batch_done_o && wait_cycles < 10) {
    tick(dut);
    wait_cycles++;
  }
  if (!dut->batch_done_o) {
    fprintf(stderr, "\n    FAIL: batch_done never asserted");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 6: Multi-DOF Variable (dof=3) ──
static int test_dof3_variable(Vgbp_pe_compute_subsystem_top* dut) {
  printf("  Test 6: Multi-DOF Variable (dof=3)...");
  reset_dut(dut);
  int pass = 1;

  // dof=3 needs compact_payload_beats=2 (256b beats) = 8 SPM 64b beats = 16 FP32 words
  const int state_words = 16;
  const int ns_words    = 16;  // 2 x 256b message beats

  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x50;
  dut->cmd_is_factor_i = 0;
  dut->cmd_dof_i = 3;
  dut->cmd_adj_count_i = 1;
  dut->cmd_state_words_i = state_words;
  dut->cmd_state_base_i = 0x500;
  tick(dut);
  dut->cmd_valid_i = 0;

  for (int i = 0; i < ns_words; ++i) {
    dut->ns_valid_i = 1;
    dut->ns_data_i = 0x3F800000 + i;
    dut->ns_last_i = (i == ns_words - 1);
    tick(dut);
  }
  dut->ns_valid_i = 0;

  int cycles = 0;
  while (!dut->done_valid_o && cycles < 3000) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 3000) {
    fprintf(stderr, "\n    FAIL: done_valid never asserted");
    pass = 0;
  }
  if (pass && dut->done_node_id_o != 0x50) {
    fprintf(stderr, "\n    FAIL: done_node_id=0x%x, expected 0x50", dut->done_node_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 7: Zero Neighbors ──
static int test_zero_neighbors(Vgbp_pe_compute_subsystem_top* dut) {
  printf("  Test 7: Zero Neighbors...");
  reset_dut(dut);
  int pass = 1;

  // Variable node with no adjacent nodes: state only, no ns_data
  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x60;
  dut->cmd_is_factor_i = 0;
  dut->cmd_dof_i = 1;
  dut->cmd_adj_count_i = 0;
  dut->cmd_state_words_i = 8;
  dut->cmd_state_base_i = 0x600;
  tick(dut);
  dut->cmd_valid_i = 0;

  // No neighbor state to feed
  int cycles = 0;
  while (!dut->done_valid_o && cycles < 2000) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 2000) {
    fprintf(stderr, "\n    FAIL: done_valid never asserted");
    pass = 0;
  }
  if (pass && dut->done_node_id_o != 0x60) {
    fprintf(stderr, "\n    FAIL: done_node_id=0x%x, expected 0x60", dut->done_node_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 8: Descriptor While SPM Stalled ──
static int test_descriptor_while_stalled(Vgbp_pe_compute_subsystem_top* dut) {
  printf("  Test 8: Descriptor While SPM Stalled...");
  reset_dut(dut);
  int pass = 1;

  // Stall SPM read port before issuing command
  dut->spm_rd0_ready_i = 0;

  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x70;
  dut->cmd_is_factor_i = 0;
  dut->cmd_dof_i = 1;
  dut->cmd_adj_count_i = 1;
  dut->cmd_state_words_i = 8;
  dut->cmd_state_base_i = 0x700;
  tick(dut);
  dut->cmd_valid_i = 0;

  // Hold stall for several cycles; descriptor should be accepted by compute_unit
  // and RSE should buffer it, so cmd_ready was asserted in issue cycle.
  for (int i = 0; i < 10; ++i) tick(dut);

  // Release SPM stall
  dut->spm_rd0_ready_i = 1;

  // Feed neighbor state
  for (int i = 0; i < 8; ++i) {
    dut->ns_valid_i = 1;
    dut->ns_data_i = 0x3F800000 + i;
    dut->ns_last_i = (i == 7);
    tick(dut);
  }
  dut->ns_valid_i = 0;

  int cycles = 0;
  while (!dut->done_valid_o && cycles < 2000) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 2000) {
    fprintf(stderr, "\n    FAIL: done_valid never asserted after stall release");
    pass = 0;
  }
  if (pass && dut->done_node_id_o != 0x70) {
    fprintf(stderr, "\n    FAIL: done_node_id=0x%x, expected 0x70", dut->done_node_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 9: Reset During Compute ──
static int test_reset_during_compute(Vgbp_pe_compute_subsystem_top* dut) {
  printf("  Test 9: Reset During Compute...");
  reset_dut(dut);
  int pass = 1;

  // Start a compute
  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x80;
  dut->cmd_is_factor_i = 0;
  dut->cmd_dof_i = 1;
  dut->cmd_adj_count_i = 1;
  dut->cmd_state_words_i = 8;
  dut->cmd_state_base_i = 0x800;
  tick(dut);
  dut->cmd_valid_i = 0;

  // Feed a few neighbor words but do not complete
  for (int i = 0; i < 3; ++i) {
    dut->ns_valid_i = 1;
    dut->ns_data_i = 0x3F800000 + i;
    dut->ns_last_i = 0;
    tick(dut);
  }
  dut->ns_valid_i = 0;

  // Assert reset while compute is in flight
  dut->rst_n = 0;
  for (int i = 0; i < 3; ++i) tick(dut);

  // done should be deasserted during reset
  if (dut->done_valid_o) {
    fprintf(stderr, "\n    FAIL: done_valid still asserted during reset");
    pass = 0;
  }

  // Release reset and issue a fresh command to prove clean recovery
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);

  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x81;
  dut->cmd_is_factor_i = 0;
  dut->cmd_dof_i = 1;
  dut->cmd_adj_count_i = 1;
  dut->cmd_state_words_i = 8;
  dut->cmd_state_base_i = 0x810;
  tick(dut);
  dut->cmd_valid_i = 0;

  for (int i = 0; i < 8; ++i) {
    dut->ns_valid_i = 1;
    dut->ns_data_i = 0x3F800000 + i;
    dut->ns_last_i = (i == 7);
    tick(dut);
  }
  dut->ns_valid_i = 0;

  int cycles = 0;
  while (!dut->done_valid_o && cycles < 2000) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 2000) {
    fprintf(stderr, "\n    FAIL: done_valid never asserted after reset recovery");
    pass = 0;
  }
  if (pass && dut->done_node_id_o != 0x81) {
    fprintf(stderr, "\n    FAIL: done_node_id=0x%x, expected 0x81", dut->done_node_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_pe_compute_subsystem_top;

  int failures = 0;
  printf("gbp_pe_compute_subsystem functional tests:\n");
  failures += test_simple_compute(dut);
  failures += test_backpressure(dut);
  failures += test_factor_node(dut);
  failures += test_multiple_adjacent(dut);
  failures += test_batch_done(dut);
  failures += test_dof3_variable(dut);
  failures += test_zero_neighbors(dut);
  failures += test_descriptor_while_stalled(dut);
  failures += test_reset_during_compute(dut);

  if (failures == 0) {
    printf("\nAll 9 tests PASSED\n");
  } else {
    printf("\n%d of 9 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
