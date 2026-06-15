#include <cstdint>
#include <cstdio>

#include "verilated.h"
#include "Vwrite_stream_engine_top.h"

static void tick(Vwrite_stream_engine_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vwrite_stream_engine_top* dut) {
  dut->rst_n = 0;
  dut->i_desc_valid = 0;
  dut->i_desc_base_addr = 0;
  dut->i_desc_word_count = 0;
  dut->i_word_valid = 0;
  dut->i_word_data = 0;
  dut->i_spm_wr_ready = 1;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  tick(dut);
}

static uint32_t lo32(uint64_t v) { return static_cast<uint32_t>(v); }
static uint32_t hi32(uint64_t v) { return static_cast<uint32_t>(v >> 32); }

static int check_beat(Vwrite_stream_engine_top* dut, int beat,
                      uint32_t exp_addr, uint32_t exp0, uint32_t exp1,
                      uint32_t exp_wstrb, int* errors) {
  int cycles = 0;
  while (!dut->o_spm_wr_valid && cycles < 20) {
    tick(dut);
    cycles++;
  }
  if (!dut->o_spm_wr_valid) {
    std::fprintf(stderr, "  [FAIL] Beat %d: No spm_wr_valid within 20 cycles\n", beat);
    (*errors)++;
    return 0;
  }
  if (dut->o_spm_wr_addr != exp_addr) {
    std::fprintf(stderr, "  [FAIL] Beat %d: Expected addr 0x%x, got 0x%x\n",
                 beat, exp_addr, dut->o_spm_wr_addr);
    (*errors)++;
  }
  uint64_t data = dut->o_spm_wr_data;
  uint32_t got0 = lo32(data);
  uint32_t got1 = hi32(data);
  if (got0 != exp0 || got1 != exp1) {
    std::fprintf(stderr, "  [FAIL] Beat %d: data mismatch (got 0x%08x_%08x, expected 0x%08x_%08x)\n",
                 beat, got1, got0, exp1, exp0);
    (*errors)++;
  } else {
    std::printf("  [PASS] Beat %d: addr=0x%x data=0x%08x_%08x wstrb=0x%x\n",
                beat, dut->o_spm_wr_addr, got1, got0, dut->o_spm_wr_wstrb);
  }
  if (dut->o_spm_wr_wstrb != exp_wstrb) {
    std::fprintf(stderr, "  [FAIL] Beat %d: Expected wstrb 0x%x, got 0x%x\n",
                 beat, exp_wstrb, dut->o_spm_wr_wstrb);
    (*errors)++;
  }
  tick(dut);  // accept write
  return 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vwrite_stream_engine_top;
  reset_dut(dut);

  std::printf("=== Write Stream Engine Unit Tests ===\n");
  int errors = 0;

  // ---------------------------------------------------------------
  // Test 1: 8 words = 4 x 64b beats
  // ---------------------------------------------------------------
  std::printf("[Test 1] Four-beat write (8 words)\n");
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x200;
  dut->i_desc_word_count = 8;
  tick(dut);
  dut->i_desc_valid = 0;

  uint32_t words[8] = {
    0x11111111, 0x22222222, 0x33333333, 0x44444444,
    0x55555555, 0x66666666, 0x77777777, 0x88888888
  };
  int beat = 0;
  for (int i = 0; i < 8; i++) {
    dut->i_word_valid = 1;
    dut->i_word_data = words[i];
    tick(dut);
    dut->i_word_valid = 0;  // deassert so acceptance tick doesn't re-feed
    // A beat is assembled every 2 words; o_spm_wr_valid asserts on the
    // cycle after assembly. Check and accept before it gets overwritten.
    if (dut->o_spm_wr_valid) {
      uint32_t exp_addr = 0x200 + beat * 2;
      uint32_t exp0 = words[beat * 2];
      uint32_t exp1 = words[beat * 2 + 1];
      if (dut->o_spm_wr_addr != exp_addr) {
        std::fprintf(stderr, "  [FAIL] Beat %d: Expected addr 0x%x, got 0x%x\n",
                     beat, exp_addr, dut->o_spm_wr_addr);
        errors++;
      }
      uint64_t data = dut->o_spm_wr_data;
      uint32_t got0 = lo32(data);
      uint32_t got1 = hi32(data);
      if (got0 != exp0 || got1 != exp1) {
        std::fprintf(stderr, "  [FAIL] Beat %d: data mismatch (got 0x%08x_%08x, exp 0x%08x_%08x)\n",
                     beat, got1, got0, exp1, exp0);
        errors++;
      } else {
        std::printf("  [PASS] Beat %d: addr=0x%x data=0x%08x_%08x wstrb=0x%x\n",
                    beat, dut->o_spm_wr_addr, got1, got0, dut->o_spm_wr_wstrb);
      }
      if (dut->o_spm_wr_wstrb != 0xFF) {
        std::fprintf(stderr, "  [FAIL] Beat %d: wstrb mismatch\n", beat);
        errors++;
      }
      tick(dut);  // accept
      beat++;
    }
  }
  // Drain any remaining beat
  while (beat < 4 && dut->o_spm_wr_valid) {
    uint32_t exp_addr = 0x200 + beat * 2;
    uint32_t exp0 = words[beat * 2];
    uint32_t exp1 = words[beat * 2 + 1];
    if (dut->o_spm_wr_addr != exp_addr) {
      std::fprintf(stderr, "  [FAIL] Beat %d: addr mismatch\n", beat);
      errors++;
    }
    uint64_t data = dut->o_spm_wr_data;
    if (lo32(data) != exp0 || hi32(data) != exp1) {
      std::fprintf(stderr, "  [FAIL] Beat %d: data mismatch\n", beat);
      errors++;
    } else {
      std::printf("  [PASS] Beat %d: addr=0x%x data=0x%08x_%08x wstrb=0x%x\n",
                  beat, dut->o_spm_wr_addr, hi32(data), lo32(data), dut->o_spm_wr_wstrb);
    }
    tick(dut);
    beat++;
  }
  if (beat != 4) {
    std::fprintf(stderr, "  [FAIL] Only %d of 4 beats seen\n", beat);
    errors++;
  }

  // ---------------------------------------------------------------
  // Test 2: Partial beat (5 words = 3 beats: 2+2+1)
  // ---------------------------------------------------------------
  std::printf("[Test 2] Partial write (5 words = 3 beats)\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x300;
  dut->i_desc_word_count = 5;
  tick(dut);
  dut->i_desc_valid = 0;

  uint32_t pwords[5] = {0xA1B2C3D4, 0xE5F60718, 0x293A4B5C, 0x6D7E8F90, 0xA1B2C3D4};
  beat = 0;
  for (int i = 0; i < 5; i++) {
    dut->i_word_valid = 1;
    dut->i_word_data = pwords[i];
    tick(dut);
    dut->i_word_valid = 0;
    if (dut->o_spm_wr_valid) {
      uint32_t exp_addr = 0x300 + beat * 2;
      uint32_t exp0 = pwords[beat * 2];
      uint32_t exp1 = (beat < 2) ? pwords[beat * 2 + 1] : 0;
      uint32_t exp_wstrb = (beat < 2) ? 0xFF : 0x0F;
      if (dut->o_spm_wr_addr != exp_addr) {
        std::fprintf(stderr, "  [FAIL] Beat %d: addr mismatch\n", beat);
        errors++;
      }
      uint64_t data = dut->o_spm_wr_data;
      if (lo32(data) != exp0 || (beat < 2 && hi32(data) != exp1)) {
        std::fprintf(stderr, "  [FAIL] Beat %d: data mismatch\n", beat);
        errors++;
      } else {
        std::printf("  [PASS] Beat %d: partial=%d wstrb=0x%x\n",
                    beat, beat == 2, dut->o_spm_wr_wstrb);
      }
      if (dut->o_spm_wr_wstrb != exp_wstrb) {
        std::fprintf(stderr, "  [FAIL] Beat %d: wstrb mismatch (expected 0x%x)\n",
                     beat, exp_wstrb);
        errors++;
      }
      tick(dut);
      beat++;
    }
  }
  dut->i_word_valid = 0;
  while (beat < 3 && dut->o_spm_wr_valid) {
    tick(dut);
    beat++;
  }
  if (beat != 3) {
    std::fprintf(stderr, "  [FAIL] Only %d of 3 beats seen\n", beat);
    errors++;
  }

  // ---------------------------------------------------------------
  // Test 3: Backpressure on SPM write (first beat)
  // ---------------------------------------------------------------
  std::printf("[Test 3] SPM write backpressure\n");
  reset_dut(dut);
  dut->i_spm_wr_ready = 0;
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x400;
  dut->i_desc_word_count = 4;  // 4 words = 2 beats
  tick(dut);
  dut->i_desc_valid = 0;

  for (int i = 0; i < 4; i++) {
    dut->i_word_valid = 1;
    dut->i_word_data = 0xA5A55A5A;
    tick(dut);
  }
  dut->i_word_valid = 0;

  // Wait for first beat request
  int cycles = 0;
  while (!dut->o_spm_wr_valid && cycles < 20) { tick(dut); cycles++; }

  // Hold for 3 cycles with ready=0
  for (int i = 0; i < 3; i++) {
    tick(dut);
    if (!dut->o_spm_wr_valid) {
      std::fprintf(stderr, "  [FAIL] spm_wr_valid dropped under backpressure\n");
      errors++;
      break;
    }
  }
  if (dut->o_spm_wr_valid) {
    std::printf("  [PASS] spm_wr_valid held under backpressure\n");
  }

  dut->i_spm_wr_ready = 1;
  tick(dut);
  if (dut->o_spm_wr_valid) {
    std::fprintf(stderr, "  [FAIL] spm_wr_valid should drop after accept\n");
    errors++;
  } else {
    std::printf("  [PASS] first beat accepted\n");
  }

  // Accept second beat if present
  if (dut->o_spm_wr_valid) {
    tick(dut);
  }

  // -------------------------------------------------------------
  // Test 4: Single word (word_count=1, partial beat wstrb=0x0F)
  // -------------------------------------------------------------
  std::printf("[Test 4] Single word write (word_count=1)\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x500;
  dut->i_desc_word_count = 1;
  tick(dut);
  dut->i_desc_valid = 0;

  dut->i_word_valid = 1;
  dut->i_word_data = 0xDEADBEEF;
  tick(dut);
  dut->i_word_valid = 0;

  int cyc = 0;
  while (!dut->o_spm_wr_valid && cyc < 20) { tick(dut); cyc++; }
  if (!dut->o_spm_wr_valid) {
    std::fprintf(stderr, "  [FAIL] No spm_wr_valid within 20 cycles\n");
    errors++;
  } else {
    uint64_t data = dut->o_spm_wr_data;
    if (lo32(data) != 0xDEADBEEF) {
      std::fprintf(stderr, "  [FAIL] data low word mismatch (got 0x%08x)\n", lo32(data));
      errors++;
    }
    if (dut->o_spm_wr_wstrb != 0x0F) {
      std::fprintf(stderr, "  [FAIL] wstrb=0x%x, expected 0x0F\n", dut->o_spm_wr_wstrb);
      errors++;
    } else {
      std::printf("  [PASS] Single word written with wstrb=0x0F\n");
    }
    tick(dut);
  }

  // Wait for desc_ready to re-assert
  cyc = 0;
  while (!dut->o_desc_ready && cyc < 20) { tick(dut); cyc++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready re-asserted after single word\n");
  }

  // -------------------------------------------------------------
  // Test 5: dim6 belief writeback (34 words = 17 beats)
  // -------------------------------------------------------------
  std::printf("[Test 5] dim6 belief writeback (34 words = 17 beats)\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x120;
  dut->i_desc_word_count = 34;
  tick(dut);
  dut->i_desc_valid = 0;

  uint32_t dim6_words[34];
  for (int i = 0; i < 34; ++i) dim6_words[i] = 0xD0000000u + static_cast<uint32_t>(i);

  int dim6_beat = 0;
  int dim6_word = 0;
  int dim6_errors = 0;
  for (int cycle = 0; cycle < 100 && (dim6_word < 34 || dim6_beat < 17); ++cycle) {
    if (dim6_word < 34) {
      dut->i_word_valid = 1;
      dut->i_word_data = dim6_words[dim6_word];
      dim6_word++;
    } else {
      dut->i_word_valid = 0;
    }
    tick(dut);
    if (dut->o_spm_wr_valid) {
      uint32_t exp_addr = 0x120u + static_cast<uint32_t>(dim6_beat) * 2u;
      uint32_t exp0 = dim6_words[dim6_beat * 2];
      uint32_t exp1 = dim6_words[dim6_beat * 2 + 1];
      if (dut->o_spm_wr_addr != exp_addr) {
        std::fprintf(stderr, "  [FAIL] Beat %d: expected addr 0x%x, got 0x%x\n",
                     dim6_beat, exp_addr, dut->o_spm_wr_addr);
        dim6_errors++;
      }
      uint64_t data = dut->o_spm_wr_data;
      if (lo32(data) != exp0 || hi32(data) != exp1) {
        std::fprintf(stderr, "  [FAIL] Beat %d: data mismatch (got 0x%08x_%08x, exp 0x%08x_%08x)\n",
                     dim6_beat, hi32(data), lo32(data), exp1, exp0);
        dim6_errors++;
      } else {
        std::printf("  [PASS] Beat %d: addr=0x%x data=0x%08x_%08x wstrb=0x%x\n",
                    dim6_beat, dut->o_spm_wr_addr, hi32(data), lo32(data), dut->o_spm_wr_wstrb);
      }
      if (dut->o_spm_wr_wstrb != 0xFF) {
        std::fprintf(stderr, "  [FAIL] Beat %d: expected wstrb 0xFF, got 0x%x\n",
                     dim6_beat, dut->o_spm_wr_wstrb);
        dim6_errors++;
      }
      tick(dut);  // accept
      dim6_beat++;
    }
  }
  if (dim6_beat != 17) {
    std::fprintf(stderr, "  [FAIL] Only %d of 17 beats observed\n", dim6_beat);
    dim6_errors++;
  } else if (dim6_errors == 0) {
    std::printf("  [PASS] All 17 beats for dim6 belief written\n");
  }
  errors += dim6_errors;

  cyc = 0;
  while (!dut->o_desc_ready && cyc < 20) { tick(dut); cyc++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert after dim6 write\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready re-asserted after dim6 write\n");
  }

  // -------------------------------------------------------------
  // Test 6: FSM returns to IDLE (desc_ready re-asserts)
  // -------------------------------------------------------------
  std::printf("[Test 5] FSM returns to IDLE after transaction\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x600;
  dut->i_desc_word_count = 2;
  tick(dut);
  dut->i_desc_valid = 0;

  dut->i_word_valid = 1;
  dut->i_word_data = 0x11111111;
  tick(dut);
  dut->i_word_data = 0x22222222;
  tick(dut);
  dut->i_word_valid = 0;

  // Wait for write to complete
  cyc = 0;
  while (!dut->o_spm_wr_valid && cyc < 20) { tick(dut); cyc++; }
  if (dut->o_spm_wr_valid) tick(dut);  // accept

  // desc_ready may take a cycle to come back after last beat
  cyc = 0;
  while (!dut->o_desc_ready && cyc < 20) { tick(dut); cyc++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready=0 after transaction complete\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready=1 after transaction (FSM in IDLE)\n");
  }

  // -------------------------------------------------------------
  // Test 6: Maximum word_count (64 words = 32 beats)
  // -------------------------------------------------------------
  std::printf("[Test 6] Maximum word_count (64 words)\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x700;
  dut->i_desc_word_count = 64;
  tick(dut);
  dut->i_desc_valid = 0;

  uint32_t mwords[64];
  for (int i = 0; i < 64; ++i) mwords[i] = 0xB0000000u + static_cast<uint32_t>(i);

  int word_idx = 0;
  int mbeat = 0;
  int max_errors = 0;
  // Feed words and capture beats as they appear.
  for (int cycle = 0; cycle < 80 && (word_idx < 64 || mbeat < 32); ++cycle) {
    if (word_idx < 64) {
      dut->i_word_valid = 1;
      dut->i_word_data = mwords[word_idx];
      word_idx++;
    } else {
      dut->i_word_valid = 0;
    }
    tick(dut);
    if (dut->o_spm_wr_valid) {
      uint32_t exp_addr = 0x700u + static_cast<uint32_t>(mbeat) * 2u;
      uint32_t exp0 = mwords[mbeat * 2];
      uint32_t exp1 = mwords[mbeat * 2 + 1];
      if (dut->o_spm_wr_addr != exp_addr) {
        std::fprintf(stderr, "  [FAIL] Beat %d: expected addr 0x%x, got 0x%x\n",
                     mbeat, exp_addr, dut->o_spm_wr_addr);
        max_errors++;
      }
      uint64_t data = dut->o_spm_wr_data;
      if (lo32(data) != exp0 || hi32(data) != exp1) {
        std::fprintf(stderr, "  [FAIL] Beat %d: data mismatch\n", mbeat);
        max_errors++;
      }
      if (dut->o_spm_wr_wstrb != 0xFF) {
        std::fprintf(stderr, "  [FAIL] Beat %d: expected wstrb 0xFF, got 0x%x\n",
                     mbeat, dut->o_spm_wr_wstrb);
        max_errors++;
      }
      dut->i_word_valid = 0;  // deassert so acceptance tick doesn't re-feed
      tick(dut);  // accept
      mbeat++;
    }
  }
  if (mbeat != 32) {
    std::fprintf(stderr, "  [FAIL] Only %d of 32 beats observed\n", beat);
    max_errors++;
  } else if (max_errors == 0) {
    std::printf("  [PASS] All 32 beats for word_count=64 written\n");
  }
  errors += max_errors;

  cyc = 0;
  while (!dut->o_desc_ready && cyc < 20) { tick(dut); cyc++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert after max count\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready re-asserted after max count\n");
  }

  // -------------------------------------------------------------
  // Test 7: word_count = 0
  // -------------------------------------------------------------
  std::printf("[Test 7] word_count=0 boundary\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x800;
  dut->i_desc_word_count = 0;
  tick(dut);
  dut->i_desc_valid = 0;

  if (dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready should be low after accepting word_count=0\n");
    errors++;
  } else {
    std::printf("  [PASS] desc_ready low after accepting word_count=0\n");
  }
  // No words fed; no SPM write should occur.
  int saw_wr = 0;
  for (int i = 0; i < 5; ++i) {
    tick(dut);
    if (dut->o_spm_wr_valid) saw_wr = 1;
  }
  if (saw_wr) {
    std::fprintf(stderr, "  [FAIL] spm_wr_valid asserted for word_count=0\n");
    errors++;
  } else {
    std::printf("  [PASS] No SPM write for word_count=0\n");
  }
  reset_dut(dut);
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready not high after reset\n");
    errors++;
  } else {
    std::printf("  [PASS] Reset clears word_count=0 state\n");
  }

  // -------------------------------------------------------------
  // Test 8: Descriptor while busy
  // -------------------------------------------------------------
  std::printf("[Test 8] Descriptor while busy\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0x900;
  dut->i_desc_word_count = 4;
  tick(dut);
  dut->i_desc_valid = 0;

  dut->i_word_valid = 1;
  dut->i_word_data = 0x11111111;
  tick(dut);
  dut->i_word_data = 0x22222222;
  tick(dut);
  // First beat should be assembled now.
  cyc = 0;
  while (!dut->o_spm_wr_valid && cyc < 20) { tick(dut); cyc++; }
  if (!dut->o_spm_wr_valid) {
    std::fprintf(stderr, "  [FAIL] First beat not assembled\n");
    errors++;
  }

  // Attempt new descriptor while busy.
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0xA00;
  dut->i_desc_word_count = 2;
  tick(dut);
  if (dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready accepted descriptor while busy\n");
    errors++;
  } else {
    std::printf("  [PASS] Descriptor rejected while busy\n");
  }
  dut->i_desc_valid = 0;

  // Finish original transaction.
  if (dut->o_spm_wr_valid) tick(dut);  // accept first beat
  dut->i_word_data = 0x33333333;
  tick(dut);
  dut->i_word_data = 0x44444444;
  tick(dut);
  dut->i_word_valid = 0;
  cyc = 0;
  while (!dut->o_spm_wr_valid && cyc < 20) { tick(dut); cyc++; }
  if (dut->o_spm_wr_valid) tick(dut);

  cyc = 0;
  while (!dut->o_desc_ready && cyc < 20) { tick(dut); cyc++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert after busy test\n");
    errors++;
  } else {
    std::printf("  [PASS] Original descriptor completed\n");
  }

  // -------------------------------------------------------------
  // Test 9: SPM never ready (no deadlock on first beat)
  // -------------------------------------------------------------
  std::printf("[Test 9] SPM never ready\n");
  reset_dut(dut);
  dut->i_spm_wr_ready = 0;
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0xB00;
  dut->i_desc_word_count = 2;  // single beat to avoid multi-beat pending issues
  tick(dut);
  dut->i_desc_valid = 0;

  dut->i_word_valid = 1;
  dut->i_word_data = 0xA5A5A5A5;
  tick(dut);
  dut->i_word_data = 0x5A5A5A5A;
  tick(dut);
  dut->i_word_valid = 0;

  cyc = 0;
  while (!dut->o_spm_wr_valid && cyc < 20) { tick(dut); cyc++; }
  if (!dut->o_spm_wr_valid) {
    std::fprintf(stderr, "  [FAIL] spm_wr_valid did not assert with SPM unready\n");
    errors++;
  } else {
    std::printf("  [PASS] spm_wr_valid asserted while SPM unready\n");
  }

  // Hold for several cycles; valid should remain asserted.
  int held = 0;
  for (int i = 0; i < 5; ++i) {
    tick(dut);
    if (dut->o_spm_wr_valid) held++;
  }
  if (held < 5) {
    std::fprintf(stderr, "  [FAIL] spm_wr_valid did not hold for 5 cycles\n");
    errors++;
  } else {
    std::printf("  [PASS] spm_wr_valid held under permanent backpressure\n");
  }

  // Release backpressure and drain the single beat.
  dut->i_spm_wr_ready = 1;
  cyc = 0;
  while (dut->o_spm_wr_valid && cyc < 20) { tick(dut); cyc++; }

  cyc = 0;
  while (!dut->o_desc_ready && cyc < 20) { tick(dut); cyc++; }
  if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready did not re-assert after draining\n");
    errors++;
  } else {
    std::printf("  [PASS] Drained successfully after releasing backpressure\n");
  }

  // -------------------------------------------------------------
  // Test 10: Reset during active write
  // -------------------------------------------------------------
  std::printf("[Test 10] Reset during active write\n");
  reset_dut(dut);
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0xC00;
  dut->i_desc_word_count = 4;
  tick(dut);
  dut->i_desc_valid = 0;

  dut->i_word_valid = 1;
  dut->i_word_data = 0x11111111;
  tick(dut);
  dut->i_word_data = 0x22222222;
  tick(dut);
  // First beat assembled and accepted.
  cyc = 0;
  while (!dut->o_spm_wr_valid && cyc < 20) { tick(dut); cyc++; }
  if (dut->o_spm_wr_valid) tick(dut);

  // Assert reset mid-transaction.
  dut->rst_n = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  tick(dut);

  if (dut->o_spm_wr_valid) {
    std::fprintf(stderr, "  [FAIL] spm_wr_valid still asserted after reset\n");
    errors++;
  } else if (!dut->o_desc_ready) {
    std::fprintf(stderr, "  [FAIL] desc_ready not high after reset\n");
    errors++;
  } else {
    std::printf("  [PASS] Reset aborted active write cleanly\n");
  }

  // Verify recovery with a new transaction.
  dut->i_desc_valid = 1;
  dut->i_desc_base_addr = 0xD00;
  dut->i_desc_word_count = 2;
  tick(dut);
  dut->i_desc_valid = 0;
  dut->i_word_valid = 1;
  dut->i_word_data = 0xDEADBEEF;
  tick(dut);
  dut->i_word_data = 0xCAFEBABE;
  tick(dut);
  dut->i_word_valid = 0;
  cyc = 0;
  while (!dut->o_spm_wr_valid && cyc < 20) { tick(dut); cyc++; }
  if (!dut->o_spm_wr_valid) {
    std::fprintf(stderr, "  [FAIL] Recovery write not issued\n");
    errors++;
  } else {
    uint64_t data = dut->o_spm_wr_data;
    if (lo32(data) != 0xDEADBEEF || hi32(data) != 0xCAFEBABE) {
      std::fprintf(stderr, "  [FAIL] Recovery write data mismatch\n");
      errors++;
    } else {
      std::printf("  [PASS] Recovery write issued correctly after reset\n");
    }
    tick(dut);
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
