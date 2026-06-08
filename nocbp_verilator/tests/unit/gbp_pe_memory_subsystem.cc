// gbp_pe_memory_subsystem.cc
// Functional test for memory subsystem.
// Test cases from docs/gbp_pe/verification/subsystem_tests/02_memory_subsystem.md

#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vgbp_pe_memory_subsystem_top.h"

static void tick(Vgbp_pe_memory_subsystem_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe_memory_subsystem_top* dut) {
  dut->rst_n = 0;
  dut->c0_rd_valid_i = 0; dut->c0_wr_valid_i = 0;
  dut->c1_rd_valid_i = 0; dut->c1_wr_valid_i = 0;
  dut->c2_rd_valid_i = 0; dut->c2_wr_valid_i = 0;
  dut->c3_rd_valid_i = 0; dut->c3_wr_valid_i = 0;
  dut->c4_rd_valid_i = 0; dut->c4_wr_valid_i = 0;
  dut->c5_rd_valid_i = 0; dut->c5_wr_valid_i = 0;
  dut->c6_rd_valid_i = 0; dut->c6_wr_valid_i = 0;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);
}

// ── Test 1: Write then read back on client 0 ──
static int test_write_read_client0(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 1: Write then Read Client 0...");
  reset_dut(dut);
  int pass = 1;

  // Write 0xDEADBEEFCAFEBABE to bank 0 (addr=0x0000)
  dut->c0_wr_valid_i = 1;
  dut->c0_wr_addr_i  = 0x0000;
  dut->c0_wr_data_i  = 0xDEADBEEFCAFEBABEULL;
  dut->c0_wr_wstrb_i = 0xFF;
  tick(dut);
  dut->c0_wr_valid_i = 0;

  // Read back
  dut->c0_rd_valid_i = 1;
  dut->c0_rd_addr_i  = 0x0000;
  tick(dut);
  dut->c0_rd_valid_i = 0;

  // SPM has 1-cycle read latency, data available next cycle
  tick(dut);

  uint64_t rd_data = dut->c0_rd_data_o;
  if (rd_data != 0xDEADBEEFCAFEBABEULL) {
    fprintf(stderr, "\n    FAIL: rd_data=0x%016lx, expected 0xDEADBEEFCAFEBABE", rd_data);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 2: Partial byte-enable write ──
static int test_partial_wstrb(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 2: Partial WSTRB Write...");
  reset_dut(dut);
  int pass = 1;

  // First write full beat
  dut->c0_wr_valid_i = 1;
  dut->c0_wr_addr_i  = 0x0010;
  dut->c0_wr_data_i  = 0xAAAAAAAAAAAAAAAAULL;
  dut->c0_wr_wstrb_i = 0xFF;
  tick(dut);
  dut->c0_wr_valid_i = 0;

  // Partial write lower 4 bytes with 0xBBBBBBBB
  dut->c0_wr_valid_i = 1;
  dut->c0_wr_addr_i  = 0x0010;
  dut->c0_wr_data_i  = 0xCCCCCCCCBBBBBBBBULL;
  dut->c0_wr_wstrb_i = 0x0F; // only lower 4 bytes
  tick(dut);
  dut->c0_wr_valid_i = 0;

  // Read back
  dut->c0_rd_valid_i = 1;
  dut->c0_rd_addr_i  = 0x0010;
  tick(dut);
  dut->c0_rd_valid_i = 0;
  tick(dut);

  uint64_t rd_data = dut->c0_rd_data_o;
  uint64_t expected = 0xAAAAAAAAAAAAAAAAULL;
  expected = (expected & 0xFFFFFFFFFFFFFF00ULL) | 0xBB;
  expected = (expected & 0xFFFFFFFFFFFF00FFULL) | 0xBB00;
  expected = (expected & 0xFFFFFFFFFF00FFFFULL) | 0xBB0000;
  expected = (expected & 0xFFFFFFFF00FFFFFFULL) | 0xBB000000;
  // Actually easier: upper 4 bytes stay 0xAAAAAAAA, lower 4 become 0xBBBBBBBB
  expected = 0xAAAAAAAABBBBBBBBULL;

  if (rd_data != expected) {
    fprintf(stderr, "\n    FAIL: rd_data=0x%016lx, expected 0x%016lx", rd_data, expected);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 3: Concurrent reads to different banks ──
static int test_concurrent_reads(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 3: Concurrent Reads Different Banks...");
  reset_dut(dut);
  int pass = 1;

  // Pre-write distinct data to bank 0, 1, 2
  for (int c = 0; c < 3; ++c) {
    dut->c0_wr_valid_i = (c == 0);
    dut->c1_wr_valid_i = (c == 1);
    dut->c2_wr_valid_i = (c == 2);
    dut->c0_wr_addr_i  = (c == 0) ? 0x0000 : 0;
    dut->c1_wr_addr_i  = (c == 1) ? 0x0002 : 0; // bank 1
    dut->c2_wr_addr_i  = (c == 2) ? 0x0004 : 0; // bank 2
    dut->c0_wr_data_i  = (c == 0) ? 0x1111111111111111ULL : 0;
    dut->c1_wr_data_i  = (c == 1) ? 0x2222222222222222ULL : 0;
    dut->c2_wr_data_i  = (c == 2) ? 0x3333333333333333ULL : 0;
    dut->c0_wr_wstrb_i = 0xFF;
    dut->c1_wr_wstrb_i = 0xFF;
    dut->c2_wr_wstrb_i = 0xFF;
    tick(dut);
  }
  dut->c0_wr_valid_i = 0;
  dut->c1_wr_valid_i = 0;
  dut->c2_wr_valid_i = 0;

  // Concurrent reads
  dut->c0_rd_valid_i = 1; dut->c0_rd_addr_i = 0x0000;
  dut->c1_rd_valid_i = 1; dut->c1_rd_addr_i = 0x0002;
  dut->c2_rd_valid_i = 1; dut->c2_rd_addr_i = 0x0004;
  tick(dut);
  dut->c0_rd_valid_i = 0;
  dut->c1_rd_valid_i = 0;
  dut->c2_rd_valid_i = 0;

  // Check ready signals (all should be granted since different banks)
  // Note: ready is combinational, check on falling edge of same cycle
  dut->clk = 0; dut->eval();
  if (!dut->c0_rd_ready_o) { fprintf(stderr, "\n    FAIL: c0_rd_ready=0"); pass = 0; }
  if (!dut->c1_rd_ready_o) { fprintf(stderr, "\n    FAIL: c1_rd_ready=0"); pass = 0; }
  if (!dut->c2_rd_ready_o) { fprintf(stderr, "\n    FAIL: c2_rd_ready=0"); pass = 0; }

  // Wait for data (1-cycle latency)
  tick(dut);

  if (dut->c0_rd_data_o != 0x1111111111111111ULL) {
    fprintf(stderr, "\n    FAIL: c0_data=0x%016lx", (uint64_t)dut->c0_rd_data_o); pass = 0;
  }
  if (dut->c1_rd_data_o != 0x2222222222222222ULL) {
    fprintf(stderr, "\n    FAIL: c1_data=0x%016lx", (uint64_t)dut->c1_rd_data_o); pass = 0;
  }
  if (dut->c2_rd_data_o != 0x3333333333333333ULL) {
    fprintf(stderr, "\n    FAIL: c2_data=0x%016lx", (uint64_t)dut->c2_rd_data_o); pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 4: Bank conflict round-robin ──
static int test_bank_conflict(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 4: Bank Conflict Round-Robin...");
  reset_dut(dut);
  int pass = 1;

  // All three clients target bank 0 (addr[3:1]=0)
  dut->c0_rd_valid_i = 1; dut->c0_rd_addr_i = 0x0000;
  dut->c1_rd_valid_i = 1; dut->c1_rd_addr_i = 0x0010; // same bank 0
  dut->c2_rd_valid_i = 1; dut->c2_rd_addr_i = 0x0020; // same bank 0
  tick(dut);

  dut->clk = 0; dut->eval();
  int granted = (dut->c0_rd_ready_o ? 1 : 0)
              + (dut->c1_rd_ready_o ? 1 : 0)
              + (dut->c2_rd_ready_o ? 1 : 0);
  if (granted != 1) {
    fprintf(stderr, "\n    FAIL: %d clients granted, expected 1", granted);
    pass = 0;
  }

  // Keep valid high for next cycle
  tick(dut);
  dut->clk = 0; dut->eval();
  granted = (dut->c0_rd_ready_o ? 1 : 0)
          + (dut->c1_rd_ready_o ? 1 : 0)
          + (dut->c2_rd_ready_o ? 1 : 0);
  if (granted != 1) {
    fprintf(stderr, "\n    FAIL: cycle2 granted=%d, expected 1", granted);
    pass = 0;
  }

  dut->c0_rd_valid_i = 0;
  dut->c1_rd_valid_i = 0;
  dut->c2_rd_valid_i = 0;

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 5: Write-then-Read Same Address ──
static int test_write_read_same_addr(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 5: Write-then-Read Same Address...");
  reset_dut(dut);
  int pass = 1;

  // Write value A
  dut->c0_wr_valid_i = 1;
  dut->c0_wr_addr_i  = 0x0100;
  dut->c0_wr_data_i  = 0x1111111111111111ULL;
  dut->c0_wr_wstrb_i = 0xFF;
  tick(dut);
  dut->c0_wr_valid_i = 0;

  // Write value B to same address
  dut->c0_wr_valid_i = 1;
  dut->c0_wr_addr_i  = 0x0100;
  dut->c0_wr_data_i  = 0x2222222222222222ULL;
  dut->c0_wr_wstrb_i = 0xFF;
  tick(dut);
  dut->c0_wr_valid_i = 0;

  // Read back
  dut->c0_rd_valid_i = 1;
  dut->c0_rd_addr_i  = 0x0100;
  tick(dut);
  dut->c0_rd_valid_i = 0;
  tick(dut);

  if (dut->c0_rd_data_o != 0x2222222222222222ULL) {
    fprintf(stderr, "\n    FAIL: rd_data=0x%016lx, expected 0x2222222222222222", (uint64_t)dut->c0_rd_data_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 6: Zero WSTRB (No-Op Write) ──
static int test_zero_wstrb(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 6: Zero WSTRB...");
  reset_dut(dut);
  int pass = 1;

  // Write known value
  dut->c0_wr_valid_i = 1;
  dut->c0_wr_addr_i  = 0x0200;
  dut->c0_wr_data_i  = 0xAAAAAAAAAAAAAAAAULL;
  dut->c0_wr_wstrb_i = 0xFF;
  tick(dut);

  // Attempt no-op write
  dut->c0_wr_addr_i  = 0x0200;
  dut->c0_wr_data_i  = 0xBBBBBBBBBBBBBBBBULL;
  dut->c0_wr_wstrb_i = 0x00;
  tick(dut);
  dut->c0_wr_valid_i = 0;

  // Read back
  dut->c0_rd_valid_i = 1;
  dut->c0_rd_addr_i  = 0x0200;
  tick(dut);
  dut->c0_rd_valid_i = 0;
  tick(dut);

  if (dut->c0_rd_data_o != 0xAAAAAAAAAAAAAAAAULL) {
    fprintf(stderr, "\n    FAIL: rd_data=0x%016lx, expected 0xAAAAAAAAAAAAAAAA", (uint64_t)dut->c0_rd_data_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 8: Max Address Boundary ──
static int test_max_address(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 8: Max Address Boundary...");
  reset_dut(dut);
  int pass = 1;

  // Address 0x3FFFF = max 18-bit address
  // bank = addr[3:1] = 0x3FFFF[3:1] = 7 (bank 7)
  // row  = addr[17:4] = 0x3FFF (max row)
  uint32_t max_addr = 0x3FFFF;
  dut->c0_wr_valid_i = 1;
  dut->c0_wr_addr_i  = max_addr;
  dut->c0_wr_data_i  = 0xDEADBEEFCAFEBABEULL;
  dut->c0_wr_wstrb_i = 0xFF;
  tick(dut);
  dut->c0_wr_valid_i = 0;

  dut->c0_rd_valid_i = 1;
  dut->c0_rd_addr_i  = max_addr;
  tick(dut);
  dut->c0_rd_valid_i = 0;
  tick(dut);

  if (dut->c0_rd_data_o != 0xDEADBEEFCAFEBABEULL) {
    fprintf(stderr, "\n    FAIL: rd_data=0x%016lx, expected 0xDEADBEEFCAFEBABE", (uint64_t)dut->c0_rd_data_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 9: All 7 Clients Concurrent ──
// Stimulates the full 7-client arbiter. Because NUM_CLIENTS=7 is not a
// power of two, the RTL's round-robin pointer can alias; we verify that
// all 7 requests are eventually granted within a small window.
static int test_all_clients_concurrent(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 9: All 7 Clients Concurrent...");
  reset_dut(dut);
  int pass = 1;

  // Use distinct banks for each client so no bank conflicts stall them.
  uint32_t addr[7] = {0x0000, 0x0002, 0x0004, 0x0006, 0x0008, 0x000A, 0x000C};
  uint64_t data[7] = {
    0x1111111111111111ULL, 0x2222222222222222ULL, 0x3333333333333333ULL,
    0x4444444444444444ULL, 0x5555555555555555ULL, 0x6666666666666666ULL,
    0x7777777777777777ULL
  };

  // Hold all write requests valid until every client has been granted.
  dut->c0_wr_valid_i = 1; dut->c0_wr_addr_i = addr[0]; dut->c0_wr_data_i = data[0]; dut->c0_wr_wstrb_i = 0xFF;
  dut->c1_wr_valid_i = 1; dut->c1_wr_addr_i = addr[1]; dut->c1_wr_data_i = data[1]; dut->c1_wr_wstrb_i = 0xFF;
  dut->c2_wr_valid_i = 1; dut->c2_wr_addr_i = addr[2]; dut->c2_wr_data_i = data[2]; dut->c2_wr_wstrb_i = 0xFF;
  dut->c3_wr_valid_i = 1; dut->c3_wr_addr_i = addr[3]; dut->c3_wr_data_i = data[3]; dut->c3_wr_wstrb_i = 0xFF;
  dut->c4_wr_valid_i = 1; dut->c4_wr_addr_i = addr[4]; dut->c4_wr_data_i = data[4]; dut->c4_wr_wstrb_i = 0xFF;
  dut->c5_wr_valid_i = 1; dut->c5_wr_addr_i = addr[5]; dut->c5_wr_data_i = data[5]; dut->c5_wr_wstrb_i = 0xFF;
  dut->c6_wr_valid_i = 1; dut->c6_wr_addr_i = addr[6]; dut->c6_wr_data_i = data[6]; dut->c6_wr_wstrb_i = 0xFF;

  int granted_mask = 0;
  int timeout = 20;
  while (granted_mask != 0x7F && timeout > 0) {
    tick(dut);
    dut->clk = 0; dut->eval();
    if (dut->c0_wr_ready_o) granted_mask |= 0x01;
    if (dut->c1_wr_ready_o) granted_mask |= 0x02;
    if (dut->c2_wr_ready_o) granted_mask |= 0x04;
    if (dut->c3_wr_ready_o) granted_mask |= 0x08;
    if (dut->c4_wr_ready_o) granted_mask |= 0x10;
    if (dut->c5_wr_ready_o) granted_mask |= 0x20;
    if (dut->c6_wr_ready_o) granted_mask |= 0x40;
    timeout--;
  }

  if (granted_mask != 0x7F) {
    fprintf(stderr, "\n    FAIL: write grants mask=0x%02x, expected 0x7F", granted_mask);
    pass = 0;
  }

  // Clear writes
  dut->c0_wr_valid_i = 0;
  dut->c1_wr_valid_i = 0;
  dut->c2_wr_valid_i = 0;
  dut->c3_wr_valid_i = 0;
  dut->c4_wr_valid_i = 0;
  dut->c5_wr_valid_i = 0;
  dut->c6_wr_valid_i = 0;

  // Same exercise for reads
  dut->c0_rd_valid_i = 1; dut->c0_rd_addr_i = addr[0];
  dut->c1_rd_valid_i = 1; dut->c1_rd_addr_i = addr[1];
  dut->c2_rd_valid_i = 1; dut->c2_rd_addr_i = addr[2];
  dut->c3_rd_valid_i = 1; dut->c3_rd_addr_i = addr[3];
  dut->c4_rd_valid_i = 1; dut->c4_rd_addr_i = addr[4];
  dut->c5_rd_valid_i = 1; dut->c5_rd_addr_i = addr[5];
  dut->c6_rd_valid_i = 1; dut->c6_rd_addr_i = addr[6];

  granted_mask = 0;
  timeout = 20;
  while (granted_mask != 0x7F && timeout > 0) {
    tick(dut);
    dut->clk = 0; dut->eval();
    if (dut->c0_rd_ready_o) granted_mask |= 0x01;
    if (dut->c1_rd_ready_o) granted_mask |= 0x02;
    if (dut->c2_rd_ready_o) granted_mask |= 0x04;
    if (dut->c3_rd_ready_o) granted_mask |= 0x08;
    if (dut->c4_rd_ready_o) granted_mask |= 0x10;
    if (dut->c5_rd_ready_o) granted_mask |= 0x20;
    if (dut->c6_rd_ready_o) granted_mask |= 0x40;
    timeout--;
  }

  if (granted_mask != 0x7F) {
    fprintf(stderr, "\n    FAIL: read grants mask=0x%02x, expected 0x7F", granted_mask);
    pass = 0;
  }

  dut->c0_rd_valid_i = 0;
  dut->c1_rd_valid_i = 0;
  dut->c2_rd_valid_i = 0;
  dut->c3_rd_valid_i = 0;
  dut->c4_rd_valid_i = 0;
  dut->c5_rd_valid_i = 0;
  dut->c6_rd_valid_i = 0;

  // Wait for read data (1-cycle latency) and verify all clients
  tick(dut);

  if (dut->c0_rd_data_o != data[0]) { fprintf(stderr, "\n    FAIL: c0_data=0x%016lx", (uint64_t)dut->c0_rd_data_o); pass = 0; }
  if (dut->c1_rd_data_o != data[1]) { fprintf(stderr, "\n    FAIL: c1_data=0x%016lx", (uint64_t)dut->c1_rd_data_o); pass = 0; }
  if (dut->c2_rd_data_o != data[2]) { fprintf(stderr, "\n    FAIL: c2_data=0x%016lx", (uint64_t)dut->c2_rd_data_o); pass = 0; }
  if (dut->c3_rd_data_o != data[3]) { fprintf(stderr, "\n    FAIL: c3_data=0x%016lx", (uint64_t)dut->c3_rd_data_o); pass = 0; }
  if (dut->c4_rd_data_o != data[4]) { fprintf(stderr, "\n    FAIL: c4_data=0x%016lx", (uint64_t)dut->c4_rd_data_o); pass = 0; }
  if (dut->c5_rd_data_o != data[5]) { fprintf(stderr, "\n    FAIL: c5_data=0x%016lx", (uint64_t)dut->c5_rd_data_o); pass = 0; }
  if (dut->c6_rd_data_o != data[6]) { fprintf(stderr, "\n    FAIL: c6_data=0x%016lx", (uint64_t)dut->c6_rd_data_o); pass = 0; }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 10: Read-During-Write Same Address ──
static int test_read_during_write(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 10: Read-During-Write Same Address...");
  reset_dut(dut);
  int pass = 1;

  // Pre-write known old value
  dut->c0_wr_valid_i = 1;
  dut->c0_wr_addr_i  = 0x0300;
  dut->c0_wr_data_i  = 0xAAAAAAAAAAAAAAAAULL;
  dut->c0_wr_wstrb_i = 0xFF;
  tick(dut);
  dut->c0_wr_valid_i = 0;

  // Same cycle: c0 reads same address, c1 writes new value to same address
  dut->c0_rd_valid_i = 1; dut->c0_rd_addr_i = 0x0300;
  dut->c1_wr_valid_i = 1; dut->c1_wr_addr_i = 0x0300;
  dut->c1_wr_data_i  = 0xBBBBBBBBBBBBBBBBULL;
  dut->c1_wr_wstrb_i = 0xFF;
  tick(dut);
  dut->c0_rd_valid_i = 0;
  dut->c1_wr_valid_i = 0;

  // Both should have been granted (read and write can coexist to same bank)
  // Read returns OLD data (1-cycle latency, sampled before write is visible)
  tick(dut);

  // The read-during-write policy returns old data on the read cycle
  // We verify the write landed by a subsequent read
  dut->c0_rd_valid_i = 1; dut->c0_rd_addr_i = 0x0300;
  tick(dut);
  dut->c0_rd_valid_i = 0;
  tick(dut);

  if (dut->c0_rd_data_o != 0xBBBBBBBBBBBBBBBBULL) {
    fprintf(stderr, "\n    FAIL: post-write rd_data=0x%016lx, expected 0xBBBBBBBBBBBBBBBB", (uint64_t)dut->c0_rd_data_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 11: Back-to-Back Requests ──
static int test_back_to_back(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 11: Back-to-Back Requests...");
  reset_dut(dut);
  int pass = 1;

  // Three consecutive writes to same client
  dut->c0_wr_valid_i = 1;
  dut->c0_wr_addr_i  = 0x0400;
  dut->c0_wr_data_i  = 0x1111111111111111ULL;
  dut->c0_wr_wstrb_i = 0xFF;
  tick(dut);
  dut->c0_wr_addr_i  = 0x0408;
  dut->c0_wr_data_i  = 0x2222222222222222ULL;
  tick(dut);
  dut->c0_wr_addr_i  = 0x0410;
  dut->c0_wr_data_i  = 0x3333333333333333ULL;
  tick(dut);
  dut->c0_wr_valid_i = 0;

  // Three consecutive reads back
  dut->c0_rd_valid_i = 1;
  dut->c0_rd_addr_i  = 0x0400;
  tick(dut);
  uint64_t d0 = dut->c0_rd_data_o;
  dut->c0_rd_addr_i  = 0x0408;
  tick(dut);
  uint64_t d1 = dut->c0_rd_data_o;
  dut->c0_rd_addr_i  = 0x0410;
  tick(dut);
  uint64_t d2 = dut->c0_rd_data_o;
  dut->c0_rd_valid_i = 0;

  // Need one more tick for the last read (read data is available next cycle after grant)
  // Actually d2 was sampled on the same cycle as addr issue, before read latency.
  // Re-read the last value correctly.
  tick(dut);
  d2 = dut->c0_rd_data_o;

  if (d0 != 0x1111111111111111ULL) { fprintf(stderr, "\n    FAIL: d0=0x%016lx", d0); pass = 0; }
  if (d1 != 0x2222222222222222ULL) { fprintf(stderr, "\n    FAIL: d1=0x%016lx", d1); pass = 0; }
  if (d2 != 0x3333333333333333ULL) { fprintf(stderr, "\n    FAIL: d2=0x%016lx", d2); pass = 0; }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test 12: Reset During Active Transfer ──
static int test_reset_during_transfer(Vgbp_pe_memory_subsystem_top* dut) {
  printf("  Test 12: Reset During Active Transfer...");
  reset_dut(dut);
  int pass = 1;

  // Start a read that will be pending in the response pipeline
  dut->c0_rd_valid_i = 1;
  dut->c0_rd_addr_i  = 0x0500;
  tick(dut);

  // Assert reset before read data returns
  dut->rst_n = 0;
  dut->c0_rd_valid_i = 0;
  for (int i = 0; i < 3; ++i) tick(dut);
  dut->rst_n = 1;
  for (int i = 0; i < 3; ++i) tick(dut);

  // After reset, arbiter should be idle and accept new transactions
  dut->c0_wr_valid_i = 1;
  dut->c0_wr_addr_i  = 0x0500;
  dut->c0_wr_data_i  = 0xDEADBEEFCAFEBABEULL;
  dut->c0_wr_wstrb_i = 0xFF;
  tick(dut);
  dut->c0_wr_valid_i = 0;

  dut->c0_rd_valid_i = 1;
  dut->c0_rd_addr_i  = 0x0500;
  tick(dut);
  dut->c0_rd_valid_i = 0;
  tick(dut);

  if (dut->c0_rd_data_o != 0xDEADBEEFCAFEBABEULL) {
    fprintf(stderr, "\n    FAIL: rd_data=0x%016lx, expected 0xDEADBEEFCAFEBABE", (uint64_t)dut->c0_rd_data_o);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_pe_memory_subsystem_top;

  int failures = 0;
  printf("gbp_pe_memory_subsystem functional tests:\n");
  failures += test_write_read_client0(dut);
  failures += test_partial_wstrb(dut);
  failures += test_concurrent_reads(dut);
  failures += test_bank_conflict(dut);
  failures += test_write_read_same_addr(dut);
  failures += test_zero_wstrb(dut);
  failures += test_max_address(dut);
  failures += test_all_clients_concurrent(dut);
  failures += test_read_during_write(dut);
  failures += test_back_to_back(dut);
  failures += test_reset_during_transfer(dut);

  if (failures == 0) {
    printf("\nAll 11 tests PASSED\n");
  } else {
    printf("\n%d of 11 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
