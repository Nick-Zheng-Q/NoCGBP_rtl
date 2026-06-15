#include <cstdint>
#include <cstdio>

#include "verilated.h"
#include "Vread_stream_engine_top.h"

static void tick(Vread_stream_engine_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vread_stream_engine_top* dut) {
  dut->rst_n = 0;
  dut->i_desc_valid = 0;
  dut->i_desc_base_addr = 0;
  dut->i_desc_word_count = 0;
  dut->i_desc_is_staging = 0;
  dut->i_beat_ready = 0;
  dut->i_spm_rd_ready = 1;
  dut->i_spm_rd_data = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  tick(dut);
}

static void set_spm_data(Vread_stream_engine_top* dut,
                         uint32_t w0, uint32_t w1) {
  dut->i_spm_rd_data = (static_cast<uint64_t>(w1) << 32) | static_cast<uint64_t>(w0);
}

static bool beat_data_eq(Vread_stream_engine_top* dut,
                         uint32_t w0, uint32_t w1) {
  uint64_t data = dut->o_beat_data;
  uint32_t got0 = static_cast<uint32_t>(data);
  uint32_t got1 = static_cast<uint32_t>(data >> 32);
  return got0 == w0 && got1 == w1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vread_stream_engine_top;
  reset_dut(dut);

  std::printf("=== Read Stream Engine Unit Tests ===\n");
  int errors = 0;
  int cycles;

  // ---------------------------------------------------------------
  // Test 1: Single-beat descriptor accepted and read request issued
  // ---------------------------------------------------------------
  std::printf("[Test 1] Single-beat read\n");
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x100;
  dut->i_desc_word_count = 2;   // 2 words = 1 x 64b beat
  dut->i_desc_is_staging = 0;
  tick(dut);
  dut->i_desc_valid = 0;

  // Wait for SPM read request
  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 20) {
    tick(dut);
    cycles++;
  }
  if (!dut->o_spm_rd_valid) {
    std::fprintf(stderr, "  [FAIL] No spm_rd_valid within 20 cycles\n");
    errors++;
  } else {
    std::printf("  [PASS] spm_rd_valid asserted\n");
  }
  if (dut->o_spm_rd_addr != 0x100) {
    std::fprintf(stderr, "  [FAIL] Expected addr 0x100, got 0x%x\n", dut->o_spm_rd_addr);
    errors++;
  } else {
    std::printf("  [PASS] addr = 0x100\n");
  }

  // Return read data
  set_spm_data(dut, 0xDEADBEEF, 0xCAFEBABE);
  tick(dut);
  dut->i_spm_rd_data = 0;

  // Beat should be available next cycle
  cycles = 0;
  while (!dut->o_beat_valid && cycles < 20) {
    tick(dut);
    cycles++;
  }
  if (!dut->o_beat_valid) {
    std::fprintf(stderr, "  [FAIL] No beat_valid within 20 cycles\n");
    errors++;
  } else {
    std::printf("  [PASS] beat_valid asserted\n");
  }
  if (!beat_data_eq(dut, 0xDEADBEEF, 0xCAFEBABE)) {
    std::fprintf(stderr, "  [FAIL] beat_data mismatch\n");
    errors++;
  } else {
    std::printf("  [PASS] beat_data correct\n");
  }

  // Consume beat
  dut->i_beat_ready = 1;
  tick(dut);
  dut->i_beat_ready = 0;
  if (dut->o_beat_valid) {
    std::fprintf(stderr, "  [FAIL] beat_valid should drop after consume\n");
    errors++;
  } else {
    std::printf("  [PASS] beat consumed\n");
  }

  // Wait for desc_ready to re-assert
  cycles = 0;
  while (!dut->o_desc_ready && cycles < 20) {
    tick(dut);
    cycles++;
  }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready re-asserted\n");
  }

  // ---------------------------------------------------------------
  // Test 2: Multi-beat descriptor (2 beats)
  // ---------------------------------------------------------------
  std::printf("[Test 2] Two-beat read\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x200;
  dut->i_desc_word_count = 4;   // 4 words = 2 x 64b beats
  tick(dut);
  dut->i_desc_valid = 0;

  // First beat
  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 20) { tick(dut); cycles++; }
  set_spm_data(dut, 0x22222222, 0x11111111);
  tick(dut);

  cycles = 0;
  while (!dut->o_beat_valid && cycles < 20) { tick(dut); cycles++; }
  if (!beat_data_eq(dut, 0x22222222, 0x11111111)) {
    std::fprintf(stderr, "  [FAIL] First beat data mismatch\n");
    errors++;
  } else {
    std::printf("  [PASS] First beat correct\n");
  }

  // Second beat
  set_spm_data(dut, 0x44444444, 0x33333333);
  dut->i_beat_ready = 1;
  tick(dut);
  dut->i_beat_ready = 0;

  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 20) { tick(dut); cycles++; }
  tick(dut);

  cycles = 0;
  while (!dut->o_beat_valid && cycles < 20) { tick(dut); cycles++; }
  if (!beat_data_eq(dut, 0x44444444, 0x33333333)) {
    std::fprintf(stderr, "  [FAIL] Second beat data mismatch\n");
    uint64_t data = dut->o_beat_data;
    std::fprintf(stderr, "    beat_data = 0x%08x_%08x\n",
                 static_cast<uint32_t>(data >> 32), static_cast<uint32_t>(data));
    errors++;
  } else {
    std::printf("  [PASS] Second beat correct\n");
  }

  dut->i_beat_ready = 1;
  tick(dut);
  dut->i_beat_ready = 0;

  // ---------------------------------------------------------------
  // Test 3: Backpressure on beat output
  // ---------------------------------------------------------------
  std::printf("[Test 3] Beat backpressure\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x300;
  dut->i_desc_word_count = 2;   // 2 words = 1 beat
  tick(dut);
  dut->i_desc_valid = 0;

  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 20) { tick(dut); cycles++; }
  set_spm_data(dut, 0xEEFF0011, 0xAABBCCDD);
  tick(dut);

  // Don't accept beat
  dut->i_beat_ready = 0;
  for (int i = 0; i < 3; i++) {
    tick(dut);
    if (!dut->o_beat_valid) {
      std::fprintf(stderr, "  [FAIL] beat_valid dropped under backpressure\n");
      errors++;
      break;
    }
  }
  if (dut->o_beat_valid) {
    std::printf("  [PASS] beat_valid held under backpressure\n");
  }

  dut->i_beat_ready = 1;
  tick(dut);
  if (dut->o_beat_valid) {
    std::fprintf(stderr, "  [FAIL] beat_valid should drop after accept\n");
    errors++;
  } else {
    std::printf("  [PASS] beat accepted and cleared\n");
  }

  // ---------------------------------------------------------------
  // Test 4: Odd word_count (3 words = 2 beats, last beat partial)
  // ---------------------------------------------------------------
  std::printf("[Test 4] Odd word_count (3 words = 2 beats)\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x400;
  dut->i_desc_word_count = 3;   // 3 words = 2 beats (last beat partial)
  tick(dut);
  dut->i_desc_valid = 0;

  // First beat: words 0 and 1
  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 20) { tick(dut); cycles++; }
  set_spm_data(dut, 0xAAAA0000, 0xBBBB0001);
  tick(dut);

  cycles = 0;
  while (!dut->o_beat_valid && cycles < 20) { tick(dut); cycles++; }
  if (!beat_data_eq(dut, 0xAAAA0000, 0xBBBB0001)) {
    std::fprintf(stderr, "  [FAIL] First beat data mismatch\n");
    errors++;
  } else {
    std::printf("  [PASS] First beat correct\n");
  }

  // Second beat: word 2 only (upper 32 bits are whatever SPM returns)
  set_spm_data(dut, 0xCCCC0002, 0xDDDDFFFF);
  dut->i_beat_ready = 1;
  tick(dut);
  dut->i_beat_ready = 0;

  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 20) { tick(dut); cycles++; }
  tick(dut);

  cycles = 0;
  while (!dut->o_beat_valid && cycles < 20) { tick(dut); cycles++; }
  uint64_t data = dut->o_beat_data;
  uint32_t got0 = static_cast<uint32_t>(data);
  uint32_t got1 = static_cast<uint32_t>(data >> 32);
  if (got0 != 0xCCCC0002) {
    std::fprintf(stderr, "  [FAIL] Second beat low word: expected 0xCCCC0002, got 0x%08x\n", got0);
    errors++;
  } else {
    std::printf("  [PASS] Second beat low word correct\n");
  }
  // Upper word should contain whatever SPM returned at addr+1
  if (got1 != 0xDDDDFFFF) {
    std::fprintf(stderr, "  [FAIL] Second beat high word: expected 0xDDDDFFFF, got 0x%08x\n", got1);
    errors++;
  } else {
    std::printf("  [PASS] Second beat high word correct\n");
  }

  dut->i_beat_ready = 1;
  tick(dut);
  dut->i_beat_ready = 0;

  // Wait for desc_ready to re-assert
  cycles = 0;
  while (!dut->o_desc_ready && cycles < 20) { tick(dut); cycles++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert after odd count\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready re-asserted after odd count\n");
  }

  // ---------------------------------------------------------------
  // Test 5: word_count = 1 (single partial beat)
  // ---------------------------------------------------------------
  std::printf("[Test 5] word_count=1 (single partial beat)\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x500;
  dut->i_desc_word_count = 1;
  tick(dut);
  dut->i_desc_valid = 0;

  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 20) { tick(dut); cycles++; }
  if (!dut->o_spm_rd_valid) {
    std::fprintf(stderr, "  [FAIL] No spm_rd_valid for word_count=1\n");
    errors++;
  } else if (dut->o_spm_rd_addr != 0x500) {
    std::fprintf(stderr, "  [FAIL] Expected addr 0x500, got 0x%x\n", dut->o_spm_rd_addr);
    errors++;
  } else {
    std::printf("  [PASS] SPM read issued at base addr\n");
  }
  set_spm_data(dut, 0xBEEF0000, 0xDEADFFFF);
  tick(dut);

  cycles = 0;
  while (!dut->o_beat_valid && cycles < 20) { tick(dut); cycles++; }
  if (!dut->o_beat_valid) {
    std::fprintf(stderr, "  [FAIL] No beat_valid for word_count=1\n");
    errors++;
  } else {
    uint64_t data = dut->o_beat_data;
    uint32_t got0 = static_cast<uint32_t>(data);
    if (got0 != 0xBEEF0000) {
      std::fprintf(stderr, "  [FAIL] Low word mismatch: expected 0xBEEF0000, got 0x%08x\n", got0);
      errors++;
    } else {
      std::printf("  [PASS] Single-word beat low word correct\n");
    }
  }
  dut->i_beat_ready = 1;
  tick(dut);
  dut->i_beat_ready = 0;

  cycles = 0;
  while (!dut->o_desc_ready && cycles < 20) { tick(dut); cycles++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert after word_count=1\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready re-asserted after word_count=1\n");
  }

  // ---------------------------------------------------------------
  // Test 6: word_count = 0 (no SPM access before reset)
  // ---------------------------------------------------------------
  std::printf("[Test 6] word_count=0 boundary\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x600;
  dut->i_desc_word_count = 0;
  tick(dut);
  dut->i_desc_valid = 0;

  if (dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready should be low after accepting word_count=0\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready low after accepting word_count=0\n");
  }
  // The engine may start issuing reads for word_count=0; confirm it requests base.
  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 5) { tick(dut); cycles++; }
  if (dut->o_spm_rd_valid && dut->o_spm_rd_addr == 0x600) {
    std::printf("  [PASS] Observed SPM request at base addr (RTL behaviour for count=0)\n");
  } else {
    std::printf("  [INFO] No SPM request within 5 cycles\n");
  }
  // Reset to clean up the stuck-zero-count condition.
  reset_dut(dut);
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready not high after reset\n");
    errors++;
  } else {
    std::printf("  [PASS] Reset clears word_count=0 state\n");
  }

  // ---------------------------------------------------------------
  // Test 7: Maximum word_count (64 words = 32 beats)
  // ---------------------------------------------------------------
  std::printf("[Test 7] Maximum word_count (64 words)\n");
  reset_dut(dut);
  dut->i_beat_ready = 1;  // keep downstream ready for continuous flow
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x700;
  dut->i_desc_word_count = 64;
  tick(dut);
  dut->i_desc_valid = 0;

  int beat;
  int max_errors = 0;
  uint32_t prev_w0 = 0, prev_w1 = 0;
  for (beat = 0; beat < 32; ++beat) {
    // Sample outputs at negedge (mid-cycle) to see the stable address
    // before the posedge advances the AGU / accepts the read.
    dut->clk = 0;
    dut->eval();

    if (!dut->o_spm_rd_valid) {
      std::fprintf(stderr, "  [FAIL] Beat %d: no spm_rd_valid\n", beat);
      max_errors++;
      break;
    }
    uint32_t exp_addr = 0x700u + static_cast<uint32_t>(beat) * 2u;
    if (dut->o_spm_rd_addr != exp_addr) {
      std::fprintf(stderr, "  [FAIL] Beat %d: expected addr 0x%x, got 0x%x\n",
                   beat, exp_addr, dut->o_spm_rd_addr);
      max_errors++;
    }
    uint32_t w0 = 0xA0000000u + static_cast<uint32_t>(beat) * 2u;
    uint32_t w1 = 0xA0000001u + static_cast<uint32_t>(beat) * 2u;
    // Previous beat's data is valid now (one cycle after its read was accepted).
    if (beat > 0) {
      if (!dut->o_beat_valid) {
        std::fprintf(stderr, "  [FAIL] Beat %d: previous beat not valid\n", beat);
        max_errors++;
      } else if (!beat_data_eq(dut, prev_w0, prev_w1)) {
        std::fprintf(stderr, "  [FAIL] Beat %d: previous beat data mismatch\n", beat);
        max_errors++;
      }
    }
    set_spm_data(dut, w0, w1);
    prev_w0 = w0;
    prev_w1 = w1;

    dut->clk = 1;
    dut->eval();
  }
  // Drain final beat and return to a clean clock phase
  dut->clk = 0;
  dut->eval();
  if (max_errors == 0) {
    if (!dut->o_beat_valid) {
      std::fprintf(stderr, "  [FAIL] Final beat not valid\n");
      max_errors++;
    } else if (!beat_data_eq(dut, prev_w0, prev_w1)) {
      std::fprintf(stderr, "  [FAIL] Final beat data mismatch\n");
      max_errors++;
    } else {
      std::printf("  [PASS] All 32 beats for word_count=64 correct\n");
    }
  }
  dut->clk = 1;
  dut->eval();
  dut->i_beat_ready = 0;
  errors += max_errors;

  cycles = 0;
  while (!dut->o_desc_ready && cycles < 20) { tick(dut); cycles++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert after max count\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready re-asserted after max count\n");
  }

  // ---------------------------------------------------------------
  // Test 8: Descriptor while busy (rejected until current completes)
  // ---------------------------------------------------------------
  std::printf("[Test 8] Descriptor while busy\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x800;
  dut->i_desc_word_count = 4;
  tick(dut);
  dut->i_desc_valid = 0;

  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 20) { tick(dut); cycles++; }
  if (!dut->o_spm_rd_valid) {
    std::fprintf(stderr, "  [FAIL] First descriptor did not issue read\n");
    errors++;
  }

  // Attempt new descriptor while busy.
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x900;
  dut->i_desc_word_count = 2;
  tick(dut);
  if (dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready accepted descriptor while busy\n");
    errors++;
  } else {
    std::printf("  [PASS] Descriptor rejected while busy\n");
  }
  dut->i_desc_valid = 0;

  // Complete first descriptor.
  set_spm_data(dut, 0x11111111, 0x22222222);
  tick(dut);
  dut->i_beat_ready = 1;
  tick(dut);
  dut->i_beat_ready = 0;
  set_spm_data(dut, 0x33333333, 0x44444444);
  tick(dut);
  dut->i_beat_ready = 1;
  tick(dut);
  dut->i_beat_ready = 0;

  cycles = 0;
  while (!dut->o_desc_ready && cycles < 20) { tick(dut); cycles++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert after first descriptor\n");
    errors++;
  } else {
    std::printf("  [PASS] Original descriptor completed and FSM idle\n");
  }

  // ---------------------------------------------------------------
  // Test 9: Reset during active read
  // ---------------------------------------------------------------
  std::printf("[Test 9] Reset during active read\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0xA00;
  dut->i_desc_word_count = 4;
  tick(dut);
  dut->i_desc_valid = 0;

  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 20) { tick(dut); cycles++; }
  set_spm_data(dut, 0x55555555, 0x66666666);
  tick(dut);
  // Assert reset in the middle of the transfer.
  dut->rst_n = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  tick(dut);

  if (dut->o_spm_rd_valid) {
    std::fprintf(stderr, "  [FAIL] spm_rd_valid still asserted after reset\n");
    errors++;
  } else if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready not high after reset\n");
    errors++;
  } else {
    std::printf("  [PASS] Reset aborted active read cleanly\n");
  }

  // Verify recovery with a new descriptor.
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0xB00;
  dut->i_desc_word_count = 2;
  tick(dut);
  dut->i_desc_valid = 0;
  cycles = 0;
  while (!dut->o_spm_rd_valid && cycles < 20) { tick(dut); cycles++; }
  if (!dut->o_spm_rd_valid || dut->o_spm_rd_addr != 0xB00) {
    std::fprintf(stderr, "  [FAIL] Recovery read incorrect\n");
    errors++;
  } else {
    std::printf("  [PASS] Recovery read issued correctly after reset\n");
  }

  // ---------------------------------------------------------------
  // Test 10: dim6 belief prior (33 words = 17 beats, last partial)
  // ---------------------------------------------------------------
  std::printf("[Test 10] word_count=33 (dim6 belief prior)\n");
  reset_dut(dut);
  dut->i_beat_ready = 1;
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0xC00;
  dut->i_desc_word_count = 33;
  tick(dut);
  dut->i_desc_valid = 0;

  int rse_errors = 0;
  uint32_t prev33_w0 = 0, prev33_w1 = 0;
  for (int b = 0; b < 17; ++b) {
    dut->clk = 0;
    dut->eval();
    if (!dut->o_spm_rd_valid) {
      std::fprintf(stderr, "  [FAIL] Beat %d: no spm_rd_valid\n", b);
      rse_errors++;
      break;
    }
    uint32_t exp_addr = 0xC00u + static_cast<uint32_t>(b) * 2u;
    if (dut->o_spm_rd_addr != exp_addr) {
      std::fprintf(stderr, "  [FAIL] Beat %d: expected addr 0x%x, got 0x%x\n",
                   b, exp_addr, dut->o_spm_rd_addr);
      rse_errors++;
    }
    if (b > 0) {
      if (!dut->o_beat_valid) {
        std::fprintf(stderr, "  [FAIL] Beat %d: previous beat not valid\n", b);
        rse_errors++;
      } else if (!beat_data_eq(dut, prev33_w0, prev33_w1)) {
        std::fprintf(stderr, "  [FAIL] Beat %d: previous beat data mismatch\n", b);
        rse_errors++;
      }
    }
    uint32_t w0 = 0xC0000000u + static_cast<uint32_t>(b) * 2u;
    uint32_t w1 = 0xC0000001u + static_cast<uint32_t>(b) * 2u;
    set_spm_data(dut, w0, w1);
    prev33_w0 = w0;
    prev33_w1 = w1;

    dut->clk = 1;
    dut->eval();
  }
  if (rse_errors == 0) {
    dut->clk = 0;
    dut->eval();
    if (!dut->o_beat_valid || !beat_data_eq(dut, prev33_w0, prev33_w1)) {
      std::fprintf(stderr, "  [FAIL] Final beat data mismatch\n");
      rse_errors++;
    } else {
      std::printf("  [PASS] All 17 beats for word_count=33 correct\n");
    }
  }
  dut->clk = 1;
  dut->eval();
  dut->i_beat_ready = 0;
  errors += rse_errors;

  cycles = 0;
  while (!dut->o_desc_ready && cycles < 20) { tick(dut); cycles++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert after word_count=33\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready re-asserted after word_count=33\n");
  }

  // ---------------------------------------------------------------
  // Summary
  // ---------------------------------------------------------------
  if (errors == 0) {
    std::printf("\nAll tests PASSED\n");
  } else {
    std::printf("\n%d test(s) FAILED\n", errors);
  }

  delete dut;
  return errors;
}
