#include <array>
#include <algorithm>
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
#include "Vgbp_pe_mesh_2pe_convergence.h"
#include "Vgbp_pe_mesh_2pe_convergence_bsg_manycore_tile_compute_mesh__pi2.h"

#include "../common/gbp_message_payload_codec.hpp"
#include "../common/gbp_terminal_metrics_adapter.hpp"

using Dut = Vgbp_pe_mesh_2pe_convergence;

static constexpr uint32_t kRowBytesLg = 5;
static constexpr uint32_t kMmioBankB0 = 0;
static constexpr uint32_t kPayloadBankB4 = 4;
static constexpr uint32_t kCreditMetaField = 0;
static constexpr uint32_t kTailField = 3;
static constexpr uint32_t kDoorbellField = 5;
static constexpr int kFixedIters = 50;
static constexpr int kPostStopCycles = 64;
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

static uint64_t g_snapshot_seq = 0;
static uint64_t g_cycle_count = 0;

struct PartitionInfo {
  std::array<std::vector<int>, 2> fac_mapping;
  std::array<std::vector<int>, 2> var_mapping;
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

static uint32_t ingress_bank_from_addr(uint32_t addr) {
  return (addr >> kRowBytesLg) & kIngressBankMask;
}

static uint32_t ingress_row_from_addr(uint32_t addr) {
  return addr >> kIngressRowShift;
}

static std::array<uint32_t, 4> adapter_payload_planes_row0_for_pe(const Dut* dut, int pe) {
  if (pe == 0) {
    return {static_cast<uint32_t>(dut->pe0_adapter_payload_plane0_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane1_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane2_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane3_row0_o)};
  }
  return {static_cast<uint32_t>(dut->pe1_adapter_payload_plane0_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane1_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane2_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane3_row0_o)};
}

static void try_capture_semantic_message(const Dut* dut,
                                         int pe,
                                         uint32_t addr,
                                         uint32_t data,
                                         bool is_local,
                                         std::array<std::vector<ObservedSemanticMessage>, 2>* observed) {
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

  gbp_message_payload_codec::EncodedPayload encoded;
  encoded.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
  encoded.bank = message.bank;
  encoded.slot = message.slot;
  encoded.direction = gbp_message_payload_codec::direction_for_slot(encoded.slot);
  encoded.dim = 2;
  encoded.eta_len = 2;
  encoded.lam_len = 3;
  gbp_message_payload_codec::Segment segment;
  segment.segment_idx = 0;
  segment.segment_count = 1;
  segment.segment_payload_words = 5;
  segment.words.assign(message.payload_words.begin(), message.payload_words.end());
  encoded.segments.push_back(segment);

  gbp_message_payload_codec::DecodedPayload decoded;
  std::string codec_error;
  size_t words_consumed = 0;
  if (!gbp_message_payload_codec::decode(encoded, &decoded, &codec_error, &words_consumed)) {
    return;
  }
  if (decoded.eta.size() != 2u || decoded.lam.size() != 3u || words_consumed != 5u) {
    return;
  }

  message.eta[0] = decoded.eta[0];
  message.eta[1] = decoded.eta[1];
  message.lam[0] = decoded.lam[0];
  message.lam[1] = decoded.lam[1];
  message.lam[2] = decoded.lam[2];
  (*observed)[pe].push_back(message);
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
    std::array<std::vector<TouchedWrite>, 2>* touched,
    std::array<std::vector<ObservedSemanticMessage>, 2>* observed_semantic_messages) {
  if (!dut->rst_n) {
    return;
  }
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
                 std::array<std::vector<TouchedWrite>, 2>* touched,
                 std::array<std::vector<ObservedSemanticMessage>, 2>* observed_semantic_messages) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
  g_cycle_count += 1;
  sample_touched_writes(dut, touched, observed_semantic_messages);
}

