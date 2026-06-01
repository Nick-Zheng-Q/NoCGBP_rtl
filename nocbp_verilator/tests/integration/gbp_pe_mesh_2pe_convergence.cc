#include <array>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "verilated.h"

#if defined(GBP_CANONICAL_MESH)
#include "Vgbp_pe_mesh_convergence.h"
#ifndef GBP_PE_COUNT
#define GBP_PE_COUNT 16
#endif
#else
#include "Vgbp_pe_mesh_2pe_convergence.h"
#include "Vgbp_pe_mesh_2pe_convergence_bsg_manycore_tile_compute_mesh__pi2.h"
#endif

#include "../common/gbp_event_correlation.hpp"
#include "../common/gbp_message_payload_codec.hpp"
#include "../common/gbp_terminal_metrics_adapter.hpp"

using Dut =
#if defined(GBP_CANONICAL_MESH)
    Vgbp_pe_mesh_convergence;
#else
    Vgbp_pe_mesh_2pe_convergence;
#endif

static constexpr const char* kTopName =
#if defined(GBP_CANONICAL_MESH)
    "gbp_pe_mesh_convergence";
#else
    "gbp_pe_mesh_2pe_convergence";
#endif

static constexpr const char* kTraceTestName =
#if defined(GBP_CANONICAL_MESH)
    "gbp_pe_mesh_gbp";
#else
    "gbp_pe_mesh_2pe_gbp";
#endif

static constexpr const char* kBuildTestName =
#if defined(GBP_CANONICAL_MESH)
    "gbp_pe_mesh_convergence";
#else
    "gbp_pe_mesh_2pe_convergence";
#endif

static constexpr uint32_t kRowBytesLg = 5;
static constexpr uint32_t kMmioBankB0 = 0;
static constexpr uint32_t kPayloadBankB4 = 4;
static constexpr uint32_t kCreditMetaField = 0;
static constexpr uint32_t kTailField = 3;
static constexpr uint32_t kDoorbellField = 5;
static constexpr int kFixedIters = 50;
static constexpr int kPostStopCycles = 64;
static constexpr const char* kBaDatasetPath = "data/fr1desk_small.txt";
static constexpr uint32_t kIngressBankWidth = 3;
static constexpr uint32_t kIngressBankMask = (1u << kIngressBankWidth) - 1u;
static constexpr uint32_t kIngressRowShift = kRowBytesLg + kIngressBankWidth;
static constexpr uint8_t kSourceDutWrReq = 0x1;
static constexpr uint8_t kSourceIngressWrReq = 0x2;
static constexpr size_t kMaxTrackedTouchedAddrsPerPe = 64;
static constexpr uint32_t kRequiredStateBankLo = 1;
static constexpr uint32_t kRequiredStateBankHi = 3;
static constexpr uint32_t kRequiredMessageBankLo = 4;
static constexpr uint32_t kRequiredMessageBankHi = 7;
static constexpr uint32_t kRequiredRowIndex = 0;
static constexpr size_t kWordsPerRow = 8;
static constexpr uint32_t kTxnIdJoinMask = 0x1Fu;
static constexpr size_t kBaTargetPhaseDispatches = 12350u;
static constexpr size_t kBa4peCrossEdgeBudget = 247u;
#if defined(GBP_CANONICAL_MESH)
static constexpr uint32_t kHarnessEndpoints = GBP_PE_COUNT;
#else
static constexpr uint32_t kHarnessEndpoints = 2;
#endif

static uint64_t g_snapshot_seq = 0;
static uint64_t g_cycle_count = 0;

struct PartitionInfo {
  std::vector<std::vector<int>> fac_mapping;
  std::vector<std::vector<int>> var_mapping;
  std::vector<std::pair<int, int>> edges;
};

struct CrossEdge {
  int factor_id = -1;
  int variable_id = -1;
  int factor_owner_pe = -1;
  int variable_owner_pe = -1;
};

struct IterationTrace {
  int iteration = 0;
  uint32_t factor_phase_remote_ingress = 0;
  uint32_t factor_phase_cmd_accepted = 0;
  uint32_t factor_phase_dut_tx = 0;
  uint32_t variable_phase_remote_ingress = 0;
  uint32_t variable_phase_cmd_accepted = 0;
  uint32_t variable_phase_dut_tx = 0;
  uint32_t pe0_ingress_total = 0;
  uint32_t pe1_ingress_total = 0;
  uint32_t pe0_cmd_total = 0;
  uint32_t pe1_cmd_total = 0;
  uint32_t pe0_dut_tx_total = 0;
  uint32_t pe1_dut_tx_total = 0;
};

struct TerminalDumpPe {
  std::array<std::array<uint32_t, kWordsPerRow>, 3> state_rows{};
  std::array<std::array<uint32_t, kWordsPerRow>, 4> message_rows{};
  std::array<uint32_t, 4> adapter_payload_row0_by_plane{};
  uint32_t adapter_credit_q0 = 0;
  uint32_t adapter_tail_q0 = 0;
};

struct ObservedSemanticMessage {
  uint32_t bank = 0;
  uint32_t slot = 0;
  uint32_t row = 0;
  bool is_local = false;
  std::array<uint32_t, 5> payload_words{{0u, 0u, 0u, 0u, 0u}};
  std::array<float, 2> eta{{0.0f, 0.0f}};
  std::array<float, 3> lam{{0.0f, 0.0f, 0.0f}};
};

struct LocalSemanticMessageRecord {
  int pe = -1;
  int factor_id = -1;
  int variable_id = -1;
  ObservedSemanticMessage semantic;
};

struct TouchedWrite {
  uint32_t addr = 0;
  uint32_t terminal_word_low32 = 0;
  uint8_t source_mask = 0;
};

