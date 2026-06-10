// new_pe_integration.cc
// Integration test: compute_unit + read_stream_engine + write_stream_engine
// + spm_arbiter + spm_bank_dpi (8 banks)
//
// Test flow:
//   1. Preload prior state into SPM bank 0
//   2. Send cmd to compute_unit
//   3. Feed neighbor states via ns_data
//   4. Wait for done_valid
//   5. Read result back from SPM

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <vector>

#include <svdpi.h>
#include "verilated.h"
#include "Vnew_pe_integration_top.h"

static constexpr int kNumBanks = 8;
static constexpr int kWordsPerBeat = 8;   // 256-bit beat / 32-bit word
static constexpr int kRowsPerBank = 4096; // 2^12
static constexpr int kBankWords = kRowsPerBank * kWordsPerBeat;

static std::array<std::vector<uint32_t>, kNumBanks> g_dpi_bank_mem;

static int resolve_bank_from_scope() {
  svScope scope = svGetScope();
  if (scope == nullptr) return 0;
  const char* name_c = svGetNameFromScope(scope);
  if (name_c == nullptr) return 0;
  std::string name(name_c);
  size_t pos = name.find("banks");
  if (pos == std::string::npos) return 0;
  size_t bra = name.find('[', pos);
  if (bra != std::string::npos) {
    size_t ket = name.find(']', bra);
    if (ket != std::string::npos) {
      return std::stoi(name.substr(bra + 1, ket - bra - 1));
    }
  }
  bra = name.find("__BRA__", pos);
  if (bra != std::string::npos) {
    size_t ket = name.find("__KET__", bra);
    if (ket != std::string::npos) {
      return std::stoi(name.substr(bra + 7, ket - bra - 7));
    }
  }
  return 0;
}

extern "C" int pmem_read(int raddr) {
  int bank = resolve_bank_from_scope();
  if (bank < 0 || bank >= kNumBanks) bank = 0;
  if (raddr < 0 || raddr >= static_cast<int>(g_dpi_bank_mem[bank].size())) return 0;
  return static_cast<int>(g_dpi_bank_mem[bank][raddr]);
}

extern "C" void pmem_write(int waddr, int wdata, char byte_num) {
  int bank = resolve_bank_from_scope();
  if (bank < 0 || bank >= kNumBanks) bank = 0;
  if (waddr < 0 || waddr >= static_cast<int>(g_dpi_bank_mem[bank].size())) return;
  uint32_t shift = static_cast<uint32_t>(static_cast<unsigned char>(byte_num)) * 8u;
  uint32_t mask = 0xFFu << shift;
  g_dpi_bank_mem[bank][waddr] = (g_dpi_bank_mem[bank][waddr] & ~mask)
                                  | (static_cast<uint32_t>(wdata) & mask);
}

static uint32_t float_to_bits(float f) {
  union { float f; uint32_t u; } conv;
  conv.f = f;
  return conv.u;
}

static void toggle_clock(Vnew_pe_integration_top* dut, int n = 1) {
  for (int i = 0; i < n; ++i) {
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
  }
}

static void reset(Vnew_pe_integration_top* dut) {
  dut->rst_n_i = 0;
  toggle_clock(dut, 5);
  dut->rst_n_i = 1;
  toggle_clock(dut, 1);
}

// Write a 32-bit word to SPM at the given *word* address
static void spm_write_word(int word_addr, uint32_t data) {
  int bank = (word_addr >> 3) & 0x7;   // word_addr[5:3]
  int row  = word_addr >> 6;            // word_addr[17:6]
  int word_in_row = word_addr & 0x7;    // word_addr[2:0]
  int bank_word_addr = row * kWordsPerBeat + word_in_row;
  if (bank_word_addr < static_cast<int>(g_dpi_bank_mem[bank].size())) {
    g_dpi_bank_mem[bank][bank_word_addr] = data;
  }
}

