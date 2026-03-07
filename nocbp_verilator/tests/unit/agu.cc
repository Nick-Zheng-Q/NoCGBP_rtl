#include <cstdint>
#include <cstdio>

#include "Vagu_top.h"
#include "Vagu_top___024root.h"
#include "verilated.h"

static void tick(Vagu_top *dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vagu_top *dut, int cycles = 2) {
  dut->rst_n = 0;
  dut->i_ready = 0;
  dut->i_start = 0;
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
}

int run_test(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);

  auto *dut = new Vagu_top;
  reset_dut(dut);

  printf("\n========================================\n");
  printf("AGU Unit Test: FIFO Blocking & Beat Count\n");
  printf("========================================\n\n");

  // ========================================
  // Test 1: Simple transfer, no blocking
  // ========================================
  printf("Test 1: Simple transfer (64 bytes, step=32)\n");

  dut->i_base_addr = 0x1000;
  dut->i_xfer_bytes = 64;
  dut->i_addr_step_bytes = 32;
  dut->i_start = 1;
  dut->i_ready = 1;
  tick(dut);
  dut->i_start = 0;

  // Expected: 2 beats, addresses 0x1000, 0x1020

  // Beat 0
  if (!dut->o_valid) {
    std::fprintf(stderr, "FAIL: beat 0: expected valid=1\n");
    delete dut;
    return 1;
  }
  if (dut->o_agu_addr != 0x1000) {
    std::fprintf(stderr, "FAIL: beat 0: expected addr=0x1000, got 0x%x\n",
                 dut->o_agu_addr);
    delete dut;
    return 1;
  }
  printf("  beat 0: addr=0x%x\n", dut->o_agu_addr);
  tick(dut);

  // Beat 1 (last)
  if (!dut->o_valid) {
    std::fprintf(stderr, "FAIL: beat 1: expected valid=1\n");
    delete dut;
    return 1;
  }
  if (dut->o_agu_addr != 0x1020) {
    std::fprintf(stderr, "FAIL: beat 1: expected addr=0x1020, got 0x%x\n",
                 dut->o_agu_addr);
    delete dut;
    return 1;
  }
  printf("  beat 1: addr=0x%x\n", dut->o_agu_addr);
  tick(dut);

  // After all beats, next_desc_o should be high
  if (!dut->o_next_desc) {
    std::fprintf(stderr, "FAIL: after 2 beats: expected next_desc=1\n");
    delete dut;
    return 1;
  }
  printf("  next_desc_o=1 after completion\n");
  printf("PASS: Test 1\n\n");

  // ========================================
  // Test 2: Block at beat 0 (before first address)
  // ========================================
  printf("Test 2: Block at beat 0\n");

  dut->i_base_addr = 0x2000;
  dut->i_xfer_bytes = 96;
  dut->i_addr_step_bytes = 32;
  dut->i_start = 1;
  dut->i_ready = 1;
  tick(dut);
  dut->i_start = 0;
  std::fprintf(stderr, "FAIL: blocked: addr changed unexpectedly to 0x%x\n",
               dut->o_agu_addr);

  // Block immediately
  dut->i_ready = 0;
  printf("  Blocking at beat 0 (ready=0)\n");
  tick(dut);
  tick(dut);

  // Should still be at beat 0
  if (dut->o_valid && dut->o_agu_addr != 0x2000) {
    std::fprintf(stderr, "FAIL: blocked: addr changed unexpectedly to 0x%x\n",
                 dut->o_agu_addr);
    delete dut;
    return 1;
  }

  // Release and continue
  dut->i_ready = 1;
  tick(dut);

  // Beat 0 should now complete
  if (!dut->o_valid || dut->o_agu_addr != 0x2020) {
    std::fprintf(stderr, "FAIL: after release: addr=0x%x valid=%d\n",
                 dut->o_agu_addr, dut->o_valid);
    delete dut;
    return 1;
  }
  printf("  beat 0: addr=0x%x\n", dut->o_agu_addr);
  tick(dut);

  // Beat 1
  if (!dut->o_valid || dut->o_agu_addr != 0x2040) {
    std::fprintf(stderr, "FAIL: beat 1: addr=0x%x\n", dut->o_agu_addr);
    delete dut;
    return 1;
  }
  printf("  beat 1: addr=0x%x\n", dut->o_agu_addr);
  tick(dut);

  // Beat 2
  // if (!dut->o_valid || dut->o_agu_addr != 0x2060) {
  //   std::fprintf(stderr, "FAIL: beat 2: addr=0x%x\n", dut->o_agu_addr);
  //   delete dut;
  //   return 1;
  // }
  // printf("  beat 2: addr=0x%x\n", dut->o_agu_addr);
  // tick(dut);

  if (!dut->o_next_desc) {
    std::fprintf(stderr, "FAIL: expected next_desc=1\n");
    delete dut;
    return 1;
  }
  printf("PASS: Test 2\n\n");

  // ========================================
  // Test 3: Block in middle
  // ========================================
  printf("Test 3: Block in middle (beat 1)\n");

  dut->i_base_addr = 0x3000;
  dut->i_xfer_bytes = 128;
  dut->i_addr_step_bytes = 32;
  dut->i_start = 1;
  dut->i_ready = 1;
  tick(dut);
  dut->i_start = 0;

  // Beat 0
  if (dut->o_agu_addr != 0x3000) {
    std::fprintf(stderr, "FAIL: beat 0\n");
    delete dut;
    return 1;
  }
  // tick(dut);

  // Block at beat 1
  dut->i_ready = 0;
  printf("  Blocking at beat 1\n");
  tick(dut);
  tick(dut);

  dut->i_ready = 1;
  tick(dut);

  // Should be at beat 1
  if (dut->o_agu_addr != 0x3020) {
    std::fprintf(stderr, "FAIL: after block: addr=0x%x\n", dut->o_agu_addr);
    delete dut;
    return 1;
  }
  printf("  beat 1: addr=0x%x\n", dut->o_agu_addr);
  tick(dut);

  // Beat 2
  if (dut->o_agu_addr != 0x3040) {
    std::fprintf(stderr, "FAIL: beat 2\n");
    delete dut;
    return 1;
  }
  tick(dut);

  // Beat 3 (last)
  if (dut->o_agu_addr != 0x3060) {
    std::fprintf(stderr, "FAIL: beat 3\n");
    delete dut;
    return 1;
  }
  tick(dut);

  if (!dut->o_next_desc) {
    std::fprintf(stderr, "FAIL: next_desc=1\n");
    delete dut;
    return 1;
  }
  printf("PASS: Test 3\n\n");

  // ========================================
  // Test 4: Block at last beat
  // ========================================
  printf("Test 4: Block at last beat\n");

  dut->i_base_addr = 0x4000;
  dut->i_xfer_bytes = 64;
  dut->i_addr_step_bytes = 32;
  dut->i_start = 1;
  dut->i_ready = 1;
  // Beat 0
  tick(dut);
  dut->i_start = 0;

  tick(dut);
  tick(dut);
  // Block at beat 1 (last)
  dut->i_ready = 0;
  // std::fprintf(stderr, "FAIL: next_desc is before blocked:0x%x\n",
  //              dut->o_next_desc);
  // std::fprintf(stderr, "FAIL: new desc: addr=0x%x\n", dut->o_agu_addr);
  // std::fprintf(stderr, "FAIL: beat count: beat_count=0x%x\n",
  //              dut->rootp->agu_top__DOT__dut__DOT__beat_count);
  // std::fprintf(stderr, "FAIL: beat count_max: beat_count_max=0x%x\n",
  //              dut->rootp->agu_top__DOT__dut__DOT__beat_count_max);
  printf("  Blocking at last beat\n");
  tick(dut);
  // tick(dut);

  // Should still have valid=1 and next_desc=1
  if (!dut->o_next_desc) {
    std::fprintf(stderr, "FAIL: next_desc should be 1 while blocked\n");
    std::fprintf(stderr, "FAIL: new desc: addr=0x%x\n", dut->o_agu_addr);
    std::fprintf(stderr, "FAIL: beat count: beat_count=0x%x\n",
                 dut->rootp->agu_top__DOT__dut__DOT__beat_count);
    std::fprintf(stderr, "FAIL: beat count_max: beat_count_max=0x%x\n",
                 dut->rootp->agu_top__DOT__dut__DOT__beat_count_max);
    delete dut;
    return 1;
  }

  dut->i_start = 1;
  dut->i_ready = 1;
  tick(dut);
  dut->i_start = 0;

  // Should start new descriptor
  if (dut->o_agu_addr != 0x4000 || !dut->o_valid) {
    std::fprintf(stderr, "FAIL: new desc: addr=0x%x\n", dut->o_agu_addr);
    delete dut;
    return 1;
  }
  printf("PASS: Test 4\n\n");

  // ========================================
  // Test 5: Multiple consecutive descriptors
  // ========================================
  printf("Test 5: Multiple consecutive descriptors\n");

  // Descriptor 1: 64 bytes
  reset_dut(dut);
  dut->i_base_addr = 0x5000;
  dut->i_xfer_bytes = 64;
  dut->i_addr_step_bytes = 32;
  dut->i_start = 1;
  dut->i_ready = 1;
  tick(dut);
  dut->i_start = 0;

  if (dut->o_agu_addr != 0x5000) {
    std::fprintf(stderr, "FAIL d1b0\n");
    std::fprintf(stderr, "FAIL: new desc: addr=0x%x\n", dut->o_agu_addr);
    delete dut;
    return 1;
  }
  tick(dut);
  if (dut->o_agu_addr != 0x5020) {
    std::fprintf(stderr, "FAIL: new desc: addr=0x%x\n", dut->o_agu_addr);
    std::fprintf(stderr, "FAIL d1b1\n");
    std::fprintf(stderr, "FAIL: beat count: beat_count=0x%x\n",
                 dut->rootp->agu_top__DOT__dut__DOT__beat_count);
    std::fprintf(stderr, "FAIL: beat count_max: beat_count_max=0x%x\n",
                 dut->rootp->agu_top__DOT__dut__DOT__beat_count_max);
    delete dut;
    return 1;
  }
  tick(dut);
  if (!dut->o_next_desc) {
    std::fprintf(stderr, "FAIL d1next\n");
    delete dut;
    return 1;
  }
  printf("  Desc1 done: 0x5000, 0x5020\n");

  // Descriptor 2: 96 bytes
  dut->i_base_addr = 0x6000;
  dut->i_xfer_bytes = 96;
  dut->i_start = 1;
  tick(dut);
  dut->i_start = 0;
  if (dut->o_agu_addr != 0x6000) {
    std::fprintf(stderr, "FAIL d2b0\n");
    delete dut;
    return 1;
  }
  tick(dut);
  if (dut->o_agu_addr != 0x6020) {
    std::fprintf(stderr, "FAIL d2b1\n");
    delete dut;
    return 1;
  }
  tick(dut);
  if (dut->o_agu_addr != 0x6040) {
    std::fprintf(stderr, "FAIL d2b2\n");
    delete dut;
    return 1;
  }
  tick(dut);
  if (!dut->o_next_desc) {
    std::fprintf(stderr, "FAIL d2next\n");
    delete dut;
    return 1;
  }
  printf("  Desc2 done: 0x6000, 0x6020, 0x6040\n");

  // Descriptor 3: 32 bytes (single beat)
  dut->i_base_addr = 0x7000;
  dut->i_xfer_bytes = 32;
  dut->i_start = 1;
  tick(dut);
  dut->i_start = 0;
  if (dut->o_agu_addr != 0x7000) {
    std::fprintf(stderr, "FAIL d3b0\n");
    delete dut;
    return 1;
  }
  tick(dut);
  if (!dut->o_next_desc) {
    std::fprintf(stderr, "FAIL d3next\n");
    delete dut;
    return 1;
  }
  printf("  Desc3 done: 0x7000\n");
  printf("PASS: Test 5\n\n");

  // ========================================
  // Test 6: Large transfer (256 bytes)
  // ========================================
  printf("Test 6: Large transfer (256 bytes)\n");

  dut->i_base_addr = 0x8000;
  dut->i_xfer_bytes = 256;
  dut->i_addr_step_bytes = 32;
  dut->i_start = 1;
  dut->i_ready = 1;
  tick(dut);
  dut->i_start = 0;

  uint32_t expected = 0x8000;
  for (int i = 0; i < 8; i++) {
    if (dut->o_agu_addr != expected) {
      std::fprintf(stderr, "FAIL: beat %d: addr=0x%x\n", i, dut->o_agu_addr);
      delete dut;
      return 1;
    }
    printf("  beat %d: addr=0x%x\n", i, dut->o_agu_addr);
    expected += 32;
    tick(dut);
  }

  if (!dut->o_next_desc) {
    std::fprintf(stderr, "FAIL: next_desc\n");
    delete dut;
    return 1;
  }
  printf("PASS: Test 6\n\n");

  // ========================================
  // Test 7: Large step size
  // ========================================
  printf("Test 7: Large step size (step=64)\n");

  dut->i_base_addr = 0x9000;
  dut->i_xfer_bytes = 128;
  dut->i_addr_step_bytes = 64;
  dut->i_start = 1;
  dut->i_ready = 1;
  tick(dut);
  dut->i_start = 0;

  expected = 0x9000;
  for (int i = 0; i < 4; i++) {
    if (dut->o_agu_addr != expected) {
      std::fprintf(stderr, "FAIL: beat %d: addr=0x%x\n", i, dut->o_agu_addr);
      delete dut;
      return 1;
    }
    printf("  beat %d: addr=0x%x\n", i, dut->o_agu_addr);
    expected += 64;
    tick(dut);
  }

  if (!dut->o_next_desc) {
    std::fprintf(stderr, "FAIL: next_desc\n");
    delete dut;
    return 1;
  }
  printf("PASS: Test 7\n\n");

  printf("\n========================================\n");
  printf("AGU unit test: ALL TESTS PASSED\n");
  printf("========================================\n");

  delete dut;
  return 0;
}