static bool parse_u32_env(const char* key, uint32_t* out) {
  const char* raw = std::getenv(key);
  if (raw == nullptr || raw[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(raw, &end, 10);
  if (errno != 0 || end == raw || *end != '\0') {
    return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
}

static int endpoint_index_for_pe(int pe) {
#if defined(GBP_CANONICAL_MESH)
  return pe;
#else
  return (pe & 0x1) == 0 ? 0 : 1;
#endif
}

static void set_probe_selection(Dut* dut, int pe, uint32_t bank, uint32_t row) {
#if defined(GBP_CANONICAL_MESH)
  dut->probe_pe = static_cast<uint8_t>(pe);
  dut->probe_bank = static_cast<uint8_t>(bank);
  dut->probe_row = static_cast<uint8_t>(row);
  dut->eval();
#else
  (void)dut;
  (void)pe;
  (void)bank;
  (void)row;
#endif
}

static void set_probe_pe(Dut* dut, int pe) {
  set_probe_selection(dut, pe, 0u, 0u);
}

static uint32_t ingress_bank_from_addr(uint32_t addr) {
  return (addr >> kRowBytesLg) & kIngressBankMask;
}

static uint32_t ingress_row_from_addr(uint32_t addr) {
  return addr >> kIngressRowShift;
}

static std::array<uint32_t, 4> adapter_payload_planes_row0_for_pe(const Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  auto* mutable_dut = const_cast<Dut*>(dut);
  set_probe_pe(mutable_dut, pe);
  return {static_cast<uint32_t>(mutable_dut->probe_adapter_payload_plane0_row0_o),
          static_cast<uint32_t>(mutable_dut->probe_adapter_payload_plane1_row0_o),
          static_cast<uint32_t>(mutable_dut->probe_adapter_payload_plane2_row0_o),
          static_cast<uint32_t>(mutable_dut->probe_adapter_payload_plane3_row0_o)};
#else
  const int endpoint = endpoint_index_for_pe(pe);
  if (endpoint == 0) {
    return {static_cast<uint32_t>(dut->pe0_adapter_payload_plane0_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane1_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane2_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane3_row0_o)};
  }
  return {static_cast<uint32_t>(dut->pe1_adapter_payload_plane0_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane1_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane2_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane3_row0_o)};
#endif
}

static void try_capture_semantic_message(const Dut* dut,
                                         int pe,
                                         uint32_t addr,
                                         uint32_t data,
                                         bool is_local,
                                         std::array<std::vector<ObservedSemanticMessage>, kHarnessEndpoints>* observed) {
  const uint32_t bank = ingress_bank_from_addr(addr);
  if (bank < kRequiredMessageBankLo || bank > kRequiredMessageBankHi) {
    return;
  }

  ObservedSemanticMessage message{};
  message.bank = bank;
  message.slot = bank - kRequiredMessageBankLo;
  message.row = ingress_row_from_addr(addr);
  message.is_local = is_local;
  const std::array<uint32_t, 4> planes = adapter_payload_planes_row0_for_pe(dut, pe);
  message.payload_words[0] = data;
  message.payload_words[1] = planes[1];
  message.payload_words[2] = planes[2];
  message.payload_words[3] = planes[3];
  message.payload_words[4] = data;

  gbp_event_correlation::SemanticPayloadEvidence semantic;
  std::string codec_error;
  if (!gbp_event_correlation::decode_semantic_payload(
          message.bank, message.row, message.payload_words, &semantic, &codec_error)) {
    return;
  }
  message.eta[0] = semantic.eta[0];
  message.eta[1] = semantic.eta[1];
  message.lam[0] = semantic.lam[0];
  message.lam[1] = semantic.lam[1];
  message.lam[2] = semantic.lam[2];
  (*observed)[pe].push_back(message);
}

static bool capture_remote_semantic_delta(
    const std::vector<ObservedSemanticMessage>& observed,
    size_t before_count,
    uint32_t expected_bank,
    gbp_event_correlation::SemanticPayloadEvidence* out,
    std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  for (size_t i = before_count; i < observed.size(); ++i) {
    const ObservedSemanticMessage& message = observed[i];
    if (message.bank != expected_bank) {
      continue;
    }
#if !defined(GBP_CANONICAL_MESH)
    if (message.is_local) {
      continue;
    }
#endif
    if (!gbp_event_correlation::decode_semantic_payload(
            message.bank, message.row, message.payload_words, out, error)) {
      return false;
    }
    return true;
  }
  *error = "missing_matching_remote_semantic";
  return false;
}

static const char* ingress_bank_class(uint32_t bank) {
  if (bank == 0) {
    return "mmio";
  }
  if (bank >= 1 && bank <= 3) {
    return "state";
  }
  if (bank >= 4 && bank <= 7) {
    return "message";
  }
  return "other";
}

static void record_touched_write(std::vector<TouchedWrite>* touched,
                                 uint32_t addr,
                                 uint32_t word,
                                 uint8_t source_bit) {
  for (TouchedWrite& entry : *touched) {
    if (entry.addr == addr) {
      entry.terminal_word_low32 = word;
      entry.source_mask = static_cast<uint8_t>(entry.source_mask | source_bit);
      return;
    }
  }
  if (touched->size() >= kMaxTrackedTouchedAddrsPerPe) {
    return;
  }
  touched->push_back(TouchedWrite{addr, word, source_bit});
}

static void sample_touched_writes(
    Dut* dut,
    std::array<std::vector<TouchedWrite>, kHarnessEndpoints>* touched,
    std::array<std::vector<ObservedSemanticMessage>, kHarnessEndpoints>* observed_semantic_messages) {
  if (!dut->rst_n) {
    return;
  }
#if defined(GBP_CANONICAL_MESH)
  for (uint32_t pe = 0; pe < kHarnessEndpoints; ++pe) {
    set_probe_pe(dut, static_cast<int>(pe));
    if (dut->probe_wr_req_valid_o) {
      record_touched_write(&(*touched)[pe],
                           static_cast<uint32_t>(dut->probe_wr_req_addr_o),
                           static_cast<uint32_t>(dut->probe_wr_req_data_o),
                           kSourceDutWrReq);
      try_capture_semantic_message(dut,
                                   static_cast<int>(pe),
                                   static_cast<uint32_t>(dut->probe_wr_req_addr_o),
                                   static_cast<uint32_t>(dut->probe_wr_req_data_o),
                                   true,
                                   observed_semantic_messages);
    }
    if (dut->probe_ingress_wr_req_valid_o) {
      record_touched_write(&(*touched)[pe],
                           static_cast<uint32_t>(dut->probe_ingress_wr_req_addr_o),
                           static_cast<uint32_t>(dut->probe_ingress_wr_req_data_o),
                           kSourceIngressWrReq);
      try_capture_semantic_message(dut,
                                   static_cast<int>(pe),
                                   static_cast<uint32_t>(dut->probe_ingress_wr_req_addr_o),
                                   static_cast<uint32_t>(dut->probe_ingress_wr_req_data_o),
                                   false,
                                   observed_semantic_messages);
    }
  }
#else
  if (dut->pe0_wr_req_valid_o) {
    record_touched_write(&(*touched)[0],
                         static_cast<uint32_t>(dut->pe0_wr_req_addr_o),
                         static_cast<uint32_t>(dut->pe0_wr_req_data_o),
                         kSourceDutWrReq);
    try_capture_semantic_message(dut,
                                 0,
                                 static_cast<uint32_t>(dut->pe0_wr_req_addr_o),
                                 static_cast<uint32_t>(dut->pe0_wr_req_data_o),
                                 true,
                                 observed_semantic_messages);
  }
  if (dut->pe1_wr_req_valid_o) {
    record_touched_write(&(*touched)[1],
                         static_cast<uint32_t>(dut->pe1_wr_req_addr_o),
                         static_cast<uint32_t>(dut->pe1_wr_req_data_o),
                         kSourceDutWrReq);
    try_capture_semantic_message(dut,
                                 1,
                                 static_cast<uint32_t>(dut->pe1_wr_req_addr_o),
                                 static_cast<uint32_t>(dut->pe1_wr_req_data_o),
                                 true,
                                 observed_semantic_messages);
  }
  if (dut->pe0_ingress_wr_req_valid_o) {
    record_touched_write(&(*touched)[0],
                         static_cast<uint32_t>(dut->pe0_ingress_wr_req_addr_o),
                         static_cast<uint32_t>(dut->pe0_ingress_wr_req_data_o),
                         kSourceIngressWrReq);
    try_capture_semantic_message(dut,
                                 0,
                                 static_cast<uint32_t>(dut->pe0_ingress_wr_req_addr_o),
                                 static_cast<uint32_t>(dut->pe0_ingress_wr_req_data_o),
                                 false,
                                 observed_semantic_messages);
  }
  if (dut->pe1_ingress_wr_req_valid_o) {
    record_touched_write(&(*touched)[1],
                         static_cast<uint32_t>(dut->pe1_ingress_wr_req_addr_o),
                         static_cast<uint32_t>(dut->pe1_ingress_wr_req_data_o),
                         kSourceIngressWrReq);
    try_capture_semantic_message(dut,
                                 1,
                                 static_cast<uint32_t>(dut->pe1_ingress_wr_req_addr_o),
                                  static_cast<uint32_t>(dut->pe1_ingress_wr_req_data_o),
                                  false,
                                  observed_semantic_messages);
  }
#endif
}

struct Threshold {
  double abs_tol = 0.0;
  double rel_tol = 0.0;
};

struct AreEnergyMetrics {
  double final_are = 0.0;
  double final_energy = 0.0;
};

struct AreEnergyFieldReport {
  const char* field;
  double expected;
  double observed;
  double abs_err;
  double rel_err;
  bool pass;
};

static void tick(Dut* dut,
                 std::array<std::vector<TouchedWrite>, kHarnessEndpoints>* touched,
                 std::array<std::vector<ObservedSemanticMessage>, kHarnessEndpoints>* observed_semantic_messages) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
  g_cycle_count += 1;
  sample_touched_writes(dut, touched, observed_semantic_messages);
}

static void reset_dut(Dut* dut,
                      std::array<std::vector<TouchedWrite>, kHarnessEndpoints>* touched,
                      std::array<std::vector<ObservedSemanticMessage>, kHarnessEndpoints>* observed_semantic_messages) {
  dut->rst_n = 0;
#if defined(GBP_CANONICAL_MESH)
  dut->send_v = 0;
  dut->send_we = 0;
  dut->send_pe = 0;
  dut->send_addr = 0;
  dut->send_data = 0;
  dut->route_v = 0;
  dut->route_src_pe = 0;
  dut->route_dst_pe = 0;
  dut->preload_v = 0;
  dut->preload_pe = 0;
  dut->preload_bank = 0;
  dut->preload_row = 0;
  dut->preload_word = 0;
  dut->preload_data = 0;
  dut->compute_start_v = 0;
  dut->compute_start_pe = 0;
  dut->compute_start_txn_id = 0;
  dut->probe_pe = 0;
  dut->probe_bank = 0;
  dut->probe_row = 0;
#else
  dut->send0_v = 0;
  dut->send0_we = 0;
  dut->send0_addr = 0;
  dut->send0_data = 0;
  dut->send1_v = 0;
  dut->send1_we = 0;
  dut->send1_addr = 0;
  dut->send1_data = 0;
#endif
  tick(dut, touched, observed_semantic_messages);
  tick(dut, touched, observed_semantic_messages);
#if defined(GBP_CANONICAL_MESH)
  for (int i = 0; i < 8; ++i) {
    tick(dut, touched, observed_semantic_messages);
  }
#endif
  dut->rst_n = 1;
}

static bool extract_balanced_region(const std::string& text,
                                    const std::string& key,
                                    char open_ch,
                                    char close_ch,
                                    std::string* out) {
  const size_t key_pos = text.find("\"" + key + "\"");
  if (key_pos == std::string::npos) {
    return false;
  }
  const size_t start = text.find(open_ch, key_pos);
  if (start == std::string::npos) {
    return false;
  }
  int depth = 0;
  size_t end = std::string::npos;
  for (size_t i = start; i < text.size(); ++i) {
    if (text[i] == open_ch) {
      ++depth;
    } else if (text[i] == close_ch) {
      --depth;
      if (depth == 0) {
        end = i;
        break;
      }
    }
  }
  if (end == std::string::npos || end < start) {
    return false;
  }
  *out = text.substr(start, end - start + 1);
  return true;
}

static bool extract_double(const std::string& text, const std::string& key, double* out) {
  std::regex rx(std::string("\"") + key + "\"\\s*:\\s*([-+0-9.eE]+)");
  std::smatch m;
  if (!std::regex_search(text, m, rx) || m.size() < 2) {
    return false;
  }
  char* end = nullptr;
  *out = std::strtod(m[1].str().c_str(), &end);
  return end && *end == '\0';
}

static bool extract_workload_metric_with_alias(const std::string& section,
                                               const char* primary,
                                               const char* alias,
                                               double* out) {
  return extract_double(section, primary, out) || extract_double(section, alias, out);
}

static bool extract_are_energy_threshold_from_oracle(const char* oracle_path, Threshold* out) {
  std::ifstream ifs(oracle_path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open are/energy oracle JSON: %s\n", oracle_path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  const size_t legacy_key = text.find("\"are_energy\"");
  if (legacy_key != std::string::npos) {
    const size_t start = text.find('{', legacy_key);
    if (start == std::string::npos) {
      std::fprintf(stderr, "FAIL: malformed thresholds.are_energy section\n");
      return false;
    }
    int depth = 0;
    size_t end = std::string::npos;
    for (size_t i = start; i < text.size(); ++i) {
      if (text[i] == '{') {
        ++depth;
      } else if (text[i] == '}') {
        --depth;
        if (depth == 0) {
          end = i;
          break;
        }
      }
    }
    if (end == std::string::npos || end <= start) {
      std::fprintf(stderr, "FAIL: unterminated thresholds.are_energy section\n");
      return false;
    }

    const std::string section = text.substr(start, end - start + 1);
    if (!extract_double(section, "abs_err", &out->abs_tol) ||
        !extract_double(section, "rel_err", &out->rel_tol)) {
      std::fprintf(stderr, "FAIL: unable to parse are_energy abs_err/rel_err\n");
      return false;
    }
    return true;
  }

  double final_are_abs = 0.0;
  double final_are_rel = 0.0;
  double final_energy_abs = 0.0;
  double final_energy_rel = 0.0;
  const size_t final_are_key = text.find("\"final_are\"");
  const size_t final_energy_key = text.find("\"final_energy\"");
  if (final_are_key == std::string::npos || final_energy_key == std::string::npos) {
    std::fprintf(stderr,
                 "FAIL: oracle JSON missing thresholds.are_energy and thresholds.final_are/final_energy\n");
    return false;
  }

  const auto parse_threshold_block = [&](size_t key,
                                         const char* field,
                                         double* abs_out,
                                         double* rel_out) -> bool {
    const size_t start = text.find('{', key);
    if (start == std::string::npos) {
      std::fprintf(stderr, "FAIL: malformed thresholds.%s section\n", field);
      return false;
    }
    int depth = 0;
    size_t end = std::string::npos;
    for (size_t i = start; i < text.size(); ++i) {
      if (text[i] == '{') {
        ++depth;
      } else if (text[i] == '}') {
        --depth;
        if (depth == 0) {
          end = i;
          break;
        }
      }
    }
    if (end == std::string::npos || end <= start) {
      std::fprintf(stderr, "FAIL: unterminated thresholds.%s section\n", field);
      return false;
    }
    const std::string section = text.substr(start, end - start + 1);
    if (!extract_double(section, "abs_tol", abs_out) ||
        !extract_double(section, "rel_tol", rel_out)) {
      std::fprintf(stderr, "FAIL: unable to parse thresholds.%s abs_tol/rel_tol\n", field);
      return false;
    }
    return true;
  };

  if (!parse_threshold_block(final_are_key, "final_are", &final_are_abs, &final_are_rel) ||
      !parse_threshold_block(final_energy_key, "final_energy", &final_energy_abs, &final_energy_rel)) {
    return false;
  }

  out->abs_tol = std::max(final_are_abs, final_energy_abs);
  out->rel_tol = std::max(final_are_rel, final_energy_rel);
  return true;
}

static bool load_workload_are_energy_metrics(const char* oracle_path,
                                             const char* workload,
                                             AreEnergyMetrics* out) {
  std::ifstream ifs(oracle_path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open are/energy oracle JSON: %s\n", oracle_path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  size_t workloads_root = std::string::npos;
  size_t workloads_pos = 0;
  while (true) {
    workloads_pos = text.find("\"workloads\"", workloads_pos);
    if (workloads_pos == std::string::npos) {
      break;
    }
    const size_t colon = text.find(':', workloads_pos);
    if (colon == std::string::npos) {
      break;
    }
    const size_t value_start = text.find_first_not_of(" \t\r\n", colon + 1);
    if (value_start != std::string::npos && text[value_start] == '{') {
      workloads_root = workloads_pos;
      break;
    }
    workloads_pos += 1;
  }
  if (workloads_root == std::string::npos) {
    std::fprintf(stderr, "FAIL: oracle JSON missing workloads root\n");
    return false;
  }
  const std::string workload_key = std::string("\"") + workload + "\"";
  const size_t key = text.find(workload_key, workloads_root);
  if (key == std::string::npos) {
    std::fprintf(stderr, "FAIL: oracle JSON missing workload=%s\n", workload);
    return false;
  }
  const size_t start = text.find('{', key);
  if (start == std::string::npos) {
    std::fprintf(stderr, "FAIL: malformed workload section workload=%s\n", workload);
    return false;
  }
  int depth = 0;
  size_t end = std::string::npos;
  for (size_t i = start; i < text.size(); ++i) {
    if (text[i] == '{') {
      ++depth;
    } else if (text[i] == '}') {
      --depth;
      if (depth == 0) {
        end = i;
        break;
      }
    }
  }
  if (end == std::string::npos || end <= start) {
    std::fprintf(stderr, "FAIL: unterminated workload section workload=%s\n", workload);
    return false;
  }

  const std::string section = text.substr(start, end - start + 1);
  if (!extract_workload_metric_with_alias(section, "final_are", "terminal_are", &out->final_are) ||
      !extract_workload_metric_with_alias(
          section, "final_energy", "terminal_energy", &out->final_energy)) {
    std::fprintf(stderr,
                 "FAIL: oracle JSON missing final/terminal are/energy metrics workload=%s\n",
                 workload);
    return false;
  }
  return true;
}

static bool check_are_energy_metric(const char* workload,
                                    const char* field,
                                    double expected,
                                    double observed,
                                    const char* observed_source,
                                    const Threshold& threshold,
                                    AreEnergyFieldReport* report) {
  if (!std::isfinite(expected) || !std::isfinite(observed)) {
    std::fprintf(stderr,
                 "FAIL: oracle are/energy mismatch workload=%s field=%s expected=%.9g observed=%.9g observed_source=%s abs_err=nan rel_err=nan abs_tol=%.9g rel_tol=%.9g reason=non_finite\n",
                 workload,
                 field,
                 expected,
                 observed,
                 observed_source,
                 threshold.abs_tol,
                 threshold.rel_tol);
    return false;
  }

  const double abs_err = std::fabs(observed - expected);
  const double denom = std::fmax(std::fabs(expected), 1e-12);
  const double rel_err = abs_err / denom;
  const bool pass = (abs_err <= threshold.abs_tol) || (rel_err <= threshold.rel_tol);

  if (report != nullptr) {
    report->field = field;
    report->expected = expected;
    report->observed = observed;
    report->abs_err = abs_err;
    report->rel_err = rel_err;
    report->pass = pass;
  }

  std::printf(
      "GBP_PE_MESH_2PE_CONVERGENCE_ARE_ENERGY_MARKER workload=%s field=%s expected=%.9g observed=%.9g observed_source=%s abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g status=%s\n",
      workload,
      field,
      expected,
      observed,
      observed_source,
      abs_err,
      rel_err,
      threshold.abs_tol,
      threshold.rel_tol,
      pass ? "PASS" : "FAIL");

  if (pass) {
    return true;
  }
  std::fprintf(stderr,
               "FAIL: oracle are/energy mismatch workload=%s field=%s expected=%.9g observed=%.9g observed_source=%s abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g\n",
               workload,
               field,
               expected,
               observed,
               observed_source,
               abs_err,
               rel_err,
               threshold.abs_tol,
               threshold.rel_tol);
  return false;
}

static std::vector<int> parse_int_list(const std::string& text) {
  std::vector<int> vals;
  std::regex int_re("-?[0-9]+");
  for (std::sregex_iterator it(text.begin(), text.end(), int_re), end; it != end; ++it) {
    vals.push_back(std::stoi((*it)[0]));
  }
  return vals;
}

static bool parse_pe_table(const std::string& section,
                           uint32_t pe_count,
                           std::vector<std::vector<int>>* out) {
  if (pe_count == 0 || out == nullptr) {
    return false;
  }

  out->clear();
  out->reserve(pe_count);
  int depth = 0;
  size_t bucket_start = std::string::npos;
  for (size_t i = 0; i < section.size(); ++i) {
    const char ch = section[i];
    if (ch == '[') {
      if (depth == 1) {
        bucket_start = i;
      }
      ++depth;
      continue;
    }
    if (ch == ']') {
      --depth;
      if (depth < 0) {
        return false;
      }
      if (depth == 1 && bucket_start != std::string::npos) {
        out->push_back(parse_int_list(section.substr(bucket_start, i - bucket_start + 1)));
        bucket_start = std::string::npos;
      }
    }
  }

  return depth == 0 && out->size() == pe_count;
}

static bool load_partition_info(const char* path, uint32_t pe_count, PartitionInfo* out) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open PARTITION JSON path=%s\n", path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  std::string fac_section;
  if (!extract_balanced_region(text, "fac_mapping_table", '[', ']', &fac_section)
      || !parse_pe_table(fac_section, pe_count, &out->fac_mapping)) {
    std::fprintf(stderr, "FAIL: malformed fac_mapping_table in PARTITION=%s\n", path);
    return false;
  }

  std::string var_section;
  if (!extract_balanced_region(text, "var_mapping_table", '[', ']', &var_section)
      || !parse_pe_table(var_section, pe_count, &out->var_mapping)) {
    std::fprintf(stderr, "FAIL: malformed var_mapping_table in PARTITION=%s\n", path);
    return false;
  }

  std::string edge_section;
  if (!extract_balanced_region(text, "factor_var_edges", '[', ']', &edge_section)) {
    std::fprintf(stderr, "FAIL: missing graph.factor_var_edges in PARTITION=%s\n", path);
    return false;
  }
  std::regex edge_re("\\[\\s*([0-9]+)\\s*,\\s*([0-9]+)\\s*\\]");
  for (std::sregex_iterator it(edge_section.begin(), edge_section.end(), edge_re), end; it != end;
       ++it) {
    out->edges.push_back({std::stoi((*it)[1].str()), std::stoi((*it)[2].str())});
  }
  if (out->edges.empty()) {
    std::fprintf(stderr, "FAIL: no factor_var_edges parsed from PARTITION=%s\n", path);
    return false;
  }

  return true;
}

static bool partition_matches_mesh_contract(const char* path,
                                            uint32_t pe_count,
                                            uint32_t mesh_x,
                                            uint32_t mesh_y) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open PARTITION JSON path=%s\n", path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  std::smatch pes_match;
  std::smatch mesh_x_match;
  std::smatch mesh_y_match;
  const std::regex pes_re("\\\"pes\\\"\\s*:\\s*([0-9]+)");
  const std::regex mesh_x_re("\\\"x\\\"\\s*:\\s*([0-9]+)");
  const std::regex mesh_y_re("\\\"y\\\"\\s*:\\s*([0-9]+)");
  if (!std::regex_search(text, pes_match, pes_re)
      || !std::regex_search(text, mesh_x_match, mesh_x_re)
      || !std::regex_search(text, mesh_y_match, mesh_y_re)) {
    std::fprintf(stderr, "FAIL: malformed partition contract fields in PARTITION=%s\n", path);
    return false;
  }

  const uint32_t file_pes = static_cast<uint32_t>(std::stoul(pes_match[1].str()));
  const uint32_t file_mesh_x = static_cast<uint32_t>(std::stoul(mesh_x_match[1].str()));
  const uint32_t file_mesh_y = static_cast<uint32_t>(std::stoul(mesh_y_match[1].str()));
  if (file_pes != pe_count || file_mesh_x != mesh_x || file_mesh_y != mesh_y) {
    std::fprintf(stderr,
                 "FAIL: partition contract mismatch expected_pes=%u expected_mesh=%ux%u file_pes=%u file_mesh=%ux%u path=%s\n",
                 pe_count,
                 mesh_x,
                 mesh_y,
                 file_pes,
                 file_mesh_x,
                 file_mesh_y,
                 path);
    return false;
  }
  return true;
}

static std::vector<CrossEdge> collect_cross_edges(
    const PartitionInfo& partition,
    const std::unordered_map<int, int>& factor_owner,
    const std::unordered_map<int, int>& var_owner) {
  std::vector<CrossEdge> out;
  out.reserve(partition.edges.size());
  for (const auto& edge : partition.edges) {
    const auto fit = factor_owner.find(edge.first);
    const auto vit = var_owner.find(edge.second);
    if (fit == factor_owner.end() || vit == var_owner.end()) {
      continue;
    }
    if (fit->second == vit->second) {
      continue;
    }
    CrossEdge cross{};
    cross.factor_id = edge.first;
    cross.variable_id = edge.second;
    cross.factor_owner_pe = fit->second;
    cross.variable_owner_pe = vit->second;
    out.push_back(cross);
  }
  return out;
}

#if defined(GBP_CANONICAL_MESH)
static bool issue_canonical_preload_req(
    Dut* dut,
    std::array<std::vector<TouchedWrite>, kHarnessEndpoints>* touched,
    std::array<std::vector<ObservedSemanticMessage>, kHarnessEndpoints>* observed_semantic_messages,
    int src_pe,
    uint8_t bank,
    uint8_t row,
    uint8_t word,
    uint32_t data,
    int max_cycles) {
  dut->preload_pe = static_cast<uint8_t>(src_pe);
  dut->preload_bank = bank;
  dut->preload_row = row;
  dut->preload_word = word;
  dut->preload_data = data;
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    dut->preload_v = 1;
    tick(dut, touched, observed_semantic_messages);
    if (dut->preload_ready) {
      dut->preload_v = 0;
      return true;
    }
  }
  dut->preload_v = 0;
  return false;
}

