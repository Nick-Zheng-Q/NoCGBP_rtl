// scoreboard_prefetcher.cc
// Unit test for scoreboard_prefetcher
// Test cases from docs/gbp_pe/verification/unit_tests/04_scoreboard_prefetcher.md
//
// NOTE: Updated for v2/v3 RTL where remote edges are auto-NOTIFIED on
// registration (adj_valid_i). rx_notif is only needed for race-case edges
// that were still IDLE when adj_valid arrived.

#include <cstdio>
#include <cstdlib>
#include "verilated.h"
#include "Vscoreboard_prefetcher_top.h"

static void tick(Vscoreboard_prefetcher_top* d) { d->clk=0; d->eval(); d->clk=1; d->eval(); }
static void eval_fall(Vscoreboard_prefetcher_top* d) { d->clk=0; d->eval(); }

static void reset_dut(Vscoreboard_prefetcher_top* d) {
  d->rst_n=0; d->rx_notif_valid_i=0; d->rx_notif_source_node_id_i=0;
  d->rx_notif_is_factor_i=0; d->rx_notif_source_x_i=0; d->rx_notif_source_y_i=0;
  d->fetch_req_ready_i=1; d->complete_valid_i=0; d->complete_txn_id_i=0;
  d->complete_node_id_i=0; d->complete_consumer_node_id_i=0;
  d->staging_reserve_ready_i=1; d->staging_batch_closed_i=0;
  d->adj_valid_i=0; d->adj_neighbor_id_i=0; d->adj_neighbor_x_i=0;
  d->adj_neighbor_y_i=0; d->adj_is_local_i=0; d->adj_last_i=0;
  d->adj_edge_idx_i=0; d->adj_current_node_id_i=0;
  d->reset_valid_i=0; d->reset_node_id_i=0; d->reset_is_factor_i=0;
  for(int i=0;i<5;i++) tick(d); d->rst_n=1; for(int i=0;i<3;i++) tick(d);
}

static void reg_edge(Vscoreboard_prefetcher_top* d, uint16_t cur, uint16_t nbr,
                      uint8_t nx, uint8_t ny, int local, int idx, int last) {
  int old_ready = d->fetch_req_ready_i;
  d->fetch_req_ready_i = 0;  // prevent premature fetch issue during registration
  d->adj_valid_i=1; d->adj_current_node_id_i=cur; d->adj_neighbor_id_i=nbr;
  d->adj_neighbor_x_i=nx; d->adj_neighbor_y_i=ny; d->adj_is_local_i=local;
  d->adj_edge_idx_i=idx; d->adj_last_i=last; tick(d);
  d->adj_valid_i=0; tick(d);
  d->fetch_req_ready_i = old_ready;
}

static void send_notif(Vscoreboard_prefetcher_top* d, uint16_t src, uint8_t sx, uint8_t sy) {
  d->rx_notif_valid_i=1; d->rx_notif_source_node_id_i=src;
  d->rx_notif_source_x_i=sx; d->rx_notif_source_y_i=sy;
  d->fetch_req_ready_i=0; tick(d); d->rx_notif_valid_i=0;
  for(int i=0;i<5;i++) tick(d); d->fetch_req_ready_i=1;
}

static int consume_fetch(Vscoreboard_prefetcher_top* d) {
  d->fetch_req_ready_i=1;
  for(int i=0;i<10;i++) {
    eval_fall(d);
    if(d->fetch_req_valid_o) { int t=d->fetch_req_txn_id_o; tick(d); for(int j=0;j<3;j++) tick(d); d->fetch_req_ready_i=0; return t; }
    tick(d);
  }
  d->fetch_req_ready_i=0; return -1;
}

static void complete_fetch(Vscoreboard_prefetcher_top* d, int txn, uint16_t consumer_node) {
  d->complete_valid_i=1; d->complete_txn_id_i=txn; d->complete_consumer_node_id_i=consumer_node; tick(d);
  d->complete_valid_i=0; d->complete_consumer_node_id_i=0; for(int i=0;i<3;i++) tick(d);
}

#define READY(n) ((d->node_ready_o >> (n)) & 1)

