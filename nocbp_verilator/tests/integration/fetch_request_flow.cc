// fetch_request_flow.cc
// Integration test: fetch request from PE_B consumer to PE_A producer.

#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vfetch_request_flow_top.h"

static void tick(Vfetch_request_flow_top* dut) {
  dut->clk = 0; dut->eval();
  dut->clk = 1; dut->eval();
}

static void reset_dut(Vfetch_request_flow_top* dut) {
  dut->rst_n = 0;
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

static bool test_fetch_request_arrives(Vfetch_request_flow_top* dut) {
  printf("  Test 1: Fetch request arrives at producer...");
  reset_dut(dut);
  bool pass = true;

  // Register edge on PE_B: consumer M=0x04 has remote edge to producer N=0x10 on PE_A(0,0)
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = 0x10;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = 0x04;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  // Send notification to PE_B scoreboard
  dut->pe_b_rx_notif_valid = 1;
  dut->pe_b_rx_notif_source_node_id = 0x10;
  dut->pe_b_rx_notif_is_factor = 0;
  dut->pe_b_rx_notif_source_x = 0;
  dut->pe_b_rx_notif_source_y = 0;
  tick(dut);
  dut->pe_b_rx_notif_valid = 0;

  // Wait for fetch request to arrive at PE_A pull server
  int cycles = 0;
  int max_tx_ready = 0;
  while (cycles < 200) {
    tick(dut);
    if (dut->pe_b_noc_tx_ready) max_tx_ready = 1;
    if (dut->pe_a_rx_fetch_req_valid) break;
    if ((cycles < 30) || (cycles % 20) == 0) {
      fprintf(stderr, "\n    DBG[cycle=%d]: sb_occ=%d, fetch_req=%d, tx_ready=%d, pc_state=%d, rx_fetch=%d, tx_busy=%d, pe_a_in_v=%d",
              cycles, dut->pe_b_scoreboard_occupancy, dut->pe_b_fetch_req_valid,
              dut->pe_b_noc_tx_ready, dut->pe_b_pull_client_state,
              dut->pe_a_rx_fetch_req_valid, dut->pe_b_tx_busy,
              dut->pe_a_rx_in_v);
    }
    cycles++;
  }

  // Debug: print last few signal values
  if (!dut->pe_a_rx_fetch_req_valid) {
    fprintf(stderr, "\n    DEBUG: cycles=%d, pe_b_fetch_req_valid=%d, pe_a_rx_fetch_req_valid=%d, sb_occ=%d, tx_ready_ever=%d, pc_state=%d",
            cycles, dut->pe_b_fetch_req_valid, dut->pe_a_rx_fetch_req_valid,
            dut->pe_b_scoreboard_occupancy, max_tx_ready,
            dut->pe_b_pull_client_state);
  }

  if (!dut->pe_a_rx_fetch_req_valid) {
    fprintf(stderr, "\n    FAIL: rx_fetch_req_valid never asserted");
    pass = false;
  } else {
    if (dut->pe_a_rx_fetch_req_target_node_id != 0x10) {
      fprintf(stderr, "\n    FAIL: target_node_id=%x, expected 0x10", dut->pe_a_rx_fetch_req_target_node_id);
      pass = false;
    }
    if (dut->pe_a_rx_fetch_req_consumer_node_id != 0x04) {
      fprintf(stderr, "\n    FAIL: consumer_node_id=%x, expected 0x04", dut->pe_a_rx_fetch_req_consumer_node_id);
      pass = false;
    }
    if (dut->pe_a_rx_fetch_req_src_x != 1) {
      fprintf(stderr, "\n    FAIL: src_x=%x, expected 1", dut->pe_a_rx_fetch_req_src_x);
      pass = false;
    }
    if (dut->pe_a_rx_fetch_req_src_y != 0) {
      fprintf(stderr, "\n    FAIL: src_y=%x, expected 0", dut->pe_a_rx_fetch_req_src_y);
      pass = false;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 2: Back-to-Back Fetch Requests ──
static bool test_back_to_back_fetch_requests(Vfetch_request_flow_top* dut) {
  printf("  Test 2: Back-to-back fetch requests...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t consumers[2] = {0x04, 0x05};
  const uint32_t producers[2] = {0x10, 0x11};

  // Register 2 edges
  for (int i = 0; i < 2; ++i) {
    dut->pe_b_adj_valid = 1;
    dut->pe_b_adj_neighbor_id = producers[i];
    dut->pe_b_adj_neighbor_x = 0;
    dut->pe_b_adj_neighbor_y = 0;
    dut->pe_b_adj_is_local = 0;
    dut->pe_b_adj_current_node_id = consumers[i];
    tick(dut);
  }
  dut->pe_b_adj_valid = 0;

  // Send 2 notifications back-to-back
  for (int i = 0; i < 2; ++i) {
    dut->pe_b_rx_notif_valid = 1;
    dut->pe_b_rx_notif_source_node_id = producers[i];
    dut->pe_b_rx_notif_is_factor = 0;
    dut->pe_b_rx_notif_source_x = 0;
    dut->pe_b_rx_notif_source_y = 0;
    tick(dut);
  }
  dut->pe_b_rx_notif_valid = 0;

  // Wait for both fetch requests to arrive at PE_A
  int reqs_seen = 0;
  uint32_t seen_target[2] = {0};
  uint32_t seen_consumer[2] = {0};
  int cycles = 0;
  int max_cycles = 300;

  while (cycles < max_cycles && reqs_seen < 2) {
    tick(dut);
    cycles++;
    if (dut->pe_a_rx_fetch_req_valid) {
      if (reqs_seen < 2) {
        seen_target[reqs_seen] = dut->pe_a_rx_fetch_req_target_node_id;
        seen_consumer[reqs_seen] = dut->pe_a_rx_fetch_req_consumer_node_id;
      }
      reqs_seen++;
    }
  }

  if (reqs_seen != 2) {
    fprintf(stderr, "\n    FAIL: reqs_seen=%d, expected 2", reqs_seen);
    pass = false;
  }

  for (int i = 0; i < 2 && pass; ++i) {
    int found = 0;
    for (int j = 0; j < 2; ++j) {
      if (seen_target[j] == producers[i] && seen_consumer[j] == consumers[i]) {
        found = 1;
        break;
      }
    }
    if (!found) {
      fprintf(stderr, "\n    FAIL: producer=%x consumer=%x not seen",
              producers[i], consumers[i]);
      pass = false;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 3: SPM Read Triggered by Fetch Request ──
static bool test_spm_read_triggered(Vfetch_request_flow_top* dut) {
  printf("  Test 3: SPM read triggered by fetch request...");
  reset_dut(dut);
  bool pass = true;

  // Register edge
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = 0x10;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = 0x04;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  // Send notification
  dut->pe_b_rx_notif_valid = 1;
  dut->pe_b_rx_notif_source_node_id = 0x10;
  dut->pe_b_rx_notif_is_factor = 0;
  dut->pe_b_rx_notif_source_x = 0;
  dut->pe_b_rx_notif_source_y = 0;
  tick(dut);
  dut->pe_b_rx_notif_valid = 0;

  // Wait for fetch request to arrive at PE_A, then verify SPM read
  int cycles = 0;
  int max_cycles = 300;
  int fetch_req_seen = 0;
  int spm_read_seen = 0;

  while (cycles < max_cycles) {
    tick(dut);
    cycles++;
    if (dut->pe_a_rx_fetch_req_valid) {
      fetch_req_seen = 1;
      // Pull_server should issue SPM read in S_LOOKUP state
      for (int i = 0; i < 10 && cycles < max_cycles; ++i) {
        if (dut->pe_a_spm_rd_addr != 0) {
          spm_read_seen = 1;
          if (dut->pe_a_spm_rd_addr != 0x10) {
            fprintf(stderr, "\n    FAIL: spm_rd_addr=%x, expected 0x10",
                    (uint32_t)dut->pe_a_spm_rd_addr);
            pass = false;
          }
          break;
        }
        tick(dut);
        cycles++;
      }
      break;
    }
  }

  if (!fetch_req_seen) {
    fprintf(stderr, "\n    FAIL: fetch request never arrived after %d cycles", cycles);
    pass = false;
  }
  if (!spm_read_seen) {
    fprintf(stderr, "\n    FAIL: SPM read never triggered");
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 4: Pull Client 3-Store MBX Addresses ──
static bool test_three_store_mbx_addresses(Vfetch_request_flow_top* dut) {
  printf("  Test 4: 3-store MBX addresses...");
  reset_dut(dut);
  bool pass = true;

  // Register edge
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = 0x10;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = 0x04;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  // Send notification
  dut->pe_b_rx_notif_valid = 1;
  dut->pe_b_rx_notif_source_node_id = 0x10;
  dut->pe_b_rx_notif_is_factor = 0;
  dut->pe_b_rx_notif_source_x = 0;
  dut->pe_b_rx_notif_source_y = 0;
  tick(dut);
  dut->pe_b_rx_notif_valid = 0;

  // Monitor PE_B NoC TX and capture 3-store addresses
  int cycles = 0;
  int stores_seen = 0;
  uint16_t seen_addrs[3] = {0};
  int max_cycles = 200;

  while (cycles < max_cycles && stores_seen < 3) {
    tick(dut);
    if (dut->pe_b_noc_tx_out_v) {
      if (stores_seen < 3) {
        seen_addrs[stores_seen] = (uint16_t)dut->pe_b_noc_tx_out_addr;
      }
      stores_seen++;
    }
    cycles++;
  }

  if (stores_seen != 3) {
    fprintf(stderr, "\n    FAIL: stores_seen=%d, expected 3", stores_seen);
    pass = false;
  }

  // Verify addresses: 0x1004, 0x1008, 0x100C
  const uint16_t expected_addrs[3] = {0x1004, 0x1008, 0x100C};
  for (int i = 0; i < 3 && pass; ++i) {
    if (seen_addrs[i] != expected_addrs[i]) {
      fprintf(stderr, "\n    FAIL: store[%d] addr=0x%04x, expected 0x%04x",
              i, seen_addrs[i], expected_addrs[i]);
      pass = false;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vfetch_request_flow_top;

  printf("Fetch Request Flow integration tests:\n");
  bool pass = true;
  pass &= test_fetch_request_arrives(dut);
  pass &= test_back_to_back_fetch_requests(dut);
  pass &= test_spm_read_triggered(dut);
  pass &= test_three_store_mbx_addresses(dut);

  printf("\n%s\n", pass ? "All tests PASSED" : "Some tests FAILED");

  delete dut;
  return pass ? 0 : 1;
}