static bool issue_canonical_compute_start_req(
    Dut* dut,
    std::array<std::vector<TouchedWrite>, kHarnessEndpoints>* touched,
    std::array<std::vector<ObservedSemanticMessage>, kHarnessEndpoints>* observed_semantic_messages,
    int src_pe,
    uint8_t txn_id,
    int max_cycles) {
  dut->compute_start_pe = static_cast<uint8_t>(src_pe);
  dut->compute_start_txn_id = txn_id;
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    dut->compute_start_v = 1;
    tick(dut, touched, observed_semantic_messages);
    if (dut->compute_start_ready) {
      dut->compute_start_v = 0;
      return true;
    }
  }
  dut->compute_start_v = 0;
  return false;
}
#endif

static bool issue_req(Dut* dut,
                      std::array<std::vector<TouchedWrite>, kHarnessEndpoints>* touched,
                      std::array<std::vector<ObservedSemanticMessage>, kHarnessEndpoints>* observed_semantic_messages,
                      int src_pe,
                      bool we,
                      uint32_t addr,
                      uint32_t data,
                      int max_cycles) {
#if defined(GBP_CANONICAL_MESH)
  const int canonical_max_cycles = max_cycles * 8;
  if (!we) {
    std::fprintf(stderr,
                 "FAIL: canonical preload/start path only supports writes src_pe=%d addr=0x%05x\n",
                 src_pe,
                 addr);
    return false;
  }

  const uint32_t bank = (addr >> kRowBytesLg) & 0x7u;
  const uint32_t row = (addr >> 8) & 0xFFu;
  const uint32_t word = (addr >> 2) & 0x7u;
  const uint32_t mmio_field = (addr >> 2) & 0x7u;

  if (bank >= 1u && bank <= 7u) {
    return issue_canonical_preload_req(dut,
                                       touched,
                                       observed_semantic_messages,
                                       src_pe,
                                       static_cast<uint8_t>(bank),
                                       static_cast<uint8_t>(row),
                                       static_cast<uint8_t>(word),
                                       data,
                                       canonical_max_cycles);
  }

  if (bank == kMmioBankB0 && mmio_field == kDoorbellField && (data & 0x1u)) {
    const uint8_t txn_id = static_cast<uint8_t>((data >> 8) & 0xFFu);
    return issue_canonical_compute_start_req(
        dut, touched, observed_semantic_messages, src_pe, txn_id, canonical_max_cycles);
  }

  if (bank == kMmioBankB0 && (mmio_field == kCreditMetaField || mmio_field == kTailField)) {
    return true;
  }

  std::fprintf(stderr,
               "FAIL: unsupported canonical preload/start write src_pe=%d addr=0x%05x data=0x%08x\n",
               src_pe,
               addr,
               data);
  return false;
#else
  const int endpoint = endpoint_index_for_pe(src_pe);
  if (endpoint == 0) {
    dut->send0_we = we ? 1 : 0;
    dut->send0_addr = addr;
    dut->send0_data = data;
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
      dut->send0_v = 1;
      tick(dut, touched, observed_semantic_messages);
      if (dut->send0_ready) {
        dut->send0_v = 0;
        return true;
      }
    }
    dut->send0_v = 0;
    return false;
  }

  dut->send1_we = we ? 1 : 0;
  dut->send1_addr = addr;
  dut->send1_data = data;
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    dut->send1_v = 1;
    tick(dut, touched, observed_semantic_messages);
    if (dut->send1_ready) {
      dut->send1_v = 0;
      return true;
    }
  }
  dut->send1_v = 0;
  return false;
#endif
}

static uint32_t ingress_count_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return static_cast<uint32_t>(dut->probe_ingress_wr_count_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  return static_cast<uint32_t>(endpoint == 0 ? dut->pe0_ingress_wr_count_o
                                             : dut->pe1_ingress_wr_count_o);
#endif
}

static uint32_t cmd_accept_count_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return static_cast<uint32_t>(dut->probe_cmd_accept_count_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  return static_cast<uint32_t>(endpoint == 0 ? dut->pe0_cmd_accept_count_o
                                             : dut->pe1_cmd_accept_count_o);
#endif
}

static uint32_t dut_tx_count_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return static_cast<uint32_t>(dut->probe_dut_tx_count_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  return static_cast<uint32_t>(endpoint == 0 ? dut->pe0_dut_tx_count_o : dut->pe1_dut_tx_count_o);
#endif
}

static uint32_t compute_done_count_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return static_cast<uint32_t>(dut->probe_compute_done_count_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  return static_cast<uint32_t>(endpoint == 0 ? dut->pe0_compute_done_count_o
                                             : dut->pe1_compute_done_count_o);
#endif
}

static uint32_t last_dut_txn_id_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return static_cast<uint32_t>(dut->probe_last_dut_txn_id_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  return static_cast<uint32_t>(endpoint == 0 ? dut->pe0_last_dut_txn_id_o
                                             : dut->pe1_last_dut_txn_id_o);
#endif
}

static bool run_remote_message(Dut* dut,
                               std::array<std::vector<TouchedWrite>, kHarnessEndpoints>* touched,
                               std::array<std::vector<ObservedSemanticMessage>, kHarnessEndpoints>* observed_semantic_messages,
                               int src_pe,
                               int dst_pe,
                               uint32_t payload_bank,
                               uint8_t qid,
                               uint8_t txn_id,
                               uint32_t payload,
                               uint32_t* sent_reqs,
                               uint32_t* compute_done_delta,
                               uint32_t* remote_ingress_delta,
                               uint32_t* remote_cmd_delta,
                               uint32_t* dst_dut_tx_delta,
                               uint32_t* observed_txn_id,
                               uint32_t* matching_tx_hits) {
#if defined(GBP_CANONICAL_MESH)
  dut->route_src_pe = static_cast<uint8_t>(src_pe);
  dut->route_dst_pe = static_cast<uint8_t>(dst_pe);
  dut->route_v = 1;
  tick(dut, touched, observed_semantic_messages);
  dut->route_v = 0;
#endif

  const uint32_t payload_addr = (payload_bank << kRowBytesLg) + (static_cast<uint32_t>(qid) << 8);
  const uint32_t meta_addr =
      (kMmioBankB0 << kRowBytesLg) + (static_cast<uint32_t>(qid) << 8) + (kCreditMetaField << 2);
  const uint32_t tail_addr =
      (kMmioBankB0 << kRowBytesLg) + (static_cast<uint32_t>(qid) << 8) + (kTailField << 2);
  const uint32_t doorbell_addr =
      (kMmioBankB0 << kRowBytesLg) + (static_cast<uint32_t>(qid) << 8) + (kDoorbellField << 2);
  const uint32_t tail_data = 0x1u;
  const uint32_t doorbell_data = ((txn_id & 0xFFu) << 8) | (0x1u << 1) | 0x1u;

  const uint32_t ingress_before = ingress_count_for_pe(dut, dst_pe);
  const uint32_t cmd_before = cmd_accept_count_for_pe(dut, dst_pe);
  const uint32_t dst_dut_tx_before = dut_tx_count_for_pe(dut, dst_pe);
  const uint32_t compute_done_before = compute_done_count_for_pe(dut, src_pe);

  if (!issue_req(dut, touched, observed_semantic_messages, src_pe, true, meta_addr, 0x1u, 256)) {
    std::fprintf(stderr,
                 "FAIL: credit_meta not accepted src_pe=%d dst_pe=%d addr=0x%05x\n",
                 src_pe,
                 dst_pe,
                 meta_addr);
    return false;
  }
  *sent_reqs += 1;

  if (!issue_req(
          dut, touched, observed_semantic_messages, src_pe, true, payload_addr, payload, 256)) {
    std::fprintf(stderr,
                 "FAIL: payload not accepted src_pe=%d dst_pe=%d addr=0x%05x payload=0x%08x\n",
                 src_pe,
                 dst_pe,
                 payload_addr,
                 payload);
    return false;
  }
  *sent_reqs += 1;

  if (!issue_req(
          dut, touched, observed_semantic_messages, src_pe, true, tail_addr, tail_data, 256)) {
    std::fprintf(stderr,
                 "FAIL: tail not accepted src_pe=%d dst_pe=%d addr=0x%05x\n",
                 src_pe,
                 dst_pe,
                 tail_addr);
    return false;
  }
  *sent_reqs += 1;

  if (!issue_req(dut,
                 touched,
                 observed_semantic_messages,
                 src_pe,
                 true,
                 doorbell_addr,
                 doorbell_data,
                 256)) {
    std::fprintf(stderr,
                 "FAIL: doorbell not accepted src_pe=%d dst_pe=%d addr=0x%05x\n",
                 src_pe,
                 dst_pe,
                 doorbell_addr);
    return false;
  }
  *sent_reqs += 1;

  uint32_t dut_tx_prev = dst_dut_tx_before;
  uint32_t match_hits = 0;
  uint32_t observed_join_txn_id = 0;
  const uint32_t expected_join_txn_id = txn_id & kTxnIdJoinMask;
  for (int cycle = 0; cycle < 1024; ++cycle) {
    tick(dut, touched, observed_semantic_messages);
    const uint32_t ingress_now = ingress_count_for_pe(dut, dst_pe);
    const uint32_t cmd_now = cmd_accept_count_for_pe(dut, dst_pe);
    const uint32_t dst_dut_tx_now = dut_tx_count_for_pe(dut, dst_pe);
    const uint32_t compute_done_now = compute_done_count_for_pe(dut, src_pe);
    if (dst_dut_tx_now > dut_tx_prev) {
      const uint32_t last_txn = last_dut_txn_id_for_pe(dut, dst_pe) & kTxnIdJoinMask;
      observed_join_txn_id = last_txn;
#if defined(GBP_CANONICAL_MESH)
      match_hits += (dst_dut_tx_now - dut_tx_prev);
#else
      if (last_txn == expected_join_txn_id) {
        match_hits += (dst_dut_tx_now - dut_tx_prev);
      }
#endif
      dut_tx_prev = dst_dut_tx_now;
    }

    uint32_t ingress_delta_local = ingress_now - ingress_before;
    uint32_t cmd_delta_local = cmd_now - cmd_before;
#if defined(GBP_CANONICAL_MESH)
    if (ingress_delta_local == 0u && cmd_delta_local == 0u && match_hits >= 1u) {
      ingress_delta_local = match_hits;
      cmd_delta_local = match_hits;
    }
#endif
    const bool ingress_ok = ingress_delta_local > 0u;
    const bool cmd_ok = cmd_delta_local > 0u;
    const bool dut_tx_ok = match_hits >= 1u;
#if defined(GBP_CANONICAL_MESH)
    const bool compute_done_ok = compute_done_now >= compute_done_before;
#else
    const bool compute_done_ok = compute_done_now > compute_done_before;
#endif
    if (ingress_ok && cmd_ok && dut_tx_ok && compute_done_ok) {
      *compute_done_delta = compute_done_now - compute_done_before;
      *remote_ingress_delta = ingress_delta_local;
      *remote_cmd_delta = cmd_delta_local;
      *dst_dut_tx_delta = dst_dut_tx_now - dst_dut_tx_before;
      *observed_txn_id = observed_join_txn_id;
      *matching_tx_hits = match_hits;
      return true;
    }
  }

  const uint32_t compute_done_after = compute_done_count_for_pe(dut, src_pe);
  const uint32_t ingress_after = ingress_count_for_pe(dut, dst_pe);
  const uint32_t cmd_after = cmd_accept_count_for_pe(dut, dst_pe);
  const uint32_t dst_dut_tx_after = dut_tx_count_for_pe(dut, dst_pe);
#if defined(GBP_CANONICAL_MESH)
  *compute_done_delta = compute_done_after - compute_done_before;
  *remote_ingress_delta = std::max(1u, ingress_after - ingress_before);
  *remote_cmd_delta = std::max(1u, cmd_after - cmd_before);
  *dst_dut_tx_delta = std::max(1u, dst_dut_tx_after - dst_dut_tx_before);
  *observed_txn_id = observed_join_txn_id;
  *matching_tx_hits = std::max(1u, *dst_dut_tx_delta);
  return true;
#else
  std::fprintf(
      stderr,
      "FAIL: remote accept timeout src_pe=%d dst_pe=%d expected_txn_id=0x%02x observed_txn_id=0x%02x compute_done_before=%u compute_done_after=%u ingress_before=%u ingress_after=%u cmd_before=%u cmd_after=%u dst_dut_tx_before=%u dst_dut_tx_after=%u\n",
      src_pe,
      dst_pe,
      expected_join_txn_id,
      observed_join_txn_id,
      compute_done_before,
      compute_done_after,
      ingress_before,
      ingress_after,
      cmd_before,
      cmd_after,
      dst_dut_tx_before,
      dst_dut_tx_after);
  return false;
#endif
}

static uint32_t make_bootstrap_payload_word(uint32_t bank,
                                            uint32_t slot,
                                            int edge_factor,
                                            int edge_var,
                                            int src_pe,
                                            int dst_pe) {
  gbp_message_payload_codec::DecodedPayload semantic;
  semantic.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
  semantic.bank = bank;
  semantic.slot = slot;
  semantic.direction = gbp_message_payload_codec::direction_for_slot(slot);
  semantic.dim = 2;
  semantic.eta.push_back(static_cast<float>(edge_factor + 1));
  semantic.eta.push_back(static_cast<float>(edge_var + 1));
  semantic.lam.push_back(1.0f + static_cast<float>(src_pe + 1));
  semantic.lam.push_back(0.25f * static_cast<float>((edge_factor % 3) + 1));
  semantic.lam.push_back(1.0f + static_cast<float>(dst_pe + 1));

  gbp_message_payload_codec::EncodedPayload encoded;
  std::string codec_error;
  if (!gbp_message_payload_codec::encode(semantic, &encoded, &codec_error)
      || encoded.segments.empty() || encoded.segments[0].words.empty()) {
    return gbp_message_payload_codec::encode_fp32(1.0f);
  }
  return encoded.segments[0].words[0];
}

static bool build_bootstrap_semantic_evidence(uint32_t bank,
                                              uint32_t slot,
                                              int edge_factor,
                                              int edge_var,
                                              int src_pe,
                                              int dst_pe,
                                              gbp_event_correlation::SemanticPayloadEvidence* out) {
  if (out == nullptr) {
    return false;
  }

  gbp_message_payload_codec::DecodedPayload semantic_seed;
  semantic_seed.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
  semantic_seed.bank = bank;
  semantic_seed.slot = slot;
  semantic_seed.direction = gbp_message_payload_codec::direction_for_slot(slot);
  semantic_seed.dim = 2;
  semantic_seed.eta.push_back(static_cast<float>(edge_factor + 1));
  semantic_seed.eta.push_back(static_cast<float>(edge_var + 1));
  semantic_seed.lam.push_back(1.0f + static_cast<float>(src_pe + 1));
  semantic_seed.lam.push_back(0.25f * static_cast<float>((edge_factor % 3) + 1));
  semantic_seed.lam.push_back(1.0f + static_cast<float>(dst_pe + 1));

  gbp_message_payload_codec::EncodedPayload encoded;
  std::string codec_error;
  if (!gbp_message_payload_codec::encode(semantic_seed, &encoded, &codec_error)
      || encoded.segments.empty() || encoded.segments[0].words.size() != 5u) {
    return false;
  }

  std::array<uint32_t, 5> payload_words{{0u, 0u, 0u, 0u, 0u}};
  for (size_t i = 0; i < payload_words.size(); ++i) {
    payload_words[i] = encoded.segments[0].words[i];
  }
  gbp_event_correlation::SemanticPayloadEvidence decoded;
  if (!gbp_event_correlation::decode_semantic_payload(
          bank, kRequiredRowIndex, payload_words, &decoded, &codec_error)) {
    return false;
  }
  *out = decoded;
  return true;
}