// Read a 32-bit word from SPM at the given *word* address
static uint32_t spm_read_word(int word_addr) {
  int bank = (word_addr >> 3) & 0x7;
  int row  = word_addr >> 6;
  int word_in_row = word_addr & 0x7;
  int bank_word_addr = row * kWordsPerBeat + word_in_row;
  if (bank_word_addr < static_cast<int>(g_dpi_bank_mem[bank].size())) {
    return g_dpi_bank_mem[bank][bank_word_addr];
  }
  return 0;
}

int run_test(int argc, char** argv) {
  // Initialize DPI memory
  for (int b = 0; b < kNumBanks; ++b) {
    g_dpi_bank_mem[b].resize(kBankWords, 0u);
  }

  auto* dut = new Vnew_pe_integration_top;
  reset(dut);

  printf("Test: new_pe_integration (compute_unit + stream engines + SPM)\n");

  // -----------------------------------------------------------------
  // 1. Preload prior state into SPM at word address 0 (bank 0, row 0)
  // -----------------------------------------------------------------
  // prior_eta = {0.0, 1.0}, prior_lam = {1.0, 0.0, 1.0}
  uint32_t prior_state[5] = {
    float_to_bits(0.0f),   // eta[0]
    float_to_bits(1.0f),   // eta[1]
    float_to_bits(1.0f),   // lam[0,0]
    float_to_bits(0.0f),   // lam[0,1]
    float_to_bits(1.0f),   // lam[1,1]
  };
  for (int i = 0; i < 5; ++i) {
    spm_write_word(i, prior_state[i]);
  }

  // -----------------------------------------------------------------
  // 2. Send command to compute_unit
  // -----------------------------------------------------------------
  dut->cmd_valid_i = 1;
  dut->cmd_node_id_i = 0;
  dut->cmd_is_factor_i = 0;  // variable node
  dut->cmd_dof_i = 2;
  dut->cmd_adj_count_i = 2;
  dut->cmd_state_words_i = 5;
  toggle_clock(dut, 1);
  dut->cmd_valid_i = 0;

  // -----------------------------------------------------------------
  // 3. Feed neighbor states via ns_data
  // -----------------------------------------------------------------
  uint32_t msg0[5] = {
    float_to_bits(0.5f), float_to_bits(0.0f),
    float_to_bits(1.0f), float_to_bits(0.0f), float_to_bits(2.0f)
  };
  uint32_t msg1[5] = {
    float_to_bits(0.0f), float_to_bits(0.5f),
    float_to_bits(2.0f), float_to_bits(0.0f), float_to_bits(1.0f)
  };

  int ns_idx = 0;
  int done_seen = 0;
  int max_cycles = 5000;

  for (int c = 0; c < max_cycles; ++c) {
    if (ns_idx < 10) {
      dut->ns_valid_i = 1;
      dut->ns_data_i = (ns_idx < 5) ? msg0[ns_idx] : msg1[ns_idx - 5];
      dut->ns_last_i = (ns_idx == 9);
    } else {
      dut->ns_valid_i = 0;
      dut->ns_last_i = 0;
    }

    toggle_clock(dut, 1);

    if (dut->ns_ready_o) {
      ns_idx++;
    }

    if (dut->done_valid_o) {
      printf("  done_valid at cycle %d! node_id=%d is_factor=%d batch_done=%d\n",
             c, dut->done_node_id_o, dut->done_is_factor_o, dut->batch_done_o);
      done_seen = 1;
      break;
    }
  }

  if (!done_seen) {
    printf("FAIL: done_valid not seen within %d cycles\n", max_cycles);
    delete dut;
    return 1;
  }

  // -----------------------------------------------------------------
  // 4. Read result back from SPM (write_stream_engine writes to addr 0)
  // -----------------------------------------------------------------
  // Wait a few more cycles for write to complete
  toggle_clock(dut, 10);

  printf("  SPM words after compute:\n");
  for (int i = 0; i < 8; ++i) {
    uint32_t w = spm_read_word(i);
    float f; memcpy(&f, &w, sizeof(f));
    printf("    word[%d] = 0x%08x (%f)\n", i, w, f);
  }

  printf("Test PASSED\n");
  delete dut;
  return 0;
}
