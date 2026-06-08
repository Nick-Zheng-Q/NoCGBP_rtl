// fetch_subsystem.cc
// Integration test for Fetch Subsystem
// scoreboard_prefetcher + pull_client + response_collector

#include <cstdio>
#include <cstdlib>
#include <cstdint>

#include "verilated.h"
#include "Vfetch_subsystem_top.h"

static void tick(Vfetch_subsystem_top* dut) {
  dut->clk = 0; dut->eval();
  dut->clk = 1; dut->eval();
}

static void eval_fall(Vfetch_subsystem_top* dut) {
  dut->clk = 0; dut->eval();
}

static void reset_dut(Vfetch_subsystem_top* dut, int cycles = 5) {
  dut->rst_n = 0;
  dut->adj_valid = 0;
  dut->tx_fetch_req_ready = 1;
  dut->rx_notif_valid = 0;
  dut->rx_fetch_resp_valid = 0;
  dut->staging_wr_ready = 1;
  dut->remote_ready = 1;
  dut->staging_reserve_ready = 1;
  dut->staging_batch_closed = 0;
  dut->staging_batch_done = 0;
  dut->reset_valid = 0;
  for (int i = 0; i < cycles; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

// ── Test Case 1: Single Remote Edge Fetch ──
static int test_single_remote_edge(Vfetch_subsystem_top* dut) {
  printf("  Test Case 1: Single Remote Edge Fetch...");
  reset_dut(dut);
  int pass = 1;

  // Register a remote edge: current node 0x20, neighbor 0x10 at (1,0)
  dut->adj_valid = 1;
  dut->adj_neighbor_id = 0x10;
  dut->adj_neighbor_x = 1;
  dut->adj_neighbor_y = 0;
  dut->adj_is_local = 0;
  dut->adj_last = 1;
  dut->adj_edge_idx = 0;
  dut->adj_current_node_id = 0x20;
  tick(dut);
  dut->adj_valid = 0;

  // Receive notification from producer
  dut->rx_notif_valid = 1;
  dut->rx_notif_source_node_id = 0x10;
  dut->rx_notif_is_factor = 1;
  dut->rx_notif_source_x = 1;
  dut->rx_notif_source_y = 0;
  tick(dut);
  dut->rx_notif_valid = 0;

  // Wait for scoreboard to become ready
  for (int i = 0; i < 10; ++i) {
    tick(dut);
    if (dut->tx_fetch_req_valid) break;
  }

  if (!dut->tx_fetch_req_valid) {
    fprintf(stderr, "\n    FAIL: fetch request not issued");
    pass = 0;
  }
  if (dut->tx_fetch_req_target_node_id != 0x10 || dut->tx_fetch_req_consumer_node_id != 0x20) {
    fprintf(stderr, "\n    FAIL: fetch request IDs mismatch");
    pass = 0;
  }

  // Accept the 3-store TX sequence
  for (int i = 0; i < 5; ++i) tick(dut);

  // Simulate fetch response arriving
  dut->rx_fetch_resp_valid = 1;
  dut->rx_fetch_resp_is_factor = 1;
  dut->rx_fetch_resp_state_words = 4;
  dut->rx_fetch_resp_node_id = 0x10;
  dut->rx_fetch_resp_consumer_node_id = 0x20;
  dut->rx_fetch_resp_txn_id = dut->tx_fetch_req_txn_id;
  tick(dut);

  // Data words
  dut->rx_fetch_resp_data_valid = 1;
  dut->rx_fetch_resp_data = 0x3F800000; // 1.0f
  tick(dut);
  dut->rx_fetch_resp_data = 0x40000000; // 2.0f
  tick(dut);
  dut->rx_fetch_resp_data = 0x40400000; // 3.0f
  tick(dut);
  dut->rx_fetch_resp_data = 0x40800000; // 4.0f
  tick(dut);
  dut->rx_fetch_resp_data_valid = 0;

  // Done
  dut->rx_fetch_resp_last = 1;
  dut->rx_fetch_resp_done_valid = 1;
  tick(dut);

  // complete_valid is combinational on rx_done_valid_i, check immediately
  if (!dut->complete_valid) {
    fprintf(stderr, "\n    FAIL: completion not signaled on done tick");
    pass = 0;
  }

  dut->rx_fetch_resp_valid = 0;
  dut->rx_fetch_resp_done_valid = 0;
  dut->rx_fetch_resp_last = 0;

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Scoreboard Full Backpressure ──
static int test_scoreboard_full(Vfetch_subsystem_top* dut) {
  printf("  Test Case 2: Scoreboard Full Backpressure...");
  reset_dut(dut);
  int pass = 1;

  // Fill scoreboard with 64 remote edges (max depth)
  for (int i = 0; i < 64; ++i) {
    dut->adj_valid = 1;
    dut->adj_neighbor_id = i;
    dut->adj_neighbor_x = 1;
    dut->adj_neighbor_y = 0;
    dut->adj_is_local = 0;
    dut->adj_last = (i == 63);
    dut->adj_edge_idx = i % 16;
    dut->adj_current_node_id = 0x20 + (i / 16);
    tick(dut);
    dut->adj_valid = 0;

    dut->rx_notif_valid = 1;
    dut->rx_notif_source_node_id = i;
    dut->rx_notif_is_factor = 1;
    dut->rx_notif_source_x = 1;
    dut->rx_notif_source_y = 0;
    tick(dut);
    dut->rx_notif_valid = 0;
  }

  // Wait for all 64 fetches to issue (scan + pull_client ~5 cycles each)
  for (int i = 0; i < 600; ++i) tick(dut);

  // Try to add one more edge - should be backpressured (adj_ready=0)
  dut->adj_valid = 1;
  dut->adj_neighbor_id = 0xFF;
  tick(dut);

  if (dut->adj_ready) {
    fprintf(stderr, "\n    FAIL: adj_ready=1 when scoreboard full");
    pass = 0;
  }
  dut->adj_valid = 0;

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: Batch Closure ──
static int test_batch_closure(Vfetch_subsystem_top* dut) {
  printf("  Test Case 3: Batch Closure...");
  reset_dut(dut);
  int pass = 1;

  // Register several edges
  for (int i = 0; i < 5; ++i) {
    dut->adj_valid = 1;
    dut->adj_neighbor_id = 0x10 + i;
    dut->adj_neighbor_x = 1;
    dut->adj_neighbor_y = 0;
    dut->adj_is_local = 0;
    dut->adj_last = (i == 4);
    dut->adj_edge_idx = i;
    dut->adj_current_node_id = 0x20;
    tick(dut);

    dut->rx_notif_valid = 1;
    dut->rx_notif_source_node_id = 0x10 + i;
    dut->rx_notif_is_factor = 1;
    tick(dut);
    dut->rx_notif_valid = 0;
  }
  dut->adj_valid = 0;

  // Wait for fetches
  for (int i = 0; i < 10; ++i) tick(dut);

  // Signal batch done
  dut->staging_batch_done = 1;
  tick(dut);
  dut->staging_batch_done = 0;

  // Check that batch closed signal was asserted
  for (int i = 0; i < 10; ++i) tick(dut);

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: Local edge does NOT generate fetch request ──
static int test_local_edge(Vfetch_subsystem_top* dut) {
  printf("  Test Case 4: Local edge skip...");
  reset_dut(dut);
  int pass = 1;

  // Register a local edge
  dut->adj_valid = 1;
  dut->adj_neighbor_id = 0x10;
  dut->adj_neighbor_x = 0;
  dut->adj_neighbor_y = 0;
  dut->adj_is_local = 1;
  dut->adj_last = 1;
  dut->adj_edge_idx = 0;
  dut->adj_current_node_id = 0x20;
  tick(dut);
  dut->adj_valid = 0;

  // No notification needed for local edges
  // Wait and verify no fetch request is issued
  for (int i = 0; i < 15; ++i) {
    tick(dut);
    if (dut->tx_fetch_req_valid) {
      fprintf(stderr, "\n    FAIL: fetch request issued for local edge");
      pass = 0;
      break;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: Completion ID propagation ──
static int test_completion_ids(Vfetch_subsystem_top* dut) {
  printf("  Test Case 5: Completion ID propagation...");
  reset_dut(dut);
  int pass = 1;

  // Register remote edge
  dut->adj_valid = 1;
  dut->adj_neighbor_id = 0x30;
  dut->adj_neighbor_x = 1;
  dut->adj_neighbor_y = 0;
  dut->adj_is_local = 0;
  dut->adj_last = 1;
  dut->adj_edge_idx = 0;
  dut->adj_current_node_id = 0x40;
  tick(dut);
  dut->adj_valid = 0;

  dut->rx_notif_valid = 1;
  dut->rx_notif_source_node_id = 0x30;
  dut->rx_notif_is_factor = 1;
  tick(dut);
  dut->rx_notif_valid = 0;

  // Wait for fetch request to issue
  for (int i = 0; i < 10; ++i) {
    tick(dut);
    if (dut->tx_fetch_req_valid) break;
  }

  uint32_t txn_id = dut->tx_fetch_req_txn_id;

  // Accept the store sequence
  for (int i = 0; i < 5; ++i) tick(dut);

  // Send response with matching IDs
  dut->rx_fetch_resp_valid = 1;
  dut->rx_fetch_resp_is_factor = 1;
  dut->rx_fetch_resp_state_words = 2;
  dut->rx_fetch_resp_node_id = 0x30;
  dut->rx_fetch_resp_consumer_node_id = 0x40;
  dut->rx_fetch_resp_txn_id = txn_id;
  tick(dut);

  dut->rx_fetch_resp_data_valid = 1;
  dut->rx_fetch_resp_data = 0x1111;
  tick(dut);
  dut->rx_fetch_resp_data = 0x2222;
  dut->rx_fetch_resp_last = 1;
  tick(dut);
  dut->rx_fetch_resp_data_valid = 0;

  dut->rx_fetch_resp_done_valid = 1;
  eval_fall(dut);

  if (!dut->complete_valid) {
    fprintf(stderr, "\n    FAIL: complete_valid=0");
    pass = 0;
  }
  if (dut->complete_node_id != 0x30) {
    fprintf(stderr, "\n    FAIL: complete_node_id=0x%x, expected 0x30", dut->complete_node_id);
    pass = 0;
  }
  if (dut->complete_consumer_node_id != 0x40) {
    fprintf(stderr, "\n    FAIL: complete_consumer_node_id=0x%x, expected 0x40", dut->complete_consumer_node_id);
    pass = 0;
  }

  dut->rx_fetch_resp_valid = 0;
  dut->rx_fetch_resp_done_valid = 0;
  dut->rx_fetch_resp_last = 0;

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vfetch_subsystem_top;

  int failures = 0;
  printf("Fetch Subsystem integration tests:\n");
  failures += test_single_remote_edge(dut);
  failures += test_scoreboard_full(dut);
  failures += test_batch_closure(dut);
  failures += test_local_edge(dut);
  failures += test_completion_ids(dut);

  if (failures == 0) {
    printf("\nAll 5 tests PASSED\n");
  } else {
    printf("\n%d of 5 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
