#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <regex>
#include <string>

#include "verilated.h"
#include "Vgbp_pe_single_pe_gbp.h"

static constexpr uint32_t kRowBytesLg = 5;
static constexpr uint32_t kMmioBankB0 = 0;
static constexpr uint32_t kPayloadBankB4 = 4;
static constexpr uint32_t kTailField = 3;
static constexpr uint32_t kDoorbellField = 5;
static constexpr int kFixedIters = 50;

struct Threshold {
  double abs_tol = 0.0;
  double rel_tol = 0.0;
};

struct StateMessageBaseline {
  double state_payload_word0 = 0.0;
  double message_word0 = 0.0;
};

struct StateMessageFieldReport {
  const char* field;
  double observed;
  double expected;
  double abs_err;
  double rel_err;
  bool pass;
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

struct RunStats {
  int cmds_issued = 0;
  int wr_ack_count = 0;
  int wr_any_count = 0;
  int decode_error_count = 0;
  int compute_start_pulses = 0;
  int compute_done_pulses = 0;
  uint32_t last_wr_txn_id = 0;
  uint32_t last_ingress_wr_addr = 0;
  uint32_t last_compute_wr_addr = 0;
};

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

static bool extract_threshold_from_oracle(const char* oracle_path, Threshold* out) {
  std::ifstream ifs(oracle_path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open oracle thresholds JSON: %s\n", oracle_path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  const size_t key = text.find("\"state_message\"");
  if (key == std::string::npos) {
    std::fprintf(stderr, "FAIL: oracle JSON missing thresholds.state_message section\n");
    return false;
  }
  const size_t start = text.find('{', key);
  if (start == std::string::npos) {
    std::fprintf(stderr, "FAIL: malformed thresholds.state_message section\n");
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
    std::fprintf(stderr, "FAIL: unterminated thresholds.state_message section\n");
    return false;
  }

  const std::string section = text.substr(start, end - start + 1);
  if (!extract_double(section, "abs_err", &out->abs_tol) ||
      !extract_double(section, "rel_err", &out->rel_tol)) {
    std::fprintf(stderr, "FAIL: unable to parse state_message abs_err/rel_err\n");
    return false;
  }
  return true;
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

static bool extract_workload_metric_with_alias(const std::string& section,
                                               const char* primary,
                                               const char* alias,
                                               double* out) {
  return extract_double(section, primary, out) || extract_double(section, alias, out);
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
      "GBP_PE_SINGLE_PE_GBP_ARE_ENERGY_MARKER workload=%s field=%s expected=%.9g reference=%.9g abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g status=%s\n",
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

static bool extract_workload_baseline(const std::string& text,
                                      const char* workload,
                                      StateMessageBaseline* out) {
  const std::string workload_key = std::string("\"") + workload + "\"";
  const size_t key = text.find(workload_key);
  if (key == std::string::npos) {
    std::fprintf(stderr, "FAIL: baseline JSON missing workload=%s\n", workload);
    return false;
  }
  const size_t start = text.find('{', key);
  if (start == std::string::npos) {
    std::fprintf(stderr, "FAIL: malformed baseline section workload=%s\n", workload);
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
    std::fprintf(stderr, "FAIL: unterminated baseline section workload=%s\n", workload);
    return false;
  }

  const std::string section = text.substr(start, end - start + 1);
  if (!extract_double(section, "state_payload_word0", &out->state_payload_word0) ||
      !extract_double(section, "message_word0", &out->message_word0)) {
    std::fprintf(stderr,
                 "FAIL: baseline missing state_payload_word0/message_word0 workload=%s\n",
                 workload);
    return false;
  }
  return true;
}

static bool load_state_message_baseline(const char* baseline_path,
                                        const char* workload,
                                        StateMessageBaseline* out) {
  std::ifstream ifs(baseline_path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open state/message baseline JSON: %s\n", baseline_path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  return extract_workload_baseline(text, workload, out);
}

static bool check_state_message_metric(const char* workload,
                                       const char* field,
                                       double observed,
                                       double expected,
                                       const Threshold& threshold,
                                       StateMessageFieldReport* report) {
  if (!std::isfinite(observed) || !std::isfinite(expected)) {
    std::fprintf(stderr,
                 "FAIL: state/message mismatch workload=%s field=%s observed=%.9g expected=%.9g abs_err=nan rel_err=nan abs_tol=%.9g rel_tol=%.9g reason=non_finite\n",
                 workload,
                 field,
                 observed,
                 expected,
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
    report->observed = observed;
    report->expected = expected;
    report->abs_err = abs_err;
    report->rel_err = rel_err;
    report->pass = pass;
  }

  std::printf(
      "GBP_PE_SINGLE_PE_GBP_STATE_MESSAGE_MARKER workload=%s field=%s observed=%.9g expected=%.9g abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g status=%s\n",
      workload,
      field,
      observed,
      expected,
      abs_err,
      rel_err,
      threshold.abs_tol,
      threshold.rel_tol,
      pass ? "PASS" : "FAIL");

  if (pass) {
    return true;
  }
  std::fprintf(stderr,
               "FAIL: state/message mismatch workload=%s field=%s observed=%.9g expected=%.9g abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g\n",
               workload,
               field,
               observed,
               expected,
               abs_err,
               rel_err,
               threshold.abs_tol,
               threshold.rel_tol);
  return false;
}

static bool write_state_message_report(const std::string& report_path,
                                       const char* workload,
                                       const Threshold& threshold,
                                       const StateMessageFieldReport& state_field,
                                       const StateMessageFieldReport& message_field,
                                       bool status) {
  std::ofstream ofs(report_path);
  if (!ofs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to write state/message report path=%s\n", report_path.c_str());
    return false;
  }

  ofs << "{\n"
      << "  \"metadata\": {\n"
      << "    \"comparison\": \"abs_or_rel\",\n"
      << "    \"pass_rule\": \"abs_err<=abs_tol||rel_err<=rel_tol\",\n"
      << "    \"thresholds\": {\n"
      << "      \"state_message\": {\n"
      << "        \"abs_err\": " << threshold.abs_tol << ",\n"
      << "        \"rel_err\": " << threshold.rel_tol << "\n"
      << "      }\n"
      << "    }\n"
      << "  },\n"
      << "  \"test\": \"gbp_pe_single_pe_gbp\",\n"
      << "  \"workload\": \"" << workload << "\",\n"
      << "  \"state_message\": {\n"
      << "    \"status\": \"" << (status ? "PASS" : "FAIL") << "\",\n"
      << "    \"fields\": {\n"
      << "      \"" << state_field.field << "\": {\n"
      << "        \"observed\": " << state_field.observed << ",\n"
      << "        \"expected\": " << state_field.expected << ",\n"
      << "        \"abs_err\": " << state_field.abs_err << ",\n"
      << "        \"rel_err\": " << state_field.rel_err << ",\n"
      << "        \"status\": \"" << (state_field.pass ? "PASS" : "FAIL") << "\"\n"
      << "      },\n"
      << "      \"" << message_field.field << "\": {\n"
      << "        \"observed\": " << message_field.observed << ",\n"
      << "        \"expected\": " << message_field.expected << ",\n"
      << "        \"abs_err\": " << message_field.abs_err << ",\n"
      << "        \"rel_err\": " << message_field.rel_err << ",\n"
      << "        \"status\": \"" << (message_field.pass ? "PASS" : "FAIL") << "\"\n"
      << "      }\n"
      << "    }\n"
      << "  }\n"
      << "}\n";
  return true;
}

static void tick(Vgbp_pe_single_pe_gbp* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe_single_pe_gbp* dut) {
  dut->rst_n = 0;
  dut->send_v = 0;
  dut->send_we = 0;
  dut->send_addr = 0;
  dut->send_data = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
}

static bool issue_req(Vgbp_pe_single_pe_gbp* dut,
                      bool we,
                      uint32_t addr,
                      uint32_t data,
                      int max_cycles) {
  dut->send_we = we ? 1 : 0;
  dut->send_addr = addr;
  dut->send_data = data;
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    dut->send_v = 1;
    tick(dut);
    if (dut->send_ready) {
      dut->send_v = 0;
      return true;
    }
  }
  dut->send_v = 0;
  return false;
}

static uint32_t payload_for_iter(const char* workload, int iter) {
  const uint32_t line_seed = 0x11000000u;
  const uint32_t lattice_seed = 0x22000000u;
  const uint32_t base = (std::string(workload) == "synthetic_line") ? line_seed : lattice_seed;
  return base | (static_cast<uint32_t>(iter) & 0xFFFFu);
}

static bool wait_for_writeback(Vgbp_pe_single_pe_gbp* dut,
                               int max_cycles,
                               RunStats* stats) {
  const int wr_any_count_start = stats->wr_any_count;
  bool prev_compute_start = false;
  bool prev_compute_done = false;
  bool prev_wr_valid = false;

  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    tick(dut);

    const bool compute_start = dut->compute_start_o;
    const bool compute_done = dut->compute_done_o;
    const bool wr_valid = dut->wr_req_valid_o;
    const bool compute_start_pulse = compute_start && !prev_compute_start;
    const bool compute_done_pulse = compute_done && !prev_compute_done;
    const bool wr_pulse = wr_valid && !prev_wr_valid;

    prev_compute_start = compute_start;
    prev_compute_done = compute_done;
    prev_wr_valid = wr_valid;

    if (compute_start_pulse) {
      stats->compute_start_pulses++;
    }
    if (compute_done_pulse) {
      stats->compute_done_pulses++;
    }
    if (dut->decode_error_o) {
      stats->decode_error_count++;
    }
    stats->last_ingress_wr_addr = static_cast<uint32_t>(dut->last_ingress_wr_addr_o);
    stats->last_compute_wr_addr = static_cast<uint32_t>(dut->last_compute_wr_addr_o);
    if (wr_pulse) {
      stats->wr_any_count++;
      stats->last_wr_txn_id = static_cast<uint32_t>(dut->wr_txn_id_o);
      if (stats->wr_any_count > wr_any_count_start) {
        stats->wr_ack_count++;
        return true;
      }
    }
  }

  return false;
}

static bool run_one_iteration(Vgbp_pe_single_pe_gbp* dut,
                              const char* workload,
                              int iter,
                              RunStats* stats) {
  const uint32_t qid = 1u;
  const uint32_t cmd_kind = 0u;
  const uint32_t txn_id = static_cast<uint32_t>((0x40 + iter) & 0xFF);
  const uint32_t payload = payload_for_iter(workload, iter);
  const uint32_t payload_addr = (kPayloadBankB4 << kRowBytesLg) + (qid << 8);
  const uint32_t mmio_tail_addr = (kMmioBankB0 << kRowBytesLg) + (qid << 8) + (kTailField << 2);
  const uint32_t mmio_doorbell_addr =
      (kMmioBankB0 << kRowBytesLg) + (qid << 8) + (kDoorbellField << 2);
  const uint32_t mmio_tail_data = 0x1u;
  const uint32_t mmio_doorbell_data = ((txn_id & 0xFFu) << 8) | ((cmd_kind & 0x3u) << 1) | 0x1u;

  if (!issue_req(dut, true, payload_addr, payload, 128)) {
    std::fprintf(stderr,
                 "FAIL: payload write not accepted iter=%d txn=0x%02x addr=0x%05x\n",
                 iter,
                 txn_id,
                 payload_addr);
    return false;
  }
  if (!issue_req(dut, true, mmio_tail_addr, mmio_tail_data, 128)) {
    std::fprintf(stderr,
                 "FAIL: tail write not accepted iter=%d txn=0x%02x addr=0x%05x\n",
                 iter,
                 txn_id,
                 mmio_tail_addr);
    return false;
  }
  if (!issue_req(dut, true, mmio_doorbell_addr, mmio_doorbell_data, 128)) {
    std::fprintf(stderr,
                 "FAIL: doorbell write not accepted iter=%d txn=0x%02x addr=0x%05x\n",
                 iter,
                 txn_id,
                 mmio_doorbell_addr);
    return false;
  }

  stats->cmds_issued++;
  if (!wait_for_writeback(dut, 1024, stats)) {
    std::fprintf(stderr,
                 "FAIL: missing DUT writeback iter=%d expected_txn=0x%02x wr_any=%d wr_ack=%d\n",
                 iter,
                 txn_id,
                 stats->wr_any_count,
                 stats->wr_ack_count);
    return false;
  }
  return true;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_pe_single_pe_gbp;

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

  reset_dut(dut);

  RunStats stats{};
  for (int iter = 0; iter < kFixedIters; ++iter) {
    if (!run_one_iteration(dut, workload, iter, &stats)) {
      delete dut;
      return 1;
    }
  }

  const int iters = stats.cmds_issued;
  const char* stop_reason = "fixed_iters";
  if (iters != kFixedIters) {
    std::fprintf(stderr,
                 "FAIL: deterministic iteration controller mismatch iters=%d expected=%d\n",
                 iters,
                 kFixedIters);
    delete dut;
    return 1;
  }
  if (stats.wr_ack_count != kFixedIters) {
    std::fprintf(stderr,
                 "FAIL: DUT writeback mismatch wr_ack=%d expected=%d\n",
                 stats.wr_ack_count,
                 kFixedIters);
    delete dut;
    return 1;
  }

  const char* oracle_threshold_path = std::getenv("GBP_ORACLE_PHASE1_PATH");
  if (oracle_threshold_path == nullptr || oracle_threshold_path[0] == '\0') {
    oracle_threshold_path = "tests/oracle/generated/gbp_oracle_phase1.json";
  }
  Threshold threshold{};
  if (!extract_threshold_from_oracle(oracle_threshold_path, &threshold)) {
    delete dut;
    return 1;
  }

  const char* baseline_path = std::getenv("GBP_STATE_MESSAGE_BASELINE_PATH");
  if (baseline_path == nullptr || baseline_path[0] == '\0') {
    baseline_path = "tests/oracle/gbp_pe_single_pe_gbp_state_message_baseline.json";
  }
  StateMessageBaseline baseline{};
  if (!load_state_message_baseline(baseline_path, workload, &baseline)) {
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

  StateMessageFieldReport state_field{"state_payload_word0", 0.0, 0.0, 0.0, 0.0, false};
  StateMessageFieldReport message_field{"message_word0", 0.0, 0.0, 0.0, 0.0, false};
  std::printf(
      "GBP_PE_SINGLE_PE_GBP_STORAGE_MARKER workload=%s ingress_addr=0x%05x compute_addr=0x%05x state_word=0x%08x message_word=0x%08x\n",
      workload,
      stats.last_ingress_wr_addr,
      stats.last_compute_wr_addr,
      static_cast<uint32_t>(dut->state_mem_word0_o),
      static_cast<uint32_t>(dut->message_mem_word0_o));

  const bool state_ok = check_state_message_metric(workload,
                                                   state_field.field,
                                                   static_cast<double>(dut->state_mem_word0_o),
                                                   baseline.state_payload_word0,
                                                   threshold,
                                                   &state_field);
  const bool message_ok = check_state_message_metric(workload,
                                                      message_field.field,
                                                      static_cast<double>(dut->message_mem_word0_o),
                                                      baseline.message_word0,
                                                      threshold,
                                                      &message_field);
  if (!(state_ok && message_ok)) {
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

  const std::string state_message_report_path =
      std::string("build/integration/gbp_pe_single_pe_gbp/") + workload + "_state_message_report.json";
  if (!write_state_message_report(state_message_report_path,
                                  workload,
                                  threshold,
                                  state_field,
                                  message_field,
                                  state_ok && message_ok)) {
    delete dut;
    return 1;
  }

  const std::string metrics_path =
      std::string("build/integration/gbp_pe_single_pe_gbp/") + workload + "_metrics.json";
  std::ofstream ofs(metrics_path);
  if (!ofs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to write metrics output: %s\n", metrics_path.c_str());
    delete dut;
    return 1;
  }

  ofs << "{\n"
      << "  \"test\": \"gbp_pe_single_pe_gbp\",\n"
      << "  \"workload\": \"" << workload << "\",\n"
      << "  \"iters\": " << iters << ",\n"
      << "  \"stop_reason\": \"" << stop_reason << "\",\n"
      << "  \"dut_last_ingress_wr_addr\": " << stats.last_ingress_wr_addr << ",\n"
      << "  \"dut_last_compute_wr_addr\": " << stats.last_compute_wr_addr << ",\n"
      << "  \"dut_state_payload_word0\": " << static_cast<uint32_t>(dut->state_mem_word0_o) << ",\n"
      << "  \"dut_message_word0\": " << static_cast<uint32_t>(dut->message_mem_word0_o) << ",\n"
      << "  \"state_message_report\": \"" << state_message_report_path << "\",\n"
      << "  \"dut_cmds_issued\": " << stats.cmds_issued << ",\n"
      << "  \"dut_wr_ack_count\": " << stats.wr_ack_count << ",\n"
      << "  \"dut_wr_any_count\": " << stats.wr_any_count << ",\n"
      << "  \"dut_decode_error_count\": " << stats.decode_error_count << ",\n"
      << "  \"dut_compute_start_pulses\": " << stats.compute_start_pulses << ",\n"
      << "  \"dut_compute_done_pulses\": " << stats.compute_done_pulses << ",\n"
      << "  \"dut_last_wr_txn_id\": " << stats.last_wr_txn_id << ",\n"
      << "  \"oracle_are_energy_compare\": {\n"
      << "    \"expected_path\": \"" << are_energy_expected_oracle_path << "\",\n"
      << "    \"reference_path\": \"" << are_energy_reference_oracle_path << "\",\n"
      << "    \"threshold_schema\": \"compare_contract.thresholds.are_energy\"\n"
      << "  },\n"
      << "  \"final_are_compare\": {\n"
      << "    \"expected\": " << final_are_report.expected << ",\n"
      << "    \"reference\": " << final_are_report.reference << ",\n"
      << "    \"abs_err\": " << final_are_report.abs_err << ",\n"
      << "    \"rel_err\": " << final_are_report.rel_err << ",\n"
      << "    \"abs_tol\": " << are_energy_threshold.abs_tol << ",\n"
      << "    \"rel_tol\": " << are_energy_threshold.rel_tol << ",\n"
      << "    \"status\": \"" << (final_are_report.pass ? "PASS" : "FAIL") << "\"\n"
      << "  },\n"
      << "  \"final_energy_compare\": {\n"
      << "    \"expected\": " << final_energy_report.expected << ",\n"
      << "    \"reference\": " << final_energy_report.reference << ",\n"
      << "    \"abs_err\": " << final_energy_report.abs_err << ",\n"
      << "    \"rel_err\": " << final_energy_report.rel_err << ",\n"
      << "    \"abs_tol\": " << are_energy_threshold.abs_tol << ",\n"
      << "    \"rel_tol\": " << are_energy_threshold.rel_tol << ",\n"
      << "    \"status\": \"" << (final_energy_report.pass ? "PASS" : "FAIL") << "\"\n"
      << "  }\n"
      << "}\n";

  std::printf(
      "gbp_pe_single_pe_gbp: PASS workload=%s iters=%d stop_reason=%s dut_wr_ack_count=%d state_payload_word0=0x%08x message_word0=0x%08x final_are_expected=%.9g final_are_reference=%.9g final_are_compare=%s final_energy_expected=%.9g final_energy_reference=%.9g final_energy_compare=%s metrics_json=%s\n",
      workload,
      iters,
      stop_reason,
      stats.wr_ack_count,
      static_cast<uint32_t>(dut->state_mem_word0_o),
      static_cast<uint32_t>(dut->message_mem_word0_o),
      final_are_report.expected,
      final_are_report.reference,
      final_are_ok ? "PASS" : "FAIL",
      final_energy_report.expected,
      final_energy_report.reference,
      final_energy_ok ? "PASS" : "FAIL",
      metrics_path.c_str());

  delete dut;
  return 0;
}
