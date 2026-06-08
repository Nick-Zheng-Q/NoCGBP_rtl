// control_subsystem.cc
// Integration test for Control Subsystem
// phase_controller + node_scheduler + metadata_scanner

#include <cstdio>
#include <cstdlib>
#include <cstdint>

#include "verilated.h"
#include "Vcontrol_subsystem_top.h"
#include "Vcontrol_subsystem_top___024root.h"

static void tick(Vcontrol_subsystem_top* dut) {
  dut->clk = 0; dut->eval();
  dut->clk = 1; dut->eval();
}

static void reset_dut(Vcontrol_subsystem_top* dut, int cycles = 5) {
  dut->rst_n = 0;
  dut->rootp->node_ready = 0;
  dut->wb_done = 0;
  dut->spm_rd_ready = 1;
  dut->spm_rd_data = 0;
  dut->adj_ready = 1;
  dut->my_x = 0;
  dut->my_y = 0;
  for (int i = 0; i < cycles; ++i) tick(dut);
  dut->rst_n = 1;
  dut->eval();  // propagate reset release, no clock edge
}

// Pack NodeHeader into 64-bit beat
static uint64_t pack_header(uint16_t node_id, uint8_t dof, uint8_t adj_count,
                            uint32_t adj_base, uint32_t state_base, uint8_t state_words) {
  uint64_t h = 0;
  h |= (uint64_t)(node_id & 0x3FF);
  h |= (uint64_t)(dof & 0xF) << 10;
  h |= (uint64_t)(adj_count & 0xF) << 14;
  h |= (uint64_t)(adj_base & 0x3FFFF) << 18;
  h |= (uint64_t)(state_base & 0x3FFFF) << 36;
  h |= (uint64_t)(state_words & 0x3F) << 54;
  return h;
}

// Pack AdjEntry into 64-bit beat
static uint64_t pack_adj_entry(uint16_t neighbor_id, uint8_t nx, uint8_t ny, uint8_t is_local) {
  uint64_t e = 0;
  e |= (uint64_t)(neighbor_id & 0x3FF);
  e |= (uint64_t)(nx & 0x3F) << 10;
  e |= (uint64_t)(ny & 0x1F) << 16;
  e |= (uint64_t)(is_local & 1) << 21;
  return e;
}