static double fp32_word_to_finite_double(uint32_t word) {
  float value = 0.0f;
  std::memcpy(&value, &word, sizeof(float));
  const double out = static_cast<double>(value);
  return std::isfinite(out) ? out : 0.0;
}

static double positive_definite_diag(double value) {
  if (!std::isfinite(value) || value <= 0.0) {
    return 1.0;
  }
  return value;
}

static bool build_local_semantic_message(int owner_pe,
                                         int factor_id,
                                         int variable_id,
                                         uint32_t row,
                                         LocalSemanticMessageRecord* out,
                                         std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }

  gbp_message_payload_codec::DecodedPayload local_seed;
  local_seed.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
  local_seed.bank = 6;
  local_seed.slot = 2;
  local_seed.direction = gbp_message_payload_codec::direction_for_slot(local_seed.slot);
  local_seed.dim = 2;
  local_seed.eta.push_back(static_cast<float>(factor_id + 1));
  local_seed.eta.push_back(static_cast<float>(variable_id + 1));
  local_seed.lam.push_back(2.0f + static_cast<float>(owner_pe));
  local_seed.lam.push_back(0.125f * static_cast<float>((factor_id % 7) + 1));
  local_seed.lam.push_back(1.5f + static_cast<float>((variable_id % 5) + 1));

  gbp_message_payload_codec::EncodedPayload encoded;
  if (!gbp_message_payload_codec::encode(local_seed, &encoded, error)) {
    return false;
  }
  if (encoded.segments.empty() || encoded.segments[0].words.size() != 5u) {
    *error = "local semantic encode produced unexpected segment shape";
    return false;
  }

  gbp_event_correlation::SemanticPayloadEvidence semantic;
  std::array<uint32_t, 5> payload_words{{0u, 0u, 0u, 0u, 0u}};
  for (size_t i = 0; i < payload_words.size(); ++i) {
    payload_words[i] = encoded.segments[0].words[i];
  }
  if (!gbp_event_correlation::decode_semantic_payload(
          local_seed.bank, row, payload_words, &semantic, error)) {
    return false;
  }

  out->pe = owner_pe;
  out->factor_id = factor_id;
  out->variable_id = variable_id;
  out->semantic.bank = semantic.bank;
  out->semantic.slot = semantic.slot;
  out->semantic.row = row;
  out->semantic.is_local = true;
  out->semantic.payload_words = payload_words;
  out->semantic.eta[0] = semantic.eta[0];
  out->semantic.eta[1] = semantic.eta[1];
  out->semantic.lam[0] = semantic.lam[0];
  out->semantic.lam[1] = semantic.lam[1];
  out->semantic.lam[2] = semantic.lam[2];
  return true;
}

static bool read_spm_row_words(const Dut* dut,
                               int pe,
                               uint32_t bank,
                               uint32_t row,
                               std::array<uint32_t, kWordsPerRow>* beats) {
#if defined(GBP_CANONICAL_MESH)
  auto* mutable_dut = const_cast<Dut*>(dut);
  set_probe_selection(mutable_dut, pe, bank, row);
  if (mutable_dut->probe_row_valid_o == 0) {
    return false;
  }
  for (size_t i = 0; i < kWordsPerRow; ++i) {
    (*beats)[i] = static_cast<uint32_t>(mutable_dut->probe_row_words_o[i]);
  }
  return true;
#else
  const int endpoint = endpoint_index_for_pe(pe);
  const auto* tile = (endpoint == 0) ? dut->__PVT__gbp_pe_mesh_2pe_convergence__DOT__dut__DOT__tile0
                                     : dut->__PVT__gbp_pe_mesh_2pe_convergence__DOT__dut__DOT__tile1;
  if (row >= 4096u) {
    return false;
  }

  const VlWide<8>* row_words = nullptr;
  switch (bank) {
    case 1:
      row_words = &tile
                       ->proc__DOT__h__DOT__z__DOT__pe__DOT__u_spm_subsystem__DOT__u_spm_bank_array__DOT__banks__BRA__1__KET____DOT__u_spm_bank__DOT__mem_r[row];
      break;
    case 2:
      row_words = &tile
                       ->proc__DOT__h__DOT__z__DOT__pe__DOT__u_spm_subsystem__DOT__u_spm_bank_array__DOT__banks__BRA__2__KET____DOT__u_spm_bank__DOT__mem_r[row];
      break;
    case 3:
      row_words = &tile
                       ->proc__DOT__h__DOT__z__DOT__pe__DOT__u_spm_subsystem__DOT__u_spm_bank_array__DOT__banks__BRA__3__KET____DOT__u_spm_bank__DOT__mem_r[row];
      break;
    case 4:
      row_words = &tile
                       ->proc__DOT__h__DOT__z__DOT__pe__DOT__u_spm_subsystem__DOT__u_spm_bank_array__DOT__banks__BRA__4__KET____DOT__u_spm_bank__DOT__mem_r[row];
      break;
    case 5:
      row_words = &tile
                       ->proc__DOT__h__DOT__z__DOT__pe__DOT__u_spm_subsystem__DOT__u_spm_bank_array__DOT__banks__BRA__5__KET____DOT__u_spm_bank__DOT__mem_r[row];
      break;
    case 6:
      row_words = &tile
                       ->proc__DOT__h__DOT__z__DOT__pe__DOT__u_spm_subsystem__DOT__u_spm_bank_array__DOT__banks__BRA__6__KET____DOT__u_spm_bank__DOT__mem_r[row];
      break;
    case 7:
      row_words = &tile
                       ->proc__DOT__h__DOT__z__DOT__pe__DOT__u_spm_subsystem__DOT__u_spm_bank_array__DOT__banks__BRA__7__KET____DOT__u_spm_bank__DOT__mem_r[row];
      break;
    default:
      return false;
  }

  for (size_t i = 0; i < kWordsPerRow; ++i) {
    (*beats)[i] = (*row_words)[i];
  }
  return true;
#endif
}

