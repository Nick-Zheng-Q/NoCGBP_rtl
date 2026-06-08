// 06_multi_node_concurrent.cc
// Integration test: concurrent multi-node fetch on single PE.

#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vmulti_node_concurrent_top.h"

static void tick(Vmulti_node_concurrent_top* dut) {
  dut->clk = 0; dut->eval();
  dut->clk = 1; dut->eval();
}

static void reset_dut(Vmulti_node_concurrent_top* dut) {
  dut->rst_n = 0;
  dut->rx_notif_valid = 0;
  dut->rx_notif_source_node_id = 0;
  dut->rx_notif_is_factor = 0;
  dut->rx_notif_source_x = 0;
  dut->rx_notif_source_y = 0;
  dut->adj_valid = 0;
  dut->adj_neighbor_id = 0;
  dut->adj_neighbor_x = 0;
  dut->adj_neighbor_y = 0;
  dut->adj_is_local = 0;
  dut->adj_current_node_id = 0;
  dut->fetch_req_ready = 1;
  dut->rx_resp_valid = 0;
  dut->rx_resp_is_factor = 0;
  dut->rx_resp_state_words = 0;
  dut->rx_resp_data = 0;
  dut->rx_resp_data_valid = 0;
  dut->rx_resp_last = 0;
  dut->rx_resp_done_valid = 0;
  dut->rx_resp_txn_id = 0;
  dut->rx_resp_node_id = 0;
  dut->rx_resp_consumer_node_id = 0;
  dut->remote_ready = 1;

  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

static bool test_multi_node_concurrent(Vmulti_node_concurrent_top* dut) {
  printf("  Test 1: Multi-node concurrent fetch...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t N1 = 0x10;  // consumer node 1
  const uint32_t M1 = 0x30;  // remote neighbor for N1
  const uint32_t N2 = 0x20;  // consumer node 2
  const uint32_t M2 = 0x40;  // remote neighbor for N2

  // ---- Step 1: Register two adjacencies ----
  // Adjacency 1: N1 has remote neighbor M1 at (1,0)
  dut->adj_valid = 1;
  dut->adj_neighbor_id = M1;
  dut->adj_neighbor_x = 1;
  dut->adj_neighbor_y = 0;
  dut->adj_is_local = 0;
  dut->adj_current_node_id = N1;
  tick(dut);

  // Adjacency 2: N2 has remote neighbor M2 at (2,0)
  dut->adj_neighbor_id = M2;
  dut->adj_neighbor_x = 2;
  dut->adj_neighbor_y = 0;
  dut->adj_is_local = 0;
  dut->adj_current_node_id = N2;
  tick(dut);
  dut->adj_valid = 0;

  // ---- Step 2: Receive two notifications ----
  dut->rx_notif_valid = 1;
  dut->rx_notif_source_node_id = M1;
  dut->rx_notif_is_factor = 1;
  dut->rx_notif_source_x = 1;
  dut->rx_notif_source_y = 0;
  tick(dut);

  dut->rx_notif_source_node_id = M2;
  dut->rx_notif_source_x = 2;
  dut->rx_notif_source_y = 0;
  tick(dut);
  dut->rx_notif_valid = 0;

  // ---- Step 3: Wait for both fetches to be IN_FLIGHT ----
  int max_wait = 200;
  int cycles = 0;

  while (dut->scoreboard_occupancy < 2 && cycles < max_wait) {
    tick(dut);
    cycles++;
  }

  if (dut->scoreboard_occupancy != 2) {
    printf("FAIL (scoreboard_occupancy=%d, expected 2 after %d cycles)\n",
           dut->scoreboard_occupancy, cycles);
    return false;
  }

  // ---- Step 4: Inject responses for both requests ----
  // Edge 0: source=M1(0x30), consumer=N1(0x10), txn_id=0
  // Edge 1: source=M2(0x40), consumer=N2(0x20), txn_id=1
  dut->rx_resp_valid = 1;
  dut->rx_resp_done_valid = 1;
  dut->rx_resp_txn_id = 0;
  dut->rx_resp_node_id = M1;
  dut->rx_resp_consumer_node_id = N1;
  tick(dut);
  dut->rx_resp_valid = 0;
  dut->rx_resp_done_valid = 0;

  // Response 2: DONE for edge 1 (out of order)
  dut->rx_resp_valid = 1;
  dut->rx_resp_done_valid = 1;
  dut->rx_resp_txn_id = 1;
  dut->rx_resp_node_id = M2;
  dut->rx_resp_consumer_node_id = N2;
  tick(dut);
  dut->rx_resp_valid = 0;
  dut->rx_resp_done_valid = 0;

  // ---- Step 5: Wait for both nodes to become ready ----
  int n1_ready = 0, n2_ready = 0;
  int ready_wait = 0;
  while (ready_wait < 50) {
    tick(dut);
    ready_wait++;

    // node_ready is VlWide<32>; check word 0 (bits [31:0])
    // N1=0x10=16, N2=0x20=32 (bit 0 of word 1)
    uint32_t word0 = dut->node_ready[0];
    uint32_t word1 = dut->node_ready[1];
    n1_ready = (word0 >> 16) & 1;
    n2_ready = (word1 >> 0) & 1;

    if (n1_ready && n2_ready && dut->scoreboard_occupancy == 0) {
      break;
    }
  }

  if (!n1_ready) {
    printf("FAIL (N1 never became ready)\n");
    pass = false;
  }
  if (!n2_ready) {
    printf("FAIL (N2 never became ready)\n");
    pass = false;
  }
  if (dut->scoreboard_occupancy != 0) {
    printf("FAIL (scoreboard_occupancy=%d, expected 0)\n", dut->scoreboard_occupancy);
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 2: Response with Data Words ──
static bool test_response_with_data(Vmulti_node_concurrent_top* dut) {
  printf("  Test 2: Response with data words...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t N1 = 0x10;
  const uint32_t M1 = 0x30;
  const uint32_t WORD0 = 0xAABBCCDD;
  const uint32_t WORD1 = 0x11223344;

  // Register adjacency
  dut->adj_valid = 1;
  dut->adj_neighbor_id = M1;
  dut->adj_neighbor_x = 1;
  dut->adj_neighbor_y = 0;
  dut->adj_is_local = 0;
  dut->adj_current_node_id = N1;
  tick(dut);
  dut->adj_valid = 0;

  // Send notification
  dut->rx_notif_valid = 1;
  dut->rx_notif_source_node_id = M1;
  dut->rx_notif_is_factor = 1;
  dut->rx_notif_source_x = 1;
  dut->rx_notif_source_y = 0;
  tick(dut);
  dut->rx_notif_valid = 0;

  // Wait for scoreboard to have entry
  int cycles = 0;
  while (dut->scoreboard_occupancy < 1 && cycles < 200) {
    tick(dut);
    cycles++;
  }
  if (dut->scoreboard_occupancy != 1) {
    printf("FAIL (occupancy=%d, expected 1)\n", dut->scoreboard_occupancy);
    return false;
  }

  // Send response: metadata first, then data words, then DONE
  int remote_words = 0;
  uint32_t rd0 = 0, rd1 = 0;
  bool last_seen = false;

  // Metadata cycle
  dut->rx_resp_valid = 1;
  dut->rx_resp_is_factor = 1;
  dut->rx_resp_state_words = 2;
  dut->rx_resp_data_valid = 0;
  dut->rx_resp_done_valid = 0;
  dut->rx_resp_txn_id = 0;
  dut->rx_resp_node_id = M1;
  dut->rx_resp_consumer_node_id = N1;
  tick(dut);

  // Data word 0
  dut->rx_resp_data = WORD0;
  dut->rx_resp_data_valid = 1;
  dut->rx_resp_last = 0;
  tick(dut);
  if (dut->remote_valid) { rd0 = dut->remote_data; remote_words++; }

  // Data word 1 (last)
  dut->rx_resp_data = WORD1;
  dut->rx_resp_last = 1;
  tick(dut);
  if (dut->remote_valid) { rd1 = dut->remote_data; remote_words++; }
  if (dut->remote_last) last_seen = true;

  // DONE
  dut->rx_resp_data_valid = 0;
  dut->rx_resp_done_valid = 1;
  tick(dut);
  dut->rx_resp_valid = 0;
  dut->rx_resp_done_valid = 0;

  // Wait for node_ready
  int n1_ready = 0;
  int max_wait = 100;
  for (int i = 0; i < max_wait; ++i) {
    tick(dut);
    n1_ready = (dut->node_ready[0] >> 16) & 1;
    if (n1_ready && dut->scoreboard_occupancy == 0) break;
  }

  if (remote_words < 2) {
    fprintf(stderr, "\n    FAIL: remote_words=%d, expected >=2", remote_words);
    pass = false;
  }
  if (rd0 != WORD0) {
    fprintf(stderr, "\n    FAIL: rd0=%08x, expected %08x", rd0, WORD0);
    pass = false;
  }
  if (rd1 != WORD1) {
    fprintf(stderr, "\n    FAIL: rd1=%08x, expected %08x", rd1, WORD1);
    pass = false;
  }
  if (!last_seen) {
    fprintf(stderr, "\n    FAIL: remote_last never asserted");
    pass = false;
  }
  if (!n1_ready) {
    fprintf(stderr, "\n    FAIL: N1 never became ready");
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 3: Back-to-Back Notifications ──
static bool test_back_to_back_notifications(Vmulti_node_concurrent_top* dut) {
  printf("  Test 3: Back-to-back notifications...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t nodes[4] = {0x10, 0x11, 0x12, 0x13};
  const uint32_t neighbors[4] = {0x30, 0x31, 0x32, 0x33};

  // Register 4 adjacencies
  for (int i = 0; i < 4; ++i) {
    dut->adj_valid = 1;
    dut->adj_neighbor_id = neighbors[i];
    dut->adj_neighbor_x = 1;
    dut->adj_neighbor_y = 0;
    dut->adj_is_local = 0;
    dut->adj_current_node_id = nodes[i];
    tick(dut);
  }
  dut->adj_valid = 0;

  // Send 4 back-to-back notifications
  for (int i = 0; i < 4; ++i) {
    dut->rx_notif_valid = 1;
    dut->rx_notif_source_node_id = neighbors[i];
    dut->rx_notif_is_factor = 1;
    dut->rx_notif_source_x = 1;
    dut->rx_notif_source_y = 0;
    tick(dut);
  }
  dut->rx_notif_valid = 0;

  // Wait for all 4 fetches to be in flight
  int cycles = 0;
  while (dut->scoreboard_occupancy < 4 && cycles < 200) {
    tick(dut);
    cycles++;
  }
  if (dut->scoreboard_occupancy != 4) {
    printf("FAIL (occupancy=%d, expected 4 after %d cycles)\n",
           dut->scoreboard_occupancy, cycles);
    return false;
  }

  // Send DONE for all 4, out of order: 3,1,0,2
  int order[4] = {3, 1, 0, 2};
  for (int i = 0; i < 4; ++i) {
    int idx = order[i];
    dut->rx_resp_valid = 1;
    dut->rx_resp_done_valid = 1;
    dut->rx_resp_txn_id = idx;
    dut->rx_resp_node_id = neighbors[idx];
    dut->rx_resp_consumer_node_id = nodes[idx];
    tick(dut);
  }
  dut->rx_resp_valid = 0;
  dut->rx_resp_done_valid = 0;

  // Wait for all nodes to become ready
  int all_ready = 0;
  int wait_cycles = 0;
  while (wait_cycles < 100) {
    tick(dut);
    wait_cycles++;
    all_ready = 1;
    for (int i = 0; i < 4; ++i) {
      int word = nodes[i] >> 5;
      int bit  = nodes[i] & 0x1F;
      if (!((dut->node_ready[word] >> bit) & 1)) all_ready = 0;
    }
    if (all_ready && dut->scoreboard_occupancy == 0) break;
  }

  if (!all_ready) {
    printf("FAIL (not all nodes ready after %d cycles)\n", wait_cycles);
    pass = false;
  }
  if (dut->scoreboard_occupancy != 0) {
    printf("FAIL (occupancy=%d, expected 0)\n", dut->scoreboard_occupancy);
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vmulti_node_concurrent_top;

  printf("Multi-Node Concurrent integration tests:\n");
  bool pass = true;
  pass &= test_multi_node_concurrent(dut);
  pass &= test_response_with_data(dut);
  pass &= test_back_to_back_notifications(dut);

  printf("\n%s\n", pass ? "All tests PASSED" : "Some tests FAILED");

  delete dut;
  return pass ? 0 : 1;
}
