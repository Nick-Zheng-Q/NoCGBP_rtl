#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <vector>

#include <svdpi.h>
#include "verilated.h"
#include "Vpe_top_integration.h"

static constexpr int kNumBanks = 8;
static constexpr int kWordsPerBeat = 8;
static constexpr int kRowsPerBank = 4096;
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

static void spm_write_word(int word_addr, uint32_t data) {
  int bank = (word_addr >> 3) & 0x7;
  int row  = word_addr >> 6;
  int word_in_row = word_addr & 0x7;
  int bank_word_addr = row * kWordsPerBeat + word_in_row;
  if (bank_word_addr < static_cast<int>(g_dpi_bank_mem[bank].size())) {
    g_dpi_bank_mem[bank][bank_word_addr] = data;
  }
}

static void tick(Vpe_top_integration* dut) {
  dut->clk = 0; dut->eval();
  dut->clk = 1; dut->eval();
}

static void reset_dut(Vpe_top_integration* dut) {
  dut->rst_n = 0;
  dut->cmd_valid = 0;
  dut->cmd_kind = 0;
  dut->cmd_txn = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
}

int run_test(int argc, char** argv) {
  // Initialize DPI memory
  for (int b = 0; b < kNumBanks; ++b) {
    g_dpi_bank_mem[b].resize(kBankWords, 0u);
  }

  // Preload prior state into SPM at word address 0 (bank 0, row 0)
  // dof=1 variable node: state = {eta[0], lam[0,0]}
  spm_write_word(0, float_to_bits(0.5f));  // eta
  spm_write_word(1, float_to_bits(1.0f));  // lam

  Verilated::commandArgs(argc, argv);
  auto* dut = new Vpe_top_integration;

  reset_dut(dut);

  // Send whitebox command (bypass mode in pe_top)
  dut->cmd_valid = 1;
  dut->cmd_kind = 0;   // variable node
  dut->cmd_txn = 0x01;
  tick(dut);
  bool saw_compute_start = dut->compute_start;
  if (dut->cmd_ready) {
    dut->cmd_valid = 0;
  }

  bool saw_compute_done = false;
  const int kMaxCycles = 200;

  for (int c = 0; c < kMaxCycles; ++c) {
    tick(dut);
    if (dut->compute_start) saw_compute_start = true;
    if (dut->compute_done) {
      saw_compute_done = true;
      break;
    }
  }

  if (!saw_compute_start) {
    fprintf(stderr, "FAIL: compute_start not observed within %d cycles\n", kMaxCycles);
    delete dut;
    return 1;
  }
  if (!saw_compute_done) {
    fprintf(stderr, "FAIL: compute_done not observed within %d cycles\n", kMaxCycles);
    delete dut;
    return 1;
  }

  // Read back result from SPM (write_stream_engine writes to addr 0)
  int bank_word_addr = 0 * kWordsPerBeat + 0; // row 0, word 0
  uint32_t eta_w = g_dpi_bank_mem[0][bank_word_addr];
  uint32_t lam_w = g_dpi_bank_mem[0][bank_word_addr + 1];
  float eta_f, lam_f;
  memcpy(&eta_f, &eta_w, sizeof(float));
  memcpy(&lam_f, &lam_w, sizeof(float));

  printf("pe_top integration: PASS (eta=%f, lam=%f)\n", eta_f, lam_f);
  delete dut;
  return 0;
}
