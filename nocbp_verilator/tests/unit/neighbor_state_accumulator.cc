// neighbor_state_accumulator.cc
// Unit test for neighbor_state_accumulator
// Test cases from docs/gbp_pe/verification/unit_tests/08_neighbor_state_accumulator.md

#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vneighbor_state_accumulator_top.h"

static void tick(Vneighbor_state_accumulator_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vneighbor_state_accumulator_top* dut) {
  dut->rst_n = 0;
  dut->local_valid_i = 0;
  dut->local_data_i = 0;
  dut->local_last_i = 0;
  dut->remote_valid_i = 0;
  dut->remote_data_i = 0;
  dut->remote_last_i = 0;
  dut->out_ready_i = 1;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);  // let state settle after reset
}

// ── Test Case 1: Local State Only ──
static int test_local_only(Vneighbor_state_accumulator_top* dut) {
  printf("  Test Case 1: Local State Only...");
  reset_dut(dut);
  int pass = 1;

  // Drive local data
  dut->local_valid_i = 1;
  dut->local_data_i = 0xAAAA;
  dut->local_last_i = 1;

  // Check combinational outputs BEFORE rising edge (state is still LOCAL)
  dut->clk = 0;
  dut->eval();

  // Should see output (local has priority when in LOCAL state)
  if (!dut->out_valid_o) {
    pass = 0;
  }
  if (dut->out_data_o != 0xAAAA) {
    fprintf(stderr, "\n    FAIL: out_data=0x%x, expected 0xAAAA", dut->out_data_o);
    pass = 0;
  }
  if (!dut->out_last_o) {
    fprintf(stderr, "\n    FAIL: out_last=0, expected 1");
    pass = 0;
  }
  if (!dut->local_ready_o) {
    fprintf(stderr, "\n    FAIL: local_ready=0, expected 1");
    pass = 0;
  }

  // Complete the tick (rising edge triggers state transition to REMOTE)
  dut->clk = 1;
  dut->eval();
  dut->local_valid_i = 0;
  tick(dut);

  // Now in REMOTE state, local_ready should be 0
  if (dut->local_ready_o) {
    fprintf(stderr, "\n    FAIL: local_ready=1 after transition to REMOTE");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Remote State Only ──
static int test_remote_only(Vneighbor_state_accumulator_top* dut) {
  printf("  Test Case 2: Remote State Only...");
  reset_dut(dut);
  int pass = 1;

  // Skip LOCAL state: send local with last=1 and 0 data
  dut->local_valid_i = 1;
  dut->local_last_i = 1;
  dut->local_data_i = 0;
  tick(dut);
  dut->local_valid_i = 0;
  tick(dut); tick(dut);

  // Now in REMOTE state. Drive remote data word 0
  dut->remote_valid_i = 1;
  dut->remote_data_i = 0xBBBB;
  dut->remote_last_i = 0;

  // Check combinational outputs before rising edge
  dut->clk = 0;
  dut->eval();

  if (!dut->out_valid_o) {
    fprintf(stderr, "\n    FAIL: out_valid=0 for remote word 0");
    pass = 0;
  }
  if (dut->out_data_o != 0xBBBB) {
    fprintf(stderr, "\n    FAIL: out_data=0x%x, expected 0xBBBB", dut->out_data_o);
    pass = 0;
  }
  if (dut->out_last_o) {
    fprintf(stderr, "\n    FAIL: out_last=1 on word 0 (not last)");
    pass = 0;
  }

  // Complete tick and drive next word
  dut->clk = 1;
  dut->eval();

  // Remote word 1 (last)
  dut->remote_data_i = 0xCCCC;
  dut->remote_last_i = 1;
  dut->clk = 0;
  dut->eval();

  if (dut->out_data_o != 0xCCCC) {
    fprintf(stderr, "\n    FAIL: out_data=0x%x, expected 0xCCCC", dut->out_data_o);
    pass = 0;
  }
  if (!dut->out_last_o) {
    fprintf(stderr, "\n    FAIL: out_last=0 on last word");
    pass = 0;
  }

  dut->clk = 1;
  dut->eval();
  dut->remote_valid_i = 0;
  tick(dut); tick(dut);

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: Mixed Local and Remote ──
static int test_mixed(Vneighbor_state_accumulator_top* dut) {
  printf("  Test Case 3: Mixed Local and Remote...");
  reset_dut(dut);
  int pass = 1;

  // Local word (last)
  dut->local_valid_i = 1;
  dut->local_data_i = 0x1111;
  dut->local_last_i = 1;

  // Check before rising edge
  dut->clk = 0;
  dut->eval();
  if (!dut->out_valid_o || dut->out_data_o != 0x1111) {
    fprintf(stderr, "\n    FAIL: local output mismatch (valid=%d data=0x%x)",
            dut->out_valid_o, dut->out_data_o);
    pass = 0;
  }
  dut->clk = 1;
  dut->eval();

  // Transition to remote
  dut->local_valid_i = 0;
  tick(dut); tick(dut);

  // Remote word (last)
  dut->remote_valid_i = 1;
  dut->remote_data_i = 0x2222;
  dut->remote_last_i = 1;

  dut->clk = 0;
  dut->eval();
  if (!dut->out_valid_o || dut->out_data_o != 0x2222) {
    fprintf(stderr, "\n    FAIL: remote output mismatch (valid=%d data=0x%x)",
            dut->out_valid_o, dut->out_data_o);
    pass = 0;
  }
  dut->clk = 1;
  dut->eval();

  dut->remote_valid_i = 0;
  tick(dut); tick(dut);

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: Backpressure ──
static int test_backpressure(Vneighbor_state_accumulator_top* dut) {
  printf("  Test Case 4: Backpressure...");
  reset_dut(dut);
  int pass = 1;

  // Drive local data with out_ready=0
  dut->out_ready_i = 0;
  dut->local_valid_i = 1;
  dut->local_data_i = 0x3333;
  dut->local_last_i = 1;

  dut->clk = 0;
  dut->eval();

  if (!dut->out_valid_o) {
    fprintf(stderr, "\n    FAIL: out_valid=0 with backpressure");
    pass = 0;
  }
  if (dut->local_ready_o) {
    fprintf(stderr, "\n    FAIL: local_ready=1 with backpressure");
    pass = 0;
  }

  // Release backpressure
  dut->out_ready_i = 1;
  dut->clk = 0;
  dut->eval();

  if (!dut->local_ready_o) {
    fprintf(stderr, "\n    FAIL: local_ready=0 after backpressure released");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vneighbor_state_accumulator_top;

  int failures = 0;

  printf("neighbor_state_accumulator unit tests:\n");
  failures += test_local_only(dut);
  failures += test_remote_only(dut);
  failures += test_mixed(dut);
  failures += test_backpressure(dut);

  if (failures == 0) {
    printf("\nAll 4 tests PASSED\n");
  } else {
    printf("\n%d of 4 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
