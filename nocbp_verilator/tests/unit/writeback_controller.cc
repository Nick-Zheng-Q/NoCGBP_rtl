// writeback_controller.cc
// Unit test for writeback_controller
// Test cases from docs/gbp_pe/verification/unit_tests/10_writeback_controller.md

#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vwriteback_controller_top.h"

static void tick(Vwriteback_controller_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void eval_fall(Vwriteback_controller_top* dut) {
  dut->clk = 0;
  dut->eval();
}

static void reset_dut(Vwriteback_controller_top* dut) {
  dut->rst_n = 0;
  dut->done_valid_i = 0;
  dut->done_node_id_i = 0;
  dut->done_is_factor_i = 0;
  dut->adj_count_i = 0;
  dut->adj_neighbor_ids_0_i = 0;
  dut->adj_neighbor_ids_1_i = 0;
  dut->adj_neighbor_ids_2_i = 0;
  dut->adj_neighbor_ids_3_i = 0;
  dut->adj_neighbor_ids_4_i = 0;
  dut->adj_neighbor_ids_5_i = 0;
  dut->adj_neighbor_ids_6_i = 0;
  dut->adj_neighbor_ids_7_i = 0;
  dut->adj_neighbor_xs_0_i = 0;
  dut->adj_neighbor_xs_1_i = 0;
  dut->adj_neighbor_xs_2_i = 0;
  dut->adj_neighbor_xs_3_i = 0;
  dut->adj_neighbor_xs_4_i = 0;
  dut->adj_neighbor_xs_5_i = 0;
  dut->adj_neighbor_xs_6_i = 0;
  dut->adj_neighbor_xs_7_i = 0;
  dut->adj_neighbor_ys_0_i = 0;
  dut->adj_neighbor_ys_1_i = 0;
  dut->adj_neighbor_ys_2_i = 0;
  dut->adj_neighbor_ys_3_i = 0;
  dut->adj_neighbor_ys_4_i = 0;
  dut->adj_neighbor_ys_5_i = 0;
  dut->adj_neighbor_ys_6_i = 0;
  dut->adj_neighbor_ys_7_i = 0;
  dut->adj_is_local_0_i = 0;
  dut->adj_is_local_1_i = 0;
  dut->adj_is_local_2_i = 0;
  dut->adj_is_local_3_i = 0;
  dut->adj_is_local_4_i = 0;
  dut->adj_is_local_5_i = 0;
  dut->adj_is_local_6_i = 0;
  dut->adj_is_local_7_i = 0;
  dut->tx_ready_i = 1;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

// ── Test Case 1: Node with 2 Remote Neighbors ──
static int test_two_remote(Vwriteback_controller_top* dut) {
  printf("  Test Case 1: 2 Remote Neighbors...");
  reset_dut(dut);
  int pass = 1;

  // Drive done and adjacency info
  dut->done_valid_i = 1;
  dut->done_node_id_i = 0x10;
  dut->done_is_factor_i = 0;
  dut->adj_count_i = 2;
  dut->adj_neighbor_ids_0_i = 0x20;
  dut->adj_neighbor_xs_0_i = 2;
  dut->adj_neighbor_ys_0_i = 1;
  dut->adj_is_local_0_i = 0;
  dut->adj_neighbor_ids_1_i = 0x30;
  dut->adj_neighbor_xs_1_i = 3;
  dut->adj_neighbor_ys_1_i = 2;
  dut->adj_is_local_1_i = 0;

  // Check reset_valid at falling edge (combinational)
  eval_fall(dut);
  if (!dut->reset_valid_o) {
    fprintf(stderr, "\n    FAIL: reset_valid=0");
    pass = 0;
  }
  if (dut->reset_node_id_o != 0x10) {
    fprintf(stderr, "\n    FAIL: reset_node_id=0x%x, expected 0x10", dut->reset_node_id_o);
    pass = 0;
  }

  // Complete tick
  dut->clk = 1;
  dut->eval();
  dut->done_valid_i = 0;

  // Collect notifications
  int notif_count = 0;
  int notif_ok[2] = {0, 0};

  for (int i = 0; i < 20; ++i) {
    eval_fall(dut);

    if (dut->tx_valid_o && dut->tx_ready_i) {
      if (notif_count < 2) {
        int ok = 1;
        if (dut->tx_source_node_id_o != 0x10) ok = 0;
        if (dut->tx_is_factor_o != 0) ok = 0;
        if (notif_count == 0) {
          if (dut->tx_target_x_o != 2 || dut->tx_target_y_o != 1) ok = 0;
        } else {
          if (dut->tx_target_x_o != 3 || dut->tx_target_y_o != 2) ok = 0;
        }
        notif_ok[notif_count] = ok;
      }
      notif_count++;
    }

    if (dut->wb_done_o) break;

    dut->clk = 1;
    dut->eval();
  }

  if (notif_count != 2) {
    fprintf(stderr, "\n    FAIL: notif_count=%d, expected 2", notif_count);
    pass = 0;
  }
  for (int i = 0; i < 2; ++i) {
    if (!notif_ok[i]) {
      fprintf(stderr, "\n    FAIL: notification %d mismatch", i);
      pass = 0;
    }
  }

  // Check wb_done
  eval_fall(dut);
  if (!dut->wb_done_o) {
    fprintf(stderr, "\n    FAIL: wb_done=0");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Mixed Local and Remote ──
static int test_mixed(Vwriteback_controller_top* dut) {
  printf("  Test Case 2: Mixed Local and Remote...");
  reset_dut(dut);
  int pass = 1;

  // 1 local, 1 remote
  dut->done_valid_i = 1;
  dut->done_node_id_i = 0x10;
  dut->done_is_factor_i = 0;
  dut->adj_count_i = 2;
  dut->adj_neighbor_ids_0_i = 0x20;
  dut->adj_neighbor_xs_0_i = 1;
  dut->adj_neighbor_ys_0_i = 0;
  dut->adj_is_local_0_i = 1;  // local
  dut->adj_neighbor_ids_1_i = 0x30;
  dut->adj_neighbor_xs_1_i = 3;
  dut->adj_neighbor_ys_1_i = 2;
  dut->adj_is_local_1_i = 0;  // remote
  tick(dut);
  dut->done_valid_i = 0;

  // Collect notifications (should be only 1 - the remote neighbor)
  int notif_count = 0;
  for (int i = 0; i < 20; ++i) {
    eval_fall(dut);
    if (dut->tx_valid_o && dut->tx_ready_i) {
      if (notif_count == 0) {
        if (dut->tx_target_x_o != 3 || dut->tx_target_y_o != 2) {
          fprintf(stderr, "\n    FAIL: target=(%d,%d), expected (3,2)",
                  dut->tx_target_x_o, dut->tx_target_y_o);
          pass = 0;
        }
      }
      notif_count++;
    }
    if (dut->wb_done_o) break;
    dut->clk = 1;
    dut->eval();
  }

  if (notif_count != 1) {
    fprintf(stderr, "\n    FAIL: notif_count=%d, expected 1 (local skipped)", notif_count);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: Backpressure ──
static int test_backpressure(Vwriteback_controller_top* dut) {
  printf("  Test Case 3: Backpressure...");
  reset_dut(dut);
  int pass = 1;

  dut->tx_ready_i = 0;
  dut->done_valid_i = 1;
  dut->done_node_id_i = 0x10;
  dut->done_is_factor_i = 0;
  dut->adj_count_i = 1;
  dut->adj_neighbor_ids_0_i = 0x20;
  dut->adj_neighbor_xs_0_i = 2;
  dut->adj_neighbor_ys_0_i = 1;
  dut->adj_is_local_0_i = 0;
  tick(dut);
  dut->done_valid_i = 0;

  // Tick with backpressure
  for (int i = 0; i < 5; ++i) {
    eval_fall(dut);
    if (!dut->tx_valid_o) {
      fprintf(stderr, "\n    FAIL: tx_valid=0 during backpressure");
      pass = 0;
    }
    dut->clk = 1;
    dut->eval();
  }

  // Release
  dut->tx_ready_i = 1;
  tick(dut);

  // Should complete
  for (int i = 0; i < 10; ++i) {
    eval_fall(dut);
    if (dut->wb_done_o) break;
    dut->clk = 1;
    dut->eval();
  }

  eval_fall(dut);
  if (!dut->wb_done_o) {
    fprintf(stderr, "\n    FAIL: wb_done=0 after backpressure released");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: All 8 Local Neighbors ──
static int test_all_local(Vwriteback_controller_top* dut) {
  printf("  Test Case 4: All 8 Local Neighbors...");
  reset_dut(dut);
  int pass = 1;

  dut->done_valid_i = 1;
  dut->done_node_id_i = 0x10;
  dut->done_is_factor_i = 0;
  dut->adj_count_i = 8;

  dut->adj_neighbor_ids_0_i = 0x20; dut->adj_neighbor_xs_0_i = 1; dut->adj_neighbor_ys_0_i = 0; dut->adj_is_local_0_i = 1;
  dut->adj_neighbor_ids_1_i = 0x21; dut->adj_neighbor_xs_1_i = 1; dut->adj_neighbor_ys_1_i = 0; dut->adj_is_local_1_i = 1;
  dut->adj_neighbor_ids_2_i = 0x22; dut->adj_neighbor_xs_2_i = 1; dut->adj_neighbor_ys_2_i = 0; dut->adj_is_local_2_i = 1;
  dut->adj_neighbor_ids_3_i = 0x23; dut->adj_neighbor_xs_3_i = 1; dut->adj_neighbor_ys_3_i = 0; dut->adj_is_local_3_i = 1;
  dut->adj_neighbor_ids_4_i = 0x24; dut->adj_neighbor_xs_4_i = 1; dut->adj_neighbor_ys_4_i = 0; dut->adj_is_local_4_i = 1;
  dut->adj_neighbor_ids_5_i = 0x25; dut->adj_neighbor_xs_5_i = 1; dut->adj_neighbor_ys_5_i = 0; dut->adj_is_local_5_i = 1;
  dut->adj_neighbor_ids_6_i = 0x26; dut->adj_neighbor_xs_6_i = 1; dut->adj_neighbor_ys_6_i = 0; dut->adj_is_local_6_i = 1;
  dut->adj_neighbor_ids_7_i = 0x27; dut->adj_neighbor_xs_7_i = 1; dut->adj_neighbor_ys_7_i = 0; dut->adj_is_local_7_i = 1;

  eval_fall(dut);
  if (!dut->reset_valid_o) {
    fprintf(stderr, "\n    FAIL: reset_valid=0");
    pass = 0;
  }
  if (dut->reset_node_id_o != 0x10) {
    fprintf(stderr, "\n    FAIL: reset_node_id=0x%x, expected 0x10", dut->reset_node_id_o);
    pass = 0;
  }

  dut->clk = 1;
  dut->eval();
  dut->done_valid_i = 0;

  int remote_notif_count = 0;
  int done_seen = 0;
  for (int i = 0; i < 20; ++i) {
    eval_fall(dut);
    if (dut->tx_valid_o && dut->tx_ready_i) {
      // Local entries should be skipped (target == self coordinates).
      if (dut->tx_target_x_o != 1 || dut->tx_target_y_o != 0) {
        fprintf(stderr, "\n    FAIL: remote notification seen for local neighbor (target=%d,%d)",
                dut->tx_target_x_o, dut->tx_target_y_o);
        pass = 0;
        remote_notif_count++;
      }
    }
    if (dut->wb_done_o) {
      done_seen = 1;
      break;
    }
    dut->clk = 1;
    dut->eval();
  }

  if (remote_notif_count != 0) {
    fprintf(stderr, "\n    FAIL: remote_notif_count=%d, expected 0 (all local)", remote_notif_count);
    pass = 0;
  }
  if (!done_seen) {
    fprintf(stderr, "\n    FAIL: wb_done never asserted");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: All 8 Remote Neighbors ──
static int test_all_remote(Vwriteback_controller_top* dut) {
  printf("  Test Case 5: All 8 Remote Neighbors...");
  reset_dut(dut);
  int pass = 1;

  dut->done_valid_i = 1;
  dut->done_node_id_i = 0x11;
  dut->done_is_factor_i = 1;
  dut->adj_count_i = 8;

  dut->adj_neighbor_ids_0_i = 0x30; dut->adj_neighbor_xs_0_i = 2; dut->adj_neighbor_ys_0_i = 0; dut->adj_is_local_0_i = 0;
  dut->adj_neighbor_ids_1_i = 0x31; dut->adj_neighbor_xs_1_i = 3; dut->adj_neighbor_ys_1_i = 1; dut->adj_is_local_1_i = 0;
  dut->adj_neighbor_ids_2_i = 0x32; dut->adj_neighbor_xs_2_i = 4; dut->adj_neighbor_ys_2_i = 0; dut->adj_is_local_2_i = 0;
  dut->adj_neighbor_ids_3_i = 0x33; dut->adj_neighbor_xs_3_i = 5; dut->adj_neighbor_ys_3_i = 1; dut->adj_is_local_3_i = 0;
  dut->adj_neighbor_ids_4_i = 0x34; dut->adj_neighbor_xs_4_i = 6; dut->adj_neighbor_ys_4_i = 0; dut->adj_is_local_4_i = 0;
  dut->adj_neighbor_ids_5_i = 0x35; dut->adj_neighbor_xs_5_i = 7; dut->adj_neighbor_ys_5_i = 1; dut->adj_is_local_5_i = 0;
  dut->adj_neighbor_ids_6_i = 0x36; dut->adj_neighbor_xs_6_i = 8; dut->adj_neighbor_ys_6_i = 0; dut->adj_is_local_6_i = 0;
  dut->adj_neighbor_ids_7_i = 0x37; dut->adj_neighbor_xs_7_i = 9; dut->adj_neighbor_ys_7_i = 1; dut->adj_is_local_7_i = 0;

  tick(dut);
  dut->done_valid_i = 0;

  int remote_notif_count = 0;
  int done_seen = 0;
  for (int i = 0; i < 20; ++i) {
    eval_fall(dut);
    if (dut->tx_valid_o && dut->tx_ready_i) {
      int ok = 1;
      if (dut->tx_source_node_id_o != 0x11) ok = 0;
      if (dut->tx_is_factor_o != 1) ok = 0;
      if (dut->tx_target_x_o == 1 && dut->tx_target_y_o == 0) ok = 0;  // should not target self
      if (remote_notif_count < 8) {
        int exp_x = 2 + remote_notif_count;
        int exp_y = (remote_notif_count % 2);
        if (dut->tx_target_x_o != exp_x || dut->tx_target_y_o != exp_y) ok = 0;
        int exp_id = 0x30 + remote_notif_count;
        if (dut->tx_target_node_id_o != exp_id) ok = 0;
      }
      if (!ok) {
        fprintf(stderr, "\n    FAIL: notification %d mismatch", remote_notif_count);
        pass = 0;
      }
      remote_notif_count++;
    }
    if (dut->wb_done_o) {
      done_seen = 1;
      break;
    }
    dut->clk = 1;
    dut->eval();
  }

  if (remote_notif_count != 8) {
    fprintf(stderr, "\n    FAIL: remote_notif_count=%d, expected 8", remote_notif_count);
    pass = 0;
  }
  if (!done_seen) {
    fprintf(stderr, "\n    FAIL: wb_done never asserted");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 6: Max adj_count with Mixed Local/Remote ──
static int test_max_mixed(Vwriteback_controller_top* dut) {
  printf("  Test Case 6: Max adj_count Mixed Local/Remote...");
  reset_dut(dut);
  int pass = 1;

  dut->done_valid_i = 1;
  dut->done_node_id_i = 0x12;
  dut->done_is_factor_i = 0;
  dut->adj_count_i = 8;

  // Pattern: local, remote, local, remote, ...
  dut->adj_neighbor_ids_0_i = 0x40; dut->adj_neighbor_xs_0_i = 1; dut->adj_neighbor_ys_0_i = 0; dut->adj_is_local_0_i = 1;
  dut->adj_neighbor_ids_1_i = 0x41; dut->adj_neighbor_xs_1_i = 2; dut->adj_neighbor_ys_1_i = 1; dut->adj_is_local_1_i = 0;
  dut->adj_neighbor_ids_2_i = 0x42; dut->adj_neighbor_xs_2_i = 1; dut->adj_neighbor_ys_2_i = 0; dut->adj_is_local_2_i = 1;
  dut->adj_neighbor_ids_3_i = 0x43; dut->adj_neighbor_xs_3_i = 3; dut->adj_neighbor_ys_3_i = 1; dut->adj_is_local_3_i = 0;
  dut->adj_neighbor_ids_4_i = 0x44; dut->adj_neighbor_xs_4_i = 1; dut->adj_neighbor_ys_4_i = 0; dut->adj_is_local_4_i = 1;
  dut->adj_neighbor_ids_5_i = 0x45; dut->adj_neighbor_xs_5_i = 4; dut->adj_neighbor_ys_5_i = 1; dut->adj_is_local_5_i = 0;
  dut->adj_neighbor_ids_6_i = 0x46; dut->adj_neighbor_xs_6_i = 1; dut->adj_neighbor_ys_6_i = 0; dut->adj_is_local_6_i = 1;
  dut->adj_neighbor_ids_7_i = 0x47; dut->adj_neighbor_xs_7_i = 5; dut->adj_neighbor_ys_7_i = 1; dut->adj_is_local_7_i = 0;

  tick(dut);
  dut->done_valid_i = 0;

  int remote_notif_count = 0;
  int done_seen = 0;
  for (int i = 0; i < 20; ++i) {
    eval_fall(dut);
    if (dut->tx_valid_o && dut->tx_ready_i) {
      // Local skips target self; only count real remote notifications.
      if (dut->tx_target_x_o == 1 && dut->tx_target_y_o == 0) {
        // local skip cycle
      } else {
        int exp_x = 2 + remote_notif_count;
        int ok = 1;
        if (dut->tx_source_node_id_o != 0x12) ok = 0;
        if (dut->tx_target_x_o != exp_x || dut->tx_target_y_o != 1) ok = 0;
        if (dut->tx_target_node_id_o != (0x41 + 2 * remote_notif_count)) ok = 0;
        if (!ok) {
          fprintf(stderr, "\n    FAIL: remote notification %d mismatch", remote_notif_count);
          pass = 0;
        }
        remote_notif_count++;
      }
    }
    if (dut->wb_done_o) {
      done_seen = 1;
      break;
    }
    dut->clk = 1;
    dut->eval();
  }

  if (remote_notif_count != 4) {
    fprintf(stderr, "\n    FAIL: remote_notif_count=%d, expected 4", remote_notif_count);
    pass = 0;
  }
  if (!done_seen) {
    fprintf(stderr, "\n    FAIL: wb_done never asserted");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 7: Reset During Notification ──
static int test_reset_during_notification(Vwriteback_controller_top* dut) {
  printf("  Test Case 7: Reset During Notification...");
  reset_dut(dut);
  int pass = 1;

  dut->done_valid_i = 1;
  dut->done_node_id_i = 0x20;
  dut->done_is_factor_i = 0;
  dut->adj_count_i = 3;
  dut->adj_neighbor_ids_0_i = 0x50; dut->adj_neighbor_xs_0_i = 2; dut->adj_neighbor_ys_0_i = 1; dut->adj_is_local_0_i = 0;
  dut->adj_neighbor_ids_1_i = 0x51; dut->adj_neighbor_xs_1_i = 3; dut->adj_neighbor_ys_1_i = 1; dut->adj_is_local_1_i = 0;
  dut->adj_neighbor_ids_2_i = 0x52; dut->adj_neighbor_xs_2_i = 4; dut->adj_neighbor_ys_2_i = 1; dut->adj_is_local_2_i = 0;

  tick(dut);
  dut->done_valid_i = 0;

  // Wait for first notification to be sent
  int first_seen = 0;
  for (int i = 0; i < 10; ++i) {
    eval_fall(dut);
    if (dut->tx_valid_o && dut->tx_ready_i) {
      first_seen = 1;
      break;
    }
    if (dut->wb_done_o) break;
    dut->clk = 1;
    dut->eval();
  }
  if (!first_seen) {
    fprintf(stderr, "\n    FAIL: first notification not seen");
    pass = 0;
  }

  // Assert reset mid-transaction
  dut->rst_n = 0;
  for (int i = 0; i < 3; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);

  eval_fall(dut);
  if (dut->tx_valid_o) {
    fprintf(stderr, "\n    FAIL: tx_valid=1 after reset");
    pass = 0;
  }
  if (dut->wb_done_o) {
    fprintf(stderr, "\n    FAIL: wb_done=1 after reset");
    pass = 0;
  }

  // Start a new transaction to verify clean recovery
  dut->done_valid_i = 1;
  dut->done_node_id_i = 0x21;
  dut->done_is_factor_i = 1;
  dut->adj_count_i = 1;
  dut->adj_neighbor_ids_0_i = 0x55; dut->adj_neighbor_xs_0_i = 5; dut->adj_neighbor_ys_0_i = 1; dut->adj_is_local_0_i = 0;
  // Clear higher entries
  dut->adj_neighbor_ids_1_i = 0; dut->adj_is_local_1_i = 0;
  dut->adj_neighbor_ids_2_i = 0; dut->adj_is_local_2_i = 0;
  dut->adj_neighbor_ids_3_i = 0; dut->adj_is_local_3_i = 0;
  dut->adj_neighbor_ids_4_i = 0; dut->adj_is_local_4_i = 0;
  dut->adj_neighbor_ids_5_i = 0; dut->adj_is_local_5_i = 0;
  dut->adj_neighbor_ids_6_i = 0; dut->adj_is_local_6_i = 0;
  dut->adj_neighbor_ids_7_i = 0; dut->adj_is_local_7_i = 0;

  eval_fall(dut);
  if (!dut->reset_valid_o || dut->reset_node_id_o != 0x21) {
    fprintf(stderr, "\n    FAIL: reset_valid/node_id mismatch after recovery");
    pass = 0;
  }

  tick(dut);
  dut->done_valid_i = 0;

  int notif_count = 0;
  int done_seen = 0;
  for (int i = 0; i < 10; ++i) {
    eval_fall(dut);
    if (dut->tx_valid_o && dut->tx_ready_i) {
      if (dut->tx_target_x_o != 5 || dut->tx_target_y_o != 1 ||
          dut->tx_target_node_id_o != 0x55) {
        fprintf(stderr, "\n    FAIL: post-reset notification mismatch");
        pass = 0;
      }
      notif_count++;
    }
    if (dut->wb_done_o) {
      done_seen = 1;
      break;
    }
    dut->clk = 1;
    dut->eval();
  }

  if (notif_count != 1) {
    fprintf(stderr, "\n    FAIL: post-reset notif_count=%d, expected 1", notif_count);
    pass = 0;
  }
  if (!done_seen) {
    fprintf(stderr, "\n    FAIL: post-reset wb_done never asserted");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 8: Back-to-Back Completions ──
static int test_back_to_back(Vwriteback_controller_top* dut) {
  printf("  Test Case 8: Back-to-Back Completions...");
  reset_dut(dut);
  int pass = 1;

  // First completion: 1 remote neighbor
  dut->done_valid_i = 1;
  dut->done_node_id_i = 0x10;
  dut->done_is_factor_i = 0;
  dut->adj_count_i = 1;
  dut->adj_neighbor_ids_0_i = 0x60; dut->adj_neighbor_xs_0_i = 2; dut->adj_neighbor_ys_0_i = 1; dut->adj_is_local_0_i = 0;

  tick(dut);
  dut->done_valid_i = 0;

  int n1 = 0;
  int done1 = 0;
  for (int i = 0; i < 10; ++i) {
    eval_fall(dut);
    if (dut->tx_valid_o && dut->tx_ready_i) {
      if (dut->tx_source_node_id_o != 0x10 || dut->tx_target_x_o != 2 || dut->tx_target_y_o != 1) {
        fprintf(stderr, "\n    FAIL: first completion notification mismatch");
        pass = 0;
      }
      n1++;
    }
    if (dut->wb_done_o) {
      done1 = 1;
      break;
    }
    dut->clk = 1;
    dut->eval();
  }
  if (n1 != 1) {
    fprintf(stderr, "\n    FAIL: first notif_count=%d, expected 1", n1);
    pass = 0;
  }
  if (!done1) {
    fprintf(stderr, "\n    FAIL: first wb_done never asserted");
    pass = 0;
  }

  // Move to IDLE cycle (state is DONE at falling edge; let one rising edge pass)
  dut->clk = 1;
  dut->eval();

  // Second completion: 2 remote neighbors
  dut->done_valid_i = 1;
  dut->done_node_id_i = 0x11;
  dut->done_is_factor_i = 1;
  dut->adj_count_i = 2;
  dut->adj_neighbor_ids_0_i = 0x70; dut->adj_neighbor_xs_0_i = 3; dut->adj_neighbor_ys_0_i = 0; dut->adj_is_local_0_i = 0;
  dut->adj_neighbor_ids_1_i = 0x71; dut->adj_neighbor_xs_1_i = 4; dut->adj_neighbor_ys_1_i = 1; dut->adj_is_local_1_i = 0;

  tick(dut);
  dut->done_valid_i = 0;

  int n2 = 0;
  int done2 = 0;
  for (int i = 0; i < 10; ++i) {
    eval_fall(dut);
    if (dut->tx_valid_o && dut->tx_ready_i) {
      int ok = 1;
      if (dut->tx_source_node_id_o != 0x11) ok = 0;
      if (n2 == 0) {
        if (dut->tx_target_x_o != 3 || dut->tx_target_y_o != 0 || dut->tx_target_node_id_o != 0x70) ok = 0;
      } else {
        if (dut->tx_target_x_o != 4 || dut->tx_target_y_o != 1 || dut->tx_target_node_id_o != 0x71) ok = 0;
      }
      if (!ok) {
        fprintf(stderr, "\n    FAIL: second completion notification %d mismatch", n2);
        pass = 0;
      }
      n2++;
    }
    if (dut->wb_done_o) {
      done2 = 1;
      break;
    }
    dut->clk = 1;
    dut->eval();
  }
  if (n2 != 2) {
    fprintf(stderr, "\n    FAIL: second notif_count=%d, expected 2", n2);
    pass = 0;
  }
  if (!done2) {
    fprintf(stderr, "\n    FAIL: second wb_done never asserted");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vwriteback_controller_top;

  int failures = 0;

  printf("writeback_controller unit tests (from 10_writeback_controller.md):\n");
  failures += test_two_remote(dut);
  failures += test_mixed(dut);
  failures += test_backpressure(dut);
  failures += test_all_local(dut);
  failures += test_all_remote(dut);
  failures += test_max_mixed(dut);
  failures += test_reset_during_notification(dut);
  failures += test_back_to_back(dut);

  if (failures == 0) {
    printf("\nAll 8 tests PASSED\n");
  } else {
    printf("\n%d of 8 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
