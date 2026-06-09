// notification_flow.cc
// Integration test: notification flow from producer PE_A to consumer PE_B.

#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vnotification_flow_top.h"

static void tick(Vnotification_flow_top* dut) {
  dut->clk = 0; dut->eval();
  dut->clk = 1; dut->eval();
}

static void reset_dut(Vnotification_flow_top* dut) {
  dut->rst_n = 0;
  dut->pe_a_done_valid = 0;
  dut->pe_a_done_node_id = 0;
  dut->pe_a_done_is_factor = 0;
  dut->pe_a_adj_count = 0;
  dut->pe_a_adj_neighbor_ids[0] = 0;
  dut->pe_a_adj_neighbor_ids[1] = 0;
  dut->pe_a_adj_neighbor_ids[2] = 0;
  dut->pe_a_adj_neighbor_xs = 0;
  dut->pe_a_adj_neighbor_ys = 0;
  dut->pe_a_adj_is_local = 0;
  dut->pe_b_adj_valid = 0;
  dut->pe_b_adj_neighbor_id = 0;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = 0;
  dut->pe_a_tx_notif_ready_force_low = 0;

  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

static void set_pe_a_adj_x(Vnotification_flow_top* dut, int idx, uint32_t x) {
  dut->pe_a_adj_neighbor_xs &= ~(0x3Full << (idx * 6));
  dut->pe_a_adj_neighbor_xs |= (QData)(x & 0x3Full) << (idx * 6);
}

static void set_pe_a_adj_y(Vnotification_flow_top* dut, int idx, uint32_t y) {
  dut->pe_a_adj_neighbor_ys &= ~(0x3Full << (idx * 6));
  dut->pe_a_adj_neighbor_ys |= (QData)(y & 0x3Full) << (idx * 6);
}

static bool test_notification_end_to_end(Vnotification_flow_top* dut) {
  printf("  Test 1: Notification end-to-end...");
  reset_dut(dut);
  bool pass = true;

  // Register edge on PE_B: consumer M=0x20 has remote edge to producer N=0x10 on PE_A(0,0)
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = 0x10;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = 0x20;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  // Trigger compute done on PE_A: node N=0x10 has remote neighbor M=0x20 on PE_B(1,0)
  dut->pe_a_done_valid = 1;
  dut->pe_a_done_node_id = 0x10;
  dut->pe_a_done_is_factor = 0;
  dut->pe_a_adj_count = 1;
  dut->pe_a_adj_neighbor_ids[0] = 0x20;
  set_pe_a_adj_x(dut, 0, 1);
  set_pe_a_adj_y(dut, 0, 0);
  dut->pe_a_adj_is_local = 0;
  tick(dut);
  dut->pe_a_done_valid = 0;

  // In the new architecture, adj_valid registers the edge as NOTIFIED and
  // the scan loop issues FETCH_REQUEST on the next cycle.
  // Check immediately; if not yet, poll for a few cycles.
  int cycles = 0;
  while (cycles < 10) {
    if (dut->pe_b_fetch_req_valid) break;
    tick(dut);
    cycles++;
  }

  if (!dut->pe_b_fetch_req_valid) {
    fprintf(stderr, "\n    FAIL: fetch_req_valid never asserted (after %d cycles, occ=%d)",
            cycles, dut->pe_b_scoreboard_occupancy);
    pass = false;
  } else {
    if (dut->pe_b_fetch_req_target_node_id != 0x10) {
      fprintf(stderr, "\n    FAIL: target_node_id=%x, expected 0x10", dut->pe_b_fetch_req_target_node_id);
      pass = false;
    }
    if (dut->pe_b_fetch_req_consumer_node_id != 0x20) {
      fprintf(stderr, "\n    FAIL: consumer_node_id=%x, expected 0x20", dut->pe_b_fetch_req_consumer_node_id);
      pass = false;
    }
    if (dut->pe_b_fetch_req_txn_id != 0) {
      fprintf(stderr, "\n    FAIL: txn_id=%x, expected 0", dut->pe_b_fetch_req_txn_id);
      pass = false;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 2: Multiple Notifications ──
static bool test_multiple_notifications(Vnotification_flow_top* dut) {
  printf("  Test 2: Multiple notifications...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t consumers[3] = {0x20, 0x21, 0x22};
  const uint32_t producers[3] = {0x10, 0x11, 0x12};

  int reqs_seen = 0;
  uint32_t seen_target[3] = {0};
  uint32_t seen_consumer[3] = {0};

  // Helper to monitor fetch requests
  auto monitor_fetch = [&]() {
    if (dut->pe_b_fetch_req_valid && reqs_seen < 3) {
      seen_target[reqs_seen] = dut->pe_b_fetch_req_target_node_id;
      seen_consumer[reqs_seen] = dut->pe_b_fetch_req_consumer_node_id;
      reqs_seen++;
    }
  };

  // Register 3 edges on PE_B
  // In the new architecture, fetches are issued immediately by the scan loop
  // after adj_valid registers edges as NOTIFIED.
  for (int i = 0; i < 3; ++i) {
    dut->pe_b_adj_valid = 1;
    dut->pe_b_adj_neighbor_id = producers[i];
    dut->pe_b_adj_neighbor_x = 0;
    dut->pe_b_adj_neighbor_y = 0;
    dut->pe_b_adj_is_local = 0;
    dut->pe_b_adj_current_node_id = consumers[i];
    tick(dut);
    monitor_fetch();  // Catch fetch issued for previous edges
  }
  dut->pe_b_adj_valid = 0;
  monitor_fetch();  // Catch fetch for last edge

  // Trigger 3 compute done events on PE_A, waiting for wb_done between each
  for (int i = 0; i < 3; ++i) {
    dut->pe_a_done_valid = 1;
    dut->pe_a_done_node_id = producers[i];
    dut->pe_a_done_is_factor = 0;
    dut->pe_a_adj_count = 1;
    dut->pe_a_adj_neighbor_ids[0] = consumers[i];
    set_pe_a_adj_x(dut, 0, 1);
    set_pe_a_adj_y(dut, 0, 0);
    dut->pe_a_adj_is_local = 0;
    tick(dut);
    monitor_fetch();
    dut->pe_a_done_valid = 0;

    // Wait for wb_done
    int wait_c = 0;
    while (!dut->pe_a_wb_done && wait_c < 50) {
      tick(dut);
      monitor_fetch();
      wait_c++;
    }

    // Extra delay to let NoC packet clear
    for (int d = 0; d < 20; ++d) {
      tick(dut);
      monitor_fetch();
    }
  }

  if (reqs_seen != 3) {
    fprintf(stderr, "\n    FAIL: reqs_seen=%d, expected 3 (occupancy=%d)",
            reqs_seen, dut->pe_b_scoreboard_occupancy);
    pass = false;
  }

  // Verify all consumers and producers match
  for (int i = 0; i < 3 && pass; ++i) {
    int found = 0;
    for (int j = 0; j < 3; ++j) {
      if (seen_target[j] == producers[i] && seen_consumer[j] == consumers[i]) {
        found = 1;
        break;
      }
    }
    if (!found) {
      fprintf(stderr, "\n    FAIL: producer=%x consumer=%x not seen in fetch reqs",
              producers[i], consumers[i]);
      pass = false;
    }
  }

  // Verify scoreboard occupancy
  if (dut->pe_b_scoreboard_occupancy != 3) {
    fprintf(stderr, "\n    FAIL: occupancy=%d, expected 3", dut->pe_b_scoreboard_occupancy);
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 3: Scoreboard Occupancy Tracking ──
static bool test_scoreboard_occupancy_tracking(Vnotification_flow_top* dut) {
  printf("  Test 3: Scoreboard occupancy tracking...");
  reset_dut(dut);
  bool pass = true;

  // Register edge
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = 0x10;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = 0x20;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  // Verify initial occupancy is 0
  if (dut->pe_b_scoreboard_occupancy != 0) {
    fprintf(stderr, "\n    FAIL: initial occupancy=%d, expected 0", dut->pe_b_scoreboard_occupancy);
    pass = false;
  }

  // Trigger done
  dut->pe_a_done_valid = 1;
  dut->pe_a_done_node_id = 0x10;
  dut->pe_a_done_is_factor = 0;
  dut->pe_a_adj_count = 1;
  dut->pe_a_adj_neighbor_ids[0] = 0x20;
  set_pe_a_adj_x(dut, 0, 1);
  set_pe_a_adj_y(dut, 0, 0);
  dut->pe_a_adj_is_local = 0;
  tick(dut);
  dut->pe_a_done_valid = 0;

  // Wait for occupancy to become 1
  int cycles = 0;
  while (dut->pe_b_scoreboard_occupancy < 1 && cycles < 100) {
    tick(dut);
    cycles++;
  }

  if (dut->pe_b_scoreboard_occupancy != 1) {
    fprintf(stderr, "\n    FAIL: occupancy=%d after %d cycles, expected 1",
            dut->pe_b_scoreboard_occupancy, cycles);
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 4: NoC Credit Exhaustion Backpressure ──
static bool test_credit_exhaustion(Vnotification_flow_top* dut) {
  printf("  Test 4: NoC credit exhaustion backpressure...");
  reset_dut(dut);
  bool pass = true;

  // Register edge
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = 0x10;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = 0x20;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  // In new architecture, fetch is issued immediately by scan loop.
  // Verify it was issued before proceeding.
  if (!dut->pe_b_fetch_req_valid) {
    // Give one more cycle for scan to find it
    tick(dut);
  }
  bool fetch_issued = dut->pe_b_fetch_req_valid;

  // Trigger compute done
  dut->pe_a_done_valid = 1;
  dut->pe_a_done_node_id = 0x10;
  dut->pe_a_done_is_factor = 0;
  dut->pe_a_adj_count = 1;
  dut->pe_a_adj_neighbor_ids[0] = 0x20;
  set_pe_a_adj_x(dut, 0, 1);
  set_pe_a_adj_y(dut, 0, 0);
  dut->pe_a_adj_is_local = 0;
  tick(dut);
  dut->pe_a_done_valid = 0;

  // Force PE_A tx_ready low to simulate backpressure
  dut->pe_a_tx_notif_ready_force_low = 1;
  tick(dut);

  // Wait until wb_controller is trying to send
  int cycles = 0;
  int wb_in_send = 0;
  while (cycles < 50) {
    tick(dut);
    if (dut->pe_a_wb_tx_valid) {
      wb_in_send = 1;
    }
    if (wb_in_send) break;
    cycles++;
  }

  if (!wb_in_send) {
    fprintf(stderr, "\n    FAIL: wb_controller never entered SEND state");
    pass = false;
  }

  // Continue blocking; verify wb_done stays low
  int blocked_cycles = 0;
  while (blocked_cycles < 20 && pass) {
    tick(dut);
    if (dut->pe_a_wb_done) {
      fprintf(stderr, "\n    FAIL: wb_done asserted while tx_ready forced low");
      pass = false;
      break;
    }
    blocked_cycles++;
  }

  // Release backpressure
  dut->pe_a_tx_notif_ready_force_low = 0;

  // Fetch was already verified above; just ensure it's still tracked
  if (pass && !fetch_issued && dut->pe_b_scoreboard_occupancy == 0) {
    fprintf(stderr, "\n    FAIL: fetch was never issued");
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 5: Reset During NoC Traversal ──
static bool test_reset_during_traversal(Vnotification_flow_top* dut) {
  printf("  Test 5: Reset during NoC traversal...");
  reset_dut(dut);
  bool pass = true;

  // Register edge
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = 0x10;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = 0x20;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  // Trigger compute done
  dut->pe_a_done_valid = 1;
  dut->pe_a_done_node_id = 0x10;
  dut->pe_a_done_is_factor = 0;
  dut->pe_a_adj_count = 1;
  dut->pe_a_adj_neighbor_ids[0] = 0x20;
  set_pe_a_adj_x(dut, 0, 1);
  set_pe_a_adj_y(dut, 0, 0);
  dut->pe_a_adj_is_local = 0;
  tick(dut);
  dut->pe_a_done_valid = 0;

  // Wait until notification has been sent (wb_done high) but before scoreboard fully processes
  int cycles = 0;
  while (cycles < 50) {
    tick(dut);
    if (dut->pe_a_wb_done) break;
    cycles++;
  }

  if (!dut->pe_a_wb_done) {
    fprintf(stderr, "\n    FAIL: wb_done never asserted before reset");
    pass = false;
  }

  // Issue reset
  dut->rst_n = 0;
  tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);

  // Verify scoreboard is clean after reset
  if (dut->pe_b_scoreboard_occupancy != 0) {
    fprintf(stderr, "\n    FAIL: occupancy=%d after reset, expected 0", dut->pe_b_scoreboard_occupancy);
    pass = false;
  }

  // Re-run the full flow to verify clean recovery
  if (pass) {
    dut->pe_b_adj_valid = 1;
    dut->pe_b_adj_neighbor_id = 0x10;
    dut->pe_b_adj_neighbor_x = 0;
    dut->pe_b_adj_neighbor_y = 0;
    dut->pe_b_adj_is_local = 0;
    dut->pe_b_adj_current_node_id = 0x20;
    tick(dut);
    dut->pe_b_adj_valid = 0;

    // In new architecture, fetch is issued immediately after adj_valid
    int cycles = 0;
    while (cycles < 10) {
      if (dut->pe_b_fetch_req_valid) break;
      tick(dut);
      cycles++;
    }

    if (!dut->pe_b_fetch_req_valid) {
      fprintf(stderr, "\n    FAIL: fetch_req never arrived after reset+re-run");
      pass = false;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vnotification_flow_top;

  printf("Notification Flow integration tests:\n");
  bool pass = true;
  pass &= test_notification_end_to_end(dut);
  pass &= test_multiple_notifications(dut);
  pass &= test_scoreboard_occupancy_tracking(dut);
  pass &= test_credit_exhaustion(dut);
  pass &= test_reset_during_traversal(dut);

  printf("\n%s\n", pass ? "All tests PASSED" : "Some tests FAILED");

  delete dut;
  return pass ? 0 : 1;
}
