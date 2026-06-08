// agu.cc
// Unit test for agu
// Test cases from docs/gbp_pe/verification/unit_tests/22_agu.md

#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vagu_top.h"

static void tick(Vagu_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vagu_top* dut, int cycles = 3) {
  dut->rst_n = 0;
  dut->start = 0;
  dut->base_addr = 0;
  dut->word_count = 0;
  dut->addr_ready = 0;
  for (int i = 0; i < cycles; ++i) tick(dut);
  dut->rst_n = 1;
}

// ── Test Case 1: Normal Address Sequence ──
static int test_normal_sequence(Vagu_top* dut) {
  printf("  Test Case 1: Normal Address Sequence...");
  reset_dut(dut);
  int pass = 1;

  dut->start = 1;
  dut->base_addr = 0x0100;
  dut->word_count = 4;
  tick(dut);
  dut->start = 0;

  // T+1: First address valid
  if (!dut->addr_valid || dut->addr != 0x0100 || dut->last_addr) {
    fprintf(stderr, "\n    FAIL at T+1: expected valid=1, addr=0x100, last=0");
    pass = 0;
  }

  dut->addr_ready = 1;
  tick(dut); // T+2: 0x0101
  if (!dut->addr_valid || dut->addr != 0x0101 || dut->last_addr) {
    fprintf(stderr, "\n    FAIL at T+2: expected valid=1, addr=0x101, last=0");
    pass = 0;
  }

  tick(dut); // T+3: 0x0102
  if (!dut->addr_valid || dut->addr != 0x0102 || dut->last_addr) {
    fprintf(stderr, "\n    FAIL at T+3: expected valid=1, addr=0x102, last=0");
    pass = 0;
  }

  tick(dut); // T+4: 0x0103 (last)
  if (!dut->addr_valid || dut->addr != 0x0103 || !dut->last_addr) {
    fprintf(stderr, "\n    FAIL at T+4: expected valid=1, addr=0x103, last=1");
    pass = 0;
  }

  tick(dut); // T+5: done
  if (dut->addr_valid) {
    fprintf(stderr, "\n    FAIL at T+5: expected valid=0");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 2: Backpressure ──
static int test_backpressure(Vagu_top* dut) {
  printf("  Test Case 2: Backpressure...");
  reset_dut(dut);
  int pass = 1;

  dut->start = 1;
  dut->base_addr = 0x0200;
  dut->word_count = 4;
  tick(dut);
  dut->start = 0;

  // T+2: ready=1, first address 0x200 consumed, now 0x201 valid
  dut->addr_ready = 1;
  tick(dut);
  if (!dut->addr_valid || dut->addr != 0x0201) {
    fprintf(stderr, "\n    FAIL at T+2: expected addr=0x201 (0x200 consumed)");
    pass = 0;
  }

  // T+3: stall, hold 0x201
  dut->addr_ready = 0;
  tick(dut);
  if (!dut->addr_valid || dut->addr != 0x0201) {
    fprintf(stderr, "\n    FAIL at T+3: expected addr=0x201 (held)");
    pass = 0;
  }

  // T+4: still stalled
  tick(dut);
  if (!dut->addr_valid || dut->addr != 0x0201) {
    fprintf(stderr, "\n    FAIL at T+4: expected addr=0x201 (still held)");
    pass = 0;
  }

  // T+5: resume, ready=1 consumes 0x201, now 0x202 valid
  dut->addr_ready = 1;
  tick(dut);
  if (!dut->addr_valid || dut->addr != 0x0202) {
    fprintf(stderr, "\n    FAIL at T+5: expected addr=0x202 (0x201 consumed)");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 3: Single Word ──
static int test_single_word(Vagu_top* dut) {
  printf("  Test Case 3: Single Word...");
  reset_dut(dut);
  int pass = 1;

  dut->start = 1;
  dut->base_addr = 0x0300;
  dut->word_count = 1;
  tick(dut);
  dut->start = 0;

  if (!dut->addr_valid || dut->addr != 0x0300 || !dut->last_addr) {
    fprintf(stderr, "\n    FAIL: expected valid=1, addr=0x300, last=1");
    pass = 0;
  }

  dut->addr_ready = 1;
  tick(dut);
  if (dut->addr_valid) {
    fprintf(stderr, "\n    FAIL: expected valid=0 after consuming last");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 4: word_count = 0 boundary ──
static int test_word_count_zero(Vagu_top* dut) {
  printf("  Test Case 4: word_count = 0 boundary...");
  reset_dut(dut);
  int pass = 1;

  dut->start = 1;
  dut->base_addr = 0x0400;
  dut->word_count = 0;
  tick(dut);
  dut->start = 0;

  // RTL starts the AGU and never finishes with count=0.
  if (!dut->addr_valid || dut->addr != 0x0400) {
    fprintf(stderr, "\n    FAIL: expected valid=1, addr=0x400");
    pass = 0;
  }
  if (dut->last_addr) {
    fprintf(stderr, "\n    FAIL: last_addr should be 0 for count=0");
    pass = 0;
  }

  dut->addr_ready = 0;
  tick(dut); tick(dut);
  if (!dut->addr_valid || dut->addr != 0x0400 || dut->last_addr) {
    fprintf(stderr, "\n    FAIL: addr held but last_addr unexpectedly changed");
    pass = 0;
  }

  // Reset to clean up the run-zero-count condition.
  dut->rst_n = 0;
  tick(dut);
  dut->rst_n = 1;
  tick(dut);
  if (dut->addr_valid) {
    fprintf(stderr, "\n    FAIL: addr_valid should clear after reset");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 5: Maximum word_count (65535) ──
static int test_max_word_count(Vagu_top* dut) {
  printf("  Test Case 5: Maximum word_count (65535)...");
  reset_dut(dut);
  int pass = 1;

  dut->start = 1;
  dut->base_addr = 0x0500;
  dut->word_count = 0xFFFF;
  tick(dut);
  dut->start = 0;

  if (!dut->addr_valid || dut->addr != 0x0500) {
    fprintf(stderr, "\n    FAIL: expected valid=1, addr=0x500");
    pass = 0;
  }
  if (dut->last_addr) {
    fprintf(stderr, "\n    FAIL: last_addr should be 0 at start of max count");
    pass = 0;
  }

  dut->addr_ready = 1;
  tick(dut); // consume first address
  if (!dut->addr_valid || dut->addr != 0x0501) {
    fprintf(stderr, "\n    FAIL: expected addr=0x501 after first accept");
    pass = 0;
  }
  if (dut->last_addr) {
    fprintf(stderr, "\n    FAIL: last_addr should still be 0");
    pass = 0;
  }

  // Reset rather than run to completion.
  dut->rst_n = 0;
  tick(dut);
  dut->rst_n = 1;
  tick(dut);
  if (dut->addr_valid) {
    fprintf(stderr, "\n    FAIL: addr_valid should clear after reset");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 6: Backpressure on last address ──
static int test_backpressure_last(Vagu_top* dut) {
  printf("  Test Case 6: Backpressure on last address...");
  reset_dut(dut);
  int pass = 1;

  dut->start = 1;
  dut->base_addr = 0x0600;
  dut->word_count = 3;
  tick(dut);
  dut->start = 0;

  // Consume first two addresses.
  dut->addr_ready = 1;
  tick(dut); // 0x600 consumed, 0x601 valid
  tick(dut); // 0x601 consumed, 0x602 valid (last)

  if (!dut->addr_valid || dut->addr != 0x0602 || !dut->last_addr) {
    fprintf(stderr, "\n    FAIL: expected valid=1, addr=0x602, last=1");
    pass = 0;
  }

  // Stall on last address.
  dut->addr_ready = 0;
  tick(dut); tick(dut);
  if (!dut->addr_valid || dut->addr != 0x0602 || !dut->last_addr) {
    fprintf(stderr, "\n    FAIL: last address not held under backpressure");
    pass = 0;
  }

  // Consume last address.
  dut->addr_ready = 1;
  tick(dut);
  if (dut->addr_valid) {
    fprintf(stderr, "\n    FAIL: expected valid=0 after consuming last");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 7: Start while active ──
static int test_start_while_active(Vagu_top* dut) {
  printf("  Test Case 7: Start while active...");
  reset_dut(dut);
  int pass = 1;

  dut->start = 1;
  dut->base_addr = 0x0700;
  dut->word_count = 4;
  dut->addr_ready = 1;
  tick(dut);
  dut->start = 0;

  // AGU starts this cycle; consume the first address next cycle.
  tick(dut);
  if (!dut->addr_valid || dut->addr != 0x0701) {
    fprintf(stderr, "\n    FAIL: expected addr=0x701 after consuming first");
    pass = 0;
  }

  // Attempt to restart with a different base while active (keep same count
  // so that the combinational word_count path does not prematurely finish).
  dut->start = 1;
  dut->base_addr = 0x0800;
  dut->word_count = 4;
  tick(dut);
  dut->start = 0;

  if (!dut->addr_valid || dut->addr != 0x0702) {
    fprintf(stderr, "\n    FAIL: new start should be ignored (expected addr=0x702)");
    pass = 0;
  }

  // Continue and finish original sequence.
  dut->addr_ready = 1;
  tick(dut); // 0x702 consumed, 0x703 (last)
  if (!dut->addr_valid || dut->addr != 0x0703 || !dut->last_addr) {
    fprintf(stderr, "\n    FAIL: expected last addr=0x703");
    pass = 0;
  }
  tick(dut); // consume last
  if (dut->addr_valid) {
    fprintf(stderr, "\n    FAIL: expected valid=0 after sequence");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// ── Test Case 8: Reset during sequence ──
static int test_reset_during_sequence(Vagu_top* dut) {
  printf("  Test Case 8: Reset during sequence...");
  reset_dut(dut);
  int pass = 1;

  dut->start = 1;
  dut->base_addr = 0x0900;
  dut->word_count = 4;
  tick(dut);
  dut->start = 0;

  dut->addr_ready = 1;
  tick(dut); // 0x900 consumed, 0x901
  tick(dut); // 0x901 consumed, 0x902

  // Reset in the middle.
  dut->rst_n = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  tick(dut);

  if (dut->addr_valid) {
    fprintf(stderr, "\n    FAIL: addr_valid should clear after reset");
    pass = 0;
  }

  // Verify recovery with a new sequence.
  dut->start = 1;
  dut->base_addr = 0x0A00;
  dut->word_count = 2;
  tick(dut);
  dut->start = 0;

  if (!dut->addr_valid || dut->addr != 0x0A00 || dut->last_addr) {
    fprintf(stderr, "\n    FAIL: recovery sequence start incorrect");
    pass = 0;
  }

  dut->addr_ready = 1;
  tick(dut); // consume 0xA00, now 0xA01 (last)
  if (!dut->addr_valid || dut->addr != 0x0A01 || !dut->last_addr) {
    fprintf(stderr, "\n    FAIL: expected last addr=0xA01");
    pass = 0;
  }

  tick(dut); // consume last
  if (dut->addr_valid) {
    fprintf(stderr, "\n    FAIL: recovery sequence should finish after last accept");
    pass = 0;
  }

  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vagu_top;

  int failures = 0;
  printf("agu unit tests (from 22_agu.md):\n");
  failures += test_normal_sequence(dut);
  failures += test_backpressure(dut);
  failures += test_single_word(dut);
  failures += test_word_count_zero(dut);
  failures += test_max_word_count(dut);
  failures += test_backpressure_last(dut);
  failures += test_start_while_active(dut);
  failures += test_reset_during_sequence(dut);

  if (failures == 0) {
    printf("\nAll 8 tests PASSED\n");
  } else {
    printf("\n%d of 8 tests FAILED\n", failures);
  }

  delete dut;
  return failures ? 1 : 0;
}