static int test_single_edge(Vscoreboard_prefetcher_top* d) {
  printf("  Test Case 1: Single Edge Lifecycle...");
  reset_dut(d); int pass=1;
  reg_edge(d, 20, 0x10, 2, 1, 0, 0, 1);
  send_notif(d, 0x10, 2, 1);
  int txn=consume_fetch(d);
  if(txn<0) { fprintf(stderr,"\n    FAIL: no fetch"); pass=0; }
  eval_fall(d);
  if(d->scoreboard_occupancy_o!=1) { fprintf(stderr,"\n    FAIL: occ=%d exp 1",d->scoreboard_occupancy_o); pass=0; }
  complete_fetch(d, txn, 20);
  eval_fall(d);
  if(d->scoreboard_occupancy_o!=0) { fprintf(stderr,"\n    FAIL: occ=%d exp 0",d->scoreboard_occupancy_o); pass=0; }
  printf("%s\n",pass?"PASS":"FAIL"); return pass?0:1;
}

static int test_node_readiness(Vscoreboard_prefetcher_top* d) {
  printf("  Test Case 2: Multiple Edges, Node Readiness...");
  reset_dut(d); int pass=1;
  reg_edge(d, 20, 0x10, 2, 1, 0, 0, 0);
  reg_edge(d, 20, 0x11, 3, 2, 0, 1, 1);
  eval_fall(d);
  if(READY(20)) { fprintf(stderr,"\n    FAIL: node_ready[20]=1 before notif"); pass=0; }
  // In v2/v3, remote edges are auto-NOTIFIED on registration.
  // consume_fetch advances all pending edges to IN_FLIGHT.
  int t0=consume_fetch(d); if(t0<0) { fprintf(stderr,"\n    FAIL: no fetch e0"); pass=0; }
  // Both edges (0 and 1) are now IN_FLIGHT. Complete edge 0 first.
  complete_fetch(d, 0, 20);
  eval_fall(d);
  if(READY(20)) { fprintf(stderr,"\n    FAIL: node_ready[20]=1 with e1 pending"); pass=0; }
  // Complete edge 1.
  complete_fetch(d, 1, 20);
  eval_fall(d);
  if(!READY(20)) { fprintf(stderr,"\n    FAIL: node_ready[20]=0 after both complete"); pass=0; }
  printf("%s\n",pass?"PASS":"FAIL"); return pass?0:1;
}

static int test_local_edge(Vscoreboard_prefetcher_top* d) {
  printf("  Test Case 3: Local Edge Immediate Ready...");
  reset_dut(d); int pass=1;
  reg_edge(d, 20, 0x10, 1, 0, 1, 0, 0);
  reg_edge(d, 20, 0x11, 3, 2, 0, 1, 1);
  eval_fall(d);
  if(READY(20)) { fprintf(stderr,"\n    FAIL: node_ready[20]=1 with remote IDLE"); pass=0; }
  send_notif(d, 0x11, 3, 2);
  int t=consume_fetch(d); if(t>=0) complete_fetch(d, t, 20);
  eval_fall(d);
  if(!READY(20)) { fprintf(stderr,"\n    FAIL: node_ready[20]=0 after both ready"); pass=0; }
  printf("%s\n",pass?"PASS":"FAIL"); return pass?0:1;
}

static int test_duplicate_notification(Vscoreboard_prefetcher_top* d) {
  printf("  Test Case 4: Duplicate Notification...");
  reset_dut(d); int pass = 1;
  reg_edge(d, 30, 0x20, 2, 1, 0, 0, 1);
  // First notification (no-op in v2/v3; edge already NOTIFIED)
  send_notif(d, 0x20, 2, 1);
  int t0 = consume_fetch(d);
  if (t0 < 0) { fprintf(stderr, "\n    FAIL: no fetch on first notif"); pass = 0; }
  // Duplicate notification for same edge
  send_notif(d, 0x20, 2, 1);
  // Should not issue another fetch because edge is already IN_FLIGHT
  d->fetch_req_ready_i = 1;
  for (int i = 0; i < 10; i++) {
    eval_fall(d);
    if (d->fetch_req_valid_o) {
      fprintf(stderr, "\n    FAIL: duplicate notif issued extra fetch"); pass = 0; break;
    }
    tick(d);
  }
  complete_fetch(d, t0, 30);
  eval_fall(d);
  if (d->scoreboard_occupancy_o != 0) {
    fprintf(stderr, "\n    FAIL: occ=%d exp 0", d->scoreboard_occupancy_o); pass = 0;
  }
  if (!READY(30)) { fprintf(stderr, "\n    FAIL: node_ready[30]=0 after complete"); pass = 0; }
  printf("%s\n", pass ? "PASS" : "FAIL"); return pass ? 0 : 1;
}

