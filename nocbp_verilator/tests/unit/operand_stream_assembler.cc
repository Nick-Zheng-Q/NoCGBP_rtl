// operand_stream_assembler.cc
// Unit test for operand_stream_assembler.

#include <cstdio>
#include <cstdint>
#include <cmath>
#include "verilated.h"
#include "Voperand_stream_assembler_top.h"
#include "Voperand_stream_assembler_top___024root.h"

static int fail(const char* test, const char* msg) {
  std::fprintf(stderr, "%s: FAIL: %s\n", test, msg);
  return 1;
}

static void tick(Voperand_stream_assembler_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Voperand_stream_assembler_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

static uint32_t f2u(float f) {
  union { float f; uint32_t u; } c; c.f = f; return c.u;
}
static float u2f(uint32_t u) {
  union { float f; uint32_t u; } c; c.u = u; return c.f;
}

static int packed_count(int d) { return d * (d + 1) / 2; }
static int packed_index(int row, int col, int d) {
  return row * d - row * (row - 1) / 2 + (col - row);
}

// Backdoor write to the SPM memory model inside the unit-test top.
// addr is a 32-bit word address; the SPM stores 64-bit beats at beat_idx = addr/2.
static void spm_write_word(Voperand_stream_assembler_top* d, uint32_t addr, uint32_t data) {
  auto* mem = &d->rootp->operand_stream_assembler_top__DOT__mem;
  uint32_t beat_idx = addr >> 1;
  uint64_t beat = mem->m_storage[beat_idx];
  if ((addr & 1u) == 0) {
    beat = (beat & 0xFFFFFFFF00000000ULL) | static_cast<uint64_t>(data);
  } else {
    beat = (beat & 0x00000000FFFFFFFFULL) | (static_cast<uint64_t>(data) << 32);
  }
  mem->m_storage[beat_idx] = beat;
}

static uint32_t spm_read_word(Voperand_stream_assembler_top* d, uint32_t addr) {
  auto* mem = &d->rootp->operand_stream_assembler_top__DOT__mem;
  uint64_t beat = mem->m_storage[addr >> 1];
  return ((addr & 1u) == 0) ? static_cast<uint32_t>(beat)
                            : static_cast<uint32_t>(beat >> 32);
}

static void clear_descriptor(Voperand_stream_assembler_top* d) {
  d->desc_valid_i = 0;
  d->desc_kind_i  = 0;
  d->desc_op_id_i = 0;
  d->desc_base_addr_i = 0;
  d->desc_nbeats_i = 0;
}

static void issue_descriptor(Voperand_stream_assembler_top* d, int kind,
                             uint32_t op_id, uint32_t base, uint16_t nbeats) {
  d->desc_valid_i     = 1;
  d->desc_kind_i      = kind;
  d->desc_op_id_i     = op_id;
  d->desc_base_addr_i = base;
  d->desc_nbeats_i    = nbeats;
  tick(d);
  clear_descriptor(d);
}

// ============================================================
// Test 1: simple 2-beat stream using the default memory pattern.
// ============================================================
static int test_2beat_default_pattern(Voperand_stream_assembler_top* d) {
  reset(d);
  const int nbeats = 2;
  std::printf("=== operand_stream_assembler: %d-beat default pattern ===\n", nbeats);

  issue_descriptor(d, 4, 0xDEADBEEF, 0, nbeats);

  int beats_received = 0;
  int cycles = 0;
  const int max_cycles = 200;

  while (cycles < max_cycles && beats_received < nbeats) {
    d->operand_ready_i = 1;
    tick(d);
    cycles++;

    if (d->operand_valid_o) {
      if (d->operand_kind_o != 4)
        return fail("operand_stream_assembler", "kind mismatch");
      if (d->operand_op_id_o != 0xDEADBEEF)
        return fail("operand_stream_assembler", "op_id mismatch");
      if (d->operand_beat_idx_o != beats_received)
        return fail("operand_stream_assembler", "beat_idx mismatch");
      bool last_expected = (beats_received == nbeats - 1);
      if (d->operand_last_o != last_expected)
        return fail("operand_stream_assembler", "last flag mismatch");

      int base_word = beats_received * 16;
      for (int w = 0; w < 16; w++) {
        uint32_t got = d->operand_data_flat_o.m_storage[w];
        uint32_t exp = static_cast<uint32_t>(base_word + w);
        if (got != exp) {
          char msg[128];
          snprintf(msg, sizeof(msg), "beat %d word %d: got 0x%08X expected 0x%08X",
                   beats_received, w, got, exp);
          return fail("operand_stream_assembler", msg);
        }
      }
      beats_received++;
    }
  }

  if (beats_received != nbeats)
    return fail("operand_stream_assembler", "did not receive expected beats");

  int ready_wait = 0;
  while (!d->desc_ready_o && ready_wait < 20) { tick(d); ready_wait++; }
  if (!d->desc_ready_o)
    return fail("operand_stream_assembler", "desc_ready_o not high after stream");

  std::printf("  received %d beats in %d cycles\n", beats_received, cycles);
  return 0;
}

// ============================================================
// Test 2: dim6 belief prior (33 scalars => 3 operand beats).
// Catches packing bugs that only show up across beat boundaries,
// e.g. L_packed[6] and L_packed[15] being dropped.
// ============================================================
static int test_dim6_belief_prior(Voperand_stream_assembler_top* d) {
  reset(d);
  std::printf("=== operand_stream_assembler: dim6 belief prior (3 beats) ===\n");

  const int dim = 6;
  const int p   = packed_count(dim);          // 21
  const int prior_words = dim + p + dim;      // 33
  const int nbeats = 3;                       // ceil(33/16)

  // Build an identity L_packed for dim6.
  float L_packed[21] = {0};
  for (int i = 0; i < dim; i++)
    L_packed[packed_index(i, i, dim)] = 1.0f;

  // Fill SPM with: eta[0..5]=1..6, L_packed[0..20]=identity, mu_old[0..5]=0.
  uint32_t addr = 0;
  for (int i = 0; i < dim; i++)      spm_write_word(d, addr++, f2u(static_cast<float>(i + 1)));
  for (int i = 0; i < p; i++)        spm_write_word(d, addr++, f2u(L_packed[i]));
  for (int i = 0; i < dim; i++)      spm_write_word(d, addr++, f2u(0.0f));
  // Clear the remainder of the 3rd operand beat so it is deterministic.
  for (; addr < static_cast<uint32_t>(nbeats * 16); addr++)
    spm_write_word(d, addr, 0);

  // Verify backdoor write landed.
  if (u2f(spm_read_word(d, 12)) != 1.0f)
    return fail("operand_stream_assembler", "dim6: backdoor L[6] not written");
  if (u2f(spm_read_word(d, 21)) != 1.0f)
    return fail("operand_stream_assembler", "dim6: backdoor L[15] not written");

  issue_descriptor(d, 4, 0xBEEF0000, 0, nbeats);

  int beats_received = 0;
  int cycles = 0;
  const int max_cycles = 300;

  while (cycles < max_cycles && beats_received < nbeats) {
    d->operand_ready_i = 1;
    tick(d);
    cycles++;

    if (d->operand_valid_o) {
      if (d->operand_kind_o != 4)
        return fail("operand_stream_assembler", "dim6: kind mismatch");
      if (d->operand_op_id_o != 0xBEEF0000)
        return fail("operand_stream_assembler", "dim6: op_id mismatch");
      if (d->operand_beat_idx_o != beats_received)
        return fail("operand_stream_assembler", "dim6: beat_idx mismatch");
      bool last_expected = (beats_received == nbeats - 1);
      if (d->operand_last_o != last_expected)
        return fail("operand_stream_assembler", "dim6: last flag mismatch");

      int base_word = beats_received * 16;
      for (int w = 0; w < 16 && (base_word + w) < prior_words; w++) {
        uint32_t got = d->operand_data_flat_o.m_storage[w];
        uint32_t exp = spm_read_word(d, base_word + w);
        if (got != exp) {
          char msg[256];
          snprintf(msg, sizeof(msg),
                   "dim6: beat %d word %d (stream word %d): got 0x%08x (%g) expected 0x%08x (%g)",
                   beats_received, w, base_word + w,
                   got, u2f(got), exp, u2f(exp));
          return fail("operand_stream_assembler", msg);
        }
      }
      beats_received++;
    }
  }

  if (beats_received != nbeats)
    return fail("operand_stream_assembler", "dim6: did not receive 3 beats");

  int ready_wait = 0;
  while (!d->desc_ready_o && ready_wait < 20) { tick(d); ready_wait++; }
  if (!d->desc_ready_o)
    return fail("operand_stream_assembler", "dim6: desc_ready_o not high after stream");

  std::printf("  dim6 prior received %d beats in %d cycles\n", beats_received, cycles);
  return 0;
}

// ============================================================
int run_test(int argc, char** argv) {
  auto* dut = new Voperand_stream_assembler_top;

  int errors = 0;
  errors += test_2beat_default_pattern(dut);
  errors += test_dim6_belief_prior(dut);

  if (errors == 0) {
    std::printf("operand_stream_assembler: ALL TESTS PASSED\n");
  } else {
    std::printf("operand_stream_assembler: %d TEST(S) FAILED\n", errors);
  }
  delete dut;
  return errors;
}