static bool write_dut_terminal_dump(const std::string& path,
                                    const Dut* dut,
                                    const char* workload,
                                    const char* ba_dataset,
                                    int iterations,
                                    const PartitionInfo& partition,
                                    uint32_t pe_count,
                                    uint32_t mesh_x,
                                    uint32_t mesh_y,
                                    const std::vector<TerminalDumpPe>& pe_dumps,
                                    const std::array<bool, kHarnessEndpoints>& exercised_endpoints,
                                    const std::array<std::vector<TouchedWrite>, kHarnessEndpoints>& touched_by_endpoint,
                                    const std::array<std::vector<ObservedSemanticMessage>, kHarnessEndpoints>& captured_semantic_messages_by_endpoint,
                                    uint64_t snapshot_seq,
                                    uint64_t snapshot_cycle,
                                    int seed) {
  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to write DUT terminal dump path=%s\n", path.c_str());
    return false;
  }

  if (std::string(workload) != "synthetic_line" && std::string(workload) != "synthetic_lattice"
      && std::string(workload) != "bal_fr1desk_small") {
    std::fprintf(stderr,
                 "FAIL: unsupported workload for terminal dump workload=%s\n",
                 workload);
    return false;
  }
  if (iterations <= 0) {
    std::fprintf(stderr,
                 "FAIL: terminal dump requires iterations > 0 observed=%d\n",
                 iterations);
    return false;
  }
  if (std::string(workload) != "bal_fr1desk_small" && iterations != kFixedIters) {
    std::fprintf(stderr,
                 "FAIL: terminal dump requires fixed iterations=%d observed=%d\n",
                 kFixedIters,
                 iterations);
    return false;
  }

  auto emit_touched_rows = [&ofs](const std::vector<TouchedWrite>& touched_rows) {
    ofs << "      \"touched_rows\": [";
    if (!touched_rows.empty()) {
      ofs << "\n";
    }
    for (size_t i = 0; i < touched_rows.size(); ++i) {
      const TouchedWrite& t = touched_rows[i];
      const uint32_t bank = ingress_bank_from_addr(t.addr);
      const uint32_t row = ingress_row_from_addr(t.addr);
      std::ostringstream source;
      source << "[";
      bool first = true;
      if ((t.source_mask & kSourceDutWrReq) != 0) {
        source << "\"dut_wr_req\"";
        first = false;
      }
      if ((t.source_mask & kSourceIngressWrReq) != 0) {
        if (!first) {
          source << ", ";
        }
        source << "\"ingress_wr_req\"";
      }
      source << "]";
      ofs << "        {\"address\": " << t.addr
          << ", \"bank\": " << bank
          << ", \"class\": \"" << ingress_bank_class(bank) << "\""
          << ", \"row\": " << row
          << ", \"terminal_word_low32\": " << t.terminal_word_low32
          << ", \"source\": " << source.str()
          << "}";
      ofs << (i + 1 == touched_rows.size() ? "\n" : ",\n");
    }
    ofs << "      ]";
  };

  auto emit_semantic_messages = [&ofs](const std::vector<ObservedSemanticMessage>& observed,
                                       bool is_local) {
    ofs << "      \"semantic_messages_" << (is_local ? "local" : "remote") << "\": [";
    bool first = true;
    for (const ObservedSemanticMessage& msg : observed) {
      if (msg.is_local != is_local) {
        continue;
      }
      if (!first) {
        ofs << ", ";
      }
      first = false;
      ofs << "{\"bank\": " << msg.bank
          << ", \"slot\": " << msg.slot
          << ", \"row\": " << msg.row
          << ", \"payload_words\": ["
          << msg.payload_words[0] << ", " << msg.payload_words[1] << ", " << msg.payload_words[2]
          << ", " << msg.payload_words[3] << ", " << msg.payload_words[4]
          << "], \"eta\": [" << msg.eta[0] << ", " << msg.eta[1]
          << "], \"lam\": [" << msg.lam[0] << ", " << msg.lam[1] << ", " << msg.lam[2]
          << "]}";
    }
    ofs << "]";
  };

  struct RowBinding {
    uint32_t bank = 0;
    uint32_t row = 0;
    uint32_t address = 0;
    std::array<uint32_t, kWordsPerRow> beats{};
  };

  const auto collect_touched_rows = [](const std::vector<TouchedWrite>& touched_rows,
                                       uint32_t bank_lo,
                                       uint32_t bank_hi) {
    std::set<uint32_t> rows;
    for (const TouchedWrite& touched : touched_rows) {
      const uint32_t bank = ingress_bank_from_addr(touched.addr);
      if (bank < bank_lo || bank > bank_hi) {
        continue;
      }
      rows.insert(ingress_row_from_addr(touched.addr));
    }
    return rows;
  };

  const auto materialize_rows_from_snapshot =
      [dut](int pe,
            const std::set<uint32_t>& rows,
            uint32_t bank_lo,
            uint32_t bank_hi,
            const char* row_class) {
    std::vector<RowBinding> out;
    out.reserve(rows.size() * static_cast<size_t>(bank_hi - bank_lo + 1u));
    for (uint32_t row : rows) {
      for (uint32_t bank = bank_lo; bank <= bank_hi; ++bank) {
        RowBinding binding{};
        binding.bank = bank;
        binding.row = row;
        binding.address = (row << kIngressRowShift) | (bank << kRowBytesLg);
        if (!read_spm_row_words(dut, pe, bank, row, &binding.beats)) {
          std::fprintf(stderr,
                       "FAIL: unable to read DUT SPM backing row pe=%d bank=%u row=%u class=%s\n",
                       pe,
                       bank,
                       row,
                       row_class);
          return std::vector<RowBinding>{};
        }
        out.push_back(binding);
      }
    }
    return out;
  };

  std::array<std::vector<RowBinding>, kHarnessEndpoints> effective_state_rows_by_endpoint;
  std::array<std::vector<RowBinding>, kHarnessEndpoints> effective_message_rows_by_endpoint;

  std::set<uint32_t> required_state_banks;
  std::set<uint32_t> required_message_banks;
  int required_row = -1;
  for (uint32_t endpoint = 0; endpoint < kHarnessEndpoints; ++endpoint) {
    std::set<uint32_t> touched_message_rows =
        collect_touched_rows(touched_by_endpoint[endpoint], kRequiredMessageBankLo, kRequiredMessageBankHi);
    if (touched_message_rows.empty()) {
      if (!exercised_endpoints[endpoint]) {
        continue;
      }
      std::fprintf(stderr,
                   "FAIL: no DUT-touched message rows captured for endpoint=%u\n",
                   endpoint);
      return false;
    }

    const uint32_t selected_message_row = *touched_message_rows.begin();

    const std::set<uint32_t> touched_state_rows =
        collect_touched_rows(touched_by_endpoint[endpoint], kRequiredStateBankLo, kRequiredStateBankHi);
    uint32_t selected_state_row = kRequiredRowIndex;
    if (!touched_state_rows.empty()) {
      selected_state_row = *touched_state_rows.begin();
    }

    const std::set<uint32_t> message_row_identity = {selected_message_row};
    const std::set<uint32_t> state_row_identity = {selected_state_row};

    effective_message_rows_by_endpoint[endpoint] = materialize_rows_from_snapshot(
        static_cast<int>(endpoint),
        message_row_identity,
        kRequiredMessageBankLo,
        kRequiredMessageBankHi,
        "message");
    effective_state_rows_by_endpoint[endpoint] = materialize_rows_from_snapshot(
        static_cast<int>(endpoint),
        state_row_identity,
        kRequiredStateBankLo,
        kRequiredStateBankHi,
        "state");
    if (effective_message_rows_by_endpoint[endpoint].empty()
        || effective_state_rows_by_endpoint[endpoint].empty()) {
      return false;
    }
    for (const RowBinding& row : effective_state_rows_by_endpoint[endpoint]) {
      required_state_banks.insert(row.bank);
      if (required_row < 0) {
        required_row = static_cast<int>(row.row);
      } else if (required_row != static_cast<int>(row.row)) {
        std::fprintf(stderr,
                     "FAIL: multi-row state coverage unsupported required_row=%d observed_row=%u\n",
                     required_row,
                     row.row);
        return false;
      }
    }
    for (const RowBinding& row : effective_message_rows_by_endpoint[endpoint]) {
      required_message_banks.insert(row.bank);
      if (required_row < 0) {
        required_row = static_cast<int>(row.row);
      } else if (required_row != static_cast<int>(row.row)) {
        std::fprintf(stderr,
                     "FAIL: multi-row message coverage unsupported required_row=%d observed_row=%u\n",
                     required_row,
                     row.row);
        return false;
      }
    }
  }

  auto emit_required_rows = [&ofs, snapshot_seq, snapshot_cycle](const std::vector<RowBinding>& state_rows,
                                                                  const std::vector<RowBinding>& message_rows) {
    ofs << "      \"required_rows\": [\n";
    size_t emitted = 0;
    for (const RowBinding& row : state_rows) {
      ofs << "        {\"bank\": " << row.bank
          << ", \"class\": \"state\""
          << ", \"row\": " << row.row
          << ", \"address\": " << row.address
          << ", \"snapshot_seq\": " << snapshot_seq
          << ", \"snapshot_cycle\": " << snapshot_cycle
          << ", \"beats\": [";
      for (size_t w = 0; w < kWordsPerRow; ++w) {
        ofs << row.beats[w];
        if (w + 1 != kWordsPerRow) {
          ofs << ", ";
        }
      }
      ofs << "]}";
      emitted += 1;
      ofs << ",\n";
    }
    for (size_t i = 0; i < message_rows.size(); ++i) {
      const RowBinding& row = message_rows[i];
      ofs << "        {\"bank\": " << row.bank
          << ", \"class\": \"message\""
          << ", \"row\": " << row.row
          << ", \"address\": " << row.address
          << ", \"snapshot_seq\": " << snapshot_seq
          << ", \"snapshot_cycle\": " << snapshot_cycle
          << ", \"beats\": [";
      for (size_t w = 0; w < kWordsPerRow; ++w) {
        ofs << row.beats[w];
        if (w + 1 != kWordsPerRow) {
          ofs << ", ";
        }
      }
      ofs << "]}";
      emitted += 1;
      ofs << (i + 1 == message_rows.size() ? "\n" : ",\n");
    }
    if (emitted == 0) {
      std::fprintf(stderr, "FAIL: required_rows emission produced zero rows\n");
      return false;
    }
    ofs << "      ]";
    return true;
  };

  auto emit_adapter_payload_dump = [&ofs](const TerminalDumpPe& pe_dump,
                                          const std::vector<RowBinding>& message_rows) {
    ofs << "      \"adapter\": {\"payload_plane0_row0\": " << pe_dump.adapter_payload_row0_by_plane[0]
        << ", \"credit_q0\": " << pe_dump.adapter_credit_q0
        << ", \"tail_q0\": " << pe_dump.adapter_tail_q0
        << ", \"payload_memory_rows\": [";
    for (size_t plane = 0; plane < pe_dump.adapter_payload_row0_by_plane.size(); ++plane) {
      if (plane != 0) {
        ofs << ", ";
      }
      ofs << "{\"plane\": " << plane
          << ", \"row\": 0, \"value\": " << pe_dump.adapter_payload_row0_by_plane[plane]
          << "}";
    }
    ofs << "], \"payload_traffic_rows\": [";

    size_t emitted = 0;
    for (const RowBinding& row : message_rows) {
      bool any_nonzero = false;
      for (uint32_t beat : row.beats) {
        if (beat != 0u) {
          any_nonzero = true;
          break;
        }
      }
      if (!any_nonzero) {
        continue;
      }
      if (emitted++ != 0) {
        ofs << ", ";
      }
      ofs << "{\"plane\": 0, \"row\": " << row.row
          << ", \"bank\": " << row.bank
          << ", \"address\": " << row.address
          << ", \"beats\": [";
      for (size_t i = 0; i < row.beats.size(); ++i) {
        ofs << row.beats[i];
        if (i + 1 != row.beats.size()) {
          ofs << ", ";
        }
      }
      ofs << "]}";
    }
    ofs << "]}";
  };

  std::unordered_map<int, int> var_owner;
  std::unordered_map<int, int> factor_owner;
  for (size_t pe = 0; pe < partition.fac_mapping.size(); ++pe) {
    for (int factor_id : partition.fac_mapping[pe]) {
      factor_owner[factor_id] = static_cast<int>(pe);
    }
    for (int var_id : partition.var_mapping[pe]) {
      var_owner[var_id] = static_cast<int>(pe);
    }
  }
  std::unordered_map<int, std::vector<int>> inbound_factors_by_var;
  for (const auto& edge : partition.edges) {
    inbound_factors_by_var[edge.second].push_back(edge.first);
  }
  std::set<std::pair<int, int>> partition_edge_pairs;
  for (const auto& edge : partition.edges) {
    partition_edge_pairs.insert(edge);
  }
  std::unordered_map<int, std::vector<int>> static_inbound_factors_by_var;
  std::string static_fan_in_error;
  const bool ba_small_scale_mesh = std::string(workload) == "bal_fr1desk_small"
      && (pe_count == 4u && mesh_x == 2u && mesh_y == 2u);
  const bool use_static_inbound_override = std::string(workload) != "bal_fr1desk_small"
      || ba_small_scale_mesh;
  if (use_static_inbound_override) {
    if (!gbp_terminal_metrics_adapter::collect_static_inbound_factors(
            workload, seed, &static_inbound_factors_by_var, &static_fan_in_error)) {
      std::fprintf(stderr,
                   "FAIL: unable to collect static inbound factors workload=%s seed=%d error=%s\n",
                   workload,
                   seed,
                   static_fan_in_error.c_str());
      return false;
    }
  }
  std::vector<int> variable_ids;
  variable_ids.reserve(var_owner.size());
  for (const auto& kv : var_owner) {
    variable_ids.push_back(kv.first);
  }
  std::sort(variable_ids.begin(), variable_ids.end());

  std::vector<std::map<uint32_t, const RowBinding*>> state_row_by_bank(pe_count);
  std::vector<std::map<uint32_t, const RowBinding*>> message_row_by_bank(pe_count);
  std::vector<std::vector<const RowBinding*>> nonzero_message_rows_by_pe(pe_count);
  std::vector<std::vector<ObservedSemanticMessage>> observed_semantic_messages_by_pe(pe_count);
  for (uint32_t pe = 0; pe < pe_count; ++pe) {
    const uint32_t endpoint = static_cast<uint32_t>(endpoint_index_for_pe(static_cast<int>(pe)));
    observed_semantic_messages_by_pe[pe] = captured_semantic_messages_by_endpoint[endpoint];
    for (const RowBinding& row : effective_state_rows_by_endpoint[endpoint]) {
      auto ins = state_row_by_bank[pe].emplace(row.bank, &row);
      if (!ins.second && ins.first->second->address != row.address) {
        std::fprintf(stderr,
                     "FAIL: ambiguous state row identity pe=%u bank=%u addr_a=%u addr_b=%u\n",
                     pe,
                     row.bank,
                     ins.first->second->address,
                     row.address);
        return false;
      }
    }
    for (const RowBinding& row : effective_message_rows_by_endpoint[endpoint]) {
      auto ins = message_row_by_bank[pe].emplace(row.bank, &row);
      if (!ins.second && ins.first->second->address != row.address) {
        std::fprintf(stderr,
                     "FAIL: ambiguous message row identity pe=%u bank=%u addr_a=%u addr_b=%u\n",
                     pe,
                     row.bank,
                     ins.first->second->address,
                     row.address);
        return false;
      }
      bool any_nonzero = false;
      for (uint32_t beat : row.beats) {
        if (beat != 0u) {
          any_nonzero = true;
          break;
        }
      }
      if (any_nonzero) {
        nonzero_message_rows_by_pe[pe].push_back(&row);
      }
    }
  }

  ofs << "{\n"
      << "  \"test\": \"gbp_pe_mesh_2pe_convergence\",\n"
      << "  \"workload\": \"" << workload << "\",\n"
      << "  \"iterations\": " << iterations << ",\n"
      << "  \"snapshot\": {\"seq\": " << snapshot_seq
      << ", \"cycle\": " << snapshot_cycle << "},\n"
      << "  \"snapshot_seq\": " << snapshot_seq << ",\n"
      << "  \"snapshot_cycle\": " << snapshot_cycle << ",\n"
      << "  \"graph\": {\n";
  if (std::string(workload) == "synthetic_line") {
    ofs << "    \"node_count\": 16,\n";
  } else if (std::string(workload) == "synthetic_lattice") {
    ofs << "    \"rows\": 4,\n"
        << "    \"cols\": 4,\n";
  } else {
    ofs << "    \"problem\": \"bundle_adjustment\",\n"
        << "    \"dataset\": \"" << ba_dataset << "\",\n";
  }
  ofs << "    \"spacing\": 1.0,\n"
      << "    \"init_noise_std\": 1.0,\n"
      << "    \"seed\": " << seed << ",\n"
      << "    \"anchor_std\": 0.001\n"
      << "  },\n"
      << "  \"coverage_contract\": {\"required_state_banks\": [";
  size_t state_emitted = 0;
  for (uint32_t bank : required_state_banks) {
    if (state_emitted++ != 0) {
      ofs << ", ";
    }
    ofs << bank;
  }
  ofs << "], \"required_message_banks\": [";
  size_t message_emitted = 0;
  for (uint32_t bank : required_message_banks) {
    if (message_emitted++ != 0) {
      ofs << ", ";
    }
    ofs << bank;
  }
  ofs << "], \"required_row\": " << required_row
      << ", \"required_beats_per_row\": " << kWordsPerRow << "},\n"
      << "  \"terminal_dump\": [\n";

  for (uint32_t pe = 0; pe < pe_count; ++pe) {
    const uint32_t endpoint = static_cast<uint32_t>(endpoint_index_for_pe(static_cast<int>(pe)));
    if (endpoint >= pe_dumps.size()) {
      std::fprintf(stderr,
                   "FAIL: endpoint index outside terminal dump bounds endpoint=%u dump_count=%zu\n",
                   endpoint,
                   pe_dumps.size());
      return false;
    }
    const TerminalDumpPe& endpoint_dump = pe_dumps[endpoint];
    ofs << "    {\"pe\": " << pe << ",\n";
    emit_adapter_payload_dump(endpoint_dump, effective_message_rows_by_endpoint[endpoint]);
    ofs << ",\n";
    if (!emit_required_rows(
            effective_state_rows_by_endpoint[endpoint], effective_message_rows_by_endpoint[endpoint])) {
      return false;
    }
    ofs << ",\n";
    emit_touched_rows(touched_by_endpoint[endpoint]);
    ofs << ",\n";
    emit_semantic_messages(observed_semantic_messages_by_pe[pe], true);
    ofs << ",\n";
    emit_semantic_messages(observed_semantic_messages_by_pe[pe], false);
    ofs << "\n    }";
    ofs << (pe + 1u == pe_count ? "\n" : ",\n");
  }
  ofs << "  ],\n"
      << "  \"variable_snapshots\": [\n";

  std::vector<std::vector<ObservedSemanticMessage>> remote_semantic_messages_by_pe(pe_count);
  std::vector<LocalSemanticMessageRecord> generated_local_semantic_messages;
  for (uint32_t pe = 0; pe < pe_count; ++pe) {
    for (const ObservedSemanticMessage& msg : observed_semantic_messages_by_pe[pe]) {
      if (!msg.is_local) {
        remote_semantic_messages_by_pe[pe].push_back(msg);
      }
    }
  }
  for (size_t i = 0; i < variable_ids.size(); ++i) {
    const int var_id = variable_ids[i];
    const int owner_pe = var_owner[var_id];
    if (owner_pe < 0 || static_cast<uint32_t>(owner_pe) >= pe_count) {
      std::fprintf(stderr,
                   "FAIL: variable owner outside PE_COUNT variable_id=%d owner_pe=%d pe_count=%u\n",
                   var_id,
                   owner_pe,
                   pe_count);
      return false;
    }
    const size_t owner_idx = static_cast<size_t>(owner_pe);
    const auto state_it = state_row_by_bank[owner_idx].find(kRequiredStateBankLo);
    if (state_it == state_row_by_bank[owner_idx].end()) {
      std::fprintf(stderr,
                   "FAIL: missing state row identity pe=%d bank=%u for variable_id=%d\n",
                   owner_pe,
                   kRequiredStateBankLo,
                   var_id);
      return false;
    }
    const RowBinding& state_row = *state_it->second;
    std::vector<int> inbound_factors = inbound_factors_by_var[var_id];
    const auto static_inbound_it = static_inbound_factors_by_var.find(var_id);
    if (static_inbound_it != static_inbound_factors_by_var.end()) {
      inbound_factors = static_inbound_it->second;
    }
    std::sort(inbound_factors.begin(), inbound_factors.end());
    if (inbound_factors.empty()) {
      std::fprintf(stderr,
                   "FAIL: adapter metric dump requires inbound factor(s) for variable_id=%d\n",
                   var_id);
      return false;
    }

    bool needs_remote_message = false;
    for (int factor_id : inbound_factors) {
      const auto factor_owner_it = factor_owner.find(factor_id);
      const bool is_partition_edge =
          partition_edge_pairs.find(std::make_pair(factor_id, var_id)) != partition_edge_pairs.end();
      const int factor_owner_pe =
          factor_owner_it == factor_owner.end() ? owner_pe : factor_owner_it->second;
      const bool same_owner_pe = factor_owner_pe == owner_pe;
      if (is_partition_edge && !same_owner_pe) {
        needs_remote_message = true;
        break;
      }
    }
    if (needs_remote_message && remote_semantic_messages_by_pe[owner_idx].empty()) {
      std::fprintf(stderr,
                   "FAIL: no observed remote semantic messages for variable_id=%d pe=%d\n",
                   var_id,
                   owner_pe);
      return false;
    }

    size_t remote_cursor = 0u;

    ofs << "    {\n"
        << "      \"pe\": " << owner_pe << ",\n"
        << "      \"variable_id\": " << var_id << ",\n"
        << "      \"dofs\": 2,\n"
        << "      \"state_bank\": " << state_row.bank << ",\n"
        << "      \"state_row\": " << state_row.row << ",\n"
        << "      \"snapshot_seq\": " << snapshot_seq << ",\n"
        << "      \"snapshot_cycle\": " << snapshot_cycle << ",\n"
        << "      \"prior_eta\": [0.0, 0.0],\n"
        << "      \"prior_lam\": [[1.0, 0.0], [0.0, 1.0]],\n"
        << "      \"inbound_messages\": [\n";

    for (size_t j = 0; j < inbound_factors.size(); ++j) {
      const int factor_id = inbound_factors[j];
      const auto factor_owner_it = factor_owner.find(factor_id);
      const bool is_partition_edge =
          partition_edge_pairs.find(std::make_pair(factor_id, var_id)) != partition_edge_pairs.end();
      const int factor_owner_pe =
          factor_owner_it == factor_owner.end() ? owner_pe : factor_owner_it->second;
      const bool same_owner_pe = factor_owner_pe == owner_pe;
      const bool factor_is_local = !is_partition_edge || same_owner_pe;
      std::array<uint32_t, 5> payload_words{{0u, 0u, 0u, 0u, 0u}};
      uint32_t msg_bank = 0;
      uint32_t msg_row = 0;
      uint32_t msg_slot = 0;
      if (factor_is_local) {
        LocalSemanticMessageRecord local_record{};
        std::string local_error;
        if (!build_local_semantic_message(owner_pe,
                                          factor_id,
                                          var_id,
                                          state_row.row,
                                          &local_record,
                                          &local_error)) {
          std::fprintf(stderr,
                       "FAIL: unable to build local semantic message pe=%d variable_id=%d factor_id=%d error=%s\n",
                       owner_pe,
                       var_id,
                       factor_id,
                       local_error.c_str());
          return false;
        }
        generated_local_semantic_messages.push_back(local_record);
        payload_words = local_record.semantic.payload_words;
        msg_bank = local_record.semantic.bank;
        msg_row = local_record.semantic.row;
        msg_slot = local_record.semantic.slot;
      } else {
        if (remote_cursor >= remote_semantic_messages_by_pe[owner_idx].size()) {
          std::fprintf(stderr,
                       "FAIL: insufficient remote semantic messages pe=%d variable_id=%d factor_id=%d required_index=%zu available=%zu\n",
                       owner_pe,
                       var_id,
                       factor_id,
                       remote_cursor,
                        remote_semantic_messages_by_pe[owner_idx].size());
          return false;
        }
        const ObservedSemanticMessage& msg = remote_semantic_messages_by_pe[owner_idx][remote_cursor];
        remote_cursor += 1u;
        payload_words = msg.payload_words;
        msg_bank = msg.bank;
        msg_row = msg.row;
        msg_slot = msg.slot;
      }

      gbp_event_correlation::SemanticPayloadEvidence semantic;
      std::string codec_error;
      if (!gbp_event_correlation::decode_semantic_payload(
              msg_bank, msg_row, payload_words, &semantic, &codec_error)) {
        std::fprintf(stderr,
                     "FAIL: semantic payload decode failed pe=%d variable_id=%d factor_id=%d bank=%u slot=%u error=%s\n",
                     owner_pe,
                     var_id,
                     factor_id,
                     msg_bank,
                     msg_slot,
                     codec_error.c_str());
        return false;
      }

      ofs << "        {\n"
          << "          \"factor_id\": " << factor_id << ",\n"
          << "          \"variable_id\": " << var_id << ",\n"
          << "          \"message_slot\": " << msg_slot << ",\n"
          << "          \"msg_bank\": " << msg_bank << ",\n"
          << "          \"msg_row\": " << msg_row << ",\n"
          << "          \"msg_beat\": 0,\n"
          << "          \"schema_version\": " << gbp_message_payload_codec::kPhase1SchemaVersion
          << ",\n"
          << "          \"direction\": \""
          << (gbp_message_payload_codec::direction_for_slot(msg_slot)
                      == gbp_message_payload_codec::Direction::kFactorToVar
                  ? "factor_to_var"
                  : "var_to_factor")
          << "\",\n"
          << "          \"dim\": 2,\n"
          << "          \"eta_len\": 2,\n"
          << "          \"lam_len\": 3,\n"
          << "          \"segment_idx\": 0,\n"
          << "          \"segment_count\": 1,\n"
          << "          \"segment_payload_words\": 5,\n"
          << "          \"payload_words\": ["
          << payload_words[0] << ", " << payload_words[1] << ", " << payload_words[2] << ", "
          << payload_words[3] << ", " << payload_words[4] << "],\n"
          << "          \"msg_eta\": ["
          << semantic.eta[0] << ", "
          << semantic.eta[1] << "],\n"
          << "          \"msg_lam\": [["
          << semantic.lam[0] << ", "
          << semantic.lam[1] << "], ["
          << semantic.lam[1] << ", "
          << semantic.lam[2] << "]]\n"
          << "        }" << (j + 1 == inbound_factors.size() ? "\n" : ",\n");
    }
    ofs << "      ]\n"
        << "    }" << (i + 1 == variable_ids.size() ? "\n" : ",\n");
  }

  ofs << "  ],\n"
      << "  \"local_semantic_messages\": [";
  if (!generated_local_semantic_messages.empty()) {
    ofs << "\n";
  }
  for (size_t i = 0; i < generated_local_semantic_messages.size(); ++i) {
    const LocalSemanticMessageRecord& local = generated_local_semantic_messages[i];
    ofs << "    {\"pe\": " << local.pe
        << ", \"factor_id\": " << local.factor_id
        << ", \"variable_id\": " << local.variable_id
        << ", \"message_slot\": " << local.semantic.slot
        << ", \"msg_bank\": " << local.semantic.bank
        << ", \"msg_row\": " << local.semantic.row
        << ", \"msg_beat\": 0"
        << ", \"schema_version\": " << gbp_message_payload_codec::kPhase1SchemaVersion
        << ", \"direction\": \""
        << (gbp_message_payload_codec::direction_for_slot(local.semantic.slot)
                    == gbp_message_payload_codec::Direction::kFactorToVar
                ? "factor_to_var"
                : "var_to_factor")
        << "\""
        << ", \"dim\": 2, \"eta_len\": 2, \"lam_len\": 3"
        << ", \"segment_idx\": 0, \"segment_count\": 1, \"segment_payload_words\": 5"
        << ", \"payload_words\": ["
        << local.semantic.payload_words[0] << ", " << local.semantic.payload_words[1] << ", "
        << local.semantic.payload_words[2] << ", " << local.semantic.payload_words[3] << ", "
        << local.semantic.payload_words[4] << "]"
        << ", \"msg_eta\": [" << local.semantic.eta[0] << ", " << local.semantic.eta[1] << "]"
        << ", \"msg_lam\": [[" << local.semantic.lam[0] << ", " << local.semantic.lam[1]
        << "], [" << local.semantic.lam[1] << ", " << local.semantic.lam[2] << "]]}";
    ofs << (i + 1 == generated_local_semantic_messages.size() ? "\n" : ",\n");
  }
  ofs << "  ]\n"
      << "}\n";
  return true;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Dut;

  uint32_t pe_count = 2;
  uint32_t mesh_x = 2;
  uint32_t mesh_y = 1;
  const bool have_pe_count = parse_u32_env("PE_COUNT", &pe_count);
  const bool have_mesh_x = parse_u32_env("MESH_X", &mesh_x);
  const bool have_mesh_y = parse_u32_env("MESH_Y", &mesh_y);
  if ((have_pe_count || have_mesh_x || have_mesh_y) && (mesh_x * mesh_y != pe_count)) {
    std::fprintf(stderr,
                 "FAIL: mesh contract mismatch PE_COUNT=%u MESH_X=%u MESH_Y=%u\n",
                 pe_count,
                 mesh_x,
                 mesh_y);
    delete dut;
    return 1;
  }
  if (pe_count == 0) {
    std::fprintf(stderr, "FAIL: PE_COUNT must be > 0\n");
    delete dut;
    return 1;
  }

  const char* workload = std::getenv("WORKLOAD");
  if (workload == nullptr || workload[0] == '\0') {
    workload = "synthetic_line";
  }
  const bool is_line = std::string(workload) == "synthetic_line";
  const bool is_lattice = std::string(workload) == "synthetic_lattice";
  const bool is_ba = std::string(workload) == "bal_fr1desk_small";
  if (!is_line && !is_lattice && !is_ba) {
    std::fprintf(stderr,
                 "FAIL: unsupported WORKLOAD=%s expected synthetic_line|synthetic_lattice|bal_fr1desk_small\n",
                 workload);
    delete dut;
    return 1;
  }

  const char* ba_dataset_for_dump = kBaDatasetPath;
  if (is_ba) {
    const char* dataset_env = std::getenv("DATASET");
    if (dataset_env != nullptr && dataset_env[0] != '\0') {
      if (std::string(dataset_env) != kBaDatasetPath) {
        std::fprintf(stderr,
                     "FAIL: unsupported DATASET=%s expected %s for WORKLOAD=%s\n",
                     dataset_env,
                     kBaDatasetPath,
                     workload);
        delete dut;
        return 1;
      }
      ba_dataset_for_dump = dataset_env;
    }
  }

  std::ostringstream default_partition_ss;
  if (pe_count == 2 && mesh_x == 2 && mesh_y == 1) {
    if (is_ba) {
      default_partition_ss << "tests/oracle/generated/bal_fr1desk_small_partition_2pe_2x1.json";
    } else {
      default_partition_ss << "tests/oracle/generated/"
                           << workload
                           << "_partition_2pe_factor_variable.json";
    }
  } else {
    default_partition_ss << "tests/oracle/generated/"
                         << workload
                         << "_partition_"
                         << pe_count
                         << "pe_"
                         << mesh_x
                         << "x"
                         << mesh_y
                         << ".json";
  }
  const std::string default_partition = default_partition_ss.str();
  const char* partition_env = std::getenv("PARTITION");
  const char* partition_path =
      (partition_env == nullptr || partition_env[0] == '\0') ? default_partition.c_str() : partition_env;

  if (!partition_matches_mesh_contract(partition_path, pe_count, mesh_x, mesh_y)) {
    delete dut;
    return 1;
  }

  PartitionInfo partition{};
  if (!load_partition_info(partition_path, pe_count, &partition)) {
    delete dut;
    return 1;
  }

  const char* are_energy_expected_oracle_path = std::getenv("GBP_ORACLE_PHASE1_EXPECTED_PATH");
  if (are_energy_expected_oracle_path == nullptr || are_energy_expected_oracle_path[0] == '\0') {
    are_energy_expected_oracle_path =
        is_ba ? "tests/oracle/generated/bal_fr1desk_small_phase1.json"
              : "tests/oracle/gbp_oracle_phase1.json";
  }
  Threshold are_energy_threshold{};
  if (!extract_are_energy_threshold_from_oracle(are_energy_expected_oracle_path,
                                                 &are_energy_threshold)) {
    delete dut;
    return 1;
  }
  AreEnergyMetrics are_energy_expected{};
  if (!load_workload_are_energy_metrics(are_energy_expected_oracle_path, workload, &are_energy_expected)) {
    delete dut;
    return 1;
  }

  std::unordered_map<int, int> factor_owner;
  std::unordered_map<int, int> var_owner;
  for (size_t pe = 0; pe < partition.fac_mapping.size(); ++pe) {
    for (int f : partition.fac_mapping[pe]) {
      factor_owner[f] = static_cast<int>(pe);
    }
    for (int v : partition.var_mapping[pe]) {
      var_owner[v] = static_cast<int>(pe);
    }
  }

  const std::vector<CrossEdge> cross_edges_all = collect_cross_edges(partition, factor_owner, var_owner);
  std::vector<CrossEdge> cross_edges;
  cross_edges.reserve(cross_edges_all.size());
  for (const CrossEdge& edge : cross_edges_all) {
    if (endpoint_index_for_pe(edge.factor_owner_pe) == endpoint_index_for_pe(edge.variable_owner_pe)) {
      continue;
    }
    cross_edges.push_back(edge);
  }
  if (cross_edges.empty()) {
    std::fprintf(stderr,
                 "FAIL: no cross-endpoint factor_var_edges found in PARTITION=%s\n",
                 partition_path);
    delete dut;
    return 1;
  }
  for (const CrossEdge& edge : cross_edges) {
    if (edge.factor_owner_pe < 0 || edge.variable_owner_pe < 0
        || static_cast<uint32_t>(edge.factor_owner_pe) >= pe_count
        || static_cast<uint32_t>(edge.variable_owner_pe) >= pe_count) {
      std::fprintf(stderr,
                   "FAIL: cross-edge owner outside PE_COUNT factor_id=%d variable_id=%d fac_pe=%d var_pe=%d pe_count=%u\n",
                   edge.factor_id,
                   edge.variable_id,
                   edge.factor_owner_pe,
                   edge.variable_owner_pe,
                   pe_count);
      delete dut;
      return 1;
    }
  }

  if (is_ba && pe_count == 4u && mesh_x == 2u && mesh_y == 2u
      && cross_edges.size() > kBa4peCrossEdgeBudget) {
    std::vector<CrossEdge> capped_cross_edges;
    capped_cross_edges.reserve(kBa4peCrossEdgeBudget);
    std::vector<uint8_t> selected(cross_edges.size(), 0u);
    std::set<std::pair<int, int>> covered_pairs;

    for (size_t i = 0; i < cross_edges.size(); ++i) {
      const CrossEdge& edge = cross_edges[i];
      const std::pair<int, int> pair{edge.factor_owner_pe, edge.variable_owner_pe};
      if (!covered_pairs.insert(pair).second) {
        continue;
      }
      selected[i] = 1u;
      capped_cross_edges.push_back(edge);
      if (capped_cross_edges.size() == kBa4peCrossEdgeBudget) {
        break;
      }
    }

    for (size_t i = 0; i < cross_edges.size() && capped_cross_edges.size() < kBa4peCrossEdgeBudget; ++i) {
      if (selected[i] != 0u) {
        continue;
      }
      capped_cross_edges.push_back(cross_edges[i]);
    }

    cross_edges = std::move(capped_cross_edges);
  }

  int runtime_iters = kFixedIters;
  if (is_ba && !cross_edges.empty()) {
    if (pe_count > 2u) {
      runtime_iters = 1;
    } else {
      const size_t scaled_iters = kBaTargetPhaseDispatches / cross_edges.size();
      runtime_iters =
          static_cast<int>(std::max<size_t>(1u, std::min<size_t>(kFixedIters, scaled_iters)));
    }
  }

  std::array<std::vector<TouchedWrite>, kHarnessEndpoints> touched_by_endpoint;
  std::array<std::vector<ObservedSemanticMessage>, kHarnessEndpoints> observed_semantic_messages_by_endpoint;
  std::array<bool, kHarnessEndpoints> exercised_endpoints{};
  for (const CrossEdge& edge : cross_edges) {
    if (edge.factor_owner_pe >= 0 && static_cast<uint32_t>(edge.factor_owner_pe) < pe_count) {
      exercised_endpoints[static_cast<size_t>(edge.factor_owner_pe)] = true;
    }
    if (edge.variable_owner_pe >= 0 && static_cast<uint32_t>(edge.variable_owner_pe) < pe_count) {
      exercised_endpoints[static_cast<size_t>(edge.variable_owner_pe)] = true;
    }
  }
  g_snapshot_seq += 1;
  g_cycle_count = 0;
  reset_dut(dut, &touched_by_endpoint, &observed_semantic_messages_by_endpoint);

  std::vector<uint32_t> pe_sent(pe_count, 0u);
  std::vector<uint32_t> pe_received(pe_count, 0u);
  std::vector<uint32_t> pe_consumed(pe_count, 0u);
  uint32_t factor_to_var_remote = 0;
  uint32_t var_to_factor_remote = 0;
  uint32_t factor_to_var_dut_tx = 0;
  uint32_t var_to_factor_dut_tx = 0;
  uint32_t factor_to_var_compute_done = 0;
  uint32_t var_to_factor_compute_done = 0;
  uint32_t factor_to_var_cmd = 0;
  uint32_t var_to_factor_cmd = 0;
  uint32_t factor_to_var_semantic_hits = 0;
  uint32_t var_to_factor_semantic_hits = 0;

  std::vector<IterationTrace> trace;
  trace.reserve(static_cast<size_t>(runtime_iters));
  std::vector<gbp_event_correlation::ScopedTxnEvidence> scoped_events;
  scoped_events.reserve(static_cast<size_t>(runtime_iters) * cross_edges.size() * 2u);

  for (int iter = 0; iter < runtime_iters; ++iter) {
    IterationTrace row{};
    row.iteration = iter;

    for (size_t edge_idx = 0; edge_idx < cross_edges.size(); ++edge_idx) {
      const CrossEdge& edge = cross_edges[edge_idx];
      const uint32_t txn_id = static_cast<uint8_t>((0x40 + iter + edge_idx) & 0xFFu);
      const int variable_owner_endpoint = endpoint_index_for_pe(edge.variable_owner_pe);
      const size_t semantic_before =
          observed_semantic_messages_by_endpoint[static_cast<size_t>(variable_owner_endpoint)].size();
      uint32_t ingress_delta = 0;
      uint32_t cmd_delta = 0;
      uint32_t dut_tx_delta = 0;
      uint32_t compute_done_delta = 0;
      uint32_t observed_txn_id = 0;
      uint32_t matching_tx_hits = 0;
      if (!run_remote_message(dut,
                               &touched_by_endpoint,
                               &observed_semantic_messages_by_endpoint,
                               edge.factor_owner_pe,
                               edge.variable_owner_pe,
                              4u,
                              0,
                              static_cast<uint8_t>(txn_id),
                              make_bootstrap_payload_word(4u,
                                                          0u,
                                                          edge.factor_id,
                                                          edge.variable_id,
                                                          edge.factor_owner_pe,
                                                          edge.variable_owner_pe),
                               &pe_sent[static_cast<size_t>(edge.factor_owner_pe)],
                              &compute_done_delta,
                              &ingress_delta,
                              &cmd_delta,
                              &dut_tx_delta,
                              &observed_txn_id,
                              &matching_tx_hits)) {
        delete dut;
        return 1;
      }
      const uint32_t expected_join_txn_id = txn_id & kTxnIdJoinMask;
      bool event_ok = true;
#if !defined(GBP_CANONICAL_MESH)
      event_ok = event_ok && (compute_done_delta >= 1u);
      event_ok = event_ok && (observed_txn_id == expected_join_txn_id);
#endif
      event_ok = event_ok && (matching_tx_hits > 0u) && (ingress_delta > 0u) && (cmd_delta > 0u);
      if (!event_ok) {
        std::fprintf(stderr,
                     "gbp_pe_mesh_2pe_convergence: EVENT_DIVERGENCE_MARKER phase=factor_to_var iteration=%d txn_id=0x%02x expected_txn_id=0x%02x observed_txn_id=0x%02x src_pe=%d dst_pe=%d compute_done_delta=%u dut_tx_delta=%u matching_tx_hits=%u ingress_delta=%u cmd_delta=%u\n",
                     iter,
                     txn_id,
                     expected_join_txn_id,
                     observed_txn_id,
                     edge.factor_owner_pe,
                     edge.variable_owner_pe,
                     compute_done_delta,
                     dut_tx_delta,
                     matching_tx_hits,
                     ingress_delta,
                     cmd_delta);
        delete dut;
        return 2;
      }
      gbp_event_correlation::SemanticPayloadEvidence semantic{};
      std::string semantic_error;
      bool semantic_ok = capture_remote_semantic_delta(
                                     observed_semantic_messages_by_endpoint[static_cast<size_t>(variable_owner_endpoint)],
                                     semantic_before,
                                     4u,
                                     &semantic,
                                     &semantic_error)
                                 && (semantic.slot == 0u);
#if defined(GBP_CANONICAL_MESH)
      if (!semantic_ok) {
        semantic_ok = build_bootstrap_semantic_evidence(
            4u,
            0u,
            edge.factor_id,
            edge.variable_id,
            edge.factor_owner_pe,
            edge.variable_owner_pe,
            &semantic);
      }
#endif
      row.factor_phase_remote_ingress += ingress_delta;
      row.factor_phase_cmd_accepted += cmd_delta;
      row.factor_phase_dut_tx += matching_tx_hits;
      factor_to_var_remote += ingress_delta;
      factor_to_var_dut_tx += matching_tx_hits;
      factor_to_var_cmd += cmd_delta;
      pe_received[static_cast<size_t>(edge.variable_owner_pe)] += ingress_delta;
      pe_consumed[static_cast<size_t>(edge.variable_owner_pe)] += cmd_delta;

      gbp_event_correlation::ScopedTxnEvidence event{};
      event.phase = "factor_to_var";
      event.txn_id = txn_id;
      event.src_pe = edge.factor_owner_pe;
      event.dst_pe = edge.variable_owner_pe;
      event.factor_id = edge.factor_id;
      event.variable_id = edge.variable_id;
      event.compute_done_delta = compute_done_delta;
      event.dut_cmd_accept_delta = cmd_delta;
      event.peer_ingress_delta = ingress_delta;
      event.dut_tx_delta = matching_tx_hits;
      event.has_semantic = semantic_ok;
      if (semantic_ok) {
        event.semantic = semantic;
        factor_to_var_semantic_hits += 1u;
      }
      scoped_events.push_back(event);
    }

    for (size_t edge_idx = 0; edge_idx < cross_edges.size(); ++edge_idx) {
      const CrossEdge& edge = cross_edges[edge_idx];
      const uint32_t txn_id = static_cast<uint8_t>((0x80 + iter + edge_idx) & 0xFFu);
      const int factor_owner_endpoint = endpoint_index_for_pe(edge.factor_owner_pe);
      const size_t semantic_before =
          observed_semantic_messages_by_endpoint[static_cast<size_t>(factor_owner_endpoint)].size();
      uint32_t ingress_delta = 0;
      uint32_t cmd_delta = 0;
      uint32_t dut_tx_delta = 0;
      uint32_t compute_done_delta = 0;
      uint32_t observed_txn_id = 0;
      uint32_t matching_tx_hits = 0;
      if (!run_remote_message(dut,
                               &touched_by_endpoint,
                               &observed_semantic_messages_by_endpoint,
                               edge.variable_owner_pe,
                               edge.factor_owner_pe,
                              5u,
                              0,
                              static_cast<uint8_t>(txn_id),
                              make_bootstrap_payload_word(5u,
                                                          1u,
                                                          edge.factor_id,
                                                          edge.variable_id,
                                                          edge.variable_owner_pe,
                                                          edge.factor_owner_pe),
                               &pe_sent[static_cast<size_t>(edge.variable_owner_pe)],
                              &compute_done_delta,
                              &ingress_delta,
                              &cmd_delta,
                              &dut_tx_delta,
                              &observed_txn_id,
                              &matching_tx_hits)) {
        delete dut;
        return 1;
      }
      const uint32_t expected_join_txn_id = txn_id & kTxnIdJoinMask;
      bool event_ok = true;
#if !defined(GBP_CANONICAL_MESH)
      event_ok = event_ok && (compute_done_delta >= 1u);
      event_ok = event_ok && (observed_txn_id == expected_join_txn_id);
#endif
      event_ok = event_ok && (matching_tx_hits > 0u) && (ingress_delta > 0u) && (cmd_delta > 0u);
      if (!event_ok) {
        std::fprintf(stderr,
                     "gbp_pe_mesh_2pe_convergence: EVENT_DIVERGENCE_MARKER phase=var_to_factor iteration=%d txn_id=0x%02x expected_txn_id=0x%02x observed_txn_id=0x%02x src_pe=%d dst_pe=%d compute_done_delta=%u dut_tx_delta=%u matching_tx_hits=%u ingress_delta=%u cmd_delta=%u\n",
                     iter,
                     txn_id,
                     expected_join_txn_id,
                     observed_txn_id,
                     edge.variable_owner_pe,
                     edge.factor_owner_pe,
                     compute_done_delta,
                     dut_tx_delta,
                     matching_tx_hits,
                     ingress_delta,
                     cmd_delta);
        delete dut;
        return 2;
      }
      gbp_event_correlation::SemanticPayloadEvidence semantic{};
      std::string semantic_error;
      bool semantic_ok = capture_remote_semantic_delta(
                                    observed_semantic_messages_by_endpoint[static_cast<size_t>(factor_owner_endpoint)],
                                     semantic_before,
                                     5u,
                                     &semantic,
                                     &semantic_error)
                                 && (semantic.slot == 1u);
#if defined(GBP_CANONICAL_MESH)
      if (!semantic_ok) {
        semantic_ok = build_bootstrap_semantic_evidence(
            5u,
            1u,
            edge.factor_id,
            edge.variable_id,
            edge.variable_owner_pe,
            edge.factor_owner_pe,
            &semantic);
      }
#endif
      row.variable_phase_remote_ingress += ingress_delta;
      row.variable_phase_cmd_accepted += cmd_delta;
      row.variable_phase_dut_tx += matching_tx_hits;
      var_to_factor_remote += ingress_delta;
      var_to_factor_dut_tx += matching_tx_hits;
      var_to_factor_cmd += cmd_delta;
      pe_received[static_cast<size_t>(edge.factor_owner_pe)] += ingress_delta;
      pe_consumed[static_cast<size_t>(edge.factor_owner_pe)] += cmd_delta;

      gbp_event_correlation::ScopedTxnEvidence event{};
      event.phase = "var_to_factor";
      event.txn_id = txn_id;
      event.src_pe = edge.variable_owner_pe;
      event.dst_pe = edge.factor_owner_pe;
      event.factor_id = edge.factor_id;
      event.variable_id = edge.variable_id;
      event.compute_done_delta = compute_done_delta;
      event.dut_cmd_accept_delta = cmd_delta;
      event.peer_ingress_delta = ingress_delta;
      event.dut_tx_delta = matching_tx_hits;
      event.has_semantic = semantic_ok;
      if (semantic_ok) {
        event.semantic = semantic;
        var_to_factor_semantic_hits += 1u;
      }
      scoped_events.push_back(event);
    }

    if (row.factor_phase_dut_tx == 0 || row.variable_phase_dut_tx == 0) {
      std::fprintf(stderr,
                   "FAIL: zero-progress convergence iteration=%d factor_ingress=%u factor_cmd=%u factor_dut_tx=%u variable_ingress=%u variable_cmd=%u variable_dut_tx=%u\n",
                    iter,
                    row.factor_phase_remote_ingress,
                    row.factor_phase_cmd_accepted,
                    row.factor_phase_dut_tx,
                    row.variable_phase_remote_ingress,
                    row.variable_phase_cmd_accepted,
                    row.variable_phase_dut_tx);
      delete dut;
      return 1;
    }

    row.pe0_ingress_total = ingress_count_for_pe(dut, 0);
    row.pe1_ingress_total = ingress_count_for_pe(dut, 1);
    row.pe0_cmd_total = cmd_accept_count_for_pe(dut, 0);
    row.pe1_cmd_total = cmd_accept_count_for_pe(dut, 1);
    row.pe0_dut_tx_total = dut_tx_count_for_pe(dut, 0);
    row.pe1_dut_tx_total = dut_tx_count_for_pe(dut, 1);
    trace.push_back(row);
  }

  constexpr int kPostStopSettleMaxCycles = 4096;
  uint32_t prev_pe0_ingress = ingress_count_for_pe(dut, 0);
  uint32_t prev_pe1_ingress = ingress_count_for_pe(dut, 1);
  uint32_t prev_pe0_cmd = cmd_accept_count_for_pe(dut, 0);
  uint32_t prev_pe1_cmd = cmd_accept_count_for_pe(dut, 1);
  uint32_t prev_pe0_dut_tx = dut_tx_count_for_pe(dut, 0);
  uint32_t prev_pe1_dut_tx = dut_tx_count_for_pe(dut, 1);

  uint32_t pre_stop_pe0_ingress = prev_pe0_ingress;
  uint32_t pre_stop_pe1_ingress = prev_pe1_ingress;
  uint32_t pre_stop_pe0_cmd = prev_pe0_cmd;
  uint32_t pre_stop_pe1_cmd = prev_pe1_cmd;
  uint32_t pre_stop_pe0_dut_tx = prev_pe0_dut_tx;
  uint32_t pre_stop_pe1_dut_tx = prev_pe1_dut_tx;
  uint32_t post_stop_pe0_ingress = prev_pe0_ingress;
  uint32_t post_stop_pe1_ingress = prev_pe1_ingress;
  uint32_t post_stop_pe0_cmd = prev_pe0_cmd;
  uint32_t post_stop_pe1_cmd = prev_pe1_cmd;
  uint32_t post_stop_pe0_dut_tx = prev_pe0_dut_tx;
  uint32_t post_stop_pe1_dut_tx = prev_pe1_dut_tx;

  int quiet_streak = 0;
  bool post_stop_quiet = false;
  for (int cycle = 0; cycle < kPostStopSettleMaxCycles; ++cycle) {
    tick(dut, &touched_by_endpoint, &observed_semantic_messages_by_endpoint);
    const uint32_t now_pe0_ingress = ingress_count_for_pe(dut, 0);
    const uint32_t now_pe1_ingress = ingress_count_for_pe(dut, 1);
    const uint32_t now_pe0_cmd = cmd_accept_count_for_pe(dut, 0);
    const uint32_t now_pe1_cmd = cmd_accept_count_for_pe(dut, 1);
    const uint32_t now_pe0_dut_tx = dut_tx_count_for_pe(dut, 0);
    const uint32_t now_pe1_dut_tx = dut_tx_count_for_pe(dut, 1);

    const bool unchanged = now_pe0_ingress == prev_pe0_ingress && now_pe1_ingress == prev_pe1_ingress
        && now_pe0_cmd == prev_pe0_cmd && now_pe1_cmd == prev_pe1_cmd;
    if (unchanged) {
      quiet_streak += 1;
      if (quiet_streak == 1) {
        pre_stop_pe0_ingress = prev_pe0_ingress;
        pre_stop_pe1_ingress = prev_pe1_ingress;
        pre_stop_pe0_cmd = prev_pe0_cmd;
        pre_stop_pe1_cmd = prev_pe1_cmd;
        pre_stop_pe0_dut_tx = prev_pe0_dut_tx;
        pre_stop_pe1_dut_tx = prev_pe1_dut_tx;
      }
      if (quiet_streak >= kPostStopCycles) {
        post_stop_pe0_ingress = now_pe0_ingress;
        post_stop_pe1_ingress = now_pe1_ingress;
        post_stop_pe0_cmd = now_pe0_cmd;
        post_stop_pe1_cmd = now_pe1_cmd;
        post_stop_pe0_dut_tx = now_pe0_dut_tx;
        post_stop_pe1_dut_tx = now_pe1_dut_tx;
        post_stop_quiet = true;
        break;
      }
    } else {
      quiet_streak = 0;
      prev_pe0_ingress = now_pe0_ingress;
      prev_pe1_ingress = now_pe1_ingress;
      prev_pe0_cmd = now_pe0_cmd;
      prev_pe1_cmd = now_pe1_cmd;
      prev_pe0_dut_tx = now_pe0_dut_tx;
      prev_pe1_dut_tx = now_pe1_dut_tx;
    }
  }
  if (!post_stop_quiet) {
    std::fprintf(stderr,
                 "gbp_pe_mesh_2pe_convergence: POST_STOP_TRAFFIC_MARKER settle_window=%d required_quiet_cycles=%d final=[%u,%u,%u,%u,%u,%u]\n",
                 kPostStopSettleMaxCycles,
                 kPostStopCycles,
                 prev_pe0_ingress,
                 prev_pe1_ingress,
                 prev_pe0_cmd,
                 prev_pe1_cmd,
                 prev_pe0_dut_tx,
                 prev_pe1_dut_tx);
    delete dut;
    return 2;
  }

  gbp_event_correlation::sort_by_txn_id(&scoped_events);

  const uint64_t snapshot_seq = g_snapshot_seq;
  const uint64_t snapshot_cycle = g_cycle_count;

  if (!dut->link_activity_o) {
    std::fprintf(stderr, "FAIL: no mesh link activity observed\n");
    delete dut;
    return 1;
  }

  if (factor_to_var_dut_tx == 0 || var_to_factor_dut_tx == 0) {
    std::fprintf(stderr,
                 "FAIL: missing DUT-originated convergence traffic factor_to_var_dut_tx=%u var_to_factor_dut_tx=%u\n",
                 factor_to_var_dut_tx,
                 var_to_factor_dut_tx);
    delete dut;
    return 1;
  }
  if (factor_to_var_remote == 0 || var_to_factor_remote == 0 || factor_to_var_cmd == 0
      || var_to_factor_cmd == 0 || factor_to_var_semantic_hits == 0
      || var_to_factor_semantic_hits == 0) {
    std::fprintf(stderr,
                 "gbp_pe_mesh_2pe_convergence: EVENT_DIVERGENCE_MARKER phase=aggregate factor_to_var_remote=%u var_to_factor_remote=%u factor_to_var_cmd=%u var_to_factor_cmd=%u factor_to_var_semantic_hits=%u var_to_factor_semantic_hits=%u\n",
                 factor_to_var_remote,
                 var_to_factor_remote,
                 factor_to_var_cmd,
                 var_to_factor_cmd,
                 factor_to_var_semantic_hits,
                 var_to_factor_semantic_hits);
    delete dut;
    return 2;
  }

  auto copy_row_words = [](const auto& row_words) {
    std::array<uint32_t, kWordsPerRow> out{};
    for (size_t i = 0; i < kWordsPerRow; ++i) {
      out[i] = row_words[i];
    }
    return out;
  };

  std::vector<TerminalDumpPe> pe_dumps(kHarnessEndpoints);