static int test_out_of_order_complete(Vscoreboard_prefetcher_top* d) {
  printf("  Test Case 5: Out-of-Order Completion...");
  reset_dut(d); int pass = 1;
  // Two edges for node 40
  reg_edge(d, 40, 0x30, 1, 0, 0, 0, 0);
  reg_edge(d, 40, 0x31, 2, 0, 0, 1, 1);
  // In v2/v3, remote edges are auto-NOTIFIED on registration.
  // consume_fetch advances all pending edges to IN_FLIGHT.
  int t0 = consume_fetch(d);
  if (t0 < 0) { fprintf(stderr, "\n    FAIL: no fetch"); pass = 0; }
  // Both edges (0 and 1) are now IN_FLIGHT. Complete out of order: t1 first, then t0
  complete_fetch(d, 1, 40);
  eval_fall(d);
  if (READY(40)) { fprintf(stderr, "\n    FAIL: node_ready[40]=1 after only t1 done"); pass = 0; }
  complete_fetch(d, 0, 40);
  eval_fall(d);
  if (!READY(40)) { fprintf(stderr, "\n    FAIL: node_ready[40]=0 after both done"); pass = 0; }
  printf("%s\n", pass ? "PASS" : "FAIL"); return pass ? 0 : 1;
}

static int test_reset_inflight(Vscoreboard_prefetcher_top* d) {
  printf("  Test Case 6: Reset During In-Flight...");
  reset_dut(d); int pass = 1;
  reg_edge(d, 50, 0x40, 3, 1, 0, 0, 1);
  send_notif(d, 0x40, 3, 1);
  int t = consume_fetch(d);
  if (t < 0) { fprintf(stderr, "\n    FAIL: no fetch"); pass = 0; }
  eval_fall(d);
  if (d->scoreboard_occupancy_o != 1) {
    fprintf(stderr, "\n    FAIL: occ=%d exp 1", d->scoreboard_occupancy_o); pass = 0;
  }
  // Reset the node
  d->reset_valid_i = 1; d->reset_node_id_i = 50; d->reset_is_factor_i = 0;
  tick(d); d->reset_valid_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
  eval_fall(d);
  if (d->scoreboard_occupancy_o != 0) {
    fprintf(stderr, "\n    FAIL: occ=%d exp 0 after reset", d->scoreboard_occupancy_o); pass = 0;
  }
  if (READY(50)) { fprintf(stderr, "\n    FAIL: node_ready[50]=1 after reset"); pass = 0; }
  printf("%s\n", pass ? "PASS" : "FAIL"); return pass ? 0 : 1;
}

static int test_scoreboard_full_signal(Vscoreboard_prefetcher_top* d) {
  printf("  Test Case 7: Scoreboard Full Signal...");
  reset_dut(d); int pass = 1;
  // Register and consume many edges to drive occupancy up
  for (int i = 0; i < 4; i++) {
    reg_edge(d, 60 + i, 0x50 + i, 1, 0, 0, 0, 1);
    send_notif(d, 0x50 + i, 1, 0);
    int t = consume_fetch(d);
    if (t < 0) { fprintf(stderr, "\n    FAIL: no fetch for edge %d", i); pass = 0; }
  }
  eval_fall(d);
  if (d->scoreboard_full_o) {
    fprintf(stderr, "\n    FAIL: full=1 with occ=%d", d->scoreboard_occupancy_o); pass = 0;
  }
  // Complete all
  for (int i = 0; i < 4; i++) complete_fetch(d, i, 60 + i);
  eval_fall(d);
  if (d->scoreboard_occupancy_o != 0) {
    fprintf(stderr, "\n    FAIL: occ=%d exp 0", d->scoreboard_occupancy_o); pass = 0;
  }
  printf("%s\n", pass ? "PASS" : "FAIL"); return pass ? 0 : 1;
}

