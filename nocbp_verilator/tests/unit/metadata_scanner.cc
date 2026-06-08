// metadata_scanner.cc
// Unit test for metadata_scanner
// Test cases from docs/gbp_pe/verification/unit_tests/03_metadata_scanner.md

#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vmetadata_scanner_top.h"

// FSM state constants (must match metadata_scanner.sv)
static const int S_IDLE       = 0;
static const int S_RD_HEADER  = 1;
static const int S_PARSE_HDR  = 2;
static const int S_RD_ADJ     = 3;
static const int S_OUTPUT_ADJ = 4;
static const int S_DONE       = 5;

// Eval at falling edge (combinational outputs reflect current state)
static void eval_fall(Vmetadata_scanner_top* dut) {
  dut->clk = 0;
  dut->eval();
}

// Full tick (rising edge triggers state update)
static void tick(Vmetadata_scanner_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vmetadata_scanner_top* dut) {
  dut->rst_n = 0;
  dut->cmd_valid_i = 0;
  dut->cmd_node_id_i = 0;
  dut->cmd_is_factor_i = 0;
  dut->adj_ready_i = 1;
  dut->spm_rd_ready_i = 1;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

// ── Test Case 1: Single Node Scan ──
static int test_single_node_scan(Vmetadata_scanner_top* dut) {
  printf("  Test Case 1: Single Node Scan...");
  reset_dut(dut);
  int pass = 1;

  // Drive command
  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x10;
  dut->cmd_is_factor_i = 0;

  // Check cmd_ready at falling edge (combinational: state is IDLE → cmd_ready=1)
  eval_fall(dut);
  if (!dut->cmd_ready_o) {
    fprintf(stderr, "\n    FAIL: cmd_ready=0");
    pass = 0;
  }

  // Rising edge: command accepted, state → RD_HEADER
  dut->clk = 1;
  dut->eval();

  // Clear command
  dut->cmd_valid_i = 0;

  // Now the scanner is in RD_HEADER with spm_rd_pending.
  // With combinational SPM: spm_rd_ready=1 immediately.
  // Next falling edge: scanner reads SPM data and parses header.
  // Next rising edge: state → PARSE_HDR → RD_ADJ (if adj_count>0)

  // Tick through states and collect outputs
  int info_seen = 0;
  int adj_count = 0;
  int adj0_ok = 0, adj1_ok = 0;

  for (int i = 0; i < 20; ++i) {
    // Falling edge: check combinational outputs
    eval_fall(dut);

    if (dut->info_valid_o && !info_seen) {
      info_seen = 1;
      if (dut->info_dof_o != 3) {
        fprintf(stderr, "\n    FAIL: dof=%d, expected 3", dut->info_dof_o);
        pass = 0;
      }
      if (dut->info_adj_count_o != 2) {
        fprintf(stderr, "\n    FAIL: adj_count=%d, expected 2", dut->info_adj_count_o);
        pass = 0;
      }
      if (dut->info_state_base_o != 8) {
        fprintf(stderr, "\n    FAIL: state_base=%d, expected 8", dut->info_state_base_o);
        pass = 0;
      }
      if (dut->info_state_words_o != 6) {
        fprintf(stderr, "\n    FAIL: state_words=%d, expected 6", dut->info_state_words_o);
        pass = 0;
      }
    }

    if (dut->adj_valid_o) {
      if (adj_count == 0) {
        if (dut->adj_neighbor_id_o == 0x20 &&
            dut->adj_neighbor_x_o == 1 &&
            dut->adj_neighbor_y_o == 0 &&
            dut->adj_is_local_o == 1 &&
            dut->adj_last_o == 0) {
          adj0_ok = 1;
        } else {
          fprintf(stderr, "\n    FAIL adj0: id=0x%x x=%d y=%d local=%d last=%d",
                  dut->adj_neighbor_id_o, dut->adj_neighbor_x_o,
                  dut->adj_neighbor_y_o, dut->adj_is_local_o, dut->adj_last_o);
        }
      } else if (adj_count == 1) {
        if (dut->adj_neighbor_id_o == 0x30 &&
            dut->adj_neighbor_x_o == 2 &&
            dut->adj_neighbor_y_o == 1 &&
            dut->adj_is_local_o == 0 &&
            dut->adj_last_o == 1) {
          adj1_ok = 1;
        } else {
          fprintf(stderr, "\n    FAIL adj1: id=0x%x x=%d y=%d local=%d last=%d",
                  dut->adj_neighbor_id_o, dut->adj_neighbor_x_o,
                  dut->adj_neighbor_y_o, dut->adj_is_local_o, dut->adj_last_o);
        }
      }
      adj_count++;
    }

    // Rising edge: advance state
    dut->clk = 1;
    dut->eval();
  }

  if (!info_seen) {
    fprintf(stderr, "\n    FAIL: info_valid never asserted");
    pass = 0;
  }
  if (adj_count != 2) {
    fprintf(stderr, "\n    FAIL: adj_count=%d, expected 2", adj_count);
    pass = 0;
  }
  if (!adj0_ok) {
    fprintf(stderr, "\n    FAIL: AdjEntry[0] mismatch");
    pass = 0;
  }
  if (!adj1_ok) {
    fprintf(stderr, "\n    FAIL: AdjEntry[1] mismatch");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Zero Neighbors ──
static int test_zero_neighbors(Vmetadata_scanner_top* dut) {
  printf("  Test Case 2: Zero Neighbors...");
  reset_dut(dut);
  int pass = 1;

  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x00;
  dut->cmd_is_factor_i = 0;
  tick(dut);
  dut->cmd_valid_i = 0;

  int adj_count = 0;
  int idle_seen = 0;
  for (int i = 0; i < 20; ++i) {
    eval_fall(dut);
    if (dut->adj_valid_o) adj_count++;
    if (dut->state_o == S_IDLE) {
      idle_seen = 1;
      break;
    }
    dut->clk = 1;
    dut->eval();
  }

  if (adj_count != 0) {
    fprintf(stderr, "\n    FAIL: adj_count=%d, expected 0", adj_count);
    pass = 0;
  }
  if (!idle_seen) {
    fprintf(stderr, "\n    FAIL: FSM did not return to IDLE");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: Maximum Neighbors All Remote ──
static int test_max_all_remote(Vmetadata_scanner_top* dut) {
  printf("  Test Case 3: Max Neighbors All Remote...");
  reset_dut(dut);
  int pass = 1;

  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x30;
  dut->cmd_is_factor_i = 0;
  tick(dut);
  dut->cmd_valid_i = 0;

  int adj_count = 0;
  int info_seen = 0;
  int idle_seen = 0;
  for (int i = 0; i < 40; ++i) {
    eval_fall(dut);

    if (dut->info_valid_o) {
      info_seen = 1;
      if (dut->info_adj_count_o != 8) {
        fprintf(stderr, "\n    FAIL: info_adj_count=%d, expected 8", dut->info_adj_count_o);
        pass = 0;
      }
    }

    if (dut->adj_valid_o) {
      if (dut->adj_is_local_o) {
        fprintf(stderr, "\n    FAIL: adj[%d] marked local, expected remote", adj_count);
        pass = 0;
      }
      if (adj_count == 7 && !dut->adj_last_o) {
        fprintf(stderr, "\n    FAIL: adj_last=0 on final entry");
        pass = 0;
      }
      if (adj_count != 7 && dut->adj_last_o) {
        fprintf(stderr, "\n    FAIL: adj_last=1 before final entry");
        pass = 0;
      }
      adj_count++;
    }

    if (dut->state_o == S_IDLE) {
      idle_seen = 1;
      if (adj_count == 8) break;
    }

    dut->clk = 1;
    dut->eval();
  }

  if (adj_count != 8) {
    fprintf(stderr, "\n    FAIL: adj_count=%d, expected 8", adj_count);
    pass = 0;
  }
  if (!info_seen) {
    fprintf(stderr, "\n    FAIL: info_valid never asserted");
    pass = 0;
  }
  if (!idle_seen) {
    fprintf(stderr, "\n    FAIL: FSM did not return to IDLE");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: Maximum Neighbors All Local ──
static int test_max_all_local(Vmetadata_scanner_top* dut) {
  printf("  Test Case 4: Max Neighbors All Local...");
  reset_dut(dut);
  int pass = 1;

  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x20;
  dut->cmd_is_factor_i = 0;
  tick(dut);
  dut->cmd_valid_i = 0;

  int adj_count = 0;
  int info_seen = 0;
  int idle_seen = 0;
  for (int i = 0; i < 40; ++i) {
    eval_fall(dut);

    if (dut->info_valid_o) {
      info_seen = 1;
      if (dut->info_adj_count_o != 8) {
        fprintf(stderr, "\n    FAIL: info_adj_count=%d, expected 8", dut->info_adj_count_o);
        pass = 0;
      }
    }

    if (dut->adj_valid_o) {
      if (!dut->adj_is_local_o) {
        fprintf(stderr, "\n    FAIL: adj[%d] marked remote, expected local", adj_count);
        pass = 0;
      }
      if (adj_count == 7 && !dut->adj_last_o) {
        fprintf(stderr, "\n    FAIL: adj_last=0 on final entry");
        pass = 0;
      }
      adj_count++;
    }

    if (dut->state_o == S_IDLE) {
      idle_seen = 1;
      if (adj_count == 8) break;
    }

    dut->clk = 1;
    dut->eval();
  }

  if (adj_count != 8) {
    fprintf(stderr, "\n    FAIL: adj_count=%d, expected 8", adj_count);
    pass = 0;
  }
  if (!info_seen) {
    fprintf(stderr, "\n    FAIL: info_valid never asserted");
    pass = 0;
  }
  if (!idle_seen) {
    fprintf(stderr, "\n    FAIL: FSM did not return to IDLE");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: SPM Read Error (SPM not ready) followed by Reset ──
static int test_spm_read_error_reset(Vmetadata_scanner_top* dut) {
  printf("  Test Case 5: SPM Read Error then Reset...");
  reset_dut(dut);
  int pass = 1;

  // Issue a command
  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0x40;
  dut->cmd_is_factor_i = 0;
  tick(dut);
  dut->cmd_valid_i = 0;

  // Force SPM read data unavailable (ready=0). Scanner should stall in RD_HEADER.
  dut->spm_rd_ready_i = 0;
  for (int i = 0; i < 4; ++i) {
    tick(dut);
    eval_fall(dut);
    if (dut->state_o != S_RD_HEADER) {
      fprintf(stderr, "\n    FAIL: state=%d, expected RD_HEADER while SPM stalled", dut->state_o);
      pass = 0;
    }
  }

  // Apply asynchronous-style reset to recover from the error/stall.
  dut->rst_n = 0;
  dut->spm_rd_ready_i = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);

  eval_fall(dut);
  if (dut->state_o != S_IDLE) {
    fprintf(stderr, "\n    FAIL: state=%d, expected IDLE after reset", dut->state_o);
    pass = 0;
  }
  if (!dut->cmd_ready_o) {
    fprintf(stderr, "\n    FAIL: cmd_ready=0 after reset");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vmetadata_scanner_top;

  int failures = 0;

  printf("metadata_scanner unit tests (from 03_metadata_scanner.md):\n");
  failures += test_single_node_scan(dut);
  failures += test_zero_neighbors(dut);
  failures += test_max_all_remote(dut);
  failures += test_max_all_local(dut);
  failures += test_spm_read_error_reset(dut);

  if (failures == 0) {
    printf("\nAll 5 tests PASSED\n");
  } else {
    printf("\n%d of 5 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
