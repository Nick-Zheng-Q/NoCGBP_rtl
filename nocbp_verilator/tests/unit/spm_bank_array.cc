// spm_bank_array.cc
// Unit test for spm_bank_array
// Test cases from docs/gbp_pe/verification/unit_tests/17_spm_bank_array.md

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vspm_bank_array_top.h"

static void tick(Vspm_bank_array_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vspm_bank_array_top* dut) {
  dut->reset_i = 1;
  dut->bank0_rd_en = 0; dut->bank1_rd_en = 0; dut->bank2_rd_en = 0; dut->bank3_rd_en = 0;
  dut->bank4_rd_en = 0; dut->bank5_rd_en = 0; dut->bank6_rd_en = 0; dut->bank7_rd_en = 0;
  dut->bank0_rd_addr = 0; dut->bank1_rd_addr = 0; dut->bank2_rd_addr = 0; dut->bank3_rd_addr = 0;
  dut->bank4_rd_addr = 0; dut->bank5_rd_addr = 0; dut->bank6_rd_addr = 0; dut->bank7_rd_addr = 0;
  dut->bank0_wr_en = 0; dut->bank1_wr_en = 0; dut->bank2_wr_en = 0; dut->bank3_wr_en = 0;
  dut->bank4_wr_en = 0; dut->bank5_wr_en = 0; dut->bank6_wr_en = 0; dut->bank7_wr_en = 0;
  dut->bank0_wr_addr = 0; dut->bank1_wr_addr = 0; dut->bank2_wr_addr = 0; dut->bank3_wr_addr = 0;
  dut->bank4_wr_addr = 0; dut->bank5_wr_addr = 0; dut->bank6_wr_addr = 0; dut->bank7_wr_addr = 0;
  dut->bank0_wr_data = 0; dut->bank1_wr_data = 0; dut->bank2_wr_data = 0; dut->bank3_wr_data = 0;
  dut->bank4_wr_data = 0; dut->bank5_wr_data = 0; dut->bank6_wr_data = 0; dut->bank7_wr_data = 0;
  dut->bank0_wr_wstrb = 0; dut->bank1_wr_wstrb = 0; dut->bank2_wr_wstrb = 0; dut->bank3_wr_wstrb = 0;
  dut->bank4_wr_wstrb = 0; dut->bank5_wr_wstrb = 0; dut->bank6_wr_wstrb = 0; dut->bank7_wr_wstrb = 0;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->reset_i = 0;
  for (int i = 0; i < 2; ++i) tick(dut);
}

// Helper macros to reduce boilerplate
#define SET_RD_EN(b, v)   dut->bank##b##_rd_en   = (v)
#define SET_RD_ADDR(b, v) dut->bank##b##_rd_addr = (v)
#define GET_RD_DATA(b)    dut->bank##b##_rd_data
#define SET_WR_EN(b, v)   dut->bank##b##_wr_en   = (v)
#define SET_WR_ADDR(b, v) dut->bank##b##_wr_addr = (v)
#define SET_WR_DATA(b, v) dut->bank##b##_wr_data = (v)
#define SET_WR_WSTRB(b, v) dut->bank##b##_wr_wstrb = (v)