// ── Test Case 1: Full Control Pipeline ──
static int test_full_pipeline(Vcontrol_subsystem_top* dut) {
  printf("  Test Case 1: Full Control Pipeline...");
  reset_dut(dut);
  int pass = 1;

  // Node 16 ready in factor phase
  dut->rootp->node_ready = (1ULL << 16);
  tick(dut);

  // T+1: scheduler should select node 16
  if (!dut->sched_valid || dut->sched_node_id != 16 || !dut->phase_factor_first) {
    fprintf(stderr, "    FAIL T+1: sched_valid=%d id=%d factor=%d",
            dut->sched_valid, dut->sched_node_id, dut->phase_factor_first);
    pass = 0;
  }

  // T+2: metadata scanner starts reading SPM (addr = node_id = 16)
  tick(dut);
  if (!dut->spm_rd_valid || dut->spm_rd_addr != 16) {
    fprintf(stderr, "\n    FAIL T+2: spm_rd_valid=%d addr=%d",
            dut->spm_rd_valid, dut->spm_rd_addr);
    pass = 0;
  }

  // Provide NodeHeader: node_id=16, dof=3, adj_count=2, adj_base=1, state_base=8, state_words=6
  dut->spm_rd_data = pack_header(16, 3, 2, 1, 8, 6);
  tick(dut);

  // T+3: metadata scanner should have parsed header and requested AdjEntry 0 (addr=1)
  tick(dut);
  if (!dut->spm_rd_valid || dut->spm_rd_addr != 1) {
    fprintf(stderr, "\n    FAIL T+3: expected spm_rd_addr=1, got %d (valid=%d)",
            dut->spm_rd_addr, dut->spm_rd_valid);
    pass = 0;
  }

  // Provide AdjEntry 0: neighbor=32, x=1, y=0, local=1
  dut->spm_rd_data = pack_adj_entry(32, 1, 0, 1);
  tick(dut);

  // T+4: scanner outputs adj 0 and requests adj 1 (addr=2)
  if (!dut->adj_valid) {
    fprintf(stderr, "\n    FAIL T+4: adj_valid=0");
    pass = 0;
  }
  if (dut->adj_neighbor_id != 32) {
    fprintf(stderr, "\n    FAIL T+4: adj_neighbor_id=%d, expected 32", dut->adj_neighbor_id);
    pass = 0;
  }
  if (!dut->info_valid) {
    fprintf(stderr, "\n    FAIL T+4: info_valid=0");
    pass = 0;
  }
  if (dut->info_dof != 3 || dut->info_adj_count != 2 || dut->info_state_base != 8 || dut->info_state_words != 6) {
    fprintf(stderr, "\n    FAIL T+4: info mismatch dof=%d adj=%d base=%d words=%d",
            dut->info_dof, dut->info_adj_count, dut->info_state_base, dut->info_state_words);
    pass = 0;
  }

  tick(dut);
  if (!dut->spm_rd_valid || dut->spm_rd_addr != 2) {
    fprintf(stderr, "\n    FAIL T+4b: expected spm_rd_addr=2, got %d", dut->spm_rd_addr);
    pass = 0;
  }

  // Provide AdjEntry 1: neighbor=48, x=2, y=1, local=0
  dut->spm_rd_data = pack_adj_entry(48, 2, 1, 0);
  tick(dut);

  // T+5: second adj entry
  if (!dut->adj_valid || dut->adj_neighbor_id != 48) {
    fprintf(stderr, "\n    FAIL T+5: second adj entry mismatch id=%d", dut->adj_neighbor_id);
    pass = 0;
  }
  if (!dut->adj_last) {
    fprintf(stderr, "\n    FAIL T+5: adj_last=0, expected 1");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Phase Switch ──
static int test_phase_switch(Vcontrol_subsystem_top* dut) {
  printf("  Test Case 2: Phase Switch...");
  reset_dut(dut);
  int pass = 1;

  // Set node 20 ready in factor phase
  dut->rootp->node_ready = (1ULL << 20);
  tick(dut);

  // Scheduler should select node 20 in factor phase
  if (!dut->sched_valid || dut->sched_node_id != 20 || !dut->phase_factor_first) {
    fprintf(stderr, "\n    FAIL T+1: expected node 20 factor phase, got id=%d factor=%d",
            dut->sched_node_id, dut->phase_factor_first);
    pass = 0;
  }

  // Provide header with 0 adjacencies so scanner finishes quickly
  dut->spm_rd_data = pack_header(20, 3, 0, 0, 8, 6);
  tick(dut);  // T+2: metadata_scanner -> S_DONE

  // Node 20 is now visited. With no other ready nodes, phase should toggle.
  tick(dut);  // T+3: metadata_scanner -> S_IDLE, phase toggles
  if (dut->phase_factor_first) {
    fprintf(stderr, "\n    FAIL: phase did not toggle after node visited");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: Multiple Nodes Sequential Scheduling ──
static int test_multiple_nodes(Vcontrol_subsystem_top* dut) {
  printf("  Test Case 3: Multiple Nodes Sequential...");
  reset_dut(dut);
  int pass = 1;

  // Three factor nodes ready: 16, 32, 48
  dut->rootp->node_ready = (1ULL << 16) | (1ULL << 32) | (1ULL << 48);

  // T+1: node 16 selected
  tick(dut);
  if (!dut->sched_valid || dut->sched_node_id != 16) {
    fprintf(stderr, "    FAIL first: expected node 16, got %d", dut->sched_node_id);
    pass = 0;
  }

  // Provide header with 0 adjacencies so scanner finishes quickly
  dut->spm_rd_data = pack_header(16, 3, 0, 0, 8, 6);
  tick(dut);  // S_RD_HEADER -> S_PARSE_HDR -> S_DONE
  tick(dut);  // S_DONE -> S_IDLE, scheduler advances to 32

  // Node 32 selected
  tick(dut);
  if (!dut->sched_valid || dut->sched_node_id != 32) {
    fprintf(stderr, "\n    FAIL second: expected node 32, got %d", dut->sched_node_id);
    pass = 0;
  }

  // Complete node 32
  dut->spm_rd_data = pack_header(32, 3, 0, 0, 8, 6);
  tick(dut);
  tick(dut);

  // Node 48 selected
  tick(dut);
  if (!dut->sched_valid || dut->sched_node_id != 48) {
    fprintf(stderr, "\n    FAIL third: expected node 48, got %d", dut->sched_node_id);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vcontrol_subsystem_top;

  int failures = 0;
  printf("Control Subsystem integration tests:\n");
  failures += test_full_pipeline(dut);
  failures += test_phase_switch(dut);
  failures += test_multiple_nodes(dut);

  if (failures == 0) {
    printf("\nAll 3 tests PASSED\n");
  } else {
    printf("\n%d of 3 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
