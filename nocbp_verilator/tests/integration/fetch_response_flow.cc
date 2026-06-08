// fetch_response_flow.cc
// Integration test: fetch response from PE_A producer to PE_B consumer.

#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vfetch_response_flow_top.h"

static void tick(Vfetch_response_flow_top* dut) {
  dut->clk = 0; dut->eval();
  dut->clk = 1; dut->eval();
}

static void reset_dut(Vfetch_response_flow_top* dut) {
  dut->rst_n = 0;
  dut->pe_a_req_valid = 0;
  dut->pe_a_req_target_node_id = 0;
  dut->pe_a_req_consumer_node_id = 0;
  dut->pe_a_req_is_factor = 0;
  dut->pe_a_req_src_x = 0;
  dut->pe_a_req_src_y = 0;
  dut->pe_a_req_txn_id = 0;
  dut->pe_a_spm_ready = 1;
  dut->pe_a_spm_data = 0;
  dut->pe_b_adj_valid = 0;
  dut->pe_b_adj_neighbor_id = 0;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = 0;
  dut->pe_b_rx_notif_valid = 0;
  dut->pe_b_rx_notif_source_node_id = 0;
  dut->pe_b_rx_notif_is_factor = 0;
  dut->pe_b_rx_notif_source_x = 0;
  dut->pe_b_rx_notif_source_y = 0;

  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

// Helper: construct NodeHeader beat for pull_server
// Layout: node_id[9:0], dof[13:10], adj_count[17:14], adj_base[35:18],
//         state_base[53:36], state_words[59:54]
static uint64_t make_header(uint32_t state_base, uint32_t state_words) {
  uint64_t beat = 0;
  beat |= (uint64_t)(state_words & 0x3F) << 54;
  beat |= (uint64_t)(state_base & 0x3FFFF) << 36;
  return beat;
}

static bool test_fetch_response_flow(Vfetch_response_flow_top* dut) {
  printf("  Test 1: Fetch response end-to-end...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t NODE_N = 0x10;
  const uint32_t NODE_M = 0x20;
  const uint32_t TXN_ID = 0;
  const uint32_t STATE_BASE = 0x100;
  const uint32_t STATE_WORDS = 2;
  const uint32_t WORD0 = 0xDEADBEEF;
  const uint32_t WORD1 = 0xCAFEBABE;

  // --- Phase 1: Create pending entry on PE_B scoreboard ---
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = NODE_N;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = NODE_M;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  dut->pe_b_rx_notif_valid = 1;
  dut->pe_b_rx_notif_source_node_id = NODE_N;
  dut->pe_b_rx_notif_is_factor = 0;
  dut->pe_b_rx_notif_source_x = 0;
  dut->pe_b_rx_notif_source_y = 0;
  tick(dut);
  dut->pe_b_rx_notif_valid = 0;

  // Wait for scoreboard to have pending entry
  int cycles = 0;
  while (cycles < 50) {
    tick(dut);
    if (dut->pe_b_scoreboard_occupancy == 1) break;
    cycles++;
  }
  if (dut->pe_b_scoreboard_occupancy != 1) {
    fprintf(stderr, "\n    FAIL: scoreboard occupancy=%d, expected 1",
            dut->pe_b_scoreboard_occupancy);
    pass = false;
  }

  // --- Phase 2: Inject fetch request into PE_A pull_server ---
  dut->pe_a_req_valid = 1;
  dut->pe_a_req_target_node_id = NODE_N;
  dut->pe_a_req_consumer_node_id = NODE_M;
  dut->pe_a_req_is_factor = 0;
  dut->pe_a_req_src_x = 1;
  dut->pe_a_req_src_y = 0;
  dut->pe_a_req_txn_id = TXN_ID;
  tick(dut);
  dut->pe_a_req_valid = 0;

  // --- Phase 3: PE_A reads SPM and sends response ---
  // Track what pull_server reads and return appropriate data
  int max_cycles = 200;
  int remote_words = 0;
  uint32_t remote_data0 = 0, remote_data1 = 0;
  bool complete_seen = false;

  while (cycles < max_cycles) {
    // Fake SPM: return data based on read address
    uint32_t rd_addr = (uint32_t)dut->pe_a_spm_rd_addr;
    if (rd_addr == NODE_N) {
      dut->pe_a_spm_data = make_header(STATE_BASE, STATE_WORDS);
    } else if (rd_addr == STATE_BASE) {
      dut->pe_a_spm_data = WORD0;
    } else if (rd_addr == STATE_BASE + 1) {
      dut->pe_a_spm_data = WORD1;
    } else {
      dut->pe_a_spm_data = 0;
    }

    tick(dut);
    cycles++;

    // Capture remote data from PE_B response_collector
    if (dut->pe_b_remote_valid) {
      if (remote_words == 0) remote_data0 = dut->pe_b_remote_data;
      else if (remote_words == 1) remote_data1 = dut->pe_b_remote_data;
      remote_words++;
    }

    if (dut->pe_b_complete_valid) {
      complete_seen = true;
      if (dut->pe_b_complete_txn_id != TXN_ID) {
        fprintf(stderr, "\n    FAIL: complete_txn_id=%x, expected %x",
                dut->pe_b_complete_txn_id, TXN_ID);
        pass = false;
      }
      if (dut->pe_b_complete_node_id != NODE_N) {
        fprintf(stderr, "\n    FAIL: complete_node_id=%x, expected %x",
                dut->pe_b_complete_node_id, NODE_N);
        pass = false;
      }
      if (dut->pe_b_complete_consumer_node_id != NODE_M) {
        fprintf(stderr, "\n    FAIL: complete_consumer_node_id=%x, expected %x",
                dut->pe_b_complete_consumer_node_id, NODE_M);
        pass = false;
      }
    }

    if (complete_seen && dut->pe_b_scoreboard_occupancy == 0) break;
  }

  // --- Phase 4: Check results ---
  if (remote_words < 2) {
    fprintf(stderr, "\n    FAIL: remote_words=%d, expected >=2", remote_words);
    pass = false;
  }
  if (remote_data0 != WORD0) {
    fprintf(stderr, "\n    FAIL: remote_data0=%08x, expected %08x", remote_data0, WORD0);
    pass = false;
  }
  if (remote_data1 != WORD1) {
    fprintf(stderr, "\n    FAIL: remote_data1=%08x, expected %08x", remote_data1, WORD1);
    pass = false;
  }
  if (!complete_seen) {
    fprintf(stderr, "\n    FAIL: complete_valid never asserted");
    pass = false;
  }
  if (dut->pe_b_scoreboard_occupancy != 0) {
    fprintf(stderr, "\n    FAIL: scoreboard occupancy=%d, expected 0",
            dut->pe_b_scoreboard_occupancy);
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 2: Single Data Word Response ──
static bool test_single_word_response(Vfetch_response_flow_top* dut) {
  printf("  Test 2: Single data word response...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t NODE_N = 0x10;
  const uint32_t NODE_M = 0x20;
  const uint32_t TXN_ID = 0;
  const uint32_t STATE_BASE = 0x100;
  const uint32_t STATE_WORDS = 1;
  const uint32_t WORD0 = 0xAABBCCDD;

  // Register adjacency and send notification
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = NODE_N;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = NODE_M;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  dut->pe_b_rx_notif_valid = 1;
  dut->pe_b_rx_notif_source_node_id = NODE_N;
  dut->pe_b_rx_notif_is_factor = 0;
  dut->pe_b_rx_notif_source_x = 0;
  dut->pe_b_rx_notif_source_y = 0;
  tick(dut);
  dut->pe_b_rx_notif_valid = 0;

  // Wait for scoreboard
  int cycles = 0;
  while (dut->pe_b_scoreboard_occupancy < 1 && cycles < 50) {
    tick(dut);
    cycles++;
  }
  if (dut->pe_b_scoreboard_occupancy != 1) {
    fprintf(stderr, "\n    FAIL: occupancy=%d, expected 1", dut->pe_b_scoreboard_occupancy);
    pass = false;
  }

  // Inject fetch request
  dut->pe_a_req_valid = 1;
  dut->pe_a_req_target_node_id = NODE_N;
  dut->pe_a_req_consumer_node_id = NODE_M;
  dut->pe_a_req_is_factor = 0;
  dut->pe_a_req_src_x = 1;
  dut->pe_a_req_src_y = 0;
  dut->pe_a_req_txn_id = TXN_ID;
  tick(dut);
  dut->pe_a_req_valid = 0;

  // Wait for response
  int remote_words = 0;
  uint32_t remote_data0 = 0;
  bool complete_seen = false;
  bool last_seen = false;
  int max_cycles = 200;

  while (cycles < max_cycles) {
    uint32_t rd_addr = (uint32_t)dut->pe_a_spm_rd_addr;
    if (rd_addr == NODE_N) {
      dut->pe_a_spm_data = make_header(STATE_BASE, STATE_WORDS);
    } else if (rd_addr == STATE_BASE) {
      dut->pe_a_spm_data = WORD0;
    } else {
      dut->pe_a_spm_data = 0;
    }

    tick(dut);
    cycles++;

    if (dut->pe_b_remote_valid) {
      remote_data0 = dut->pe_b_remote_data;
      remote_words++;
    }
    if (dut->pe_b_remote_last) last_seen = true;
    if (dut->pe_b_complete_valid) complete_seen = true;

    if (complete_seen && dut->pe_b_scoreboard_occupancy == 0) break;
  }

  if (remote_words < 1) {
    fprintf(stderr, "\n    FAIL: remote_words=%d, expected >=1", remote_words);
    pass = false;
  }
  if (remote_data0 != WORD0) {
    fprintf(stderr, "\n    FAIL: remote_data0=%08x, expected %08x", remote_data0, WORD0);
    pass = false;
  }
  if (!last_seen) {
    fprintf(stderr, "\n    FAIL: remote_last never asserted");
    pass = false;
  }
  if (!complete_seen) {
    fprintf(stderr, "\n    FAIL: complete never seen");
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 3: Zero Data Words Response ──
static bool test_zero_word_response(Vfetch_response_flow_top* dut) {
  printf("  Test 3: Zero data words response...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t NODE_N = 0x10;
  const uint32_t NODE_M = 0x20;
  const uint32_t TXN_ID = 0;
  const uint32_t STATE_BASE = 0x100;
  const uint32_t STATE_WORDS = 0;

  // Register adjacency and send notification
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = NODE_N;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = NODE_M;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  dut->pe_b_rx_notif_valid = 1;
  dut->pe_b_rx_notif_source_node_id = NODE_N;
  dut->pe_b_rx_notif_is_factor = 0;
  dut->pe_b_rx_notif_source_x = 0;
  dut->pe_b_rx_notif_source_y = 0;
  tick(dut);
  dut->pe_b_rx_notif_valid = 0;

  // Wait for scoreboard
  int cycles = 0;
  while (dut->pe_b_scoreboard_occupancy < 1 && cycles < 50) {
    tick(dut);
    cycles++;
  }
  if (dut->pe_b_scoreboard_occupancy != 1) {
    fprintf(stderr, "\n    FAIL: occupancy=%d, expected 1", dut->pe_b_scoreboard_occupancy);
    pass = false;
  }

  // Inject fetch request
  dut->pe_a_req_valid = 1;
  dut->pe_a_req_target_node_id = NODE_N;
  dut->pe_a_req_consumer_node_id = NODE_M;
  dut->pe_a_req_is_factor = 0;
  dut->pe_a_req_src_x = 1;
  dut->pe_a_req_src_y = 0;
  dut->pe_a_req_txn_id = TXN_ID;
  tick(dut);
  dut->pe_a_req_valid = 0;

  // Wait for completion
  bool complete_seen = false;
  int max_cycles = 200;

  while (cycles < max_cycles) {
    uint32_t rd_addr = (uint32_t)dut->pe_a_spm_rd_addr;
    if (rd_addr == NODE_N) {
      dut->pe_a_spm_data = make_header(STATE_BASE, STATE_WORDS);
    } else {
      dut->pe_a_spm_data = 0;
    }

    tick(dut);
    cycles++;

    if (dut->pe_b_complete_valid) complete_seen = true;
    if (complete_seen && dut->pe_b_scoreboard_occupancy == 0) break;
  }

  if (!complete_seen) {
    fprintf(stderr, "\n    FAIL: complete never seen");
    pass = false;
  }
  if (dut->pe_b_scoreboard_occupancy != 0) {
    fprintf(stderr, "\n    FAIL: occupancy=%d, expected 0", dut->pe_b_scoreboard_occupancy);
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vfetch_response_flow_top;

  printf("Fetch Response Flow integration tests:\n");
  bool pass = true;
  pass &= test_fetch_response_flow(dut);
  pass &= test_single_word_response(dut);
  pass &= test_zero_word_response(dut);

  printf("\n%s\n", pass ? "All tests PASSED" : "Some tests FAILED");

  delete dut;
  return pass ? 0 : 1;
}