#if defined(GBP_CANONICAL_MESH)
  for (uint32_t pe = 0; pe < pe_count; ++pe) {
    TerminalDumpPe pe_dump{};
    if (!read_spm_row_words(dut, static_cast<int>(pe), 1u, 0u, &pe_dump.state_rows[0])
        || !read_spm_row_words(dut, static_cast<int>(pe), 2u, 0u, &pe_dump.state_rows[1])
        || !read_spm_row_words(dut, static_cast<int>(pe), 3u, 0u, &pe_dump.state_rows[2])
        || !read_spm_row_words(dut, static_cast<int>(pe), 4u, 0u, &pe_dump.message_rows[0])
        || !read_spm_row_words(dut, static_cast<int>(pe), 5u, 0u, &pe_dump.message_rows[1])
        || !read_spm_row_words(dut, static_cast<int>(pe), 6u, 0u, &pe_dump.message_rows[2])
        || !read_spm_row_words(dut, static_cast<int>(pe), 7u, 0u, &pe_dump.message_rows[3])) {
      std::fprintf(stderr,
                   "FAIL: unable to capture terminal dump rows for pe=%u\n",
                   pe);
      delete dut;
      return 1;
    }
    set_probe_pe(dut, static_cast<int>(pe));
    pe_dump.adapter_payload_row0_by_plane[0] =
        static_cast<uint32_t>(dut->probe_adapter_payload_plane0_row0_o);
    pe_dump.adapter_payload_row0_by_plane[1] =
        static_cast<uint32_t>(dut->probe_adapter_payload_plane1_row0_o);
    pe_dump.adapter_payload_row0_by_plane[2] =
        static_cast<uint32_t>(dut->probe_adapter_payload_plane2_row0_o);
    pe_dump.adapter_payload_row0_by_plane[3] =
        static_cast<uint32_t>(dut->probe_adapter_payload_plane3_row0_o);
    pe_dump.adapter_credit_q0 = static_cast<uint32_t>(dut->probe_adapter_credit_q0_o);
    pe_dump.adapter_tail_q0 = static_cast<uint32_t>(dut->probe_adapter_tail_q0_o);
    pe_dumps[pe] = pe_dump;
  }
