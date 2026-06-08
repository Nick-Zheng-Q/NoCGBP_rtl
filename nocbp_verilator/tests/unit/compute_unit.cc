// compute_unit.cc - Unit test for compute_unit (new architecture wrapper)
// Feeds prior state via rd_beat, checks wr_word output and done_valid.
// BEAT_BITS = 64: each rd_beat is 64b (2 x FP32 words). Assembler needs 4 beats
// to form one 256b engine word.

#include <cstdio>
#include <cstdint>
#include <cmath>
#include "Vcompute_unit.h"

static Vcompute_unit* dut;

static void toggle_clock(int n = 1) {
  for (int i = 0; i < n; ++i) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
  }
}

static void reset() {
  dut->rst_n = 0;
  toggle_clock(5);
  dut->rst_n = 1;
  toggle_clock(1);
}

static uint32_t float_to_bits(float f) {
  union { float f; uint32_t u; } conv;
  conv.f = f;
  return conv.u;
}

// Pack two 32-bit words into a 64-bit beat
static uint64_t pack_beat(uint32_t w0, uint32_t w1) {
  return (static_cast<uint64_t>(w1) << 32) | static_cast<uint64_t>(w0);
}

int run_test(int argc, char** argv) {
  dut = new Vcompute_unit;

  reset();

  // ── Test 1: variable node update (dof=2, no neighbors) ──
  printf("Test 1: variable node update (dof=2, no neighbors)\n");

  // Prepare prior state: 8 words = 1 x 256b engine word.
  uint32_t prior_words[8] = {
    float_to_bits(0.0f),   // eta[0]
    float_to_bits(1.0f),   // eta[1]
    float_to_bits(1.0f),   // lam[0,0]
    float_to_bits(0.0f),   // lam[0,1]
    float_to_bits(1.0f),   // lam[1,1]
    0, 0, 0                // padding
  };

  // Start command
  dut->cmd_valid = 1;
  dut->cmd_node_id = 42;
  dut->cmd_is_factor = 0;  // variable node
  dut->cmd_dof = 2;
  dut->cmd_adj_count = 0;  // no neighbors -> simple load/store flow
  dut->cmd_state_words = 8;
  toggle_clock(1);
  dut->cmd_valid = 0;

  // Feed rd_beat data: 4 beats of 64b = 1 assembled 256b engine word
  int rd_beat_idx = 0;
  const int NUM_RD_BEATS = 4;
  dut->rd_beat_valid = 1;
  dut->rd_beat_data = pack_beat(prior_words[0], prior_words[1]);

  int wr_count = 0;
  int done_seen = 0;
  int max_cycles = 5000;

  for (int c = 0; c < max_cycles; ++c) {
    // Accept outputs
    dut->wr_word_ready = 1;
    dut->wr_desc_ready = 1;

    toggle_clock(1);

    // Update rd_beat for NEXT cycle (after current tick)
    if (dut->rd_beat_ready && rd_beat_idx < NUM_RD_BEATS) {
      rd_beat_idx++;
      if (rd_beat_idx < NUM_RD_BEATS) {
        int w0 = rd_beat_idx * 2;
        int w1 = w0 + 1;
        dut->rd_beat_data = pack_beat(prior_words[w0], prior_words[w1]);
      } else {
        dut->rd_beat_valid = 0;
      }
    }

    if (dut->wr_word_valid) {
      printf("  wr_word[%d] = 0x%08x (%f)\n", wr_count,
             dut->wr_word_data,
             *reinterpret_cast<float*>(&dut->wr_word_data));
      wr_count++;
    }

    if (dut->done_valid) {
      printf("  done_valid! node_id=%d is_factor=%d batch_done=%d\n",
             dut->done_node_id, dut->done_is_factor, dut->batch_done);
      done_seen = 1;
      break;
    }
  }

  if (!done_seen) {
    printf("FAIL: done_valid not seen within %d cycles\n", max_cycles);
    return 1;
  }

  printf("Test 1 PASSED\n");
  return 0;
}
