// node_scheduler.cc
// Unit test for node_scheduler
// Test cases from docs/gbp_pe/verification/unit_tests/02_node_scheduler.md

#include <cstdio>
#include <cstdlib>
#include <cstdint>

#include "verilated.h"
#include "Vnode_scheduler_top.h"

static constexpr int NODE_WORDS = 32;  // 1024 nodes / 32 bits

static void tick(Vnode_scheduler_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vnode_scheduler_top* dut, int cycles = 3) {
  dut->rst_n = 0;
  dut->phase_factor_first_i = 1;
  for (int i = 0; i < NODE_WORDS; ++i) dut->node_ready_i[i] = 0;
  for (int i = 0; i < NODE_WORDS; ++i) dut->visited_mask_i[i] = 0;
  dut->sched_ready_i = 0;
  for (int i = 0; i < cycles; ++i) tick(dut);
  dut->rst_n = 1;
  tick(dut); // one extra cycle to get initial registered output
}

static void clear_node_ready(Vnode_scheduler_top* dut) {
  for (int i = 0; i < NODE_WORDS; ++i) dut->node_ready_i[i] = 0;
}

static void clear_visited_mask(Vnode_scheduler_top* dut) {
  for (int i = 0; i < NODE_WORDS; ++i) dut->visited_mask_i[i] = 0;
}

static void set_all_nodes_ready(Vnode_scheduler_top* dut) {
  for (int i = 0; i < NODE_WORDS; ++i) dut->node_ready_i[i] = 0xFFFFFFFFu;
}

static void set_node_ready_bit(Vnode_scheduler_top* dut, int node_id) {
  int word = node_id / 32;
  int bit  = node_id % 32;
  dut->node_ready_i[word] |= (1u << bit);
}

static void set_visited_bit(Vnode_scheduler_top* dut, int node_id) {
  int word = node_id / 32;
  int bit  = node_id % 32;
  dut->visited_mask_i[word] |= (1u << bit);
}