// ── Test Case 1: Single Bank Access ──
static int test_single_bank(Vspm_bank_array_top* dut) {
  printf("  Test Case 1: Single Bank Access...");
  reset_dut(dut);
  int pass = 1;

  SET_WR_EN(0, 1);
  SET_WR_ADDR(0, 0x10);
  SET_WR_DATA(0, 0xDEADBEEFCAFEBABEULL);
  SET_WR_WSTRB(0, 0xFF);
  tick(dut);
  SET_WR_EN(0, 0);

  SET_RD_EN(0, 1);
  SET_RD_ADDR(0, 0x10);
  tick(dut);
  SET_RD_EN(0, 0);
  tick(dut);

  if (GET_RD_DATA(0) != 0xDEADBEEFCAFEBABEULL) {
    fprintf(stderr, "\n    FAIL: bank0 data=0x%016lX, expected 0xDEADBEEFCAFEBABE",
            (unsigned long long)GET_RD_DATA(0));
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Parallel Bank Access ──
static int test_parallel_banks(Vspm_bank_array_top* dut) {
  printf("  Test Case 2: Parallel Bank Access...");
  reset_dut(dut);
  int pass = 1;

  SET_WR_EN(0, 1); SET_WR_ADDR(0, 0x10); SET_WR_DATA(0, 0x1111111111111111ULL); SET_WR_WSTRB(0, 0xFF);
  SET_WR_EN(1, 1); SET_WR_ADDR(1, 0x20); SET_WR_DATA(1, 0x2222222222222222ULL); SET_WR_WSTRB(1, 0xFF);
  tick(dut);
  SET_WR_EN(0, 0); SET_WR_EN(1, 0);

  SET_RD_EN(0, 1); SET_RD_ADDR(0, 0x10);
  SET_RD_EN(1, 1); SET_RD_ADDR(1, 0x20);
  tick(dut);
  SET_RD_EN(0, 0); SET_RD_EN(1, 0);
  tick(dut);

  if (GET_RD_DATA(0) != 0x1111111111111111ULL) {
    fprintf(stderr, "\n    FAIL: bank0 data=0x%016lX", (unsigned long long)GET_RD_DATA(0));
    pass = 0;
  }
  if (GET_RD_DATA(1) != 0x2222222222222222ULL) {
    fprintf(stderr, "\n    FAIL: bank1 data=0x%016lX", (unsigned long long)GET_RD_DATA(1));
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: Write and Read Same Bank ──
static int test_write_read_same_bank(Vspm_bank_array_top* dut) {
  printf("  Test Case 3: Write and Read Same Bank...");
  reset_dut(dut);
  int pass = 1;

  SET_WR_EN(2, 1); SET_WR_ADDR(2, 0x30); SET_WR_DATA(2, 0xCAFEBABEDEADBEEFULL); SET_WR_WSTRB(2, 0xFF);
  tick(dut);
  SET_WR_EN(2, 0);

  SET_RD_EN(2, 1); SET_RD_ADDR(2, 0x30);
  tick(dut);
  SET_RD_EN(2, 0);
  tick(dut);

  if (GET_RD_DATA(2) != 0xCAFEBABEDEADBEEFULL) {
    fprintf(stderr, "\n    FAIL: bank2 data=0x%016lX, expected 0xCAFEBABEDEADBEEF",
            (unsigned long long)GET_RD_DATA(2));
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: All Banks Active ──
static int test_all_banks(Vspm_bank_array_top* dut) {
  printf("  Test Case 4: All Banks Active...");
  reset_dut(dut);
  int pass = 1;

  // Write to all 8 banks
  SET_WR_EN(0, 1); SET_WR_ADDR(0, 0); SET_WR_DATA(0, 0x1000); SET_WR_WSTRB(0, 0xFF);
  SET_WR_EN(1, 1); SET_WR_ADDR(1, 1); SET_WR_DATA(1, 0x1001); SET_WR_WSTRB(1, 0xFF);
  SET_WR_EN(2, 1); SET_WR_ADDR(2, 2); SET_WR_DATA(2, 0x1002); SET_WR_WSTRB(2, 0xFF);
  SET_WR_EN(3, 1); SET_WR_ADDR(3, 3); SET_WR_DATA(3, 0x1003); SET_WR_WSTRB(3, 0xFF);
  SET_WR_EN(4, 1); SET_WR_ADDR(4, 4); SET_WR_DATA(4, 0x1004); SET_WR_WSTRB(4, 0xFF);
  SET_WR_EN(5, 1); SET_WR_ADDR(5, 5); SET_WR_DATA(5, 0x1005); SET_WR_WSTRB(5, 0xFF);
  SET_WR_EN(6, 1); SET_WR_ADDR(6, 6); SET_WR_DATA(6, 0x1006); SET_WR_WSTRB(6, 0xFF);
  SET_WR_EN(7, 1); SET_WR_ADDR(7, 7); SET_WR_DATA(7, 0x1007); SET_WR_WSTRB(7, 0xFF);
  tick(dut);
  SET_WR_EN(0, 0); SET_WR_EN(1, 0); SET_WR_EN(2, 0); SET_WR_EN(3, 0);
  SET_WR_EN(4, 0); SET_WR_EN(5, 0); SET_WR_EN(6, 0); SET_WR_EN(7, 0);

  // Read all 8 banks in parallel
  SET_RD_EN(0, 1); SET_RD_ADDR(0, 0);
  SET_RD_EN(1, 1); SET_RD_ADDR(1, 1);
  SET_RD_EN(2, 1); SET_RD_ADDR(2, 2);
  SET_RD_EN(3, 1); SET_RD_ADDR(3, 3);
  SET_RD_EN(4, 1); SET_RD_ADDR(4, 4);
  SET_RD_EN(5, 1); SET_RD_ADDR(5, 5);
  SET_RD_EN(6, 1); SET_RD_ADDR(6, 6);
  SET_RD_EN(7, 1); SET_RD_ADDR(7, 7);
  tick(dut);
  SET_RD_EN(0, 0); SET_RD_EN(1, 0); SET_RD_EN(2, 0); SET_RD_EN(3, 0);
  SET_RD_EN(4, 0); SET_RD_EN(5, 0); SET_RD_EN(6, 0); SET_RD_EN(7, 0);
  tick(dut);

  if (GET_RD_DATA(0) != 0x1000) { fprintf(stderr, "\n    FAIL: bank0"); pass = 0; }
  if (GET_RD_DATA(1) != 0x1001) { fprintf(stderr, "\n    FAIL: bank1"); pass = 0; }
  if (GET_RD_DATA(2) != 0x1002) { fprintf(stderr, "\n    FAIL: bank2"); pass = 0; }
  if (GET_RD_DATA(3) != 0x1003) { fprintf(stderr, "\n    FAIL: bank3"); pass = 0; }
  if (GET_RD_DATA(4) != 0x1004) { fprintf(stderr, "\n    FAIL: bank4"); pass = 0; }
  if (GET_RD_DATA(5) != 0x1005) { fprintf(stderr, "\n    FAIL: bank5"); pass = 0; }
  if (GET_RD_DATA(6) != 0x1006) { fprintf(stderr, "\n    FAIL: bank6"); pass = 0; }
  if (GET_RD_DATA(7) != 0x1007) { fprintf(stderr, "\n    FAIL: bank7"); pass = 0; }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: Bank Independence ──
static int test_bank_independence(Vspm_bank_array_top* dut) {
  printf("  Test Case 5: Bank Independence...");
  reset_dut(dut);
  int pass = 1;

  SET_WR_EN(0, 1); SET_WR_ADDR(0, 0x05); SET_WR_DATA(0, 0xAAAAAAAAAAAAAAAAULL); SET_WR_WSTRB(0, 0xFF);
  SET_WR_EN(7, 1); SET_WR_ADDR(7, 0x05); SET_WR_DATA(7, 0xBBBBBBBBBBBBBBBBULL); SET_WR_WSTRB(7, 0xFF);
  tick(dut);
  SET_WR_EN(0, 0); SET_WR_EN(7, 0);

  SET_RD_EN(0, 1); SET_RD_ADDR(0, 0x05);
  SET_RD_EN(7, 1); SET_RD_ADDR(7, 0x05);
  tick(dut);
  SET_RD_EN(0, 0); SET_RD_EN(7, 0);
  tick(dut);

  if (GET_RD_DATA(0) != 0xAAAAAAAAAAAAAAAAULL) {
    fprintf(stderr, "\n    FAIL: bank0 data=0x%016lX", (unsigned long long)GET_RD_DATA(0));
    pass = 0;
  }
  if (GET_RD_DATA(7) != 0xBBBBBBBBBBBBBBBBULL) {
    fprintf(stderr, "\n    FAIL: bank7 data=0x%016lX", (unsigned long long)GET_RD_DATA(7));
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 6: Maximum Address Range ──
static int test_max_address_range(Vspm_bank_array_top* dut) {
  printf("  Test Case 6: Maximum Address Range...");
  reset_dut(dut);
  int pass = 1;

  const uint32_t max_addr = (1u << 14) - 1; // 0x3FFF

  // Write unique data to max address in every bank
  SET_WR_EN(0, 1); SET_WR_ADDR(0, max_addr); SET_WR_DATA(0, 0x100ULL); SET_WR_WSTRB(0, 0xFF);
  SET_WR_EN(1, 1); SET_WR_ADDR(1, max_addr); SET_WR_DATA(1, 0x101ULL); SET_WR_WSTRB(1, 0xFF);
  SET_WR_EN(2, 1); SET_WR_ADDR(2, max_addr); SET_WR_DATA(2, 0x102ULL); SET_WR_WSTRB(2, 0xFF);
  SET_WR_EN(3, 1); SET_WR_ADDR(3, max_addr); SET_WR_DATA(3, 0x103ULL); SET_WR_WSTRB(3, 0xFF);
  SET_WR_EN(4, 1); SET_WR_ADDR(4, max_addr); SET_WR_DATA(4, 0x104ULL); SET_WR_WSTRB(4, 0xFF);
  SET_WR_EN(5, 1); SET_WR_ADDR(5, max_addr); SET_WR_DATA(5, 0x105ULL); SET_WR_WSTRB(5, 0xFF);
  SET_WR_EN(6, 1); SET_WR_ADDR(6, max_addr); SET_WR_DATA(6, 0x106ULL); SET_WR_WSTRB(6, 0xFF);
  SET_WR_EN(7, 1); SET_WR_ADDR(7, max_addr); SET_WR_DATA(7, 0x107ULL); SET_WR_WSTRB(7, 0xFF);
  tick(dut);
  SET_WR_EN(0, 0); SET_WR_EN(1, 0); SET_WR_EN(2, 0); SET_WR_EN(3, 0);
  SET_WR_EN(4, 0); SET_WR_EN(5, 0); SET_WR_EN(6, 0); SET_WR_EN(7, 0);

  // Read all banks in parallel
  SET_RD_EN(0, 1); SET_RD_ADDR(0, max_addr);
  SET_RD_EN(1, 1); SET_RD_ADDR(1, max_addr);
  SET_RD_EN(2, 1); SET_RD_ADDR(2, max_addr);
  SET_RD_EN(3, 1); SET_RD_ADDR(3, max_addr);
  SET_RD_EN(4, 1); SET_RD_ADDR(4, max_addr);
  SET_RD_EN(5, 1); SET_RD_ADDR(5, max_addr);
  SET_RD_EN(6, 1); SET_RD_ADDR(6, max_addr);
  SET_RD_EN(7, 1); SET_RD_ADDR(7, max_addr);
  tick(dut);
  SET_RD_EN(0, 0); SET_RD_EN(1, 0); SET_RD_EN(2, 0); SET_RD_EN(3, 0);
  SET_RD_EN(4, 0); SET_RD_EN(5, 0); SET_RD_EN(6, 0); SET_RD_EN(7, 0);
  tick(dut);

  if (GET_RD_DATA(0) != 0x100ULL) { fprintf(stderr, "\n    FAIL: bank0"); pass = 0; }
  if (GET_RD_DATA(1) != 0x101ULL) { fprintf(stderr, "\n    FAIL: bank1"); pass = 0; }
  if (GET_RD_DATA(2) != 0x102ULL) { fprintf(stderr, "\n    FAIL: bank2"); pass = 0; }
  if (GET_RD_DATA(3) != 0x103ULL) { fprintf(stderr, "\n    FAIL: bank3"); pass = 0; }
  if (GET_RD_DATA(4) != 0x104ULL) { fprintf(stderr, "\n    FAIL: bank4"); pass = 0; }
  if (GET_RD_DATA(5) != 0x105ULL) { fprintf(stderr, "\n    FAIL: bank5"); pass = 0; }
  if (GET_RD_DATA(6) != 0x106ULL) { fprintf(stderr, "\n    FAIL: bank6"); pass = 0; }
  if (GET_RD_DATA(7) != 0x107ULL) { fprintf(stderr, "\n    FAIL: bank7"); pass = 0; }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 7: Reset During Access ──
static int test_reset_during_access(Vspm_bank_array_top* dut) {
  printf("  Test Case 7: Reset During Access...");
  reset_dut(dut);
  int pass = 1;

  const uint64_t expected = 0xDEADBEEFCAFEBABEULL;

  // Write known data to bank 0
  SET_WR_EN(0, 1);
  SET_WR_ADDR(0, 0x60);
  SET_WR_DATA(0, expected);
  SET_WR_WSTRB(0, 0xFF);
  tick(dut);
  SET_WR_EN(0, 0);

  // Confirm data is stored
  SET_RD_EN(0, 1);
  SET_RD_ADDR(0, 0x60);
  tick(dut);
  SET_RD_EN(0, 0);
  tick(dut);
  if (GET_RD_DATA(0) != expected) {
    fprintf(stderr, "\n    FAIL: pre-reset data=0x%016lX",
            (unsigned long long)GET_RD_DATA(0));
    pass = 0;
  }

  // Assert reset while reading
  dut->reset_i = 1;
  SET_RD_EN(0, 1);
  SET_RD_ADDR(0, 0x60);
  tick(dut);
  if (GET_RD_DATA(0) != 0) {
    fprintf(stderr, "\n    FAIL: reset read data=0x%016lX, expected 0",
            (unsigned long long)GET_RD_DATA(0));
    pass = 0;
  }

  // Deassert reset and verify no corruption
  dut->reset_i = 0;
  SET_RD_EN(0, 1);
  SET_RD_ADDR(0, 0x60);
  tick(dut);
  SET_RD_EN(0, 0);
  tick(dut);
  if (GET_RD_DATA(0) != expected) {
    fprintf(stderr, "\n    FAIL: post-reset data=0x%016lX",
            (unsigned long long)GET_RD_DATA(0));
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 8: Back-to-Back Access ──
static int test_back_to_back_access(Vspm_bank_array_top* dut) {
  printf("  Test Case 8: Back-to-Back Access...");
  reset_dut(dut);
  int pass = 1;

  uint64_t data[4] = {
    0xA0A0A0A0A0A0A0A0ULL,
    0xB1B1B1B1B1B1B1B1ULL,
    0xC2C2C2C2C2C2C2C2ULL,
    0xD3D3D3D3D3D3D3D3ULL
  };

  // Back-to-back writes to bank 4
  for (int i = 0; i < 4; ++i) {
    SET_WR_EN(4, 1);
    SET_WR_ADDR(4, 0x10 + i);
    SET_WR_DATA(4, data[i]);
    SET_WR_WSTRB(4, 0xFF);
    tick(dut);
  }
  SET_WR_EN(4, 0);

  // Back-to-back reads
  for (int i = 0; i < 4; ++i) {
    SET_RD_EN(4, 1);
    SET_RD_ADDR(4, 0x10 + i);
    tick(dut);
    SET_RD_EN(4, 0);
    tick(dut);
    if (GET_RD_DATA(4) != data[i]) {
      fprintf(stderr, "\n    FAIL: addr 0x%x data=0x%016lX, expected 0x%016lX",
              0x10 + i, (unsigned long long)GET_RD_DATA(4), data[i]);
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 9: Same-Bank Conflict ──
static int test_same_bank_conflict(Vspm_bank_array_top* dut) {
  printf("  Test Case 9: Same-Bank Conflict...");
  reset_dut(dut);
  int pass = 1;

  const uint64_t old_data = 0x1111111122222222ULL;
  const uint64_t new_data = 0xAAAAAAAA55555555ULL;

  // Pre-write a known value
  SET_WR_EN(5, 1);
  SET_WR_ADDR(5, 0x70);
  SET_WR_DATA(5, old_data);
  SET_WR_WSTRB(5, 0xFF);
  tick(dut);

  // Simultaneous read and write to the same bank / same address
  SET_RD_EN(5, 1);
  SET_RD_ADDR(5, 0x70);
  SET_WR_EN(5, 1);
  SET_WR_ADDR(5, 0x70);
  SET_WR_DATA(5, new_data);
  SET_WR_WSTRB(5, 0xFF);
  tick(dut);
  SET_RD_EN(5, 0);
  SET_WR_EN(5, 0);

  // Capture read result (old data)
  tick(dut);
  if (GET_RD_DATA(5) != old_data) {
    fprintf(stderr, "\n    FAIL: simultaneous-read data=0x%016lX, expected old 0x%016lX",
            (unsigned long long)GET_RD_DATA(5), old_data);
    pass = 0;
  }

  // Confirm the write completed
  SET_RD_EN(5, 1);
  SET_RD_ADDR(5, 0x70);
  tick(dut);
  SET_RD_EN(5, 0);
  tick(dut);
  if (GET_RD_DATA(5) != new_data) {
    fprintf(stderr, "\n    FAIL: post-write data=0x%016lX, expected 0x%016lX",
            (unsigned long long)GET_RD_DATA(5), new_data);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vspm_bank_array_top;

  int failures = 0;
  printf("spm_bank_array unit tests:\n");
  failures += test_single_bank(dut);
  failures += test_parallel_banks(dut);
  failures += test_write_read_same_bank(dut);
  failures += test_all_banks(dut);
  failures += test_bank_independence(dut);
  failures += test_max_address_range(dut);
  failures += test_reset_during_access(dut);
  failures += test_back_to_back_access(dut);
  failures += test_same_bank_conflict(dut);

  if (failures == 0) {
    printf("\nAll 9 tests PASSED\n");
  } else {
    printf("\n%d of 9 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
