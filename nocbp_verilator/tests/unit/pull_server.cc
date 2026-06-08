// pull_server.cc
// Unit test for pull_server
// Test cases from docs/gbp_pe/verification/unit_tests/06_pull_server.md

#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vpull_server_top.h"

static void tick(Vpull_server_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void eval_fall(Vpull_server_top* dut) {
  dut->clk = 0;
  dut->eval();
}

static void reset_dut(Vpull_server_top* dut) {
  dut->rst_n = 0;
  dut->req_valid_i = 0;
  dut->req_target_node_id_i = 0;
  dut->req_consumer_node_id_i = 0;
  dut->req_is_factor_i = 0;
  dut->req_fetch_src_x_i = 0;
  dut->req_fetch_src_y_i = 0;
  dut->req_txn_id_i = 0;
  dut->tx_fetch_resp_ready_i = 1;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

// ── Test Case 1: Normal Fetch Response ──
static int test_normal_response(Vpull_server_top* dut) {
  printf("  Test Case 1: Normal Fetch Response...");
  reset_dut(dut);
  int pass = 1;

  // Issue request
  dut->req_valid_i = 1;
  dut->req_target_node_id_i = 0x10;
  dut->req_consumer_node_id_i = 0x20;
  dut->req_is_factor_i = 0;
  dut->req_fetch_src_x_i = 2;
  dut->req_fetch_src_y_i = 1;
  dut->req_txn_id_i = 0x05;

  // Check req_ready at falling edge
  eval_fall(dut);
  if (!dut->req_ready_o) {
    fprintf(stderr, "\n    FAIL: req_ready=0");
    pass = 0;
  }

  // Complete tick (request accepted)
  dut->clk = 1;
  dut->eval();
  dut->req_valid_i = 0;

  // Wait for tx output and collect data words
  int data_count = 0;
  uint32_t data_words[4] = {0};
  int done_seen = 0;
  int tx_node_ok = 0, tx_consumer_ok = 0, tx_txn_ok = 0;
  int last_seen = 0;

  for (int i = 0; i < 30; ++i) {
    eval_fall(dut);

    if (dut->tx_fetch_resp_valid_o) {
      if (dut->tx_fetch_resp_data_valid_o) {
        if (data_count < 4) {
          data_words[data_count] = dut->tx_fetch_resp_data_o;
        }
        if (dut->tx_fetch_resp_node_id_o == 0x10) tx_node_ok = 1;
        if (dut->tx_fetch_resp_consumer_node_id_o == 0x20) tx_consumer_ok = 1;
        if (dut->tx_fetch_resp_txn_id_o == 0x05) tx_txn_ok = 1;
        if (dut->tx_fetch_resp_last_o) last_seen = 1;
        data_count++;
      } else if (dut->tx_fetch_resp_valid_o) {
        // Done cycle: tx_valid=1, tx_data_valid=0
        done_seen = 1;
      }
    }

    dut->clk = 1;
    dut->eval();
  }

  // Check results
  if (data_count != 2) {
    fprintf(stderr, "\n    FAIL: data_count=%d, expected 2", data_count);
    pass = 0;
  }
  if (data_words[0] != 0xDEAD0000) {
    fprintf(stderr, "\n    FAIL: data[0]=0x%x, expected 0xDEAD0000", data_words[0]);
    pass = 0;
  }
  if (data_words[1] != 0xBEEF0001) {
    fprintf(stderr, "\n    FAIL: data[1]=0x%x, expected 0xBEEF0001", data_words[1]);
    pass = 0;
  }
  if (!tx_node_ok) {
    fprintf(stderr, "\n    FAIL: tx_node_id mismatch");
    pass = 0;
  }
  if (!tx_consumer_ok) {
    fprintf(stderr, "\n    FAIL: tx_consumer_node_id mismatch");
    pass = 0;
  }
  if (!tx_txn_ok) {
    fprintf(stderr, "\n    FAIL: tx_txn_id mismatch");
    pass = 0;
  }
  if (!last_seen) {
    fprintf(stderr, "\n    FAIL: tx_last never asserted on final data word");
    pass = 0;
  }
  if (!done_seen) {
    fprintf(stderr, "\n    FAIL: done store cycle never seen");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: FSM Returns to IDLE ──
static int test_fsm_idle(Vpull_server_top* dut) {
  printf("  Test Case 3: FSM Returns to IDLE...");
  reset_dut(dut);
  int pass = 1;

  // Issue request
  dut->req_valid_i = 1;
  dut->req_target_node_id_i = 0x10;
  dut->req_consumer_node_id_i = 0x20;
  dut->req_is_factor_i = 0;
  dut->req_fetch_src_x_i = 2;
  dut->req_fetch_src_y_i = 1;
  dut->req_txn_id_i = 0x05;
  tick(dut);
  dut->req_valid_i = 0;

  // Wait for transaction to complete (tx_valid goes low after last cycle)
  int cycles = 0;
  int tx_was_high = 0;
  while (cycles < 50) {
    eval_fall(dut);
    if (dut->tx_fetch_resp_valid_o) tx_was_high = 1;
    if (tx_was_high && !dut->tx_fetch_resp_valid_o) break;
    tick(dut);
    cycles++;
  }

  // req_ready should be 1 after transaction completes
  eval_fall(dut);
  if (!dut->req_ready_o) {
    fprintf(stderr, "\n    FAIL: req_ready=0 after transaction complete");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Backpressure ──
static int test_backpressure(Vpull_server_top* dut) {
  printf("  Test Case 2: Backpressure...");
  reset_dut(dut);
  int pass = 1;

  // Issue request with tx_ready=0
  dut->tx_fetch_resp_ready_i = 0;
  dut->req_valid_i = 1;
  dut->req_target_node_id_i = 0x10;
  dut->req_consumer_node_id_i = 0x20;
  dut->req_is_factor_i = 0;
  dut->req_fetch_src_x_i = 2;
  dut->req_fetch_src_y_i = 1;
  dut->req_txn_id_i = 0x05;
  tick(dut);
  dut->req_valid_i = 0;

  // Tick a few times with backpressure
  for (int i = 0; i < 5; ++i) tick(dut);

  // tx_valid should be asserted (trying to send)
  eval_fall(dut);
  if (!dut->tx_fetch_resp_valid_o) {
    fprintf(stderr, "\n    FAIL: tx_valid=0 with backpressure");
    pass = 0;
  }

  // Release backpressure
  dut->tx_fetch_resp_ready_i = 1;
  tick(dut);

  // Data should flow
  eval_fall(dut);
  if (!dut->tx_fetch_resp_valid_o) {
    fprintf(stderr, "\n    FAIL: tx_valid=0 after backpressure released");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: tx_last and tx_data_valid Timing ──
static int test_tx_last_and_data_valid(Vpull_server_top* dut) {
  printf("  Test Case 4: tx_last and tx_data_valid Timing...");
  reset_dut(dut);
  int pass = 1;

  dut->req_valid_i = 1;
  dut->req_target_node_id_i = 0x10;
  dut->req_consumer_node_id_i = 0x20;
  dut->req_is_factor_i = 0;
  dut->req_fetch_src_x_i = 2;
  dut->req_fetch_src_y_i = 1;
  dut->req_txn_id_i = 0x05;
  tick(dut);
  dut->req_valid_i = 0;

  int data_count = 0;
  int done_seen = 0;
  uint32_t expected_data[2] = {0xDEAD0000, 0xBEEF0001};

  for (int i = 0; i < 30; ++i) {
    eval_fall(dut);

    if (dut->tx_fetch_resp_valid_o) {
      if (dut->tx_fetch_resp_data_valid_o) {
        if (data_count < 2) {
          if (dut->tx_fetch_resp_data_o != expected_data[data_count]) {
            fprintf(stderr, "\n    FAIL: data[%d]=0x%x, expected 0x%x",
                    data_count, dut->tx_fetch_resp_data_o, expected_data[data_count]);
            pass = 0;
          }
          if (data_count == 0 && dut->tx_fetch_resp_last_o) {
            fprintf(stderr, "\n    FAIL: tx_last=1 on first data word");
            pass = 0;
          }
          if (data_count == 1 && !dut->tx_fetch_resp_last_o) {
            fprintf(stderr, "\n    FAIL: tx_last=0 on final data word");
            pass = 0;
          }
        }
        data_count++;
      } else {
        if (done_seen) {
          fprintf(stderr, "\n    FAIL: multiple done store cycles");
          pass = 0;
        }
        done_seen = 1;
        if (dut->tx_fetch_resp_last_o) {
          fprintf(stderr, "\n    FAIL: tx_last=1 on done store cycle");
          pass = 0;
        }
      }
    }

    tick(dut);
  }

  if (data_count != 2) {
    fprintf(stderr, "\n    FAIL: data_count=%d, expected 2", data_count);
    pass = 0;
  }
  if (!done_seen) {
    fprintf(stderr, "\n    FAIL: done store cycle never seen");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: Zero state_words Response ──
static int test_zero_state_words(Vpull_server_top* dut) {
  printf("  Test Case 5: Zero state_words Response...");
  reset_dut(dut);
  int pass = 1;

  dut->req_valid_i = 1;
  dut->req_target_node_id_i = 0x11;  // header has state_words=0
  dut->req_consumer_node_id_i = 0x21;
  dut->req_is_factor_i = 0;
  dut->req_fetch_src_x_i = 2;
  dut->req_fetch_src_y_i = 1;
  dut->req_txn_id_i = 0x06;
  tick(dut);
  dut->req_valid_i = 0;

  int data_count = 0;
  int done_seen = 0;

  for (int i = 0; i < 20; ++i) {
    eval_fall(dut);

    if (dut->tx_fetch_resp_valid_o) {
      if (dut->tx_fetch_resp_data_valid_o) {
        data_count++;
      } else {
        done_seen++;
        if (dut->tx_fetch_resp_state_words_o != 0) {
          fprintf(stderr, "\n    FAIL: state_words=%d on zero-word done",
                  (int)dut->tx_fetch_resp_state_words_o);
          pass = 0;
        }
      }
    }

    tick(dut);
  }

  if (data_count != 0) {
    fprintf(stderr, "\n    FAIL: data_count=%d, expected 0", data_count);
    pass = 0;
  }
  if (done_seen != 1) {
    fprintf(stderr, "\n    FAIL: done_seen=%d, expected 1", done_seen);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 6: Max state_words Response ──
static int test_max_state_words(Vpull_server_top* dut) {
  printf("  Test Case 6: Max state_words Response...");
  reset_dut(dut);
  int pass = 1;

  dut->req_valid_i = 1;
  dut->req_target_node_id_i = 0x12;  // header has state_words=4
  dut->req_consumer_node_id_i = 0x22;
  dut->req_is_factor_i = 0;
  dut->req_fetch_src_x_i = 2;
  dut->req_fetch_src_y_i = 1;
  dut->req_txn_id_i = 0x07;
  tick(dut);
  dut->req_valid_i = 0;

  int data_count = 0;
  int done_seen = 0;
  uint32_t expected_data[4] = {
    0xA0000000, 0xA0000001, 0xA0000002, 0xA0000003
  };

  for (int i = 0; i < 40; ++i) {
    eval_fall(dut);

    if (dut->tx_fetch_resp_valid_o) {
      if (dut->tx_fetch_resp_data_valid_o) {
        if (data_count < 4) {
          if (dut->tx_fetch_resp_data_o != expected_data[data_count]) {
            fprintf(stderr, "\n    FAIL: data[%d]=0x%x, expected 0x%x",
                    data_count, dut->tx_fetch_resp_data_o, expected_data[data_count]);
            pass = 0;
          }
          if (data_count < 3 && dut->tx_fetch_resp_last_o) {
            fprintf(stderr, "\n    FAIL: tx_last=1 before final data word");
            pass = 0;
          }
          if (data_count == 3 && !dut->tx_fetch_resp_last_o) {
            fprintf(stderr, "\n    FAIL: tx_last=0 on final data word");
            pass = 0;
          }
        }
        data_count++;
      } else {
        done_seen++;
      }
    }

    tick(dut);
  }

  if (data_count != 4) {
    fprintf(stderr, "\n    FAIL: data_count=%d, expected 4", data_count);
    pass = 0;
  }
  if (done_seen != 1) {
    fprintf(stderr, "\n    FAIL: done_seen=%d, expected 1", done_seen);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vpull_server_top;

  int failures = 0;

  printf("pull_server unit tests (from 06_pull_server.md):\n");
  failures += test_normal_response(dut);
  failures += test_backpressure(dut);
  failures += test_fsm_idle(dut);
  failures += test_tx_last_and_data_valid(dut);
  failures += test_zero_state_words(dut);
  failures += test_max_state_words(dut);

  if (failures == 0) {
    printf("\nAll 6 tests PASSED\n");
  } else {
    printf("\n%d of 6 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
