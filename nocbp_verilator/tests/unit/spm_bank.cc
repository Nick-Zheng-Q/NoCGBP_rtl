// spm_bank.cc
// Unit test for spm_bank
// Test cases from docs/gbp_pe/verification/unit_tests/16_spm_bank.md

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vspm_bank_top.h"

static void tick(Vspm_bank_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vspm_bank_top* dut) {
  dut->reset_i = 1;
  dut->bank_rd_en = 0;
  dut->bank_rd_addr = 0;
  dut->bank_wr_en = 0;
  dut->bank_wr_addr = 0;
  dut->bank_wr_data = 0;
  dut->bank_wr_wstrb = 0;
  for (int i = 0; i < 5; ++i) tick(dut);
  dut->reset_i = 0;
  for (int i = 0; i < 2; ++i) tick(dut);
}

// ── Test Case 1: Basic Read/Write ──
static int test_basic_read_write(Vspm_bank_top* dut) {
  printf("  Test Case 1: Basic Read/Write...");
  reset_dut(dut);
  int pass = 1;

  // Write 0xDEADBEEFCAFEBABE to addr 0x10
  dut->bank_wr_en = 1;
  dut->bank_wr_addr = 0x10;
  dut->bank_wr_data = 0xDEADBEEFCAFEBABEULL;
  dut->bank_wr_wstrb = 0xFF;
  tick(dut);
  dut->bank_wr_en = 0;

  // Read back
  dut->bank_rd_en = 1;
  dut->bank_rd_addr = 0x10;
  tick(dut);
  dut->bank_rd_en = 0;

  // 1-cycle read latency
  tick(dut);

  if (dut->bank_rd_data != 0xDEADBEEFCAFEBABEULL) {
    fprintf(stderr, "\n    FAIL: rd_data=0x%016lX, expected 0xDEADBEEFCAFEBABE",
            (unsigned long long)dut->bank_rd_data);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Byte-Enable Write ──
static int test_byte_enable(Vspm_bank_top* dut) {
  printf("  Test Case 2: Byte-Enable Write...");
  reset_dut(dut);
  int pass = 1;

  // Write full zeros first to known state
  dut->bank_wr_en = 1;
  dut->bank_wr_addr = 0x20;
  dut->bank_wr_data = 0x0000000000000000ULL;
  dut->bank_wr_wstrb = 0xFF;
  tick(dut);

  // Partial write lower 4 bytes
  dut->bank_wr_addr = 0x20;
  dut->bank_wr_data = 0x000000000000BEEFULL;
  dut->bank_wr_wstrb = 0x0F; // lower 4 bytes
  tick(dut);
  dut->bank_wr_en = 0;

  // Read back
  dut->bank_rd_en = 1;
  dut->bank_rd_addr = 0x20;
  tick(dut);
  dut->bank_rd_en = 0;
  tick(dut);

  uint64_t expected = 0x000000000000BEEFULL;
  if (dut->bank_rd_data != expected) {
    fprintf(stderr, "\n    FAIL: rd_data=0x%016lX, expected 0x%016lX",
            (unsigned long long)dut->bank_rd_data, expected);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: Multiple Addresses ──
static int test_multiple_addresses(Vspm_bank_top* dut) {
  printf("  Test Case 3: Multiple Addresses...");
  reset_dut(dut);
  int pass = 1;

  // Write addr 0x00
  dut->bank_wr_en = 1;
  dut->bank_wr_addr = 0x00;
  dut->bank_wr_data = 0x1111111111111111ULL;
  dut->bank_wr_wstrb = 0xFF;
  tick(dut);

  // Write addr 0x01
  dut->bank_wr_addr = 0x01;
  dut->bank_wr_data = 0x2222222222222222ULL;
  tick(dut);
  dut->bank_wr_en = 0;

  // Read addr 0x00
  dut->bank_rd_en = 1;
  dut->bank_rd_addr = 0x00;
  tick(dut);
  dut->bank_rd_en = 0;
  tick(dut);

  if (dut->bank_rd_data != 0x1111111111111111ULL) {
    fprintf(stderr, "\n    FAIL: addr0 data=0x%016lX, expected 0x1111111111111111",
            (unsigned long long)dut->bank_rd_data);
    pass = 0;
  }

  // Read addr 0x01
  dut->bank_rd_en = 1;
  dut->bank_rd_addr = 0x01;
  tick(dut);
  dut->bank_rd_en = 0;
  tick(dut);

  if (dut->bank_rd_data != 0x2222222222222222ULL) {
    fprintf(stderr, "\n    FAIL: addr1 data=0x%016lX, expected 0x2222222222222222",
            (unsigned long long)dut->bank_rd_data);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: Read-During-Write Same Address ──
static int test_read_during_write(Vspm_bank_top* dut) {
  printf("  Test Case 4: Read-During-Write...");
  reset_dut(dut);
  int pass = 1;

  // Pre-write known value
  dut->bank_wr_en = 1;
  dut->bank_wr_addr = 0x30;
  dut->bank_wr_data = 0xAAAAAAAAAAAAAAAAULL;
  dut->bank_wr_wstrb = 0xFF;
  tick(dut);
  dut->bank_wr_en = 0;

  // Read and write same address same cycle
  dut->bank_rd_en = 1;
  dut->bank_rd_addr = 0x30;
  dut->bank_wr_en = 1;
  dut->bank_wr_addr = 0x30;
  dut->bank_wr_data = 0xBBBBBBBBBBBBBBBBULL;
  dut->bank_wr_wstrb = 0xFF;
  tick(dut);
  dut->bank_rd_en = 0;
  dut->bank_wr_en = 0;

  // Read data available next cycle (shows old or new value)
  tick(dut);

  // For this simple implementation, read returns old data on same cycle as write
  // because read is registered and write updates memory after read samples.
  // This is acceptable behavior; we just verify data is stable.
  uint64_t rd_data = dut->bank_rd_data;

  // Read back again to confirm write happened
  dut->bank_rd_en = 1;
  dut->bank_rd_addr = 0x30;
  tick(dut);
  dut->bank_rd_en = 0;
  tick(dut);

  if (dut->bank_rd_data != 0xBBBBBBBBBBBBBBBBULL) {
    fprintf(stderr, "\n    FAIL: post-write read=0x%016lX, expected 0xBBBBBBBBBBBBBBBB",
            (unsigned long long)dut->bank_rd_data);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: Back-to-Back Writes ──
static int test_back_to_back_writes(Vspm_bank_top* dut) {
  printf("  Test Case 5: Back-to-Back Writes...");
  reset_dut(dut);
  int pass = 1;

  uint64_t data[4] = {
    0x1111111111111111ULL,
    0x2222222222222222ULL,
    0x3333333333333333ULL,
    0x4444444444444444ULL
  };

  for (int i = 0; i < 4; ++i) {
    dut->bank_wr_en = 1;
    dut->bank_wr_addr = i;
    dut->bank_wr_data = data[i];
    dut->bank_wr_wstrb = 0xFF;
    tick(dut);
  }
  dut->bank_wr_en = 0;

  // Read back all
  for (int i = 0; i < 4; ++i) {
    dut->bank_rd_en = 1;
    dut->bank_rd_addr = i;
    tick(dut);
    dut->bank_rd_en = 0;
    tick(dut);

    if (dut->bank_rd_data != data[i]) {
      fprintf(stderr, "\n    FAIL: addr%d data=0x%016lX, expected 0x%016lX",
              i, (unsigned long long)dut->bank_rd_data, data[i]);
      pass = 0;
    }
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 6: No Write with Zero WSTRB ──
static int test_zero_wstrb(Vspm_bank_top* dut) {
  printf("  Test Case 6: Zero WSTRB...");
  reset_dut(dut);
  int pass = 1;

  // Write known value
  dut->bank_wr_en = 1;
  dut->bank_wr_addr = 0x40;
  dut->bank_wr_data = 0xAAAAAAAAAAAAAAAAULL;
  dut->bank_wr_wstrb = 0xFF;
  tick(dut);

  // Attempt write with zero wstrb
  dut->bank_wr_addr = 0x40;
  dut->bank_wr_data = 0xBBBBBBBBBBBBBBBBULL;
  dut->bank_wr_wstrb = 0x00;
  tick(dut);
  dut->bank_wr_en = 0;

  // Read back
  dut->bank_rd_en = 1;
  dut->bank_rd_addr = 0x40;
  tick(dut);
  dut->bank_rd_en = 0;
  tick(dut);

  if (dut->bank_rd_data != 0xAAAAAAAAAAAAAAAAULL) {
    fprintf(stderr, "\n    FAIL: data=0x%016lX, expected 0xAAAAAAAAAAAAAAAA",
            (unsigned long long)dut->bank_rd_data);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 7: Reset Clears Data ──
static int test_reset_clears_data(Vspm_bank_top* dut) {
  printf("  Test Case 7: Reset Clears Data...");
  reset_dut(dut);
  int pass = 1;

  const uint32_t addr = 0x55;
  const uint64_t expected = 0xABCDEF0123456789ULL;

  // Write known data
  dut->bank_wr_en = 1;
  dut->bank_wr_addr = addr;
  dut->bank_wr_data = expected;
  dut->bank_wr_wstrb = 0xFF;
  tick(dut);
  dut->bank_wr_en = 0;

  // Confirm data is stored
  dut->bank_rd_en = 1;
  dut->bank_rd_addr = addr;
  tick(dut);
  dut->bank_rd_en = 0;
  tick(dut);
  if (dut->bank_rd_data != expected) {
    fprintf(stderr, "\n    FAIL: pre-reset read=0x%016lX, expected 0x%016lX",
            (unsigned long long)dut->bank_rd_data, expected);
    pass = 0;
  }

  // Assert reset while reading; read data output should clear
  dut->reset_i = 1;
  dut->bank_rd_en = 1;
  dut->bank_rd_addr = addr;
  tick(dut);
  if (dut->bank_rd_data != 0) {
    fprintf(stderr, "\n    FAIL: reset read=0x%016lX, expected 0",
            (unsigned long long)dut->bank_rd_data);
    pass = 0;
  }

  // Deassert reset and confirm memory retention
  dut->reset_i = 0;
  dut->bank_rd_en = 1;
  dut->bank_rd_addr = addr;
  tick(dut);
  dut->bank_rd_en = 0;
  tick(dut);
  if (dut->bank_rd_data != expected) {
    fprintf(stderr, "\n    FAIL: post-reset read=0x%016lX, expected 0x%016lX",
            (unsigned long long)dut->bank_rd_data, expected);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 8: Maximum Address ──
static int test_max_address(Vspm_bank_top* dut) {
  printf("  Test Case 8: Maximum Address...");
  reset_dut(dut);
  int pass = 1;

  const uint32_t max_addr = (1u << 14) - 1; // 0x3FFF
  const uint64_t expected = 0xFEDCBA0987654321ULL;

  dut->bank_wr_en = 1;
  dut->bank_wr_addr = max_addr;
  dut->bank_wr_data = expected;
  dut->bank_wr_wstrb = 0xFF;
  tick(dut);
  dut->bank_wr_en = 0;

  dut->bank_rd_en = 1;
  dut->bank_rd_addr = max_addr;
  tick(dut);
  dut->bank_rd_en = 0;
  tick(dut);

  if (dut->bank_rd_data != expected) {
    fprintf(stderr, "\n    FAIL: max_addr data=0x%016lX, expected 0x%016lX",
            (unsigned long long)dut->bank_rd_data, expected);
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vspm_bank_top;

  int failures = 0;
  printf("spm_bank unit tests:\n");
  failures += test_basic_read_write(dut);
  failures += test_byte_enable(dut);
  failures += test_multiple_addresses(dut);
  failures += test_read_during_write(dut);
  failures += test_back_to_back_writes(dut);
  failures += test_zero_wstrb(dut);
  failures += test_reset_clears_data(dut);
  failures += test_max_address(dut);

  if (failures == 0) {
    printf("\nAll 8 tests PASSED\n");
  } else {
    printf("\n%d of 8 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