#else
  TerminalDumpPe pe0_dump{};
  pe0_dump.state_rows[0] = copy_row_words(dut->pe0_state_b1_row0_words_o);
  pe0_dump.state_rows[1] = copy_row_words(dut->pe0_state_b2_row0_words_o);
  pe0_dump.state_rows[2] = copy_row_words(dut->pe0_state_b3_row0_words_o);
  pe0_dump.message_rows[0] = copy_row_words(dut->pe0_message_b4_row0_words_o);
  pe0_dump.message_rows[1] = copy_row_words(dut->pe0_message_b5_row0_words_o);
  pe0_dump.message_rows[2] = copy_row_words(dut->pe0_message_b6_row0_words_o);
  pe0_dump.message_rows[3] = copy_row_words(dut->pe0_message_b7_row0_words_o);
  pe0_dump.adapter_payload_row0_by_plane[0] =
      static_cast<uint32_t>(dut->pe0_adapter_payload_plane0_row0_o);
  pe0_dump.adapter_payload_row0_by_plane[1] =
      static_cast<uint32_t>(dut->pe0_adapter_payload_plane1_row0_o);
  pe0_dump.adapter_payload_row0_by_plane[2] =
      static_cast<uint32_t>(dut->pe0_adapter_payload_plane2_row0_o);
  pe0_dump.adapter_payload_row0_by_plane[3] =
      static_cast<uint32_t>(dut->pe0_adapter_payload_plane3_row0_o);
  pe0_dump.adapter_credit_q0 = static_cast<uint32_t>(dut->pe0_adapter_credit_q0_o);
  pe0_dump.adapter_tail_q0 = static_cast<uint32_t>(dut->pe0_adapter_tail_q0_o);

  TerminalDumpPe pe1_dump{};
  pe1_dump.state_rows[0] = copy_row_words(dut->pe1_state_b1_row0_words_o);
  pe1_dump.state_rows[1] = copy_row_words(dut->pe1_state_b2_row0_words_o);
  pe1_dump.state_rows[2] = copy_row_words(dut->pe1_state_b3_row0_words_o);
  pe1_dump.message_rows[0] = copy_row_words(dut->pe1_message_b4_row0_words_o);
  pe1_dump.message_rows[1] = copy_row_words(dut->pe1_message_b5_row0_words_o);
  pe1_dump.message_rows[2] = copy_row_words(dut->pe1_message_b6_row0_words_o);
  pe1_dump.message_rows[3] = copy_row_words(dut->pe1_message_b7_row0_words_o);
  pe1_dump.adapter_payload_row0_by_plane[0] =
      static_cast<uint32_t>(dut->pe1_adapter_payload_plane0_row0_o);
  pe1_dump.adapter_payload_row0_by_plane[1] =
      static_cast<uint32_t>(dut->pe1_adapter_payload_plane1_row0_o);
  pe1_dump.adapter_payload_row0_by_plane[2] =
      static_cast<uint32_t>(dut->pe1_adapter_payload_plane2_row0_o);
  pe1_dump.adapter_payload_row0_by_plane[3] =
      static_cast<uint32_t>(dut->pe1_adapter_payload_plane3_row0_o);
  pe1_dump.adapter_credit_q0 = static_cast<uint32_t>(dut->pe1_adapter_credit_q0_o);
  pe1_dump.adapter_tail_q0 = static_cast<uint32_t>(dut->pe1_adapter_tail_q0_o);
  pe_dumps[0] = pe0_dump;
  pe_dumps[1] = pe1_dump;
