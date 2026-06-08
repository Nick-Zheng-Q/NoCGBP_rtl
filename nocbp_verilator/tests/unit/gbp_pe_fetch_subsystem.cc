// gbp_pe_fetch_subsystem.cc
// Functional test for fetch subsystem.
// Verifies scoreboard_prefetcher -> pull_client path.

#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vgbp_pe_fetch_subsystem_top.h"

static void tick(Vgbp_pe_fetch_subsystem_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe_fetch_subsystem_top* dut) {
  dut->rst_n = 0;
  dut->adj_valid_i = 0;
  dut->rx_notif_valid_i = 0;
  dut->tx_fetch_req_ready_i = 1;
  dut->remote_ready_i = 1;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

// ── Test 1: Remote adjacency + notification produces fetch request ──
static int test_remote_fetch(Vgbp_pe_fetch_subsystem_top* dut) {
  printf("  Test 1: Remote Fetch Request...");
  reset_dut(dut);
  int pass = 1;

  // Send remote adjacency: node 0x10 depends on remote node 0x20
  dut->adj_valid_i = 1;
  dut->adj_neighbor_id_i = 0x20;
  dut->adj_neighbor_x_i = 1;
  dut->adj_neighbor_y_i = 2;
  dut->adj_is_local_i = 0;
  dut->adj_last_i = 1;
  dut->adj_edge_idx_i = 0;
  dut->adj_current_node_id_i = 0x10;
  tick(dut);
  dut->adj_valid_i = 0;

  // Send notification from node 0x20
  dut->rx_notif_valid_i = 1;
  dut->rx_notif_source_node_id_i = 0x20;
  dut->rx_notif_is_factor_i = 0;
  dut->rx_notif_source_x_i = 1;
  dut->rx_notif_source_y_i = 2;
  tick(dut);
  dut->rx_notif_valid_i = 0;

  // Wait for fetch request
  int cycles = 0;
  while (!dut->tx_fetch_req_valid_o && cycles < 50) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 50) {
    fprintf(stderr, "\n    FAIL: tx_fetch_req_valid never asserted");
    pass = 0;
  } else {
    if (dut->tx_fetch_req_target_node_id_o != 0x20) {
      fprintf(stderr, "\n    FAIL: target_node_id=0x%x, expected 0x20",
              dut->tx_fetch_req_target_node_id_o);
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 2: Local adjacency is filtered (no fetch request) ──
static int test_local_filtered(Vgbp_pe_fetch_subsystem_top* dut) {
  printf("  Test 2: Local Adjacency Filtered...");
  reset_dut(dut);
  int pass = 1;

  // Send local adjacency
  dut->adj_valid_i = 1;
  dut->adj_neighbor_id_i = 0x30;
  dut->adj_neighbor_x_i = 0;
  dut->adj_neighbor_y_i = 0;
  dut->adj_is_local_i = 1;
  dut->adj_last_i = 1;
  dut->adj_edge_idx_i = 0;
  dut->adj_current_node_id_i = 0x10;
  tick(dut);
  dut->adj_valid_i = 0;

  // Wait a few cycles
  for (int i = 0; i < 10; ++i) tick(dut);

  if (dut->tx_fetch_req_valid_o) {
    fprintf(stderr, "\n    FAIL: tx_fetch_req_valid asserted for local adjacency");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 3: Backpressure on tx_fetch_req_ready ──
static int test_backpressure(Vgbp_pe_fetch_subsystem_top* dut) {
  printf("  Test 3: Backpressure...");
  reset_dut(dut);
  int pass = 1;

  // Stall the TX path
  dut->tx_fetch_req_ready_i = 0;

  // Send remote adjacency + notification
  dut->adj_valid_i = 1;
  dut->adj_neighbor_id_i = 0x40;
  dut->adj_neighbor_x_i = 3;
  dut->adj_neighbor_y_i = 4;
  dut->adj_is_local_i = 0;
  dut->adj_last_i = 1;
  dut->adj_edge_idx_i = 0;
  dut->adj_current_node_id_i = 0x10;
  tick(dut);
  dut->adj_valid_i = 0;

  dut->rx_notif_valid_i = 1;
  dut->rx_notif_source_node_id_i = 0x40;
  dut->rx_notif_is_factor_i = 0;
  dut->rx_notif_source_x_i = 3;
  dut->rx_notif_source_y_i = 4;
  tick(dut);
  dut->rx_notif_valid_i = 0;

  // Wait for fetch request to appear (should still assert even if ready=0)
  int cycles = 0;
  while (!dut->tx_fetch_req_valid_o && cycles < 50) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 50) {
    fprintf(stderr, "\n    FAIL: tx_fetch_req_valid never asserted under backpressure");
    pass = 0;
  }

  // Now release ready and accept the request
  dut->tx_fetch_req_ready_i = 1;
  tick(dut);

  // After acceptance, valid should clear (pull_client consumes it)
  // Note: pull_client sends 3 stores; valid stays high during S_S0/S_S1/S_S2.
  // We just verify the request was generated; detailed store sequencing is
  // tested at pull_client level.

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 4: Response Full Path ──
static int test_response_full_path(Vgbp_pe_fetch_subsystem_top* dut) {
  printf("  Test 4: Response Full Path...");
  reset_dut(dut);
  int pass = 1;

  // Register remote edge: consumer=0x10, neighbor=0x20
  dut->adj_valid_i = 1;
  dut->adj_neighbor_id_i = 0x20;
  dut->adj_neighbor_x_i = 1;
  dut->adj_neighbor_y_i = 2;
  dut->adj_is_local_i = 0;
  dut->adj_last_i = 1;
  dut->adj_edge_idx_i = 0;
  dut->adj_current_node_id_i = 0x10;
  tick(dut);
  dut->adj_valid_i = 0;

  // Send notification from node 0x20
  dut->rx_notif_valid_i = 1;
  dut->rx_notif_source_node_id_i = 0x20;
  dut->rx_notif_is_factor_i = 0;
  dut->rx_notif_source_x_i = 1;
  dut->rx_notif_source_y_i = 2;
  tick(dut);
  dut->rx_notif_valid_i = 0;

  // Wait for fetch request
  int cycles = 0;
  while (!dut->tx_fetch_req_valid_o && cycles < 50) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 50) {
    fprintf(stderr, "\n    FAIL: tx_fetch_req_valid never asserted");
    pass = 0;
  }

  // Accept the fetch request
  tick(dut);

  // Send fetch response
  // txn_id=0 matches the edge index assigned by scoreboard
  dut->rx_fetch_resp_valid_i = 1;
  dut->rx_fetch_resp_is_factor_i = 0;
  dut->rx_fetch_resp_state_words_i = 2;
  dut->rx_fetch_resp_data_valid_i = 0;
  dut->rx_fetch_resp_last_i = 0;
  dut->rx_fetch_resp_done_valid_i = 0;
  dut->rx_fetch_resp_txn_id_i = 0;
  dut->rx_fetch_resp_node_id_i = 0x20;       // source node
  dut->rx_fetch_resp_consumer_node_id_i = 0x10;  // consumer node
  tick(dut);

  // Data word 0
  dut->rx_fetch_resp_data_i = 0x11111111;
  dut->rx_fetch_resp_data_valid_i = 1;
  tick(dut);

  // Data word 1 (last)
  dut->rx_fetch_resp_data_i = 0x22222222;
  dut->rx_fetch_resp_last_i = 1;
  tick(dut);

  // Done
  dut->rx_fetch_resp_data_valid_i = 0;
  dut->rx_fetch_resp_last_i = 0;
  dut->rx_fetch_resp_done_valid_i = 1;
  tick(dut);

  // Clear response
  dut->rx_fetch_resp_valid_i = 0;
  dut->rx_fetch_resp_done_valid_i = 0;

  // Wait one cycle for completion to propagate
  tick(dut);

  // Verify node_ready_o[0x10] is high
  // node_ready_o is 1024 bits = 32 x 32-bit words. Node 16 is word 0 bit 16.
  if ((dut->node_ready_o[0] & (1u << 16)) == 0) {
    fprintf(stderr, "\n    FAIL: node_ready_o[16]=0 after completion");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 5: Multi-Entry Deduplication ──
static int test_deduplication(Vgbp_pe_fetch_subsystem_top* dut) {
  printf("  Test 5: Multi-Entry Deduplication...");
  reset_dut(dut);
  int pass = 1;

  // Register same neighbor 3 times for same consumer
  for (int i = 0; i < 3; ++i) {
    dut->adj_valid_i = 1;
    dut->adj_neighbor_id_i = 0x30;
    dut->adj_neighbor_x_i = 2;
    dut->adj_neighbor_y_i = 3;
    dut->adj_is_local_i = 0;
    dut->adj_last_i = (i == 2);
    dut->adj_edge_idx_i = i;
    dut->adj_current_node_id_i = 0x11;
    tick(dut);
  }
  dut->adj_valid_i = 0;

  // Send one notification
  dut->rx_notif_valid_i = 1;
  dut->rx_notif_source_node_id_i = 0x30;
  tick(dut);
  dut->rx_notif_valid_i = 0;

  // Count fetch request rising edges over 30 cycles
  int req_count = 0;
  int prev_valid = 0;
  for (int i = 0; i < 30; ++i) {
    if (dut->tx_fetch_req_valid_o && !prev_valid) req_count++;
    prev_valid = dut->tx_fetch_req_valid_o;
    tick(dut);
  }

  // The scoreboard keeps 3 separate entries (one per edge registration),
  // so 3 fetches should be issued, one per edge.
  if (req_count != 3) {
    fprintf(stderr, "\n    FAIL: %d fetch requests, expected 3", req_count);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 6: Notification Mismatch ──
static int test_notification_mismatch(Vgbp_pe_fetch_subsystem_top* dut) {
  printf("  Test 6: Notification Mismatch...");
  reset_dut(dut);
  int pass = 1;

  // Register edge for neighbor 0x40
  dut->adj_valid_i = 1;
  dut->adj_neighbor_id_i = 0x40;
  dut->adj_is_local_i = 0;
  dut->adj_last_i = 1;
  dut->adj_current_node_id_i = 0x12;
  tick(dut);
  dut->adj_valid_i = 0;

  // Send notification from wrong node 0x50
  dut->rx_notif_valid_i = 1;
  dut->rx_notif_source_node_id_i = 0x50;
  tick(dut);
  dut->rx_notif_valid_i = 0;

  // Wait and verify no fetch request
  for (int i = 0; i < 15; ++i) tick(dut);
  if (dut->tx_fetch_req_valid_o) {
    fprintf(stderr, "\n    FAIL: fetch request issued for mismatched notification");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// Helper: read a bit from 1024-bit node_ready_o vector
static int node_ready_bit(Vgbp_pe_fetch_subsystem_top* dut, int node_id) {
  int word = node_id / 32;
  int bit  = node_id % 32;
  return (dut->node_ready_o[word] >> bit) & 1u;
}

// ── Test 7: Scoreboard Full ──
// The response_collector closes the batch when OUTSTANDING_DEPTH (8) fetches
// are in flight. Once the batch is closed, scoreboard_prefetcher deasserts
// adj_ready_o and refuses new edges even though the edge table is not full.
static int test_scoreboard_full(Vgbp_pe_fetch_subsystem_top* dut) {
  printf("  Test 7: Scoreboard Full...");
  reset_dut(dut);
  int pass = 1;

  const int BATCH_LIMIT = 8;

  // Register BATCH_LIMIT remote edges with unique consumers / sources
  for (int i = 0; i < BATCH_LIMIT; ++i) {
    dut->adj_valid_i = 1;
    dut->adj_neighbor_id_i = 0x100 + i;
    dut->adj_neighbor_x_i = 1;
    dut->adj_neighbor_y_i = 2;
    dut->adj_is_local_i = 0;
    dut->adj_last_i = 1;
    dut->adj_edge_idx_i = 0;
    dut->adj_current_node_id_i = 0x200 + i;
    tick(dut);
  }
  dut->adj_valid_i = 0;

  // Notify every registered source
  for (int i = 0; i < BATCH_LIMIT; ++i) {
    dut->rx_notif_valid_i = 1;
    dut->rx_notif_source_node_id_i = 0x100 + i;
    dut->rx_notif_is_factor_i = 0;
    dut->rx_notif_source_x_i = 1;
    dut->rx_notif_source_y_i = 2;
    tick(dut);
  }
  dut->rx_notif_valid_i = 0;

  // Wait until all fetch requests have been issued and accepted
  int cycles = 0;
  while (dut->scoreboard_occupancy_o < BATCH_LIMIT && cycles < 200) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 200) {
    fprintf(stderr, "\n    FAIL: occupancy=%d, expected %d", dut->scoreboard_occupancy_o, BATCH_LIMIT);
    pass = 0;
  }

  // Batch should now be closed -> adj_ready_o must be 0
  if (dut->adj_ready_o) {
    fprintf(stderr, "\n    FAIL: adj_ready=1 when batch closed (full)");
    pass = 0;
  }

  // Try to inject one more edge; scoreboard should reject it
  int occ_before = dut->scoreboard_occupancy_o;
  dut->adj_valid_i = 1;
  dut->adj_neighbor_id_i = 0x400;
  dut->adj_is_local_i = 0;
  dut->adj_last_i = 1;
  dut->adj_current_node_id_i = 0x400;
  tick(dut);
  dut->adj_valid_i = 0;
  tick(dut);

  if (dut->scoreboard_occupancy_o != occ_before) {
    fprintf(stderr, "\n    FAIL: occupancy changed from %d to %d after rejected adj", occ_before, dut->scoreboard_occupancy_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 8: Node Ready Bitmap - Local Edge ──
static int test_node_ready_local(Vgbp_pe_fetch_subsystem_top* dut) {
  printf("  Test 8: Node Ready Bitmap (Local)...");
  reset_dut(dut);
  int pass = 1;

  // Register one local edge for consumer node 0x20
  dut->adj_valid_i = 1;
  dut->adj_neighbor_id_i = 0x21;
  dut->adj_neighbor_x_i = 0;
  dut->adj_neighbor_y_i = 0;
  dut->adj_is_local_i = 1;
  dut->adj_last_i = 1;
  dut->adj_edge_idx_i = 0;
  dut->adj_current_node_id_i = 0x20;
  tick(dut);
  dut->adj_valid_i = 0;

  // Wait one cycle for node_has_edge/pending tracking to update
  tick(dut);

  if (!node_ready_bit(dut, 0x20)) {
    fprintf(stderr, "\n    FAIL: node_ready_o[0x20]=0 after local edge");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 9: Node Ready Bitmap - Remote Edge Completion ──
static int test_node_ready_remote(Vgbp_pe_fetch_subsystem_top* dut) {
  printf("  Test 9: Node Ready Bitmap (Remote)...");
  reset_dut(dut);
  int pass = 1;

  // Register remote edge for consumer node 0x30
  dut->adj_valid_i = 1;
  dut->adj_neighbor_id_i = 0x40;
  dut->adj_neighbor_x_i = 1;
  dut->adj_neighbor_y_i = 2;
  dut->adj_is_local_i = 0;
  dut->adj_last_i = 1;
  dut->adj_edge_idx_i = 0;
  dut->adj_current_node_id_i = 0x30;
  tick(dut);
  dut->adj_valid_i = 0;

  // Pending should be non-zero, so node_ready[0x30] should NOT be set yet
  tick(dut);
  if (node_ready_bit(dut, 0x30)) {
    fprintf(stderr, "\n    FAIL: node_ready_o[0x30]=1 before remote completion");
    pass = 0;
  }

  // Send notification to transition edge to NOTIFIED and trigger fetch
  dut->rx_notif_valid_i = 1;
  dut->rx_notif_source_node_id_i = 0x40;
  dut->rx_notif_is_factor_i = 0;
  dut->rx_notif_source_x_i = 1;
  dut->rx_notif_source_y_i = 2;
  tick(dut);
  dut->rx_notif_valid_i = 0;

  // Wait for fetch request to be generated and accepted
  int cycles = 0;
  while (!dut->tx_fetch_req_valid_o && cycles < 50) {
    tick(dut);
    cycles++;
  }
  if (cycles >= 50) {
    fprintf(stderr, "\n    FAIL: fetch request never asserted");
    pass = 0;
  }

  // Accept fetch request so pull_client can proceed
  tick(dut);

  // Send a minimal fetch response with done_valid to complete the edge
  dut->rx_fetch_resp_valid_i = 1;
  dut->rx_fetch_resp_is_factor_i = 0;
  dut->rx_fetch_resp_state_words_i = 0;
  dut->rx_fetch_resp_data_i = 0;
  dut->rx_fetch_resp_data_valid_i = 0;
  dut->rx_fetch_resp_last_i = 0;
  dut->rx_fetch_resp_done_valid_i = 1;
  dut->rx_fetch_resp_txn_id_i = 0;
  dut->rx_fetch_resp_node_id_i = 0x40;
  dut->rx_fetch_resp_consumer_node_id_i = 0x30;
  tick(dut);
  dut->rx_fetch_resp_valid_i = 0;
  dut->rx_fetch_resp_done_valid_i = 0;

  // Give scoreboard a cycle to decrement pending
  tick(dut);

  if (!node_ready_bit(dut, 0x30)) {
    fprintf(stderr, "\n    FAIL: node_ready_o[0x30]=0 after remote completion");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 10: Node Ready Bitmap - Mixed Local + Remote ──
static int test_node_ready_mixed(Vgbp_pe_fetch_subsystem_top* dut) {
  printf("  Test 10: Node Ready Bitmap (Mixed)...");
  reset_dut(dut);
  int pass = 1;

  // Register one local and one remote edge for the SAME consumer 0x50
  // Edge 0: local
  dut->adj_valid_i = 1;
  dut->adj_neighbor_id_i = 0x60;
  dut->adj_neighbor_x_i = 0;
  dut->adj_neighbor_y_i = 0;
  dut->adj_is_local_i = 1;
  dut->adj_last_i = 0;
  dut->adj_edge_idx_i = 0;
  dut->adj_current_node_id_i = 0x50;
  tick(dut);

  // Edge 1: remote
  dut->adj_valid_i = 1;
  dut->adj_neighbor_id_i = 0x61;
  dut->adj_neighbor_x_i = 1;
  dut->adj_neighbor_y_i = 2;
  dut->adj_is_local_i = 0;
  dut->adj_last_i = 1;
  dut->adj_edge_idx_i = 1;
  dut->adj_current_node_id_i = 0x50;
  tick(dut);
  dut->adj_valid_i = 0;

  // Pending=1 from remote edge, so node_ready should be 0
  tick(dut);
  if (node_ready_bit(dut, 0x50)) {
    fprintf(stderr, "\n    FAIL: node_ready_o[0x50]=1 before remote completion");
    pass = 0;
  }

  // Notify and complete remote edge
  dut->rx_notif_valid_i = 1;
  dut->rx_notif_source_node_id_i = 0x61;
  tick(dut);
  dut->rx_notif_valid_i = 0;

  int cycles = 0;
  while (!dut->tx_fetch_req_valid_o && cycles < 50) {
    tick(dut);
    cycles++;
  }
  if (pass && cycles >= 50) {
    fprintf(stderr, "\n    FAIL: fetch request never asserted");
    pass = 0;
  }
  tick(dut);

  dut->rx_fetch_resp_valid_i = 1;
  dut->rx_fetch_resp_done_valid_i = 1;
  dut->rx_fetch_resp_txn_id_i = 1;
  dut->rx_fetch_resp_consumer_node_id_i = 0x50;
  tick(dut);
  dut->rx_fetch_resp_valid_i = 0;
  dut->rx_fetch_resp_done_valid_i = 0;

  tick(dut);

  // Now both edges satisfied (local ready + remote complete) -> pending=0
  if (!node_ready_bit(dut, 0x50)) {
    fprintf(stderr, "\n    FAIL: node_ready_o[0x50]=0 after mixed completion");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_pe_fetch_subsystem_top;

  int failures = 0;
  printf("gbp_pe_fetch_subsystem functional tests:\n");
  failures += test_remote_fetch(dut);
  failures += test_local_filtered(dut);
  failures += test_backpressure(dut);
  failures += test_response_full_path(dut);
  failures += test_deduplication(dut);
  failures += test_notification_mismatch(dut);
  failures += test_scoreboard_full(dut);
  failures += test_node_ready_local(dut);
  failures += test_node_ready_remote(dut);
  failures += test_node_ready_mixed(dut);

  if (failures == 0) {
    printf("\nAll 10 tests PASSED\n");
  } else {
    printf("\n%d of 10 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
