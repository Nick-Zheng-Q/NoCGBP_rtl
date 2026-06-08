// pull_client.cc
// Unit test for pull_client
// Test cases from docs/gbp_pe/verification/unit_tests/05_pull_client.md

#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vpull_client_top.h"

static void tick(Vpull_client_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void eval_fall(Vpull_client_top* dut) {
  dut->clk = 0;
  dut->eval();
}

static void reset_dut(Vpull_client_top* dut) {
  dut->rst_n = 0;
  dut->req_valid_i = 0;
  dut->req_target_node_id_i = 0;
  dut->req_consumer_node_id_i = 0;
  dut->req_is_factor_i = 0;
  dut->req_target_x_i = 0;
  dut->req_target_y_i = 0;
  dut->req_txn_id_i = 0;
  dut->tx_ready_i = 1;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

// ── Test Case 1: Normal Fetch Request ──
static int test_normal_request(Vpull_client_top* dut) {
  printf("  Test Case 1: Normal Fetch Request...");
  reset_dut(dut);
  int pass = 1;

  // Issue request
  dut->req_valid_i = 1;
  dut->req_target_node_id_i = 0x10;
  dut->req_consumer_node_id_i = 0x20;
  dut->req_is_factor_i = 0;
  dut->req_target_x_i = 2;
  dut->req_target_y_i = 1;
  dut->req_txn_id_i = 0x03;

  // Check req_ready before rising edge
  eval_fall(dut);
  if (!dut->req_ready_o) {
    fprintf(stderr, "\n    FAIL: req_ready=0");
    pass = 0;
  }

  // Complete tick (request accepted)
  dut->clk = 1;
  dut->eval();
  dut->req_valid_i = 0;

  // Collect 3 stores
  int store_count = 0;
  int store_ok[3] = {0, 0, 0};

  for (int i = 0; i < 15; ++i) {
    eval_fall(dut);

    if (dut->tx_valid_o && dut->tx_ready_i) {
      if (store_count < 3) {
        int ok = 1;
        // All stores should have same target coords and node IDs
        if (dut->tx_target_node_id_o != 0x10) ok = 0;
        if (dut->tx_consumer_node_id_o != 0x20) ok = 0;
        if (dut->tx_is_factor_o != 0) ok = 0;
        if (dut->tx_target_x_o != 2) ok = 0;
        if (dut->tx_target_y_o != 1) ok = 0;
        if (dut->tx_txn_id_o != 0x03) ok = 0;
        if (dut->tx_store_idx_o != store_count) ok = 0;
        store_ok[store_count] = ok;
      }
      store_count++;
    }

    dut->clk = 1;
    dut->eval();
  }

  if (store_count != 3) {
    fprintf(stderr, "\n    FAIL: store_count=%d, expected 3", store_count);
    pass = 0;
  }
  for (int i = 0; i < 3; ++i) {
    if (!store_ok[i]) {
      fprintf(stderr, "\n    FAIL: store %d data mismatch", i);
      pass = 0;
    }
  }

  // After 3 stores, tx_valid should be 0
  eval_fall(dut);
  if (dut->tx_valid_o) {
    fprintf(stderr, "\n    FAIL: tx_valid=1 after 3 stores");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Backpressure ──
static int test_backpressure(Vpull_client_top* dut) {
  printf("  Test Case 2: Backpressure...");
  reset_dut(dut);
  int pass = 1;

  // Issue request with tx_ready=0
  dut->tx_ready_i = 0;
  dut->req_valid_i = 1;
  dut->req_target_node_id_i = 0x10;
  dut->req_consumer_node_id_i = 0x20;
  dut->req_is_factor_i = 0;
  dut->req_target_x_i = 2;
  dut->req_target_y_i = 1;
  dut->req_txn_id_i = 0x03;
  tick(dut);
  dut->req_valid_i = 0;

  // Tick with backpressure - tx_valid should stay high
  for (int i = 0; i < 5; ++i) {
    eval_fall(dut);
    if (!dut->tx_valid_o) {
      fprintf(stderr, "\n    FAIL: tx_valid=0 during backpressure at cycle %d", i);
      pass = 0;
    }
    dut->clk = 1;
    dut->eval();
  }

  // Check store_idx stays at 0 (first store not consumed)
  eval_fall(dut);
  if (dut->tx_store_idx_o != 0) {
    fprintf(stderr, "\n    FAIL: store_idx=%d during backpressure, expected 0", dut->tx_store_idx_o);
    pass = 0;
  }

  // Release backpressure
  dut->tx_ready_i = 1;
  tick(dut);

  // Now store 0 consumed, store 1 should appear
  eval_fall(dut);
  if (dut->tx_store_idx_o != 1) {
    fprintf(stderr, "\n    FAIL: store_idx=%d after release, expected 1", dut->tx_store_idx_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: Three-Store Payload Encoding ──
static int test_store_payloads(Vpull_client_top* dut) {
  printf("  Test Case 3: Three-Store Payload Encoding...");
  reset_dut(dut);
  int pass = 1;

  dut->req_valid_i = 1;
  dut->req_target_node_id_i = 0x10;
  dut->req_consumer_node_id_i = 0x20;
  dut->req_is_factor_i = 1;
  dut->req_target_x_i = 2;
  dut->req_target_y_i = 1;
  dut->req_txn_id_i = 0x03;
  tick(dut);
  dut->req_valid_i = 0;

  int store_count = 0;
  int seen[3] = {0, 0, 0};

  for (int i = 0; i < 15; ++i) {
    eval_fall(dut);

    if (dut->tx_valid_o && dut->tx_ready_i) {
      int idx = dut->tx_store_idx_o;
      if (idx < 0 || idx > 2) {
        fprintf(stderr, "\n    FAIL: invalid store_idx %d", idx);
        pass = 0;
      } else {
        seen[idx] = 1;
        if (idx == 0) {
          if (dut->tx_is_factor_o != 1) {
            fprintf(stderr, "\n    FAIL: store 0 is_factor mismatch");
            pass = 0;
          }
          if (dut->tx_consumer_node_id_o != 0x20) {
            fprintf(stderr, "\n    FAIL: store 0 consumer_node_id mismatch");
            pass = 0;
          }
        } else if (idx == 1) {
          if (dut->tx_target_node_id_o != 0x10) {
            fprintf(stderr, "\n    FAIL: store 1 target_node_id mismatch");
            pass = 0;
          }
        } else if (idx == 2) {
          if (dut->tx_txn_id_o != 0x03) {
            fprintf(stderr, "\n    FAIL: store 2 txn_id mismatch");
            pass = 0;
          }
        }
      }
      store_count++;
    }

    tick(dut);
  }

  if (store_count != 3) {
    fprintf(stderr, "\n    FAIL: store_count=%d, expected 3", store_count);
    pass = 0;
  }
  for (int i = 0; i < 3; ++i) {
    if (!seen[i]) {
      fprintf(stderr, "\n    FAIL: missing store %d", i);
      pass = 0;
    }
  }

  eval_fall(dut);
  if (dut->tx_valid_o) {
    fprintf(stderr, "\n    FAIL: tx_valid=1 after 3 stores");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: Back-to-Back Requests ──
static int test_back_to_back_requests(Vpull_client_top* dut) {
  printf("  Test Case 4: Back-to-Back Requests...");
  reset_dut(dut);
  int pass = 1;

  // Issue first request
  dut->req_valid_i = 1;
  dut->req_target_node_id_i = 0x10;
  dut->req_consumer_node_id_i = 0x20;
  dut->req_is_factor_i = 0;
  dut->req_target_x_i = 2;
  dut->req_target_y_i = 1;
  dut->req_txn_id_i = 0x0A;
  tick(dut);
  dut->req_valid_i = 0;

  int first_count = 0;
  int second_count = 0;
  int phase = 1;
  int issue_second = 0;

  for (int i = 0; i < 40; ++i) {
    eval_fall(dut);

    if (dut->tx_valid_o && dut->tx_ready_i) {
      if (phase == 1) {
        first_count++;
        int idx = dut->tx_store_idx_o;
        if (idx == 0) {
          if (dut->tx_is_factor_o != 0 || dut->tx_consumer_node_id_o != 0x20) pass = 0;
        } else if (idx == 1) {
          if (dut->tx_target_node_id_o != 0x10) pass = 0;
        } else if (idx == 2) {
          if (dut->tx_txn_id_o != 0x0A) pass = 0;
        }
      } else {
        second_count++;
        int idx = dut->tx_store_idx_o;
        if (idx == 0) {
          if (dut->tx_is_factor_o != 1 || dut->tx_consumer_node_id_o != 0x40) pass = 0;
        } else if (idx == 1) {
          if (dut->tx_target_node_id_o != 0x30) pass = 0;
        } else if (idx == 2) {
          if (dut->tx_txn_id_o != 0x0B) pass = 0;
        }
      }
    }

    // Issue second request as soon as the first transaction completes and FSM is idle
    if (phase == 1 && first_count == 3 && dut->req_ready_o) {
      dut->req_valid_i = 1;
      dut->req_target_node_id_i = 0x30;
      dut->req_consumer_node_id_i = 0x40;
      dut->req_is_factor_i = 1;
      dut->req_target_x_i = 3;
      dut->req_target_y_i = 2;
      dut->req_txn_id_i = 0x0B;
      issue_second = 1;
    }

    tick(dut);
    if (issue_second) {
      dut->req_valid_i = 0;
      phase = 2;
      issue_second = 0;
    }
  }

  if (first_count != 3) {
    fprintf(stderr, "\n    FAIL: first_count=%d, expected 3", first_count);
    pass = 0;
  }
  if (second_count != 3) {
    fprintf(stderr, "\n    FAIL: second_count=%d, expected 3", second_count);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vpull_client_top;

  int failures = 0;

  printf("pull_client unit tests (from 05_pull_client.md):\n");
  failures += test_normal_request(dut);
  failures += test_backpressure(dut);
  failures += test_store_payloads(dut);
  failures += test_back_to_back_requests(dut);

  if (failures == 0) {
    printf("\nAll 4 tests PASSED\n");
  } else {
    printf("\n%d of 4 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
