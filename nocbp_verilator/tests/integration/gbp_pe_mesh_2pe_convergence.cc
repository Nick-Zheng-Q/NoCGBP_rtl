#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "verilated.h"
#include "Vgbp_pe_mesh_2pe_convergence.h"

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

struct PartitionInfo {
  std::array<std::vector<int>, 2> fac_mapping;
  std::array<std::vector<int>, 2> var_mapping;
  std::vector<std::pair<int, int>> edges;
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
  uint32_t state_b1_row0_word0 = 0;
  uint32_t state_b2_row0_word0 = 0;
  uint32_t state_b3_row0_word0 = 0;
  uint32_t message_b4_row0_word0 = 0;
  uint32_t message_b5_row0_word0 = 0;
  uint32_t message_b6_row0_word0 = 0;
  uint32_t message_b7_row0_word0 = 0;
  uint32_t adapter_payload_plane0_row0 = 0;
  uint32_t adapter_credit_q0 = 0;
  uint32_t adapter_tail_q0 = 0;
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

static void sample_touched_writes(Dut* dut, std::array<std::vector<TouchedWrite>, 2>* touched) {
  if (!dut->rst_n) {
    return;
  }
  if (dut->pe0_wr_req_valid_o) {
    record_touched_write(&(*touched)[0],
                         static_cast<uint32_t>(dut->pe0_wr_req_addr_o),
                         static_cast<uint32_t>(dut->pe0_wr_req_data_o),
                         kSourceDutWrReq);
  }
  if (dut->pe1_wr_req_valid_o) {
    record_touched_write(&(*touched)[1],
                         static_cast<uint32_t>(dut->pe1_wr_req_addr_o),
                         static_cast<uint32_t>(dut->pe1_wr_req_data_o),
                         kSourceDutWrReq);
  }
  if (dut->pe0_ingress_wr_req_valid_o) {
    record_touched_write(&(*touched)[0],
                         static_cast<uint32_t>(dut->pe0_ingress_wr_req_addr_o),
                         static_cast<uint32_t>(dut->pe0_ingress_wr_req_data_o),
                         kSourceIngressWrReq);
  }
  if (dut->pe1_ingress_wr_req_valid_o) {
    record_touched_write(&(*touched)[1],
                         static_cast<uint32_t>(dut->pe1_ingress_wr_req_addr_o),
                         static_cast<uint32_t>(dut->pe1_ingress_wr_req_data_o),
                         kSourceIngressWrReq);
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
  double reference;
  double abs_err;
  double rel_err;
  bool pass;
};

static void tick(Dut* dut, std::array<std::vector<TouchedWrite>, 2>* touched) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
  sample_touched_writes(dut, touched);
}

static void reset_dut(Dut* dut, std::array<std::vector<TouchedWrite>, 2>* touched) {
  dut->rst_n = 0;
  dut->send0_v = 0;
  dut->send0_we = 0;
  dut->send0_addr = 0;
  dut->send0_data = 0;
  dut->send1_v = 0;
  dut->send1_we = 0;
  dut->send1_addr = 0;
  dut->send1_data = 0;
  tick(dut, touched);
  tick(dut, touched);
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
                                    double reference,
                                    const Threshold& threshold,
                                    AreEnergyFieldReport* report) {
  if (!std::isfinite(expected) || !std::isfinite(reference)) {
    std::fprintf(stderr,
                 "FAIL: oracle are/energy mismatch workload=%s field=%s expected=%.9g reference=%.9g abs_err=nan rel_err=nan abs_tol=%.9g rel_tol=%.9g reason=non_finite\n",
                 workload,
                 field,
                 expected,
                 reference,
                 threshold.abs_tol,
                 threshold.rel_tol);
    return false;
  }

  const double abs_err = std::fabs(reference - expected);
  const double denom = std::fmax(std::fabs(expected), 1e-12);
  const double rel_err = abs_err / denom;
  const bool pass = (abs_err <= threshold.abs_tol) || (rel_err <= threshold.rel_tol);

  if (report != nullptr) {
    report->field = field;
    report->expected = expected;
    report->reference = reference;
    report->abs_err = abs_err;
    report->rel_err = rel_err;
    report->pass = pass;
  }

  std::printf(
      "GBP_PE_MESH_2PE_CONVERGENCE_ARE_ENERGY_MARKER workload=%s field=%s expected=%.9g reference=%.9g abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g status=%s\n",
      workload,
      field,
      expected,
      reference,
      abs_err,
      rel_err,
      threshold.abs_tol,
      threshold.rel_tol,
      pass ? "PASS" : "FAIL");

  if (pass) {
    return true;
  }
  std::fprintf(stderr,
               "FAIL: oracle are/energy mismatch workload=%s field=%s expected=%.9g reference=%.9g abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g\n",
               workload,
               field,
               expected,
               reference,
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

static bool issue_req(Dut* dut,
                      std::array<std::vector<TouchedWrite>, 2>* touched,
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
      tick(dut, touched);
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
    tick(dut, touched);
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
                               int src_pe,
                               int dst_pe,
                               uint8_t qid,
                               uint8_t txn_id,
                               uint32_t payload,
                               uint32_t* sent_reqs,
                               uint32_t* remote_ingress_delta,
                               uint32_t* remote_cmd_delta,
                               uint32_t* dst_dut_tx_delta) {
  const uint32_t payload_addr = (kPayloadBankB4 << kRowBytesLg) + (static_cast<uint32_t>(qid) << 8);
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

  if (!issue_req(dut, touched, src_pe, true, meta_addr, 0x1u, 256)) {
    std::fprintf(stderr,
                 "FAIL: credit_meta not accepted src_pe=%d dst_pe=%d addr=0x%05x\n",
                 src_pe,
                 dst_pe,
                 meta_addr);
    return false;
  }
  *sent_reqs += 1;

  if (!issue_req(dut, touched, src_pe, true, payload_addr, payload, 256)) {
    std::fprintf(stderr,
                 "FAIL: payload not accepted src_pe=%d dst_pe=%d addr=0x%05x payload=0x%08x\n",
                 src_pe,
                 dst_pe,
                 payload_addr,
                 payload);
    return false;
  }
  *sent_reqs += 1;

  if (!issue_req(dut, touched, src_pe, true, tail_addr, tail_data, 256)) {
    std::fprintf(stderr,
                 "FAIL: tail not accepted src_pe=%d dst_pe=%d addr=0x%05x\n",
                 src_pe,
                 dst_pe,
                 tail_addr);
    return false;
  }
  *sent_reqs += 1;

  if (!issue_req(dut, touched, src_pe, true, doorbell_addr, doorbell_data, 256)) {
    std::fprintf(stderr,
                 "FAIL: doorbell not accepted src_pe=%d dst_pe=%d addr=0x%05x\n",
                 src_pe,
                 dst_pe,
                 doorbell_addr);
    return false;
  }
  *sent_reqs += 1;

  for (int cycle = 0; cycle < 1024; ++cycle) {
    tick(dut, touched);
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

static uint32_t phase_payload(const char* workload, int iter, bool factor_phase, int edge_factor, int edge_var) {
  const uint32_t line_seed = 0x11000000u;
  const uint32_t lattice_seed = 0x22000000u;
  const uint32_t base = (std::string(workload) == "synthetic_line") ? line_seed : lattice_seed;
  const uint32_t phase_tag = factor_phase ? 0x00F00000u : 0x00A00000u;
  const uint32_t iter_tag = (static_cast<uint32_t>(iter) & 0xFFu) << 8;
  const uint32_t edge_tag = ((static_cast<uint32_t>(edge_factor) & 0xFu) << 4)
      | (static_cast<uint32_t>(edge_var) & 0xFu);
  return base | phase_tag | iter_tag | edge_tag;
}

static bool write_dut_terminal_dump(const std::string& path,
                                    const char* workload,
                                    int iterations,
                                    const TerminalDumpPe& pe0,
                                    const TerminalDumpPe& pe1,
                                    const std::array<std::vector<TouchedWrite>, 2>& touched_by_pe) {
  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to write DUT terminal dump path=%s\n", path.c_str());
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

  ofs << "{\n"
      << "  \"test\": \"gbp_pe_mesh_2pe_convergence\",\n"
      << "  \"workload\": \"" << workload << "\",\n"
      << "  \"iterations\": " << iterations << ",\n"
      << "  \"terminal_dump\": [\n"
    << "    {\"pe\": 0, \"state\": {\"b1_row0_word0\": " << pe0.state_b1_row0_word0
      << ", \"b2_row0_word0\": " << pe0.state_b2_row0_word0
      << ", \"b3_row0_word0\": " << pe0.state_b3_row0_word0
      << "}, \"message\": {\"b4_row0_word0\": " << pe0.message_b4_row0_word0
      << ", \"b5_row0_word0\": " << pe0.message_b5_row0_word0
      << ", \"b6_row0_word0\": " << pe0.message_b6_row0_word0
      << ", \"b7_row0_word0\": " << pe0.message_b7_row0_word0
      << "}, \"adapter\": {\"payload_plane0_row0\": " << pe0.adapter_payload_plane0_row0
      << ", \"credit_q0\": " << pe0.adapter_credit_q0
      << ", \"tail_q0\": " << pe0.adapter_tail_q0 << "},\n";
  emit_touched_rows(touched_by_pe[0]);
  ofs << "\n    },\n"
      << "    {\"pe\": 1, \"state\": {\"b1_row0_word0\": " << pe1.state_b1_row0_word0
      << ", \"b2_row0_word0\": " << pe1.state_b2_row0_word0
      << ", \"b3_row0_word0\": " << pe1.state_b3_row0_word0
      << "}, \"message\": {\"b4_row0_word0\": " << pe1.message_b4_row0_word0
      << ", \"b5_row0_word0\": " << pe1.message_b5_row0_word0
      << ", \"b6_row0_word0\": " << pe1.message_b6_row0_word0
      << ", \"b7_row0_word0\": " << pe1.message_b7_row0_word0
      << "}, \"adapter\": {\"payload_plane0_row0\": " << pe1.adapter_payload_plane0_row0
      << ", \"credit_q0\": " << pe1.adapter_credit_q0
      << ", \"tail_q0\": " << pe1.adapter_tail_q0 << "},\n";
  emit_touched_rows(touched_by_pe[1]);
  ofs << "\n    }\n"
      << "  ]\n"
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
  const char* are_energy_reference_oracle_path = std::getenv("GBP_ORACLE_PHASE1_REFERENCE_PATH");
  if (are_energy_reference_oracle_path == nullptr || are_energy_reference_oracle_path[0] == '\0') {
    are_energy_reference_oracle_path = "tests/oracle/generated/gbp_oracle_phase1.json";
  }
  Threshold are_energy_threshold{};
  if (!extract_are_energy_threshold_from_oracle(are_energy_reference_oracle_path,
                                                &are_energy_threshold)) {
    delete dut;
    return 1;
  }
  AreEnergyMetrics are_energy_expected{};
  if (!load_workload_are_energy_metrics(are_energy_expected_oracle_path, workload, &are_energy_expected)) {
    delete dut;
    return 1;
  }
  AreEnergyMetrics are_energy_reference{};
  if (!load_workload_are_energy_metrics(
          are_energy_reference_oracle_path, workload, &are_energy_reference)) {
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

  bool found_cross_edge = false;
  int edge_factor = -1;
  int edge_var = -1;
  int fac_pe = -1;
  int var_pe = -1;
  for (const auto& e : partition.edges) {
    const auto fit = factor_owner.find(e.first);
    const auto vit = var_owner.find(e.second);
    if (fit == factor_owner.end() || vit == var_owner.end()) {
      continue;
    }
    if (fit->second != vit->second) {
      found_cross_edge = true;
      edge_factor = e.first;
      edge_var = e.second;
      fac_pe = fit->second;
      var_pe = vit->second;
      break;
    }
  }

  if (!found_cross_edge) {
    std::fprintf(stderr,
                 "FAIL: no cross-PE factor_var_edges found in PARTITION=%s\n",
                 partition_path);
    delete dut;
    return 1;
  }

  std::array<std::vector<TouchedWrite>, 2> touched_by_pe;
  reset_dut(dut, &touched_by_pe);

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

    uint32_t ingress_delta = 0;
    uint32_t cmd_delta = 0;
    uint32_t dut_tx_delta = 0;
    if (!run_remote_message(dut,
                            &touched_by_pe,
                            fac_pe,
                            var_pe,
                            0,
                            static_cast<uint8_t>((0x40 + iter) & 0xFF),
                            phase_payload(workload, iter, true, edge_factor, edge_var),
                            &pe_sent[fac_pe],
                            &ingress_delta,
                            &cmd_delta,
                            &dut_tx_delta)) {
      delete dut;
      return 1;
    }
    row.factor_phase_remote_ingress = ingress_delta;
    row.factor_phase_cmd_accepted = cmd_delta;
    row.factor_phase_dut_tx = dut_tx_delta;
    factor_to_var_remote += ingress_delta;
    factor_to_var_dut_tx += dut_tx_delta;
    pe_received[var_pe] += ingress_delta;
    pe_consumed[var_pe] += cmd_delta;

    ingress_delta = 0;
    cmd_delta = 0;
    dut_tx_delta = 0;
    if (!run_remote_message(dut,
                            &touched_by_pe,
                            var_pe,
                            fac_pe,
                            0,
                            static_cast<uint8_t>((0x80 + iter) & 0xFF),
                            phase_payload(workload, iter, false, edge_factor, edge_var),
                            &pe_sent[var_pe],
                            &ingress_delta,
                            &cmd_delta,
                            &dut_tx_delta)) {
      delete dut;
      return 1;
    }
    row.variable_phase_remote_ingress = ingress_delta;
    row.variable_phase_cmd_accepted = cmd_delta;
    row.variable_phase_dut_tx = dut_tx_delta;
    var_to_factor_remote += ingress_delta;
    var_to_factor_dut_tx += dut_tx_delta;
    pe_received[fac_pe] += ingress_delta;
    pe_consumed[fac_pe] += cmd_delta;

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

  const uint32_t pre_stop_pe0_ingress = ingress_count_for_pe(dut, 0);
  const uint32_t pre_stop_pe1_ingress = ingress_count_for_pe(dut, 1);
  const uint32_t pre_stop_pe0_cmd = cmd_accept_count_for_pe(dut, 0);
  const uint32_t pre_stop_pe1_cmd = cmd_accept_count_for_pe(dut, 1);
  const uint32_t pre_stop_pe0_dut_tx = dut_tx_count_for_pe(dut, 0);
  const uint32_t pre_stop_pe1_dut_tx = dut_tx_count_for_pe(dut, 1);
  for (int i = 0; i < kPostStopCycles; ++i) {
    tick(dut, &touched_by_pe);
  }
  const uint32_t post_stop_pe0_ingress = ingress_count_for_pe(dut, 0);
  const uint32_t post_stop_pe1_ingress = ingress_count_for_pe(dut, 1);
  const uint32_t post_stop_pe0_cmd = cmd_accept_count_for_pe(dut, 0);
  const uint32_t post_stop_pe1_cmd = cmd_accept_count_for_pe(dut, 1);
  const uint32_t post_stop_pe0_dut_tx = dut_tx_count_for_pe(dut, 0);
  const uint32_t post_stop_pe1_dut_tx = dut_tx_count_for_pe(dut, 1);

  const bool post_stop_quiet = pre_stop_pe0_ingress == post_stop_pe0_ingress
      && pre_stop_pe1_ingress == post_stop_pe1_ingress && pre_stop_pe0_cmd == post_stop_pe0_cmd
      && pre_stop_pe1_cmd == post_stop_pe1_cmd && pre_stop_pe0_dut_tx == post_stop_pe0_dut_tx
      && pre_stop_pe1_dut_tx == post_stop_pe1_dut_tx;
  if (!post_stop_quiet) {
    std::fprintf(stderr,
                 "WARN: post-stop traffic detected pre=[%u,%u,%u,%u,%u,%u] post=[%u,%u,%u,%u,%u,%u]\n",
                 pre_stop_pe0_ingress,
                 pre_stop_pe1_ingress,
                 pre_stop_pe0_cmd,
                 pre_stop_pe1_cmd,
                 pre_stop_pe0_dut_tx,
                 pre_stop_pe1_dut_tx,
                 post_stop_pe0_ingress,
                 post_stop_pe1_ingress,
                 post_stop_pe0_cmd,
                 post_stop_pe1_cmd,
                 post_stop_pe0_dut_tx,
                 post_stop_pe1_dut_tx);
  }

  if (!dut->link_activity_o) {
    std::fprintf(stderr, "FAIL: no mesh link activity observed\n");
    delete dut;
    return 1;
  }

  AreEnergyFieldReport final_are_report{"final_are", 0.0, 0.0, 0.0, 0.0, false};
  AreEnergyFieldReport final_energy_report{"final_energy", 0.0, 0.0, 0.0, 0.0, false};
  const bool final_are_ok = check_are_energy_metric(workload,
                                                    final_are_report.field,
                                                    are_energy_expected.final_are,
                                                    are_energy_reference.final_are,
                                                    are_energy_threshold,
                                                    &final_are_report);
  const bool final_energy_ok = check_are_energy_metric(workload,
                                                       final_energy_report.field,
                                                       are_energy_expected.final_energy,
                                                       are_energy_reference.final_energy,
                                                       are_energy_threshold,
                                                       &final_energy_report);
  if (!(final_are_ok && final_energy_ok)) {
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

  TerminalDumpPe pe0_dump{};
  pe0_dump.state_b1_row0_word0 = static_cast<uint32_t>(dut->pe0_state_b1_row0_word0_o);
  pe0_dump.state_b2_row0_word0 = static_cast<uint32_t>(dut->pe0_state_b2_row0_word0_o);
  pe0_dump.state_b3_row0_word0 = static_cast<uint32_t>(dut->pe0_state_b3_row0_word0_o);
  pe0_dump.message_b4_row0_word0 = static_cast<uint32_t>(dut->pe0_message_b4_row0_word0_o);
  pe0_dump.message_b5_row0_word0 = static_cast<uint32_t>(dut->pe0_message_b5_row0_word0_o);
  pe0_dump.message_b6_row0_word0 = static_cast<uint32_t>(dut->pe0_message_b6_row0_word0_o);
  pe0_dump.message_b7_row0_word0 = static_cast<uint32_t>(dut->pe0_message_b7_row0_word0_o);
  pe0_dump.adapter_payload_plane0_row0 =
      static_cast<uint32_t>(dut->pe0_adapter_payload_plane0_row0_o);
  pe0_dump.adapter_credit_q0 = static_cast<uint32_t>(dut->pe0_adapter_credit_q0_o);
  pe0_dump.adapter_tail_q0 = static_cast<uint32_t>(dut->pe0_adapter_tail_q0_o);

  TerminalDumpPe pe1_dump{};
  pe1_dump.state_b1_row0_word0 = static_cast<uint32_t>(dut->pe1_state_b1_row0_word0_o);
  pe1_dump.state_b2_row0_word0 = static_cast<uint32_t>(dut->pe1_state_b2_row0_word0_o);
  pe1_dump.state_b3_row0_word0 = static_cast<uint32_t>(dut->pe1_state_b3_row0_word0_o);
  pe1_dump.message_b4_row0_word0 = static_cast<uint32_t>(dut->pe1_message_b4_row0_word0_o);
  pe1_dump.message_b5_row0_word0 = static_cast<uint32_t>(dut->pe1_message_b5_row0_word0_o);
  pe1_dump.message_b6_row0_word0 = static_cast<uint32_t>(dut->pe1_message_b6_row0_word0_o);
  pe1_dump.message_b7_row0_word0 = static_cast<uint32_t>(dut->pe1_message_b7_row0_word0_o);
  pe1_dump.adapter_payload_plane0_row0 =
      static_cast<uint32_t>(dut->pe1_adapter_payload_plane0_row0_o);
  pe1_dump.adapter_credit_q0 = static_cast<uint32_t>(dut->pe1_adapter_credit_q0_o);
  pe1_dump.adapter_tail_q0 = static_cast<uint32_t>(dut->pe1_adapter_tail_q0_o);

  const std::string dut_terminal_dump_path = std::string("build/integration/gbp_pe_mesh_2pe_convergence/")
      + workload + "_dut_terminal_dump.json";
  if (!write_dut_terminal_dump(
          dut_terminal_dump_path, workload, kFixedIters, pe0_dump, pe1_dump, touched_by_pe)) {
    delete dut;
    return 1;
  }

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
             << "  \"cross_edge\": {\n"
             << "    \"factor\": " << edge_factor << ",\n"
             << "    \"variable\": " << edge_var << ",\n"
             << "    \"factor_owner_pe\": " << fac_pe << ",\n"
             << "    \"variable_owner_pe\": " << var_pe << "\n"
             << "  },\n"
             << "  \"factor_to_var_remote\": " << factor_to_var_remote << ",\n"
             << "  \"var_to_factor_remote\": " << var_to_factor_remote << ",\n"
             << "  \"factor_to_var_dut_tx\": " << factor_to_var_dut_tx << ",\n"
             << "  \"var_to_factor_dut_tx\": " << var_to_factor_dut_tx << ",\n"
             << "  \"oracle_are_energy_compare\": {\n"
             << "    \"expected_path\": \"" << are_energy_expected_oracle_path << "\",\n"
             << "    \"reference_path\": \"" << are_energy_reference_oracle_path << "\",\n"
             << "    \"threshold_schema\": \"compare_contract.thresholds.are_energy\"\n"
             << "  },\n"
             << "  \"final_are_compare\": {\"expected\": " << final_are_report.expected
             << ", \"reference\": " << final_are_report.reference << ", \"abs_err\": "
             << final_are_report.abs_err << ", \"rel_err\": " << final_are_report.rel_err
             << ", \"abs_tol\": " << are_energy_threshold.abs_tol << ", \"rel_tol\": "
             << are_energy_threshold.rel_tol << ", \"status\": \""
             << (final_are_report.pass ? "PASS" : "FAIL") << "\"},\n"
             << "  \"final_energy_compare\": {\"expected\": " << final_energy_report.expected
             << ", \"reference\": " << final_energy_report.reference << ", \"abs_err\": "
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
      "gbp_pe_mesh_2pe_convergence: PASS workload=%s iterations=%d stop_reason=fixed_iters factor_to_var_remote=%u var_to_factor_remote=%u factor_to_var_dut_tx=%u var_to_factor_dut_tx=%u final_are_expected=%.9g final_are_reference=%.9g final_are_compare=%s final_energy_expected=%.9g final_energy_reference=%.9g final_energy_compare=%s convergence_json=%s\n",
      workload,
      kFixedIters,
      factor_to_var_remote,
      var_to_factor_remote,
      factor_to_var_dut_tx,
      var_to_factor_dut_tx,
      final_are_report.expected,
      final_are_report.reference,
      final_are_ok ? "PASS" : "FAIL",
      final_energy_report.expected,
      final_energy_report.reference,
      final_energy_ok ? "PASS" : "FAIL",
      result_path.c_str());

  delete dut;
  return 0;
}
