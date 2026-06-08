// 04_full_pull_cycle.cc
// Integration test: complete pull cycle end-to-end.

#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vfull_pull_cycle_top.h"

static void tick(Vfull_pull_cycle_top* dut) {
  dut->clk = 0; dut->eval();
  dut->clk = 1; dut->eval();
}

static void reset_dut(Vfull_pull_cycle_top* dut) {
  dut->rst_n = 0;
  dut->pe_a_done_valid = 0;
  dut->pe_a_done_node_id = 0;
  dut->pe_a_done_is_factor = 0;
  dut->pe_a_adj_count = 0;
  // VlWide<3> for 80-bit adj_neighbor_ids
  dut->pe_a_adj_neighbor_ids[0] = 0;
  dut->pe_a_adj_neighbor_ids[1] = 0;
  dut->pe_a_adj_neighbor_ids[2] = 0;
  // VL_IN64 for 48-bit adj_neighbor_xs
  dut->pe_a_adj_neighbor_xs = 0;
  // VL_IN64 for 40-bit adj_neighbor_ys
  dut->pe_a_adj_neighbor_ys = 0;
  dut->pe_a_adj_is_local = 0;
  dut->pe_a_spm_ready = 0;
  dut->pe_a_spm_data = 0;
  dut->pe_b_adj_valid = 0;
  dut->pe_b_adj_neighbor_id = 0;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = 0;

  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

static bool test_full_pull_cycle(Vfull_pull_cycle_top* dut) {
  printf("  Test 1: Full pull cycle end-to-end...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t NODE_N = 0x10;  // completed node on PE_A
  const uint32_t NODE_M = 0x20;  // consumer node on PE_B

  // ---- Step 1: Register adjacency on PE_B (M -> N at (0,0)) ----
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = NODE_N;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;  // remote
  dut->pe_b_adj_current_node_id = NODE_M;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  // ---- Step 2: Provide adjacency info to PE_A (N -> M at (1,0)) ----
  dut->pe_a_adj_count = 1;
  dut->pe_a_adj_neighbor_ids[0] = NODE_M;  // entry 0 in bits [9:0]
  dut->pe_a_adj_neighbor_ids[1] = 0;
  dut->pe_a_adj_neighbor_ids[2] = 0;
  dut->pe_a_adj_neighbor_xs = 1ULL;  // target x=1 (PE_B)
  dut->pe_a_adj_neighbor_ys = 0ULL;
  dut->pe_a_adj_is_local = 0;  // remote

  // ---- Step 3: Pulse done_valid on PE_A ----
  dut->pe_a_done_valid = 1;
  dut->pe_a_done_node_id = NODE_N;
  dut->pe_a_done_is_factor = 0;
  tick(dut);
  dut->pe_a_done_valid = 0;

  // ---- Step 4-5: Wait for notification -> fetch request -> fetch response ----
  int scoreboard_nonempty_cycle = -1;
  int fetch_sent_cycle = -1;
  int complete_seen_cycle = -1;
  int spm_ready_set = 0;
  int cycles = 0;
  int max_cycles = 5000;

  while (cycles < max_cycles) {
    tick(dut);
    cycles++;

    // Monitor scoreboard occupancy
    if (dut->pe_b_scoreboard_occupancy > 0 && scoreboard_nonempty_cycle < 0) {
      scoreboard_nonempty_cycle = cycles;
    }

    // Monitor fetch request sent from PE_B
    if (dut->pe_b_tx_busy && fetch_sent_cycle < 0) {
      fetch_sent_cycle = cycles;
    }

    // Monitor completion on PE_B
    if (dut->pe_b_complete_valid && complete_seen_cycle < 0) {
      complete_seen_cycle = cycles;
    }

    // Check if node_ready bit set for M
    // pe_b_node_ready is VlWide<32> (1024 bits), word 1 holds bits [63:32]
    // NODE_M = 0x20 = 32, so it's bit 0 of word 1
    uint32_t node_ready_word1 = dut->pe_b_node_ready[1];
    int node_m_ready = (node_ready_word1 >> 0) & 1;
    if (node_m_ready && complete_seen_cycle > 0) {
      if (dut->pe_b_scoreboard_occupancy == 0) {
        break;
      }
    }

    // Provide SPM data when PE_A requests it
    uint32_t rd_addr = (uint32_t)dut->pe_a_spm_rd_addr;
    if (rd_addr != 0 && !spm_ready_set) {
      dut->pe_a_spm_ready = 1;
      // NodeHeader layout:
      // node_id[9:0], dof[13:10], adj_count[17:14], adj_base[35:18],
      // state_base[53:36], state_words[59:54]
      uint64_t header = 0;
      header |= ((uint64_t)NODE_N & 0x3FF) << 0;
      header |= ((uint64_t)4 & 0xF) << 10;          // dof=4
      header |= ((uint64_t)1 & 0xF) << 14;          // adj_count=1
      header |= ((uint64_t)0 & 0x3FFFF) << 18;      // adj_base
      header |= ((uint64_t)0x100 & 0x3FFFF) << 36;  // state_base
      header |= ((uint64_t)8 & 0x3F) << 54;         // state_words=8
      dut->pe_a_spm_data = header;
      spm_ready_set = 1;
    } else if (spm_ready_set && dut->pe_a_spm_ready) {
      dut->pe_a_spm_data = 0xDEADBEEF00000000ULL | (uint64_t)(rd_addr & 0xFFFF);
    } else {
      dut->pe_a_spm_ready = 0;
    }
  }

  if (cycles >= max_cycles) {
    printf("FAIL (timeout at cycle %d)\n", cycles);
    printf("    scoreboard_nonempty=%d, fetch_sent=%d, complete=%d\n",
           scoreboard_nonempty_cycle, fetch_sent_cycle, complete_seen_cycle);
    printf("    scoreboard_occupancy=%d\n", dut->pe_b_scoreboard_occupancy);
    pass = false;
  }

  // Verify all milestones were reached
  if (scoreboard_nonempty_cycle < 0) {
    fprintf(stderr, "\n    FAIL: scoreboard never became nonempty");
    pass = false;
  }
  if (fetch_sent_cycle < 0) {
    fprintf(stderr, "\n    FAIL: fetch request never sent");
    pass = false;
  }
  if (complete_seen_cycle < 0) {
    fprintf(stderr, "\n    FAIL: completion never seen");
    pass = false;
  }

  printf("%s (cycles=%d)\n", pass ? "PASS" : "FAIL", cycles);
  return pass;
}

// ── Test 2: Remote Data Flow Verification ──
static bool test_remote_data_flow(Vfull_pull_cycle_top* dut) {
  printf("  Test 2: Remote data flow...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t NODE_N = 0x10;
  const uint32_t NODE_M = 0x20;
  const uint32_t WORD0 = 0xAAAABBBB;
  const uint32_t WORD1 = 0xCCCCDDDD;

  // Register adjacency
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = NODE_N;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = NODE_M;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  // PE_A adjacency
  dut->pe_a_adj_count = 1;
  dut->pe_a_adj_neighbor_ids[0] = NODE_M;
  dut->pe_a_adj_neighbor_ids[1] = 0;
  dut->pe_a_adj_neighbor_ids[2] = 0;
  dut->pe_a_adj_neighbor_xs = 1ULL;
  dut->pe_a_adj_neighbor_ys = 0ULL;
  dut->pe_a_adj_is_local = 0;

  // Trigger done
  dut->pe_a_done_valid = 1;
  dut->pe_a_done_node_id = NODE_N;
  dut->pe_a_done_is_factor = 0;
  tick(dut);
  dut->pe_a_done_valid = 0;

  int remote_words = 0;
  uint32_t remote_data0 = 0, remote_data1 = 0;
  bool remote_last_seen = false;
  int spm_ready_set = 0;
  int cycles = 0;
  int max_cycles = 5000;

  while (cycles < max_cycles) {
    uint32_t rd_addr = (uint32_t)dut->pe_a_spm_rd_addr;
    if (rd_addr != 0 && !spm_ready_set) {
      dut->pe_a_spm_ready = 1;
      uint64_t header = 0;
      header |= ((uint64_t)NODE_N & 0x3FF) << 0;
      header |= ((uint64_t)4 & 0xF) << 10;
      header |= ((uint64_t)1 & 0xF) << 14;
      header |= ((uint64_t)0 & 0x3FFFF) << 18;
      header |= ((uint64_t)0x100 & 0x3FFFF) << 36;
      header |= ((uint64_t)2 & 0x3F) << 54;  // state_words=2
      dut->pe_a_spm_data = header;
      spm_ready_set = 1;
    } else if (spm_ready_set && dut->pe_a_spm_ready) {
      dut->pe_a_spm_data = (rd_addr == 0x100) ? WORD0 : WORD1;
    } else {
      dut->pe_a_spm_ready = 0;
    }

    tick(dut);
    cycles++;

    if (dut->pe_b_remote_valid) {
      if (remote_words == 0) remote_data0 = dut->pe_b_remote_data;
      else if (remote_words == 1) remote_data1 = dut->pe_b_remote_data;
      remote_words++;
    }
    if (dut->pe_b_remote_last) remote_last_seen = true;

    if (dut->pe_b_complete_valid && dut->pe_b_scoreboard_occupancy == 0) break;
  }

  if (remote_words < 2) {
    fprintf(stderr, "\n    FAIL: remote_words=%d, expected at least 2", remote_words);
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
  if (!remote_last_seen) {
    fprintf(stderr, "\n    FAIL: remote_last never asserted");
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ── Test 3: Scoreboard Occupancy Tracking ──
static bool test_scoreboard_occupancy(Vfull_pull_cycle_top* dut) {
  printf("  Test 3: Scoreboard occupancy tracking...");
  reset_dut(dut);
  bool pass = true;

  const uint32_t NODE_N = 0x10;
  const uint32_t NODE_M = 0x20;

  // Register adjacency
  dut->pe_b_adj_valid = 1;
  dut->pe_b_adj_neighbor_id = NODE_N;
  dut->pe_b_adj_neighbor_x = 0;
  dut->pe_b_adj_neighbor_y = 0;
  dut->pe_b_adj_is_local = 0;
  dut->pe_b_adj_current_node_id = NODE_M;
  tick(dut);
  dut->pe_b_adj_valid = 0;

  // PE_A adjacency
  dut->pe_a_adj_count = 1;
  dut->pe_a_adj_neighbor_ids[0] = NODE_M;
  dut->pe_a_adj_neighbor_ids[1] = 0;
  dut->pe_a_adj_neighbor_ids[2] = 0;
  dut->pe_a_adj_neighbor_xs = 1ULL;
  dut->pe_a_adj_neighbor_ys = 0ULL;
  dut->pe_a_adj_is_local = 0;

  // Trigger done
  dut->pe_a_done_valid = 1;
  dut->pe_a_done_node_id = NODE_N;
  dut->pe_a_done_is_factor = 0;
  tick(dut);
  dut->pe_a_done_valid = 0;

  int occupancy_went_to_1 = 0;
  int occupancy_returned_to_0 = 0;
  int spm_ready_set = 0;
  int cycles = 0;
  int max_cycles = 5000;

  while (cycles < max_cycles) {
    uint32_t rd_addr = (uint32_t)dut->pe_a_spm_rd_addr;
    if (rd_addr != 0 && !spm_ready_set) {
      dut->pe_a_spm_ready = 1;
      uint64_t header = 0;
      header |= ((uint64_t)NODE_N & 0x3FF) << 0;
      header |= ((uint64_t)4 & 0xF) << 10;
      header |= ((uint64_t)1 & 0xF) << 14;
      header |= ((uint64_t)0 & 0x3FFFF) << 18;
      header |= ((uint64_t)0x100 & 0x3FFFF) << 36;
      header |= ((uint64_t)2 & 0x3F) << 54;
      dut->pe_a_spm_data = header;
      spm_ready_set = 1;
    } else if (spm_ready_set && dut->pe_a_spm_ready) {
      dut->pe_a_spm_data = (rd_addr == 0x100) ? 0x11111111 : 0x22222222;
    } else {
      dut->pe_a_spm_ready = 0;
    }

    tick(dut);
    cycles++;

    if (dut->pe_b_scoreboard_occupancy == 1) occupancy_went_to_1 = 1;
    if (occupancy_went_to_1 && dut->pe_b_scoreboard_occupancy == 0)
      occupancy_returned_to_0 = 1;

    if (dut->pe_b_complete_valid && dut->pe_b_scoreboard_occupancy == 0) break;
  }

  if (!occupancy_went_to_1) {
    fprintf(stderr, "\n    FAIL: occupancy never went to 1");
    pass = false;
  }
  if (!occupancy_returned_to_0) {
    fprintf(stderr, "\n    FAIL: occupancy never returned to 0 after going to 1");
    pass = false;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vfull_pull_cycle_top;

  printf("Full Pull Cycle integration tests:\n");
  bool pass = true;
  pass &= test_full_pull_cycle(dut);
  pass &= test_remote_data_flow(dut);
  pass &= test_scoreboard_occupancy(dut);

  printf("\n%s\n", pass ? "All tests PASSED" : "Some tests FAILED");

  delete dut;
  return pass ? 0 : 1;
}