static int test_scoreboard_full_blocks(Vscoreboard_prefetcher_top* d) {
  printf("  Test Case 8: Scoreboard Full Blocks New Entries...");
  reset_dut(d);
  int pass = 1;
  d->fetch_req_ready_i = 1;
  int txn[64];

  // Fill scoreboard with 64 in-flight fetches
  for (int i = 0; i < 64; i++) {
    reg_edge(d, 100 + i, 0x200 + i, 1, 0, 0, 0, 1);
    // In v2/v3, edges are auto-NOTIFIED on registration.
    // fetch_req_ready_i is restored to 1 by reg_edge, so the fetch is
    // pending immediately. No rx_notif needed.

    int got = 0;
    for (int c = 0; c < 10; c++) {
      eval_fall(d);
      if (d->fetch_req_valid_o) {
        txn[i] = d->fetch_req_txn_id_o;
        tick(d);  // block 3 runs: edge -> IN_FLIGHT
        got = 1;
        break;
      }
      tick(d);
    }
    if (!got) {
      fprintf(stderr, "\n    FAIL: no fetch issued for edge %d", i);
      pass = 0;
    }
  }

  eval_fall(d);
  if (d->scoreboard_occupancy_o != 64) {
    fprintf(stderr, "\n    FAIL: occ=%d exp 64", d->scoreboard_occupancy_o);
    pass = 0;
  }
  if (!d->scoreboard_full_o) {
    fprintf(stderr, "\n    FAIL: full=0 when occ=64");
    pass = 0;
  }
  if (d->adj_ready_o) {
    fprintf(stderr, "\n    FAIL: adj_ready=1 when full");
    pass = 0;
  }

  // Attempt to register a new edge while full - should be rejected
  d->adj_valid_i = 1;
  d->adj_current_node_id_i = 100;
  d->adj_neighbor_id_i = 0x300;
  tick(d);
  d->adj_valid_i = 0;
  eval_fall(d);
  if (d->scoreboard_occupancy_o != 64) {
    fprintf(stderr, "\n    FAIL: occ changed while full (occ=%d)", d->scoreboard_occupancy_o);
    pass = 0;
  }
  if (d->adj_ready_o) {
    fprintf(stderr, "\n    FAIL: adj_ready=1 after register attempt while full");
    pass = 0;
  }

  // Free one slot
  complete_fetch(d, txn[0], 100);
  eval_fall(d);
  if (d->scoreboard_occupancy_o != 63) {
    fprintf(stderr, "\n    FAIL: occ=%d exp 63 after complete", d->scoreboard_occupancy_o);
    pass = 0;
  }
  if (d->scoreboard_full_o) {
    fprintf(stderr, "\n    FAIL: full=1 when occ=63");
    pass = 0;
  }
  if (!d->adj_ready_o) {
    fprintf(stderr, "\n    FAIL: adj_ready=0 after slot freed");
    pass = 0;
  }

  // New edge should now be accepted and fetch issued
  // Use a new node (200) because node 100 is already registered and
  // node_registered_r blocks duplicate registration in v2/v3.
  reg_edge(d, 200, 0x300, 2, 1, 0, 0, 1);
  // In v2/v3, edge is auto-NOTIFIED. No rx_notif needed.

  int got = -1;
  for (int c = 0; c < 10; c++) {
    eval_fall(d);
    if (d->fetch_req_valid_o) {
      got = d->fetch_req_txn_id_o;
      tick(d);
      break;
    }
    tick(d);
  }
  if (got < 0) {
    fprintf(stderr, "\n    FAIL: no fetch after freeing slot");
    pass = 0;
  }

  eval_fall(d);
  if (d->scoreboard_occupancy_o != 64) {
    fprintf(stderr, "\n    FAIL: occ=%d exp 64 after refill", d->scoreboard_occupancy_o);
    pass = 0;
  }
  if (!d->scoreboard_full_o) {
    fprintf(stderr, "\n    FAIL: full=0 after refill");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* d = new Vscoreboard_prefetcher_top;
  int f = 0;
  printf("scoreboard_prefetcher unit tests:\n");
  f += test_single_edge(d);
  f += test_node_readiness(d);
  f += test_local_edge(d);
  f += test_duplicate_notification(d);
  f += test_out_of_order_complete(d);
  f += test_reset_inflight(d);
  f += test_scoreboard_full_signal(d);
  f += test_scoreboard_full_blocks(d);
  printf(f ? "\n%d of 8 tests FAILED\n" : "\nAll 8 tests PASSED\n", f);
  delete d; return f ? 1 : 0;
}