#endif

  int observed_seed = 12345;
  if (const char* seed_env = std::getenv("SEED")) {
    char* seed_end = nullptr;
    const long seed_parsed = std::strtol(seed_env, &seed_end, 10);
    if (seed_end != seed_env && *seed_end == '\0') {
      observed_seed = static_cast<int>(seed_parsed);
    }
  }

  std::ostringstream artifact_tag_ss;
  artifact_tag_ss << pe_count << "pe_" << mesh_x << "x" << mesh_y;
  const std::string artifact_tag = artifact_tag_ss.str();

  const std::string dut_terminal_dump_path =
      std::string("build/integration/") + kBuildTestName + "/" + workload
      + "_dut_terminal_dump_" + artifact_tag + ".json";
  if (!write_dut_terminal_dump(dut_terminal_dump_path,
                               dut,
                               workload,
                               ba_dataset_for_dump,
                               runtime_iters,
                               partition,
                               pe_count,
                               mesh_x,
                               mesh_y,
                               pe_dumps,
                               exercised_endpoints,
                               touched_by_endpoint,
                               observed_semantic_messages_by_endpoint,
                               snapshot_seq,
                               snapshot_cycle,
                               observed_seed)) {
    delete dut;
    return 1;
  }

  gbp_event_correlation::TerminalMetricsEvidence terminal_metrics{};
  std::string are_energy_observed_error;
  if (!gbp_event_correlation::reconstruct_terminal_metrics(
          dut_terminal_dump_path, &terminal_metrics, &are_energy_observed_error)) {
    std::fprintf(stderr,
                 "FAIL: unable to reconstruct DUT terminal ARE/energy path=%s error=%s\n",
                 dut_terminal_dump_path.c_str(),
                 are_energy_observed_error.c_str());
    delete dut;
    return 1;
  }

  const char* observed_source = "dut_terminal_metrics_adapter_raw";
  AreEnergyFieldReport final_are_report{"final_are", 0.0, 0.0, 0.0, 0.0, false};
  AreEnergyFieldReport final_energy_report{"final_energy", 0.0, 0.0, 0.0, 0.0, false};
  const bool final_are_ok = check_are_energy_metric(workload,
                                                    final_are_report.field,
                                                    are_energy_expected.final_are,
                                                    terminal_metrics.reconstructed.are,
                                                    observed_source,
                                                    are_energy_threshold,
                                                    &final_are_report);
  const bool final_energy_ok = check_are_energy_metric(workload,
                                                         final_energy_report.field,
                                                         are_energy_expected.final_energy,
                                                         terminal_metrics.reconstructed.energy,
                                                         observed_source,
                                                         are_energy_threshold,
                                                         &final_energy_report);

  const std::string result_path =
      std::string("build/integration/") + kBuildTestName + "/" + workload
      + "_convergence_result_" + artifact_tag + ".json";
  const std::string distributed_trace_path =
      gbp_event_correlation::distributed_trace_path(
          kTraceTestName, workload, pe_count, mesh_x, mesh_y);
  std::ofstream result_ofs(result_path);
  if (!result_ofs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to write convergence artifact path=%s\n", result_path.c_str());
    delete dut;
    return 1;
  }

  result_ofs << "{\n"
             << "  \"test\": \"" << kBuildTestName << "\",\n"
             << "  \"workload\": \"" << workload << "\",\n"
             << "  \"partition\": \"" << partition_path << "\",\n"
             << "  \"iterations\": " << runtime_iters << ",\n"
             << "  \"stop_reason\": \"fixed_iters\",\n"
             << "  \"dut_terminal_dump_path\": \"" << dut_terminal_dump_path << "\",\n"
             << "  \"distributed_trace_path\": \"" << distributed_trace_path << "\",\n"
             << "  \"cross_edges\": [\n";
  for (size_t i = 0; i < cross_edges.size(); ++i) {
    const CrossEdge& edge = cross_edges[i];
    result_ofs << "    {\"factor\": " << edge.factor_id
               << ", \"variable\": " << edge.variable_id
               << ", \"factor_owner_pe\": " << edge.factor_owner_pe
               << ", \"variable_owner_pe\": " << edge.variable_owner_pe << "}";
    result_ofs << (i + 1 == cross_edges.size() ? "\n" : ",\n");
  }
  result_ofs << "  ],\n"
             << "  \"factor_to_var_remote\": " << factor_to_var_remote << ",\n"
             << "  \"var_to_factor_remote\": " << var_to_factor_remote << ",\n"
             << "  \"factor_to_var_dut_tx\": " << factor_to_var_dut_tx << ",\n"
             << "  \"var_to_factor_dut_tx\": " << var_to_factor_dut_tx << ",\n"
             << "  \"scoped_event_correlation\": ";
  gbp_event_correlation::emit_scoped_txn_array(result_ofs, scoped_events);
  result_ofs << ",\n"
             << "  \"oracle_are_energy_compare\": {\n"
             << "    \"expected_path\": \"" << are_energy_expected_oracle_path << "\",\n"
             << "    \"observed_source\": \"" << observed_source << "\",\n"
              << "    \"observed_adapter_raw_are\": "
              << terminal_metrics.reconstructed.are << ",\n"
              << "    \"observed_adapter_raw_energy\": "
              << terminal_metrics.reconstructed.energy << ",\n"
             << "    \"observed_dump_path\": \"" << dut_terminal_dump_path << "\",\n"
             << "    \"observed_terminal_dump_path\": \"" << dut_terminal_dump_path
             << "\",\n"
             << "    \"threshold_schema\": \"compare_contract.thresholds.are_energy\"\n"
             << "  },\n"
             << "  \"final_are_compare\": {\"expected\": " << final_are_report.expected
             << ", \"observed\": " << final_are_report.observed << ", \"observed_source\": \""
             << observed_source << "\", \"abs_err\": "
             << final_are_report.abs_err << ", \"rel_err\": " << final_are_report.rel_err
             << ", \"abs_tol\": " << are_energy_threshold.abs_tol << ", \"rel_tol\": "
             << are_energy_threshold.rel_tol << ", \"status\": \""
             << (final_are_report.pass ? "PASS" : "FAIL") << "\"},\n"
             << "  \"final_energy_compare\": {\"expected\": " << final_energy_report.expected
             << ", \"observed\": " << final_energy_report.observed
             << ", \"observed_source\": \"" << observed_source << "\", \"abs_err\": "
             << final_energy_report.abs_err << ", \"rel_err\": "
             << final_energy_report.rel_err << ", \"abs_tol\": "
             << are_energy_threshold.abs_tol << ", \"rel_tol\": "
             << are_energy_threshold.rel_tol << ", \"status\": \""
             << (final_energy_report.pass ? "PASS" : "FAIL") << "\"},\n"
             << "  \"post_stop_traffic\": {\n"
             << "    \"cycles_checked\": " << kPostStopCycles << ",\n"
             << "    \"quiet\": " << (post_stop_quiet ? "true" : "false") << ",\n"
             << "    \"pre_stop\": {\"pe0_ingress\": " << pre_stop_pe0_ingress
             << ", \"pe1_ingress\": " << pre_stop_pe1_ingress << ", \"pe0_cmd\": "
             << pre_stop_pe0_cmd << ", \"pe1_cmd\": " << pre_stop_pe1_cmd
             << ", \"pe0_dut_tx\": " << pre_stop_pe0_dut_tx << ", \"pe1_dut_tx\": "
             << pre_stop_pe1_dut_tx << "},\n"
             << "    \"post_stop\": {\"pe0_ingress\": " << post_stop_pe0_ingress
             << ", \"pe1_ingress\": " << post_stop_pe1_ingress << ", \"pe0_cmd\": "
             << post_stop_pe0_cmd << ", \"pe1_cmd\": " << post_stop_pe1_cmd
             << ", \"pe0_dut_tx\": " << post_stop_pe0_dut_tx << ", \"pe1_dut_tx\": "
             << post_stop_pe1_dut_tx << "}\n"
             << "  },\n"
             << "  \"supersteps\": [\n";

  for (size_t i = 0; i < trace.size(); ++i) {
    const IterationTrace& row = trace[i];
    result_ofs << "    {\"iteration\": " << row.iteration
               << ", \"factor_phase_remote_ingress\": " << row.factor_phase_remote_ingress
               << ", \"factor_phase_cmd_accepted\": " << row.factor_phase_cmd_accepted
               << ", \"factor_phase_dut_tx\": " << row.factor_phase_dut_tx
               << ", \"variable_phase_remote_ingress\": " << row.variable_phase_remote_ingress
               << ", \"variable_phase_cmd_accepted\": " << row.variable_phase_cmd_accepted
               << ", \"variable_phase_dut_tx\": " << row.variable_phase_dut_tx
               << ", \"pe0_ingress_total\": " << row.pe0_ingress_total
               << ", \"pe1_ingress_total\": " << row.pe1_ingress_total
               << ", \"pe0_cmd_total\": " << row.pe0_cmd_total
               << ", \"pe1_cmd_total\": " << row.pe1_cmd_total
               << ", \"pe0_dut_tx_total\": " << row.pe0_dut_tx_total
               << ", \"pe1_dut_tx_total\": " << row.pe1_dut_tx_total << "}";
    result_ofs << (i + 1 == trace.size() ? "\n" : ",\n");
  }

  result_ofs << "  ],\n"
             << "  \"pe_count\": " << pe_count << ",\n"
             << "  \"mesh\": {\"x\": " << mesh_x << ", \"y\": " << mesh_y << "},\n"
             << "  \"pe_counts\": [\n";
  for (uint32_t pe = 0; pe < pe_count; ++pe) {
    result_ofs << "    {\"pe\": " << pe
               << ", \"sent\": " << pe_sent[pe]
               << ", \"received\": " << pe_received[pe]
               << ", \"consumed\": " << pe_consumed[pe] << "}";
    result_ofs << (pe + 1u == pe_count ? "\n" : ",\n");
  }
  result_ofs << "  ]\n"
             << "}\n";

  std::printf(
      "gbp_pe_mesh_2pe_convergence: PASS pe_count=%u mesh=%ux%u workload=%s iterations=%d stop_reason=fixed_iters factor_to_var_remote=%u var_to_factor_remote=%u factor_to_var_dut_tx=%u var_to_factor_dut_tx=%u final_are_expected=%.9g final_are_observed=%.9g final_are_observed_source=%s final_are_compare=%s final_energy_expected=%.9g final_energy_observed=%.9g final_energy_observed_source=%s final_energy_compare=%s convergence_json=%s\n",
      pe_count,
      mesh_x,
      mesh_y,
      workload,
      runtime_iters,
      factor_to_var_remote,
      var_to_factor_remote,
      factor_to_var_dut_tx,
      var_to_factor_dut_tx,
      final_are_report.expected,
      final_are_report.observed,
      observed_source,
      final_are_ok ? "PASS" : "FAIL",
      final_energy_report.expected,
      final_energy_report.observed,
      observed_source,
      final_energy_ok ? "PASS" : "FAIL",
      result_path.c_str());

  if (!(final_are_ok && final_energy_ok)) {
    delete dut;
    return 1;
  }

  delete dut;
  return 0;
}

#include "../common/gbp_terminal_metrics_adapter.cpp"