// ── Test Case 1: Round-Robin Scheduling ──
static int test_rr_scheduling(Vnode_scheduler_top* dut) {
  printf("  Test Case 1: Round-Robin Scheduling...");
  reset_dut(dut);
  int pass = 1;

  // T+1: Nodes 0-3 ready, no visited, assert sched_ready to capture
  dut->node_ready_i[0] = 0xF;
  dut->sched_ready_i = 1;
  tick(dut);

  if (!dut->sched_valid_o || dut->sched_node_id_o != 0 || !dut->sched_is_factor_o || dut->no_schedulable_nodes_o) {
    fprintf(stderr, "\n    FAIL at T+1: expected valid=1, id=0, is_factor=1, no_sched=0");
    pass = 0;
  }

  // T+2: Node 0 visited, scheduler should pick node 1
  dut->visited_mask_i[0] = 0x1;
  tick(dut);

  if (!dut->sched_valid_o || dut->sched_node_id_o != 1) {
    fprintf(stderr, "\n    FAIL at T+2: expected valid=1, id=1 (got id=%d)", dut->sched_node_id_o);
    pass = 0;
  }

  // T+3: Nodes 0-1 visited, scheduler should pick node 2
  dut->visited_mask_i[0] = 0x3;
  tick(dut);

  if (!dut->sched_valid_o || dut->sched_node_id_o != 2) {
    fprintf(stderr, "\n    FAIL at T+3: expected valid=1, id=2 (got id=%d)", dut->sched_node_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: No Schedulable Nodes ──
static int test_no_schedulable_nodes(Vnode_scheduler_top* dut) {
  printf("  Test Case 2: No Schedulable Nodes...");
  reset_dut(dut);
  int pass = 1;

  // No nodes ready
  tick(dut);

  if (dut->sched_valid_o || !dut->no_schedulable_nodes_o) {
    fprintf(stderr, "\n    FAIL at T+1: expected valid=0, no_sched=1");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Corner Case: Wrap-around RR ──
static int test_wraparound_rr(Vnode_scheduler_top* dut) {
  printf("  Corner Case: Wrap-around RR...");
  reset_dut(dut);
  int pass = 1;

  // Schedule nodes 0 and 1
  dut->node_ready_i[0] = 0x3;
  dut->sched_ready_i = 1;
  tick(dut); // node 0 accepted, next_index=1
  tick(dut); // node 1 accepted, next_index=2
  dut->sched_ready_i = 0;

  // Clear ready, then only node 0 ready again
  dut->node_ready_i[0] = 0x0;
  tick(dut);
  dut->node_ready_i[0] = 0x1;
  dut->visited_mask_i[0] = 0x0;
  tick(dut);

  // Because sched_ready=0, outputs hold last value (node 1)
  // Assert sched_ready to get new value
  dut->sched_ready_i = 1;
  tick(dut);

  if (!dut->sched_valid_o || dut->sched_node_id_o != 0) {
    fprintf(stderr, "\n    FAIL: expected valid=1, id=0 after wrap-around (got id=%d)", dut->sched_node_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: sched_is_factor matches current phase ──
// Covers phase matching and "Phase change during selection" corner case.
static int test_sched_is_factor_phase_matching(Vnode_scheduler_top* dut) {
  printf("  Test Case 4: sched_is_factor phase matching...");
  reset_dut(dut);
  int pass = 1;

  clear_node_ready(dut);
  clear_visited_mask(dut);
  set_node_ready_bit(dut, 7);
  dut->sched_ready_i = 1;

  // Factor phase: sched_is_factor should be 1
  dut->phase_factor_first_i = 1;
  tick(dut);
  if (!dut->sched_valid_o || dut->sched_node_id_o != 7 || !dut->sched_is_factor_o) {
    fprintf(stderr, "\n    FAIL at factor phase: expected valid=1, id=7, is_factor=1");
    pass = 0;
  }

  // Variable phase: toggling phase_factor_first should change sched_is_factor immediately
  dut->phase_factor_first_i = 0;
  tick(dut);
  if (!dut->sched_valid_o || dut->sched_is_factor_o) {
    fprintf(stderr, "\n    FAIL at variable phase: expected is_factor=0 after phase change");
    pass = 0;
  }

  // Toggle back to factor mid-selection
  dut->phase_factor_first_i = 1;
  tick(dut);
  if (!dut->sched_valid_o || !dut->sched_is_factor_o) {
    fprintf(stderr, "\n    FAIL returning to factor phase: expected is_factor=1");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: All nodes ready ──
static int test_all_nodes_ready(Vnode_scheduler_top* dut) {
  printf("  Test Case 5: All nodes ready...");
  reset_dut(dut);
  int pass = 1;

  clear_visited_mask(dut);
  set_all_nodes_ready(dut);
  dut->sched_ready_i = 1;

  tick(dut);
  if (!dut->sched_valid_o || dut->sched_node_id_o != 0) {
    fprintf(stderr, "\n    FAIL first selection: expected valid=1, id=0 (got id=%d)", dut->sched_node_id_o);
    pass = 0;
  }

  tick(dut);
  if (!dut->sched_valid_o || dut->sched_node_id_o != 1) {
    fprintf(stderr, "\n    FAIL second selection: expected valid=1, id=1 (got id=%d)", dut->sched_node_id_o);
    pass = 0;
  }

  tick(dut);
  if (!dut->sched_valid_o || dut->sched_node_id_o != 2) {
    fprintf(stderr, "\n    FAIL third selection: expected valid=1, id=2 (got id=%d)", dut->sched_node_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 6: Single node ready ──
static int test_single_node_ready(Vnode_scheduler_top* dut) {
  printf("  Test Case 6: Single node ready...");
  reset_dut(dut);
  int pass = 1;

  clear_node_ready(dut);
  clear_visited_mask(dut);
  set_node_ready_bit(dut, 42);
  dut->sched_ready_i = 1;

  tick(dut);
  if (!dut->sched_valid_o || dut->sched_node_id_o != 42) {
    fprintf(stderr, "\n    FAIL selection: expected valid=1, id=42 (got id=%d)", dut->sched_node_id_o);
    pass = 0;
  }

  // Mark every node visited (disabling discovery mode) -> no schedulable nodes
  clear_node_ready(dut);
  for (int i = 0; i < NODE_WORDS; ++i) dut->visited_mask_i[i] = 0xFFFFFFFFu;
  tick(dut);
  if (dut->sched_valid_o || !dut->no_schedulable_nodes_o) {
    fprintf(stderr, "\n    FAIL after visit: expected valid=0, no_sched=1");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 7: Visited mask update respected ──
static int test_visited_mask_update(Vnode_scheduler_top* dut) {
  printf("  Test Case 7: Visited mask update...");
  reset_dut(dut);
  int pass = 1;

  clear_node_ready(dut);
  clear_visited_mask(dut);
  dut->node_ready_i[0] = 0x7;  // nodes 0, 1, 2 ready
  dut->sched_ready_i = 1;

  tick(dut);
  if (!dut->sched_valid_o || dut->sched_node_id_o != 0) {
    fprintf(stderr, "\n    FAIL first: expected valid=1, id=0");
    pass = 0;
  }

  // Visit node 0, expect node 1 next
  clear_visited_mask(dut);
  dut->visited_mask_i[0] = 0x1;
  tick(dut);
  if (!dut->sched_valid_o || dut->sched_node_id_o != 1) {
    fprintf(stderr, "\n    FAIL second: expected valid=1, id=1 (got id=%d)", dut->sched_node_id_o);
    pass = 0;
  }

  // Visit nodes 0-2, expect no schedulable nodes
  dut->visited_mask_i[0] = 0x7;
  tick(dut);
  if (dut->sched_valid_o || !dut->no_schedulable_nodes_o) {
    fprintf(stderr, "\n    FAIL exhausted: expected valid=0, no_sched=1");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vnode_scheduler_top;

  int failures = 0;
  printf("node_scheduler unit tests (from 02_node_scheduler.md):\n");
  failures += test_rr_scheduling(dut);
  failures += test_no_schedulable_nodes(dut);
  failures += test_wraparound_rr(dut);
  failures += test_sched_is_factor_phase_matching(dut);
  failures += test_all_nodes_ready(dut);
  failures += test_single_node_ready(dut);
  failures += test_visited_mask_update(dut);

  if (failures == 0) {
    printf("\nAll 7 tests PASSED\n");
  } else {
    printf("\n%d of 7 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
