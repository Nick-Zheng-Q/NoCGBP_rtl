// response_collector.cc
// Unit test for response_collector
// Test cases from docs/gbp_pe/verification/unit_tests/07_response_collector.md

#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vresponse_collector_top.h"

static void tick(Vresponse_collector_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void eval_fall(Vresponse_collector_top* dut) {
  dut->clk = 0;
  dut->eval();
}

static void reset_dut(Vresponse_collector_top* dut) {
  dut->rst_n = 0;
  dut->rx_valid_i = 0;
  dut->rx_is_factor_i = 0;
  dut->rx_state_words_i = 0;
  dut->rx_data_i = 0;
  dut->rx_data_valid_i = 0;
  dut->rx_last_i = 0;
  dut->rx_done_valid_i = 0;
  dut->rx_txn_id_i = 0;
  dut->rx_node_id_i = 0;
  dut->rx_consumer_node_id_i = 0;
  dut->remote_ready_i = 1;
  dut->staging_wr_ready_i = 1;
  dut->staging_reserve_valid_i = 0;
  dut->staging_reserve_words_i = 0;
  dut->staging_batch_done_i = 0;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

// ── Test Case 1: Normal Response Collection ──
static int test_normal(Vresponse_collector_top* dut) {
  printf("  Test Case 1: Normal Response Collection...");
  reset_dut(dut);
  int pass = 1;

  // Metadata
  dut->rx_valid_i = 1;
  dut->rx_is_factor_i = 0;
  dut->rx_state_words_i = 2;
  tick(dut);

  // Data word 0
  dut->rx_data_valid_i = 1;
  dut->rx_data_i = 0xAAAA;
  dut->rx_last_i = 0;
  eval_fall(dut);
  if (!dut->remote_valid_o || dut->remote_data_o != 0xAAAA) {
    fprintf(stderr, "\n    FAIL: data word 0 mismatch");
    pass = 0;
  }
  if (dut->remote_last_o) {
    fprintf(stderr, "\n    FAIL: remote_last=1 on word 0");
    pass = 0;
  }
  tick(dut);

  // Data word 1 (last)
  dut->rx_data_i = 0xBBBB;
  dut->rx_last_i = 1;
  eval_fall(dut);
  if (!dut->remote_valid_o || dut->remote_data_o != 0xBBBB) {
    fprintf(stderr, "\n    FAIL: data word 1 mismatch");
    pass = 0;
  }
  if (!dut->remote_last_o) {
    fprintf(stderr, "\n    FAIL: remote_last=0 on last word");
    pass = 0;
  }
  tick(dut);

  // Done
  dut->rx_data_valid_i = 0;
  dut->rx_last_i = 0;
  dut->rx_done_valid_i = 1;
  dut->rx_txn_id_i = 0x03;
  eval_fall(dut);
  if (!dut->complete_valid_o) {
    fprintf(stderr, "\n    FAIL: complete_valid=0");
    pass = 0;
  }
  if (dut->complete_txn_id_o != 0x03) {
    fprintf(stderr, "\n    FAIL: complete_txn_id=0x%x, expected 0x03", dut->complete_txn_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Backpressure ──
static int test_backpressure(Vresponse_collector_top* dut) {
  printf("  Test Case 2: Backpressure...");
  reset_dut(dut);
  int pass = 1;

  // Metadata
  dut->rx_valid_i = 1;
  dut->rx_state_words_i = 1;
  tick(dut);

  // Data with remote_ready=0
  dut->remote_ready_i = 0;
  dut->rx_data_valid_i = 1;
  dut->rx_data_i = 0xCCCC;
  dut->rx_last_i = 1;
  eval_fall(dut);

  // remote_valid should still be asserted (pass-through)
  if (!dut->remote_valid_o) {
    fprintf(stderr, "\n    FAIL: remote_valid=0 with backpressure");
    pass = 0;
  }

  // rx_ready should always be 1
  if (!dut->rx_ready_o) {
    fprintf(stderr, "\n    FAIL: rx_ready=0");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: complete_node_id and complete_consumer_node_id ──
static int test_complete_ids(Vresponse_collector_top* dut) {
  printf("  Test Case 3: complete_node_id passthrough...");
  reset_dut(dut);
  int pass = 1;

  dut->rx_valid_i = 1;
  dut->rx_state_words_i = 2;
  dut->rx_node_id_i = 0x123;
  dut->rx_consumer_node_id_i = 0x2AB;  // 10-bit max
  tick(dut);

  dut->rx_valid_i = 0;
  dut->rx_data_valid_i = 1;
  dut->rx_data_i = 0xAAAA;
  dut->rx_last_i = 0;
  tick(dut);

  dut->rx_data_i = 0xBBBB;
  dut->rx_last_i = 1;
  tick(dut);

  dut->rx_data_valid_i = 0;
  dut->rx_done_valid_i = 1;
  dut->rx_txn_id_i = 0x05;
  eval_fall(dut);

  if (!dut->complete_valid_o) {
    fprintf(stderr, "\n    FAIL: complete_valid=0");
    pass = 0;
  }
  if (dut->complete_node_id_o != 0x123) {
    fprintf(stderr, "\n    FAIL: complete_node_id=0x%x, expected 0x123", dut->complete_node_id_o);
    pass = 0;
  }
  if (dut->complete_consumer_node_id_o != 0x2AB) {
    fprintf(stderr, "\n    FAIL: complete_consumer_node_id=0x%x, expected 0x2AB", dut->complete_consumer_node_id_o);
    pass = 0;
  }
  if (dut->complete_txn_id_o != 0x05) {
    fprintf(stderr, "\n    FAIL: complete_txn_id=0x%x, expected 0x05", dut->complete_txn_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: STAGING write address tracking ──
static int test_staging_addr(Vresponse_collector_top* dut) {
  printf("  Test Case 4: STAGING write address...");
  reset_dut(dut);
  int pass = 1;

  // Reserve space
  dut->staging_reserve_valid_i = 1;
  dut->staging_reserve_words_i = 4;
  tick(dut);
  dut->staging_reserve_valid_i = 0;

  // Metadata for txn 0
  dut->rx_valid_i = 1;
  dut->rx_state_words_i = 4;
  tick(dut);
  dut->rx_valid_i = 0;

  // 4 data words = 2 beats
  dut->rx_data_valid_i = 1;
  dut->rx_data_i = 0x1111;
  dut->rx_last_i = 0;
  tick(dut);
  dut->rx_data_i = 0x2222;
  tick(dut);

  // After 2 words, staging_wr_valid should assert for beat 0 at addr 0
  eval_fall(dut);
  if (!dut->staging_wr_valid_o) {
    fprintf(stderr, "\n    FAIL: staging_wr_valid=0 after first 2 words");
    pass = 0;
  }
  if (dut->staging_wr_addr_o != 0) {
    fprintf(stderr, "\n    FAIL: staging_wr_addr=0x%x, expected 0", dut->staging_wr_addr_o);
    pass = 0;
  }
  tick(dut);

  // Continue with last 2 words
  dut->rx_data_i = 0x3333;
  tick(dut);
  dut->rx_data_i = 0x4444;
  dut->rx_last_i = 1;
  tick(dut);

  eval_fall(dut);
  if (!dut->staging_wr_valid_o) {
    fprintf(stderr, "\n    FAIL: staging_wr_valid=0 after second 2 words");
    pass = 0;
  }
  if (dut->staging_wr_addr_o != 2) { // word addr advances by 2 per beat
    fprintf(stderr, "\n    FAIL: staging_wr_addr=0x%x, expected 2", dut->staging_wr_addr_o);
    pass = 0;
  }
  tick(dut);

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: Odd word count (partial beat) ──
static int test_odd_words(Vresponse_collector_top* dut) {
  printf("  Test Case 5: Odd word count (partial beat)...");
  reset_dut(dut);
  int pass = 1;

  dut->staging_reserve_valid_i = 1;
  dut->staging_reserve_words_i = 3;
  tick(dut);
  dut->staging_reserve_valid_i = 0;

  dut->rx_valid_i = 1;
  dut->rx_state_words_i = 3;
  tick(dut);
  dut->rx_valid_i = 0;

  // Word 0
  dut->rx_data_valid_i = 1;
  dut->rx_data_i = 0xA1B2;
  dut->rx_last_i = 0;
  tick(dut);

  // Word 1
  dut->rx_data_i = 0xC3D4;
  tick(dut);

  // After 2 words: full beat at addr 0
  eval_fall(dut);
  if (!dut->staging_wr_valid_o) {
    fprintf(stderr, "\n    FAIL: staging_wr_valid=0 after first 2 words");
    pass = 0;
  }
  if (dut->staging_wr_addr_o != 0) {
    fprintf(stderr, "\n    FAIL: first beat addr=0x%x, expected 0", dut->staging_wr_addr_o);
    pass = 0;
  }
  tick(dut);

  // Word 2 (last, odd)
  dut->rx_data_i = 0xE5F6;
  dut->rx_last_i = 1;
  tick(dut);

  // Odd word count should produce partial beat at addr 2
  eval_fall(dut);
  if (!dut->staging_wr_valid_o) {
    fprintf(stderr, "\n    FAIL: staging_wr_valid=0 after last odd word");
    pass = 0;
  }
  if (dut->staging_wr_addr_o != 1) {
    fprintf(stderr, "\n    FAIL: partial beat addr=0x%x, expected 1", dut->staging_wr_addr_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 6: batch_done resets staging bump ──
static int test_batch_done(Vresponse_collector_top* dut) {
  printf("  Test Case 6: batch_done reset...");
  reset_dut(dut);
  int pass = 1;

  // Reserve and process one transaction
  dut->staging_reserve_valid_i = 1;
  dut->staging_reserve_words_i = 2;
  tick(dut);
  dut->staging_reserve_valid_i = 0;

  dut->rx_valid_i = 1;
  dut->rx_state_words_i = 2;
  tick(dut);
  dut->rx_valid_i = 0;

  dut->rx_data_valid_i = 1;
  dut->rx_data_i = 0x1111;
  dut->rx_last_i = 0;
  tick(dut);
  dut->rx_data_i = 0x2222;
  dut->rx_last_i = 1;
  tick(dut);
  dut->rx_data_valid_i = 0;

  dut->rx_done_valid_i = 1;
  tick(dut);
  dut->rx_done_valid_i = 0;

  // Assert batch_done
  dut->staging_batch_done_i = 1;
  tick(dut);
  dut->staging_batch_done_i = 0;

  // Next reservation should start at addr 0 again
  dut->staging_reserve_valid_i = 1;
  dut->staging_reserve_words_i = 2;
  tick(dut);
  dut->staging_reserve_valid_i = 0;

  dut->rx_valid_i = 1;
  dut->rx_state_words_i = 2;
  tick(dut);
  dut->rx_valid_i = 0;

  dut->rx_data_valid_i = 1;
  dut->rx_data_i = 0x3333;
  dut->rx_last_i = 0;
  tick(dut);
  dut->rx_data_i = 0x4444;
  dut->rx_last_i = 1;
  tick(dut);

  eval_fall(dut);
  if (!dut->staging_wr_valid_o) {
    fprintf(stderr, "\n    FAIL: staging_wr_valid=0 after post-batch data");
    pass = 0;
  }
  if (dut->staging_wr_addr_o != 0) {
    fprintf(stderr, "\n    FAIL: post-batch addr=0x%x, expected 0 (bump reset)", dut->staging_wr_addr_o);
    pass = 0;
  }
  tick(dut);

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 7: Maximum-width node IDs on completion ──
static int test_complete_ids_max(Vresponse_collector_top* dut) {
  printf("  Test Case 7: complete_node_id max values...");
  reset_dut(dut);
  int pass = 1;

  dut->rx_valid_i = 1;
  dut->rx_state_words_i = 1;
  dut->rx_node_id_i = 0x3FF;
  dut->rx_consumer_node_id_i = 0x3FF;
  tick(dut);
  dut->rx_valid_i = 0;

  dut->rx_data_valid_i = 1;
  dut->rx_data_i = 0xDEAD;
  dut->rx_last_i = 1;
  tick(dut);
  dut->rx_data_valid_i = 0;

  dut->rx_done_valid_i = 1;
  dut->rx_txn_id_i = 0x3F;
  eval_fall(dut);

  if (!dut->complete_valid_o) {
    fprintf(stderr, "\n    FAIL: complete_valid=0");
    pass = 0;
  }
  if (dut->complete_node_id_o != 0x3FF) {
    fprintf(stderr, "\n    FAIL: complete_node_id=0x%x, expected 0x3FF", dut->complete_node_id_o);
    pass = 0;
  }
  if (dut->complete_consumer_node_id_o != 0x3FF) {
    fprintf(stderr, "\n    FAIL: complete_consumer_node_id=0x%x, expected 0x3FF", dut->complete_consumer_node_id_o);
    pass = 0;
  }
  if (dut->complete_txn_id_o != 0x3F) {
    fprintf(stderr, "\n    FAIL: complete_txn_id=0x%x, expected 0x3F", dut->complete_txn_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 8: complete_node_id/consumer_node_id across consecutive responses ──
static int test_complete_ids_consecutive(Vresponse_collector_top* dut) {
  printf("  Test Case 8: complete_node_id across consecutive responses...");
  reset_dut(dut);
  int pass = 1;

  // Transaction A
  dut->staging_reserve_valid_i = 1;
  dut->staging_reserve_words_i = 2;
  tick(dut);
  dut->staging_reserve_valid_i = 0;

  dut->rx_valid_i = 1;
  dut->rx_state_words_i = 2;
  dut->rx_node_id_i = 0x111;
  dut->rx_consumer_node_id_i = 0x222;
  tick(dut);
  dut->rx_valid_i = 0;

  dut->rx_data_valid_i = 1;
  dut->rx_data_i = 0xAAAA;
  dut->rx_last_i = 0;
  tick(dut);
  dut->rx_data_i = 0xBBBB;
  dut->rx_last_i = 1;
  tick(dut);
  dut->rx_data_valid_i = 0;

  dut->rx_done_valid_i = 1;
  dut->rx_txn_id_i = 0x01;
  eval_fall(dut);
  if (!dut->complete_valid_o) {
    fprintf(stderr, "\n    FAIL: txn A complete_valid=0");
    pass = 0;
  }
  if (dut->complete_node_id_o != 0x111) {
    fprintf(stderr, "\n    FAIL: txn A complete_node_id=0x%x, expected 0x111", dut->complete_node_id_o);
    pass = 0;
  }
  if (dut->complete_consumer_node_id_o != 0x222) {
    fprintf(stderr, "\n    FAIL: txn A complete_consumer_node_id=0x%x, expected 0x222", dut->complete_consumer_node_id_o);
    pass = 0;
  }
  tick(dut);
  dut->rx_done_valid_i = 0;

  // Transaction B
  dut->staging_reserve_valid_i = 1;
  dut->staging_reserve_words_i = 2;
  tick(dut);
  dut->staging_reserve_valid_i = 0;

  dut->rx_valid_i = 1;
  dut->rx_state_words_i = 2;
  dut->rx_node_id_i = 0x333;
  dut->rx_consumer_node_id_i = 0x3AA;
  tick(dut);
  dut->rx_valid_i = 0;

  dut->rx_data_valid_i = 1;
  dut->rx_data_i = 0xCCCC;
  dut->rx_last_i = 0;
  tick(dut);
  dut->rx_data_i = 0xDDDD;
  dut->rx_last_i = 1;
  tick(dut);
  dut->rx_data_valid_i = 0;

  dut->rx_done_valid_i = 1;
  dut->rx_txn_id_i = 0x02;
  eval_fall(dut);
  if (!dut->complete_valid_o) {
    fprintf(stderr, "\n    FAIL: txn B complete_valid=0");
    pass = 0;
  }
  if (dut->complete_node_id_o != 0x333) {
    fprintf(stderr, "\n    FAIL: txn B complete_node_id=0x%x, expected 0x333", dut->complete_node_id_o);
    pass = 0;
  }
  if (dut->complete_consumer_node_id_o != 0x3AA) {
    fprintf(stderr, "\n    FAIL: txn B complete_consumer_node_id=0x%x, expected 0x3AA", dut->complete_consumer_node_id_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vresponse_collector_top;

  int failures = 0;

  printf("response_collector unit tests (from 07_response_collector.md):\n");
  failures += test_normal(dut);
  failures += test_backpressure(dut);
  failures += test_complete_ids(dut);
  failures += test_staging_addr(dut);
  failures += test_odd_words(dut);
  failures += test_batch_done(dut);
  failures += test_complete_ids_max(dut);
  failures += test_complete_ids_consecutive(dut);

  if (failures == 0) {
    printf("\nAll 8 tests PASSED\n");
  } else {
    printf("\n%d of 8 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