static void reset_dut(Dut* dut,
                      std::array<std::vector<TouchedWrite>, 2>* touched,
                      std::array<std::vector<ObservedSemanticMessage>, 2>* observed_semantic_messages) {
  dut->rst_n = 0;
  dut->send0_v = 0;
  dut->send0_we = 0;
  dut->send0_addr = 0;
  dut->send0_data = 0;
  dut->send1_v = 0;
  dut->send1_we = 0;
  dut->send1_addr = 0;
  dut->send1_data = 0;
  tick(dut, touched, observed_semantic_messages);
  tick(dut, touched, observed_semantic_messages);
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

  const size_t key = text.find("\"are_energy\"");
  if (key == std::string::npos) {
    std::fprintf(stderr, "FAIL: oracle JSON missing thresholds.are_energy section\n");
    return false;
  }
  const size_t start = text.find('{', key);
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

static bool parse_two_pe_table(const std::string& section, std::array<std::vector<int>, 2>* out) {
  std::regex table_re("\\[\\s*\\[([^\\]]*)\\]\\s*,\\s*\\[([^\\]]*)\\]\\s*\\]");
  std::smatch m;
  if (!std::regex_search(section, m, table_re) || m.size() < 3) {
    return false;
  }
  (*out)[0] = parse_int_list(m[1].str());
  (*out)[1] = parse_int_list(m[2].str());
  return true;
}

static bool load_partition_info(const char* path, PartitionInfo* out) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open PARTITION JSON path=%s\n", path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  std::string fac_section;
  if (!extract_balanced_region(text, "fac_mapping_table", '[', ']', &fac_section)
      || !parse_two_pe_table(fac_section, &out->fac_mapping)) {
    std::fprintf(stderr, "FAIL: malformed fac_mapping_table in PARTITION=%s\n", path);
    return false;
  }

  std::string var_section;
  if (!extract_balanced_region(text, "var_mapping_table", '[', ']', &var_section)
      || !parse_two_pe_table(var_section, &out->var_mapping)) {
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

static bool issue_req(Dut* dut,
                      std::array<std::vector<TouchedWrite>, 2>* touched,
                      std::array<std::vector<ObservedSemanticMessage>, 2>* observed_semantic_messages,
                      int src_pe,
                      bool we,
                      uint32_t addr,
                      uint32_t data,
                      int max_cycles) {
  if (src_pe == 0) {
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
}

static uint32_t ingress_count_for_pe(Dut* dut, int pe) {
  return static_cast<uint32_t>(pe == 0 ? dut->pe0_ingress_wr_count_o : dut->pe1_ingress_wr_count_o);
}

static uint32_t cmd_accept_count_for_pe(Dut* dut, int pe) {
  return static_cast<uint32_t>(pe == 0 ? dut->pe0_cmd_accept_count_o : dut->pe1_cmd_accept_count_o);
}

static uint32_t dut_tx_count_for_pe(Dut* dut, int pe) {
  return static_cast<uint32_t>(pe == 0 ? dut->pe0_dut_tx_count_o : dut->pe1_dut_tx_count_o);
}

static bool run_remote_message(Dut* dut,
                               std::array<std::vector<TouchedWrite>, 2>* touched,
                               std::array<std::vector<ObservedSemanticMessage>, 2>* observed_semantic_messages,
                               int src_pe,
                               int dst_pe,
                               uint32_t payload_bank,
                               uint8_t qid,
                               uint8_t txn_id,
                               uint32_t payload,
                               uint32_t* sent_reqs,
                               uint32_t* remote_ingress_delta,
                               uint32_t* remote_cmd_delta,
                               uint32_t* dst_dut_tx_delta) {
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

  for (int cycle = 0; cycle < 1024; ++cycle) {
    tick(dut, touched, observed_semantic_messages);
    const uint32_t ingress_now = ingress_count_for_pe(dut, dst_pe);
    const uint32_t cmd_now = cmd_accept_count_for_pe(dut, dst_pe);
    const uint32_t dst_dut_tx_now = dut_tx_count_for_pe(dut, dst_pe);
    if (dst_dut_tx_now > dst_dut_tx_before) {
      *remote_ingress_delta = ingress_now - ingress_before;
      *remote_cmd_delta = cmd_now - cmd_before;
      *dst_dut_tx_delta = dst_dut_tx_now - dst_dut_tx_before;
      return true;
    }
  }

  std::fprintf(
      stderr,
      "FAIL: remote accept timeout src_pe=%d dst_pe=%d ingress_before=%u ingress_after=%u cmd_before=%u cmd_after=%u dst_dut_tx_before=%u dst_dut_tx_after=%u\n",
      src_pe,
      dst_pe,
      ingress_before,
      ingress_count_for_pe(dut, dst_pe),
      cmd_before,
      cmd_accept_count_for_pe(dut, dst_pe),
      dst_dut_tx_before,
      dut_tx_count_for_pe(dut, dst_pe));
  return false;
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

  gbp_message_payload_codec::DecodedPayload semantic;
  semantic.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
  semantic.bank = 6;
  semantic.slot = 2;
  semantic.direction = gbp_message_payload_codec::direction_for_slot(semantic.slot);
  semantic.dim = 2;
  semantic.eta.push_back(static_cast<float>(factor_id + 1));
  semantic.eta.push_back(static_cast<float>(variable_id + 1));
  semantic.lam.push_back(2.0f + static_cast<float>(owner_pe));
  semantic.lam.push_back(0.125f * static_cast<float>((factor_id % 7) + 1));
  semantic.lam.push_back(1.5f + static_cast<float>((variable_id % 5) + 1));

  gbp_message_payload_codec::EncodedPayload encoded;
  if (!gbp_message_payload_codec::encode(semantic, &encoded, error)) {
    return false;
  }
  if (encoded.segments.empty() || encoded.segments[0].words.size() != 5u) {
    *error = "local semantic encode produced unexpected segment shape";
    return false;
  }

  gbp_message_payload_codec::DecodedPayload decoded;
  size_t words_consumed = 0;
  if (!gbp_message_payload_codec::decode(encoded, &decoded, error, &words_consumed)) {
    return false;
  }
  if (decoded.eta.size() != 2u || decoded.lam.size() != 3u || words_consumed != 5u) {
    *error = "local semantic decode shape mismatch";
    return false;
  }

  out->pe = owner_pe;
  out->factor_id = factor_id;
  out->variable_id = variable_id;
  out->semantic.bank = semantic.bank;
  out->semantic.slot = semantic.slot;
  out->semantic.row = row;
  out->semantic.is_local = true;
  for (size_t i = 0; i < 5; ++i) {
    out->semantic.payload_words[i] = encoded.segments[0].words[i];
  }
  out->semantic.eta[0] = decoded.eta[0];
  out->semantic.eta[1] = decoded.eta[1];
  out->semantic.lam[0] = decoded.lam[0];
  out->semantic.lam[1] = decoded.lam[1];
  out->semantic.lam[2] = decoded.lam[2];
  return true;
}

static bool read_spm_row_words(const Dut* dut,
                               int pe,
                               uint32_t bank,
                               uint32_t row,
                               std::array<uint32_t, kWordsPerRow>* beats) {
  const auto* tile = (pe == 0) ? dut->__PVT__gbp_pe_mesh_2pe_convergence__DOT__dut__DOT__tile0
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
}

static bool write_dut_terminal_dump(const std::string& path,
                                    const Dut* dut,
                                    const char* workload,
                                    int iterations,
                                    const PartitionInfo& partition,
                                    const TerminalDumpPe& pe0,
                                    const TerminalDumpPe& pe1,
                                    const std::array<std::vector<TouchedWrite>, 2>& touched_by_pe,
                                    const std::array<std::vector<ObservedSemanticMessage>, 2>& captured_semantic_messages_by_pe,
                                    uint64_t snapshot_seq,
                                    uint64_t snapshot_cycle,
                                    int seed) {
  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to write DUT terminal dump path=%s\n", path.c_str());
    return false;
  }

  if (std::string(workload) != "synthetic_line" && std::string(workload) != "synthetic_lattice") {
    std::fprintf(stderr,
                 "FAIL: unsupported workload for terminal dump workload=%s\n",
                 workload);
    return false;
  }
  if (iterations != kFixedIters) {
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

  std::array<std::vector<RowBinding>, 2> effective_state_rows_by_pe;
  std::array<std::vector<RowBinding>, 2> effective_message_rows_by_pe;

  std::set<uint32_t> required_state_banks;
  std::set<uint32_t> required_message_banks;
  int required_row = -1;
  for (int pe = 0; pe < 2; ++pe) {
    std::set<uint32_t> touched_message_rows =
        collect_touched_rows(touched_by_pe[pe], kRequiredMessageBankLo, kRequiredMessageBankHi);
    if (touched_message_rows.empty()) {
      std::fprintf(stderr, "FAIL: no DUT-touched message rows captured for pe=%d\n", pe);
      return false;
    }

    const uint32_t selected_message_row = *touched_message_rows.begin();

    const std::set<uint32_t> touched_state_rows =
        collect_touched_rows(touched_by_pe[pe], kRequiredStateBankLo, kRequiredStateBankHi);
    uint32_t selected_state_row = kRequiredRowIndex;
    if (!touched_state_rows.empty()) {
      selected_state_row = *touched_state_rows.begin();
    }

    const std::set<uint32_t> message_row_identity = {selected_message_row};
    const std::set<uint32_t> state_row_identity = {selected_state_row};

    effective_message_rows_by_pe[pe] = materialize_rows_from_snapshot(pe,
                                                                      message_row_identity,
                                                                      kRequiredMessageBankLo,
                                                                      kRequiredMessageBankHi,
                                                                      "message");
    effective_state_rows_by_pe[pe] = materialize_rows_from_snapshot(pe,
                                                                    state_row_identity,
                                                                    kRequiredStateBankLo,
                                                                    kRequiredStateBankHi,
                                                                    "state");
    if (effective_message_rows_by_pe[pe].empty() || effective_state_rows_by_pe[pe].empty()) {
      return false;
    }
    for (const RowBinding& row : effective_state_rows_by_pe[pe]) {
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
    for (const RowBinding& row : effective_message_rows_by_pe[pe]) {
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
  for (int pe = 0; pe < 2; ++pe) {
    for (int factor_id : partition.fac_mapping[pe]) {
      factor_owner[factor_id] = pe;
    }
    for (int var_id : partition.var_mapping[pe]) {
      var_owner[var_id] = pe;
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
  if (!gbp_terminal_metrics_adapter::collect_static_inbound_factors(
          workload, seed, &static_inbound_factors_by_var, &static_fan_in_error)) {
    std::fprintf(stderr,
                 "FAIL: unable to collect static inbound factors workload=%s seed=%d error=%s\n",
                 workload,
                 seed,
                 static_fan_in_error.c_str());
    return false;
  }
  std::vector<int> variable_ids;
  variable_ids.reserve(var_owner.size());
  for (const auto& kv : var_owner) {
    variable_ids.push_back(kv.first);
  }
  std::sort(variable_ids.begin(), variable_ids.end());

  std::array<std::map<uint32_t, const RowBinding*>, 2> state_row_by_bank;
  std::array<std::map<uint32_t, const RowBinding*>, 2> message_row_by_bank;
  std::array<std::vector<const RowBinding*>, 2> nonzero_message_rows_by_pe;
  std::array<std::vector<ObservedSemanticMessage>, 2> observed_semantic_messages_by_pe =
      captured_semantic_messages_by_pe;
  for (int pe = 0; pe < 2; ++pe) {
    for (const RowBinding& row : effective_state_rows_by_pe[pe]) {
      auto ins = state_row_by_bank[pe].emplace(row.bank, &row);
      if (!ins.second && ins.first->second->address != row.address) {
        std::fprintf(stderr,
                     "FAIL: ambiguous state row identity pe=%d bank=%u addr_a=%u addr_b=%u\n",
                     pe,
                     row.bank,
                     ins.first->second->address,
                     row.address);
        return false;
      }
    }
    for (const RowBinding& row : effective_message_rows_by_pe[pe]) {
      auto ins = message_row_by_bank[pe].emplace(row.bank, &row);
      if (!ins.second && ins.first->second->address != row.address) {
        std::fprintf(stderr,
                     "FAIL: ambiguous message row identity pe=%d bank=%u addr_a=%u addr_b=%u\n",
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
  } else {
    ofs << "    \"rows\": 4,\n"
        << "    \"cols\": 4,\n";
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
      << "  \"terminal_dump\": [\n"
      << "    {\"pe\": 0,\n";
  emit_adapter_payload_dump(pe0, effective_message_rows_by_pe[0]);
  ofs << ",\n";
  if (!emit_required_rows(effective_state_rows_by_pe[0], effective_message_rows_by_pe[0])) {
    return false;
  }
  ofs << ",\n";
  emit_touched_rows(touched_by_pe[0]);
  ofs << ",\n";
  emit_semantic_messages(observed_semantic_messages_by_pe[0], true);
  ofs << ",\n";
  emit_semantic_messages(observed_semantic_messages_by_pe[0], false);
  ofs << "\n    },\n"
      << "    {\"pe\": 1,\n";
  emit_adapter_payload_dump(pe1, effective_message_rows_by_pe[1]);
  ofs << ",\n";
  if (!emit_required_rows(effective_state_rows_by_pe[1], effective_message_rows_by_pe[1])) {
    return false;
  }
  ofs << ",\n";
  emit_touched_rows(touched_by_pe[1]);
  ofs << ",\n";
  emit_semantic_messages(observed_semantic_messages_by_pe[1], true);
  ofs << ",\n";
  emit_semantic_messages(observed_semantic_messages_by_pe[1], false);
  ofs << "\n    }\n"
      << "  ],\n"
      << "  \"variable_snapshots\": [\n";

  std::array<std::vector<ObservedSemanticMessage>, 2> remote_semantic_messages_by_pe;
  std::vector<LocalSemanticMessageRecord> generated_local_semantic_messages;
  for (int pe = 0; pe < 2; ++pe) {
    for (const ObservedSemanticMessage& msg : observed_semantic_messages_by_pe[pe]) {
      if (!msg.is_local) {
        remote_semantic_messages_by_pe[pe].push_back(msg);
      }
    }
  }
  for (size_t i = 0; i < variable_ids.size(); ++i) {
    const int var_id = variable_ids[i];
    const int owner_pe = var_owner[var_id];
    const auto state_it = state_row_by_bank[owner_pe].find(kRequiredStateBankLo);
    if (state_it == state_row_by_bank[owner_pe].end()) {
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

    if (remote_semantic_messages_by_pe[owner_pe].empty()) {
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
      const bool factor_is_local =
          !is_partition_edge
          || (factor_owner_it != factor_owner.end() && factor_owner_it->second == owner_pe);
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
        if (remote_cursor >= remote_semantic_messages_by_pe[owner_pe].size()) {
          std::fprintf(stderr,
                       "FAIL: insufficient remote semantic messages pe=%d variable_id=%d factor_id=%d required_index=%zu available=%zu\n",
                       owner_pe,
                       var_id,
                       factor_id,
                       remote_cursor,
                       remote_semantic_messages_by_pe[owner_pe].size());
          return false;
        }
        const ObservedSemanticMessage& msg = remote_semantic_messages_by_pe[owner_pe][remote_cursor];
        remote_cursor += 1u;
        payload_words = msg.payload_words;
        msg_bank = msg.bank;
        msg_row = msg.row;
        msg_slot = msg.slot;
      }

      gbp_message_payload_codec::EncodedPayload encoded;
      encoded.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
      encoded.bank = msg_bank;
      encoded.slot = msg_slot;
      encoded.direction = gbp_message_payload_codec::direction_for_slot(encoded.slot);
      encoded.dim = 2;
      encoded.eta_len = 2;
      encoded.lam_len = 3;
      gbp_message_payload_codec::Segment segment;
      segment.segment_idx = 0;
      segment.segment_count = 1;
      segment.segment_payload_words = 5;
      segment.words.assign(payload_words.begin(), payload_words.end());
      encoded.segments.push_back(segment);

      gbp_message_payload_codec::DecodedPayload decoded;
      std::string codec_error;
      size_t words_consumed = 0;
      if (!gbp_message_payload_codec::decode(encoded, &decoded, &codec_error, &words_consumed)) {
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
      if (decoded.eta.size() != 2u || decoded.lam.size() != 3u || words_consumed != 5u) {
        std::fprintf(stderr,
                     "FAIL: semantic payload shape mismatch pe=%d variable_id=%d factor_id=%d bank=%u slot=%u eta=%zu lam=%zu words=%zu\n",
                     owner_pe,
                     var_id,
                     factor_id,
                     msg_bank,
                     msg_slot,
                     decoded.eta.size(),
                     decoded.lam.size(),
                     words_consumed);
        return false;
      }

      ofs << "        {\n"
          << "          \"factor_id\": " << factor_id << ",\n"
          << "          \"variable_id\": " << var_id << ",\n"
          << "          \"message_slot\": " << msg_slot << ",\n"
          << "          \"msg_bank\": " << msg_bank << ",\n"
          << "          \"msg_row\": " << msg_row << ",\n"
          << "          \"msg_beat\": 0,\n"
          << "          \"schema_version\": " << encoded.schema_version << ",\n"
          << "          \"direction\": \""
          << (encoded.direction == gbp_message_payload_codec::Direction::kFactorToVar
                  ? "factor_to_var"
                  : "var_to_factor")
          << "\",\n"
          << "          \"dim\": " << encoded.dim << ",\n"
          << "          \"eta_len\": " << encoded.eta_len << ",\n"
          << "          \"lam_len\": " << encoded.lam_len << ",\n"
          << "          \"segment_idx\": 0,\n"
          << "          \"segment_count\": 1,\n"
          << "          \"segment_payload_words\": 5,\n"
          << "          \"payload_words\": ["
          << payload_words[0] << ", " << payload_words[1] << ", " << payload_words[2] << ", "
          << payload_words[3] << ", " << payload_words[4] << "],\n"
          << "          \"msg_eta\": ["
          << decoded.eta[0] << ", "
          << decoded.eta[1] << "],\n"
          << "          \"msg_lam\": [["
          << decoded.lam[0] << ", "
          << decoded.lam[1] << "], ["
          << decoded.lam[1] << ", "
          << decoded.lam[2] << "]]\n"
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

  const char* workload = std::getenv("WORKLOAD");
  if (workload == nullptr || workload[0] == '\0') {
    workload = "synthetic_line";
  }
  const bool is_line = std::string(workload) == "synthetic_line";
  const bool is_lattice = std::string(workload) == "synthetic_lattice";
  if (!is_line && !is_lattice) {
    std::fprintf(stderr,
                 "FAIL: unsupported WORKLOAD=%s expected synthetic_line|synthetic_lattice\n",
                 workload);
    delete dut;
    return 1;
  }

  const std::string default_partition =
      std::string("tests/oracle/generated/") + workload + "_partition_2pe_factor_variable.json";
  const char* partition_env = std::getenv("PARTITION");
  const char* partition_path =
      (partition_env == nullptr || partition_env[0] == '\0') ? default_partition.c_str() : partition_env;

  PartitionInfo partition{};
  if (!load_partition_info(partition_path, &partition)) {
    delete dut;
    return 1;
  }

  const char* are_energy_expected_oracle_path = std::getenv("GBP_ORACLE_PHASE1_EXPECTED_PATH");
  if (are_energy_expected_oracle_path == nullptr || are_energy_expected_oracle_path[0] == '\0') {
    are_energy_expected_oracle_path = "tests/oracle/gbp_oracle_phase1.json";
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
  for (int pe = 0; pe < 2; ++pe) {
    for (int f : partition.fac_mapping[pe]) {
      factor_owner[f] = pe;
    }
    for (int v : partition.var_mapping[pe]) {
      var_owner[v] = pe;
    }
  }

  const std::vector<CrossEdge> cross_edges = collect_cross_edges(partition, factor_owner, var_owner);
  if (cross_edges.empty()) {
    std::fprintf(stderr,
                 "FAIL: no cross-PE factor_var_edges found in PARTITION=%s\n",
                 partition_path);
    delete dut;
    return 1;
  }
  for (const CrossEdge& edge : cross_edges) {
    if (!((edge.factor_owner_pe == 0 && edge.variable_owner_pe == 1)
          || (edge.factor_owner_pe == 1 && edge.variable_owner_pe == 0))) {
      std::fprintf(stderr,
                   "FAIL: unsupported owner mapping for 2-PE mesh factor_id=%d variable_id=%d fac_pe=%d var_pe=%d\n",
                   edge.factor_id,
                   edge.variable_id,
                   edge.factor_owner_pe,
                   edge.variable_owner_pe);
      delete dut;
      return 1;
    }
  }

  std::array<std::vector<TouchedWrite>, 2> touched_by_pe;
  std::array<std::vector<ObservedSemanticMessage>, 2> observed_semantic_messages_by_pe;
  g_snapshot_seq += 1;
  g_cycle_count = 0;
  reset_dut(dut, &touched_by_pe, &observed_semantic_messages_by_pe);

  uint32_t pe_sent[2] = {0, 0};
  uint32_t pe_received[2] = {0, 0};
  uint32_t pe_consumed[2] = {0, 0};
  uint32_t factor_to_var_remote = 0;
  uint32_t var_to_factor_remote = 0;
  uint32_t factor_to_var_dut_tx = 0;
  uint32_t var_to_factor_dut_tx = 0;

  std::vector<IterationTrace> trace;
  trace.reserve(kFixedIters);

  for (int iter = 0; iter < kFixedIters; ++iter) {
    IterationTrace row{};
    row.iteration = iter;

    for (size_t edge_idx = 0; edge_idx < cross_edges.size(); ++edge_idx) {
      const CrossEdge& edge = cross_edges[edge_idx];
      uint32_t ingress_delta = 0;
      uint32_t cmd_delta = 0;
      uint32_t dut_tx_delta = 0;
      if (!run_remote_message(dut,
                              &touched_by_pe,
                              &observed_semantic_messages_by_pe,
                              edge.factor_owner_pe,
                              edge.variable_owner_pe,
                              4u,
                              0,
                              static_cast<uint8_t>((0x40 + iter + edge_idx) & 0xFFu),
                              make_bootstrap_payload_word(4u,
                                                          0u,
                                                          edge.factor_id,
                                                          edge.variable_id,
                                                          edge.factor_owner_pe,
                                                          edge.variable_owner_pe),
                              &pe_sent[edge.factor_owner_pe],
                              &ingress_delta,
                              &cmd_delta,
                              &dut_tx_delta)) {
        delete dut;
        return 1;
      }
      row.factor_phase_remote_ingress += ingress_delta;
      row.factor_phase_cmd_accepted += cmd_delta;
      row.factor_phase_dut_tx += dut_tx_delta;
      factor_to_var_remote += ingress_delta;
      factor_to_var_dut_tx += dut_tx_delta;
      pe_received[edge.variable_owner_pe] += ingress_delta;
      pe_consumed[edge.variable_owner_pe] += cmd_delta;
    }

    for (size_t edge_idx = 0; edge_idx < cross_edges.size(); ++edge_idx) {
      const CrossEdge& edge = cross_edges[edge_idx];
      uint32_t ingress_delta = 0;
      uint32_t cmd_delta = 0;
      uint32_t dut_tx_delta = 0;
      if (!run_remote_message(dut,
                              &touched_by_pe,
                              &observed_semantic_messages_by_pe,
                              edge.variable_owner_pe,
                              edge.factor_owner_pe,
                              5u,
                              0,
                              static_cast<uint8_t>((0x80 + iter + edge_idx) & 0xFFu),
                              make_bootstrap_payload_word(5u,
                                                          1u,
                                                          edge.factor_id,
                                                          edge.variable_id,
                                                          edge.variable_owner_pe,
                                                          edge.factor_owner_pe),
                              &pe_sent[edge.variable_owner_pe],
                              &ingress_delta,
                              &cmd_delta,
                              &dut_tx_delta)) {
        delete dut;
        return 1;
      }
      row.variable_phase_remote_ingress += ingress_delta;
      row.variable_phase_cmd_accepted += cmd_delta;
      row.variable_phase_dut_tx += dut_tx_delta;
      var_to_factor_remote += ingress_delta;
      var_to_factor_dut_tx += dut_tx_delta;
      pe_received[edge.factor_owner_pe] += ingress_delta;
      pe_consumed[edge.factor_owner_pe] += cmd_delta;
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
    tick(dut, &touched_by_pe, &observed_semantic_messages_by_pe);
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
                 "FAIL: post-stop traffic not quiet after settle_window=%d required_quiet_cycles=%d final=[%u,%u,%u,%u,%u,%u]\n",
                 kPostStopSettleMaxCycles,
                 kPostStopCycles,
                 prev_pe0_ingress,
                 prev_pe1_ingress,
                 prev_pe0_cmd,
                 prev_pe1_cmd,
                 prev_pe0_dut_tx,
                 prev_pe1_dut_tx);
    delete dut;
    return 1;
  }

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

  auto copy_row_words = [](const auto& row_words) {
    std::array<uint32_t, kWordsPerRow> out{};
    for (size_t i = 0; i < kWordsPerRow; ++i) {
      out[i] = row_words[i];
    }
    return out;
  };

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

  int observed_seed = 12345;
  if (const char* seed_env = std::getenv("SEED")) {
    char* seed_end = nullptr;
    const long seed_parsed = std::strtol(seed_env, &seed_end, 10);
    if (seed_end != seed_env && *seed_end == '\0') {
      observed_seed = static_cast<int>(seed_parsed);
    }
  }

  const std::string dut_terminal_dump_path = std::string("build/integration/gbp_pe_mesh_2pe_convergence/")
      + workload + "_dut_terminal_dump.json";
  if (!write_dut_terminal_dump(dut_terminal_dump_path,
                               dut,
                               workload,
                               kFixedIters,
                               partition,
                               pe0_dump,
                               pe1_dump,
                               touched_by_pe,
                               observed_semantic_messages_by_pe,
                               snapshot_seq,
                               snapshot_cycle,
                               observed_seed)) {
    delete dut;
    return 1;
  }

  gbp_terminal_metrics_adapter::Metrics are_energy_observed{};
  std::string are_energy_observed_error;
  if (!gbp_terminal_metrics_adapter::reconstruct_metrics_from_dump(
          dut_terminal_dump_path, &are_energy_observed, &are_energy_observed_error)) {
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
                                                    are_energy_observed.are,
                                                    observed_source,
                                                    are_energy_threshold,
                                                    &final_are_report);
  const bool final_energy_ok = check_are_energy_metric(workload,
                                                       final_energy_report.field,
                                                       are_energy_expected.final_energy,
                                                       are_energy_observed.energy,
                                                       observed_source,
                                                       are_energy_threshold,
                                                       &final_energy_report);

  const std::string result_path =
      std::string("build/integration/gbp_pe_mesh_2pe_convergence/") + workload + "_convergence_result.json";
  const std::string distributed_trace_path =
      std::string("build/integration/gbp_pe_mesh_2pe_gbp/") + workload + "_distributed_trace.json";
  std::ofstream result_ofs(result_path);
  if (!result_ofs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to write convergence artifact path=%s\n", result_path.c_str());
    delete dut;
    return 1;
  }

  result_ofs << "{\n"
             << "  \"test\": \"gbp_pe_mesh_2pe_convergence\",\n"
             << "  \"workload\": \"" << workload << "\",\n"
             << "  \"partition\": \"" << partition_path << "\",\n"
             << "  \"iterations\": " << kFixedIters << ",\n"
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
             << "  \"oracle_are_energy_compare\": {\n"
             << "    \"expected_path\": \"" << are_energy_expected_oracle_path << "\",\n"
             << "    \"observed_source\": \"" << observed_source << "\",\n"
              << "    \"observed_adapter_raw_are\": " << are_energy_observed.are << ",\n"
              << "    \"observed_adapter_raw_energy\": " << are_energy_observed.energy << ",\n"
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
             << "  \"pe_counts\": [\n"
             << "    {\"pe\": 0, \"sent\": " << pe_sent[0] << ", \"received\": " << pe_received[0]
             << ", \"consumed\": " << pe_consumed[0] << "},\n"
             << "    {\"pe\": 1, \"sent\": " << pe_sent[1] << ", \"received\": " << pe_received[1]
             << ", \"consumed\": " << pe_consumed[1] << "}\n"
             << "  ]\n"
             << "}\n";

  std::printf(
      "gbp_pe_mesh_2pe_convergence: PASS workload=%s iterations=%d stop_reason=fixed_iters factor_to_var_remote=%u var_to_factor_remote=%u factor_to_var_dut_tx=%u var_to_factor_dut_tx=%u final_are_expected=%.9g final_are_observed=%.9g final_are_observed_source=%s final_are_compare=%s final_energy_expected=%.9g final_energy_observed=%.9g final_energy_observed_source=%s final_energy_compare=%s convergence_json=%s\n",
      workload,
      kFixedIters,
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
