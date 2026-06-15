// wb_to_wse_adapter.cc
// Unit test for wb_to_wse_adapter.

#include <cstdio>
#include <cstdint>
#include "verilated.h"
#include "Vwb_to_wse_adapter_top.h"

static void tick(Vwb_to_wse_adapter_top* dut) {
  dut->clk_i = 0; dut->eval();
  dut->clk_i = 1; dut->eval();
}

static void reset(Vwb_to_wse_adapter_top* dut) {
  dut->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(dut);
  dut->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(dut);
}

static int fail(const char* test, const char* msg) {
  std::fprintf(stderr, "%s: FAIL: %s\n", test, msg);
  return 1;
}

int run_test(int argc, char** argv) {
  auto* dut = new Vwb_to_wse_adapter_top;

  // Defaults
  dut->wb_valid_i = 0;
  dut->wb_addr_i = 0;
  dut->wb_nwords_i = 0;
  dut->wb_kind_i = 0;
  for (int i = 0; i < 36; i++) dut->wb_payload_flat_i.m_storage[i] = 0; // max 36 words
  dut->wb_fail_i = 0;
  dut->wb_regularized_i = 0;
  dut->wb_nan_guard_i = 0;
  dut->desc_ready_i = 0;
  dut->word_ready_i = 0;
  reset(dut);

  printf("=== wb_to_wse_adapter: 5-word writeback record ===\n");

  const uint32_t base_addr = 0x100;
  const uint16_t nwords = 5;

  dut->wb_valid_i = 1;
  dut->wb_addr_i = base_addr;
  dut->wb_nwords_i = nwords;
  dut->wb_kind_i = 1;  // WB_BELIEF
  for (int i = 0; i < nwords; i++) {
    dut->wb_payload_flat_i.m_storage[i] = 0xA0000000 + i;
  }

  // Wait for adapter to accept the writeback record.
  int wait_cycles = 0;
  while (!dut->wb_ready_o && wait_cycles < 20) {
    tick(dut);
    wait_cycles++;
  }
  if (!dut->wb_ready_o) {
    return fail("wb_to_wse_adapter", "wb_ready_o never asserted");
  }
  tick(dut);
  dut->wb_valid_i = 0;

  // Wait for descriptor.
  wait_cycles = 0;
  while (!dut->desc_valid_o && wait_cycles < 20) {
    tick(dut);
    wait_cycles++;
  }
  if (!dut->desc_valid_o) {
    return fail("wb_to_wse_adapter", "desc_valid_o never asserted");
  }
  if (dut->desc_base_addr_o != base_addr) {
    return fail("wb_to_wse_adapter", "desc_base_addr mismatch");
  }
  if (dut->desc_word_count_o != nwords) {
    return fail("wb_to_wse_adapter", "desc_word_count mismatch");
  }

  // Accept descriptor.
  dut->desc_ready_i = 1;
  tick(dut);
  dut->desc_ready_i = 0;

  // Collect words.  word_data_o updates on the same posedge that accepts
  // the current word, so we sample it at clk=0 before the rising edge.
  uint32_t received[5] = {0};
  int words = 0;
  int max_cycles = 50;
  int cycle;
  for (cycle = 0; cycle < max_cycles && words < nwords; cycle++) {
    if (!dut->word_valid_o) {
      tick(dut);
      continue;
    }
    dut->word_ready_i = 1;
    dut->clk_i = 0;
    dut->eval();
    uint32_t data = dut->word_data_o;
    dut->clk_i = 1;
    dut->eval();
    dut->word_ready_i = 0;
    received[words++] = data;
  }

  if (words != nwords) {
    return fail("wb_to_wse_adapter", "did not receive expected words");
  }

  for (int i = 0; i < nwords; i++) {
    if (received[i] != 0xA0000000 + i) {
      char msg[128];
      snprintf(msg, sizeof(msg), "word[%d]=0x%08X expected 0x%08X",
               i, received[i], 0xA0000000 + i);
      return fail("wb_to_wse_adapter", msg);
    }
  }

  // Adapter should be ready for another record.
  wait_cycles = 0;
  while (!dut->wb_ready_o && wait_cycles < 20) {
    tick(dut);
    wait_cycles++;
  }
  if (!dut->wb_ready_o) {
    return fail("wb_to_wse_adapter", "wb_ready_o not high after stream");
  }

  printf("  received %d words correctly\n", words);

  // -------------------------------------------------------------
  // dim6 belief writeback record: 34 words (eta 6 + L 21 + mu 6 + residual 1)
  // -------------------------------------------------------------
  printf("\n=== wb_to_wse_adapter: dim6 belief (34 words) ===\n");
  reset(dut);

  const uint16_t dim6_nwords = 34;
  uint32_t dim6_payload[36] = {0};
  for (int i = 0; i < 6; i++) dim6_payload[i] = 0xE0000000u + i;          // eta
  for (int i = 0; i < 21; i++) dim6_payload[6 + i] = 0xB0000000u + i;      // L
  for (int i = 0; i < 6; i++) dim6_payload[27 + i] = 0xC0000000u + i;      // mu
  dim6_payload[33] = 0xDEADBEEF;                                            // residual

  dut->wb_valid_i = 1;
  dut->wb_addr_i = 0x120;
  dut->wb_nwords_i = dim6_nwords;
  dut->wb_kind_i = 1;  // WB_BELIEF
  for (int i = 0; i < 36; i++) {
    dut->wb_payload_flat_i.m_storage[i] = dim6_payload[i];
  }

  wait_cycles = 0;
  while (!dut->wb_ready_o && wait_cycles < 20) { tick(dut); wait_cycles++; }
  if (!dut->wb_ready_o) {
    return fail("wb_to_wse_adapter dim6", "wb_ready_o never asserted");
  }
  tick(dut);
  dut->wb_valid_i = 0;

  wait_cycles = 0;
  while (!dut->desc_valid_o && wait_cycles < 20) { tick(dut); wait_cycles++; }
  if (!dut->desc_valid_o) {
    return fail("wb_to_wse_adapter dim6", "desc_valid_o never asserted");
  }
  if (dut->desc_word_count_o != dim6_nwords) {
    char msg[128]; snprintf(msg, sizeof(msg),
      "desc_word_count mismatch: expected %u, got %u",
      dim6_nwords, dut->desc_word_count_o);
    return fail("wb_to_wse_adapter dim6", msg);
  }

  dut->desc_ready_i = 1;
  tick(dut);
  dut->desc_ready_i = 0;

  uint32_t dim6_received[34] = {0};
  words = 0;
  max_cycles = 200;
  for (cycle = 0; cycle < max_cycles && words < dim6_nwords; cycle++) {
    if (!dut->word_valid_o) { tick(dut); continue; }
    dut->word_ready_i = 1;
    dut->clk_i = 0;
    dut->eval();
    uint32_t data = dut->word_data_o;
    dut->clk_i = 1;
    dut->eval();
    dut->word_ready_i = 0;
    dim6_received[words++] = data;
  }
  if (words != dim6_nwords) {
    return fail("wb_to_wse_adapter dim6", "did not receive expected words");
  }

  bool dim6_ok = true;
  for (int i = 0; i < dim6_nwords; i++) {
    if (dim6_received[i] != dim6_payload[i]) {
      char msg[128]; snprintf(msg, sizeof(msg),
        "word[%d]=0x%08X expected 0x%08X", i, dim6_received[i], dim6_payload[i]);
      return fail("wb_to_wse_adapter dim6", msg);
    }
  }
  printf("  received %d dim6 words correctly\n", words);

  wait_cycles = 0;
  while (!dut->wb_ready_o && wait_cycles < 20) { tick(dut); wait_cycles++; }
  if (!dut->wb_ready_o) {
    return fail("wb_to_wse_adapter dim6", "wb_ready_o not high after stream");
  }

  printf("wb_to_wse_adapter: PASS\n");
  delete dut;
  return 0;
}
