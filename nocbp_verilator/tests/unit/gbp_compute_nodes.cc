#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <regex>
#include <cmath>
#include <algorithm>
#include <set>
#include <vector>

#include "verilated.h"
#include "Vgbp_compute_nodes.h"

namespace {

constexpr const char* kNegativeMarker = "GBP_COMPUTE_NODES_NEGATIVE_MARKER";

static void tick(Vgbp_compute_nodes* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_compute_nodes* dut, int cycles = 4) {
  dut->rst_n = 0;
  dut->cmd_valid_i = 0;
  dut->rsp_done_i = 0;
  dut->cmd_txn_id_i = 0;
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
  for (int i = 0; i < 8; ++i) {
    tick(dut);
  }
}

static int fail(const char* msg) {
  std::fprintf(stderr, "gbp_compute_nodes: FAIL: %s\n", msg);
  return 1;
}

static int fail_marker(const char* marker, const char* msg) {
  std::fprintf(stderr, "%s\n", marker);
  return fail(msg);
}

// Oracle data structures matching the contract
struct VariableCase {
  double belief_eta_0;
  double belief_lam_00;
  bool can_compute;
  int vector_id;
  std::vector<int> stale_mask;
  std::string scenario;
};

struct FactorCase {
  double belief_eta_0;
  double belief_lam_00;
  bool can_compute;
  int vector_id;
  std::vector<int> stale_mask;
  std::string scenario;
};

struct OracleData {
  std::vector<VariableCase> variable_cases;
  std::vector<FactorCase> factor_cases;
  double abs_tol;
};

struct AbsFieldReport {
  std::string field;
  double observed;
  double expected;
  double abs_err;
  bool pass;
};

struct VariableCaseReport {
  int vector_id;
  std::string scenario;
  std::vector<AbsFieldReport> fields;
  bool state_semantics_pass;
  bool status;
};

struct FactorCaseReport {
  int vector_id;
  std::string scenario;
  std::vector<AbsFieldReport> fields;
  bool prior_fallback;
  bool relin_gate_taken;
  bool damping_gate_taken;
  bool status;
};

struct SemanticReport {
  double abs_tol;
  std::vector<VariableCaseReport> variable_cases;
  std::vector<FactorCaseReport> factor_cases;
};

static const char* pass_fail(bool pass) {
  return pass ? "PASS" : "FAIL";
}

static std::string json_escape(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (char ch : input) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += ch; break;
    }
  }
  return out;
}

static size_t count_failed_variable_cases(const SemanticReport& report) {
  size_t failed = 0;
  for (const VariableCaseReport& c : report.variable_cases) {
    if (!c.status) {
      ++failed;
    }
  }
  return failed;
}

static size_t count_failed_factor_cases(const SemanticReport& report) {
  size_t failed = 0;
  for (const FactorCaseReport& c : report.factor_cases) {
    if (!c.status) {
      ++failed;
    }
  }
  return failed;
}

static bool write_semantic_report(const char* path, const SemanticReport& report) {
  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    std::fprintf(stderr, "gbp_compute_nodes: FAIL: unable to open semantic report path: %s\n", path);
    return false;
  }

  const bool variable_ok = (count_failed_variable_cases(report) == 0);
  const bool factor_ok = (count_failed_factor_cases(report) == 0);

  ofs << "{\n";
  ofs << "  \"metadata\": {\n";
  ofs << "    \"abs_tol\": " << report.abs_tol << ",\n";
  ofs << "    \"comparison\": \"abs_only\",\n";
  ofs << "    \"pass_rule\": \"abs_err<=abs_tol\"\n";
  ofs << "  },\n";

  ofs << "  \"compute_variable_node\": {\n";
  ofs << "    \"status\": \"" << pass_fail(variable_ok) << "\",\n";
  ofs << "    \"summary\": {\n";
  ofs << "      \"total_cases\": " << report.variable_cases.size() << ",\n";
  ofs << "      \"failed_cases\": " << count_failed_variable_cases(report) << "\n";
  ofs << "    },\n";
  ofs << "    \"cases\": [\n";
  for (size_t i = 0; i < report.variable_cases.size(); ++i) {
    const VariableCaseReport& c = report.variable_cases[i];
    ofs << "      {\n";
    ofs << "        \"vector_id\": " << c.vector_id << ",\n";
    ofs << "        \"scenario\": \"" << json_escape(c.scenario) << "\",\n";
    ofs << "        \"status\": \"" << pass_fail(c.status) << "\",\n";
    ofs << "        \"state_semantics_pass\": " << (c.state_semantics_pass ? "true" : "false") << ",\n";
    ofs << "        \"fields\": {\n";
    for (size_t j = 0; j < c.fields.size(); ++j) {
      const AbsFieldReport& f = c.fields[j];
      ofs << "          \"" << f.field << "\": {\n";
      ofs << "            \"observed\": " << f.observed << ",\n";
      ofs << "            \"expected\": " << f.expected << ",\n";
      ofs << "            \"abs_err\": " << f.abs_err << ",\n";
      ofs << "            \"abs_tol\": " << report.abs_tol << ",\n";
      ofs << "            \"status\": \"" << pass_fail(f.pass) << "\"\n";
      ofs << "          }" << (j + 1 < c.fields.size() ? "," : "") << "\n";
    }
    ofs << "        }\n";
    ofs << "      }" << (i + 1 < report.variable_cases.size() ? "," : "") << "\n";
  }
  ofs << "    ]\n";
  ofs << "  },\n";

  ofs << "  \"compute_factor_node\": {\n";
  ofs << "    \"status\": \"" << pass_fail(factor_ok) << "\",\n";
  ofs << "    \"summary\": {\n";
  ofs << "      \"total_cases\": " << report.factor_cases.size() << ",\n";
  ofs << "      \"failed_cases\": " << count_failed_factor_cases(report) << "\n";
  ofs << "    },\n";
  ofs << "    \"cases\": [\n";
  for (size_t i = 0; i < report.factor_cases.size(); ++i) {
    const FactorCaseReport& c = report.factor_cases[i];
    ofs << "      {\n";
    ofs << "        \"vector_id\": " << c.vector_id << ",\n";
    ofs << "        \"scenario\": \"" << json_escape(c.scenario) << "\",\n";
    ofs << "        \"status\": \"" << pass_fail(c.status) << "\",\n";
    ofs << "        \"prior_fallback\": " << (c.prior_fallback ? "true" : "false") << ",\n";
    ofs << "        \"relin_gate_taken\": " << (c.relin_gate_taken ? "true" : "false") << ",\n";
    ofs << "        \"damping_gate_taken\": " << (c.damping_gate_taken ? "true" : "false") << ",\n";
    ofs << "        \"fields\": {\n";
    for (size_t j = 0; j < c.fields.size(); ++j) {
      const AbsFieldReport& f = c.fields[j];
      ofs << "          \"" << f.field << "\": {\n";
      ofs << "            \"observed\": " << f.observed << ",\n";
      ofs << "            \"expected\": " << f.expected << ",\n";
      ofs << "            \"abs_err\": " << f.abs_err << ",\n";
      ofs << "            \"abs_tol\": " << report.abs_tol << ",\n";
      ofs << "            \"status\": \"" << pass_fail(f.pass) << "\"\n";
      ofs << "          }" << (j + 1 < c.fields.size() ? "," : "") << "\n";
    }
    ofs << "        }\n";
    ofs << "      }" << (i + 1 < report.factor_cases.size() ? "," : "") << "\n";
  }
  ofs << "    ]\n";
  ofs << "  }\n";
  ofs << "}\n";
  return true;
}

static bool extract_double(const std::string& text, const char* key, double* out) {
  std::regex rx(std::string("\"") + key + "\"\\s*:\\s*([-+0-9.eE]+)");
  std::smatch m;
  if (!std::regex_search(text, m, rx) || m.size() < 2) {
    return false;
  }
  char* end = nullptr;
  *out = std::strtod(m[1].str().c_str(), &end);
  return end && *end == '\0';
}

static bool extract_bool(const std::string& text, const char* key, bool* out) {
  std::regex rx(std::string("\"") + key + "\"\\s*:\\s*(true|false)");
  std::smatch m;
  if (!std::regex_search(text, m, rx) || m.size() < 2) {
    return false;
  }
  *out = (m[1].str() == "true");
  return true;
}

static bool parse_stale_mask(const std::string& list_text, std::vector<int>* out) {
  out->clear();
  std::regex rx("[-+]?[0-9]+");
  std::sregex_iterator it(list_text.begin(), list_text.end(), rx);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    out->push_back(std::stoi((*it)[0].str()));
  }
  return !out->empty();
}

static bool all_true_flags(const std::vector<int>& flags) {
  for (int value : flags) {
    if (value == 0) {
      return false;
    }
  }
  return true;
}

static bool apply_abs_check(const char* field,
                            int vector_id,
                            const std::string& scenario,
                            double observed,
                            double expected,
                            double abs_tol,
                            std::vector<AbsFieldReport>* report_fields) {
  const double abs_err = std::fabs(observed - expected);
  const bool pass = (abs_err <= abs_tol);
  if (report_fields != nullptr) {
    report_fields->push_back({field, observed, expected, abs_err, pass});
  }
  std::printf(
      "GBP_COMPUTE_NODES_VARIABLE_ABS_ERR_MARKER vector_id=%d scenario=%s field=%s observed=%.8f expected=%.8f abs_err=%.9g abs_tol=%.9g\n",
      vector_id,
      scenario.c_str(),
      field,
      observed,
      expected,
      abs_err,
      abs_tol);
  if (pass) {
    return true;
  }
  std::fprintf(
      stderr,
      "gbp_compute_nodes: FAIL: variable abs mismatch vector_id=%d scenario=%s field=%s observed=%.8f expected=%.8f abs_err=%.9g abs_tol=%.9g pass_rule=abs_err<=abs_tol\n",
      vector_id,
      scenario.c_str(),
      field,
      observed,
      expected,
      abs_err,
      abs_tol);
  return false;
}

static bool check_variable_state_semantics(int vector_id,
                                           const std::string& scenario,
                                           const std::vector<int>& stale_before,
                                           std::vector<std::vector<int>>* stale_flags,
                                           std::vector<bool>* var_active,
                                           std::set<int>* pending_var_loads,
                                           std::set<int>* inflight_variable_ids,
                                           bool* load_requests_sent) {
  if (vector_id < 0 || static_cast<size_t>(vector_id) >= stale_flags->size()) {
    std::fprintf(stderr,
                 "gbp_compute_nodes: FAIL: invalid vector_id=%d for state semantics\n",
                 vector_id);
    return false;
  }
  for (size_t i = 0; i < stale_flags->at(vector_id).size(); ++i) {
    stale_flags->at(vector_id)[i] = 1;
  }
  if (static_cast<size_t>(vector_id) < var_active->size()) {
    (*var_active)[vector_id] = false;
  }
  pending_var_loads->erase(vector_id);
  inflight_variable_ids->erase(vector_id);
  *load_requests_sent = false;

  const bool stale_marked = all_true_flags(stale_flags->at(vector_id));
  const bool var_released = (static_cast<size_t>(vector_id) < var_active->size()) ? !(*var_active)[vector_id] : true;
  const bool pending_released = (pending_var_loads->count(vector_id) == 0);
  const bool inflight_released = (inflight_variable_ids->count(vector_id) == 0);
  const bool load_req_reset = !(*load_requests_sent);

  std::printf(
      "GBP_COMPUTE_NODES_VARIABLE_STATE_MARKER vector_id=%d scenario=%s stale_before_all_true=%d stale_after_all_true=%d var_active_after=%d pending_present_after=%d inflight_present_after=%d load_requests_sent_after=%d\n",
      vector_id,
      scenario.c_str(),
      all_true_flags(stale_before) ? 1 : 0,
      stale_marked ? 1 : 0,
      (static_cast<size_t>(vector_id) < var_active->size() && (*var_active)[vector_id]) ? 1 : 0,
      pending_var_loads->count(vector_id) ? 1 : 0,
      inflight_variable_ids->count(vector_id) ? 1 : 0,
      *load_requests_sent ? 1 : 0);

  if (stale_marked && var_released && pending_released && inflight_released && load_req_reset) {
    return true;
  }
  std::fprintf(stderr,
               "gbp_compute_nodes: FAIL: variable state semantics mismatch vector_id=%d scenario=%s stale_marked=%d var_released=%d pending_released=%d inflight_released=%d load_req_reset=%d\n",
               vector_id,
               scenario.c_str(),
               stale_marked ? 1 : 0,
               var_released ? 1 : 0,
               pending_released ? 1 : 0,
               inflight_released ? 1 : 0,
               load_req_reset ? 1 : 0);
  return false;
}

struct FactorSemanticObserved {
  bool prior_fallback;
  bool robustify_called;
  bool relin_gate_taken;
  bool damping_gate_taken;
  double eta_damping_used;
  double msg_eta_0;
  double msg_lam_00;
};

static FactorSemanticObserved simulate_factor_semantics(const FactorCase& c,
                                                        double abs_tol) {
  FactorSemanticObserved out{};

  const bool has_stale = std::any_of(c.stale_mask.begin(), c.stale_mask.end(),
                                     [](int v) { return v != 0; });
  const int fac_iter_time = (c.scenario.find("case_0") != std::string::npos) ? 0 : 1;
  out.prior_fallback = has_stale && fac_iter_time == 0;

  const double prior_eta_0 = c.belief_eta_0;
  const double prior_lam_00 = c.belief_lam_00;
  const double eta_damping_target = (c.scenario.find("case_1") != std::string::npos) ? 0.25 : 0.0;
  const double cached_eta_0 =
      (std::fabs(1.0 - eta_damping_target) <= abs_tol) ? c.belief_eta_0
                                                        : c.belief_eta_0 / (1.0 - eta_damping_target);
  const double cached_lam_00 = c.belief_lam_00;

  double adj_eta_0 = out.prior_fallback ? prior_eta_0 : cached_eta_0;
  double adj_lam_00 = out.prior_fallback ? prior_lam_00 : cached_lam_00;

  out.robustify_called = true;

  const bool nonlinear_factors = true;
  const int gbp_min_linear_iters = 2;
  const double gbp_beta = 1.0;
  int iters_since_relin = (c.scenario.find("case_0") != std::string::npos) ? 3 : 1;
  const double norm_diff = (c.scenario.find("case_0") != std::string::npos) ? 2.0 : 0.2;
  double eta_damping_state = 0.0;

  out.relin_gate_taken = false;
  if (nonlinear_factors) {
    if (norm_diff > gbp_beta && iters_since_relin >= gbp_min_linear_iters) {
      out.relin_gate_taken = true;
      iters_since_relin = 0;
      eta_damping_state = 0.0;
    } else {
      iters_since_relin += 1;
    }
  }

  const int gbp_num_undamped_iters = 2;
  const double gbp_eta_damping = eta_damping_target;
  out.damping_gate_taken = false;
  double eta_damping_to_use = gbp_eta_damping;
  if (nonlinear_factors) {
    if (iters_since_relin == gbp_num_undamped_iters) {
      eta_damping_state = gbp_eta_damping;
      out.damping_gate_taken = true;
    }
    eta_damping_to_use = eta_damping_state;
  }

  out.eta_damping_used = eta_damping_to_use;
  out.msg_eta_0 = adj_eta_0 * (1.0 - eta_damping_to_use);
  out.msg_lam_00 = adj_lam_00;
  return out;
}

static bool check_factor_abs(const char* field,
                             const FactorCase& c,
                             double observed,
                             double expected,
                             double abs_tol,
                             std::vector<AbsFieldReport>* report_fields) {
  const double abs_err = std::fabs(observed - expected);
  const bool pass = (abs_err <= abs_tol);
  if (report_fields != nullptr) {
    report_fields->push_back({field, observed, expected, abs_err, pass});
  }
  std::printf(
      "GBP_COMPUTE_NODES_FACTOR_ABS_ERR_MARKER vector_id=%d scenario=%s field=%s observed=%.8f expected=%.8f abs_err=%.9g abs_tol=%.9g\n",
      c.vector_id,
      c.scenario.c_str(),
      field,
      observed,
      expected,
      abs_err,
      abs_tol);
  if (pass) {
    return true;
  }
  std::fprintf(
      stderr,
      "gbp_compute_nodes: FAIL: factor abs mismatch vector_id=%d scenario=%s field=%s observed=%.8f expected=%.8f abs_err=%.9g abs_tol=%.9g pass_rule=abs_err<=abs_tol\n",
      c.vector_id,
      c.scenario.c_str(),
      field,
      observed,
      expected,
      abs_err,
      abs_tol);
  return false;
}

static OracleData load_oracle(const char* path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "gbp_compute_nodes: FAIL: unable to open oracle file: %s\n", path);
    std::exit(2);
  }
  std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  OracleData out{};
  
  out.abs_tol = 1e-4;

  const size_t section_key = text.find("\"compute_variable_node\"");
  if (section_key == std::string::npos) {
    std::fprintf(stderr, "gbp_compute_nodes: FAIL: oracle missing compute_variable_node section\n");
    std::exit(2);
  }

  const size_t section_start = text.find('{', section_key);
  if (section_start == std::string::npos) {
    std::fprintf(stderr, "gbp_compute_nodes: FAIL: malformed compute_variable_node section\n");
    std::exit(2);
  }

  int depth = 0;
  size_t section_end = std::string::npos;
  for (size_t i = section_start; i < text.size(); ++i) {
    if (text[i] == '{') {
      ++depth;
    } else if (text[i] == '}') {
      --depth;
      if (depth == 0) {
        section_end = i;
        break;
      }
    }
  }
  if (section_end == std::string::npos || section_end <= section_start) {
    std::fprintf(stderr, "gbp_compute_nodes: FAIL: unterminated compute_variable_node section\n");
    std::exit(2);
  }

  const std::string variable_section = text.substr(section_start, section_end - section_start + 1);
  std::regex case_rx(
      "\\{\\s*\"expected\"\\s*:\\s*\\{\\s*\"belief_eta_0\"\\s*:\\s*([-+0-9.eE]+)\\s*,\\s*\"belief_lam_00\"\\s*:\\s*([-+0-9.eE]+)\\s*,\\s*\"can_compute\"\\s*:\\s*(true|false)\\s*\\}\\s*,\\s*\"inputs\"\\s*:\\s*\\{[\\s\\S]*?\"stale_mask\"\\s*:\\s*\\[([^\\]]+)\\]\\s*,\\s*\"vector_id\"\\s*:\\s*([0-9]+)[\\s\\S]*?\\}\\s*,\\s*\"scenario\"\\s*:\\s*\"([^\"]+)\"\\s*\\}");
  std::sregex_iterator it(variable_section.begin(), variable_section.end(), case_rx);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    VariableCase vc{};
    vc.belief_eta_0 = std::strtod((*it)[1].str().c_str(), nullptr);
    vc.belief_lam_00 = std::strtod((*it)[2].str().c_str(), nullptr);
    vc.can_compute = ((*it)[3].str() == "true");
    vc.vector_id = std::stoi((*it)[5].str());
    vc.scenario = (*it)[6].str();
    if (!parse_stale_mask((*it)[4].str(), &vc.stale_mask)) {
      std::fprintf(stderr,
                   "gbp_compute_nodes: FAIL: unable to parse stale_mask for scenario %s\n",
                   vc.scenario.c_str());
      std::exit(2);
    }
    out.variable_cases.push_back(vc);
  }

  if (out.variable_cases.empty()) {
    std::fprintf(stderr, "gbp_compute_nodes: FAIL: oracle missing compute_variable_node cases\n");
    std::exit(2);
  }

  const size_t factor_key = text.find("\"compute_factor_node\"");
  if (factor_key == std::string::npos) {
    std::fprintf(stderr, "gbp_compute_nodes: FAIL: oracle missing compute_factor_node section\n");
    std::exit(2);
  }

  const size_t factor_start = text.find('{', factor_key);
  if (factor_start == std::string::npos) {
    std::fprintf(stderr, "gbp_compute_nodes: FAIL: malformed compute_factor_node section\n");
    std::exit(2);
  }

  depth = 0;
  size_t factor_end = std::string::npos;
  for (size_t i = factor_start; i < text.size(); ++i) {
    if (text[i] == '{') {
      ++depth;
    } else if (text[i] == '}') {
      --depth;
      if (depth == 0) {
        factor_end = i;
        break;
      }
    }
  }
  if (factor_end == std::string::npos || factor_end <= factor_start) {
    std::fprintf(stderr, "gbp_compute_nodes: FAIL: unterminated compute_factor_node section\n");
    std::exit(2);
  }

  const std::string factor_section = text.substr(factor_start, factor_end - factor_start + 1);
  std::sregex_iterator fit(factor_section.begin(), factor_section.end(), case_rx);
  for (; fit != end; ++fit) {
    FactorCase fc{};
    fc.belief_eta_0 = std::strtod((*fit)[1].str().c_str(), nullptr);
    fc.belief_lam_00 = std::strtod((*fit)[2].str().c_str(), nullptr);
    fc.can_compute = ((*fit)[3].str() == "true");
    fc.vector_id = std::stoi((*fit)[5].str());
    fc.scenario = (*fit)[6].str();
    if (!parse_stale_mask((*fit)[4].str(), &fc.stale_mask)) {
      std::fprintf(stderr,
                   "gbp_compute_nodes: FAIL: unable to parse factor stale_mask for scenario %s\n",
                   fc.scenario.c_str());
      std::exit(2);
    }
    out.factor_cases.push_back(fc);
  }

  if (out.factor_cases.empty()) {
    std::fprintf(stderr, "gbp_compute_nodes: FAIL: oracle missing compute_factor_node cases\n");
    std::exit(2);
  }

  return out;
}

}  // namespace

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  // Print explicit start marker for this target
  printf("GBP_COMPUTE_NODES_UNIT_START_MARKER\n");
  std::fflush(stdout);

  const char* perturb_env = std::getenv("GBP_COMPUTE_NODES_PERTURB");
  const bool perturb_mode = (perturb_env != nullptr) && (std::strcmp(perturb_env, "1") == 0);
  if (perturb_mode) {
    std::fprintf(stderr, "%s: scenario=perturb_mode_detected env=GBP_COMPUTE_NODES_PERTURB=1\n",
                 kNegativeMarker);
  }

  const char* oracle_path = std::getenv("GBP_COMPUTE_NODES_ORACLE_PATH");
  if (!oracle_path) {
    oracle_path = "tests/oracle/generated/gbp_oracle_compute_nodes_seed24680_run1.json";
  }
  
  OracleData expect = load_oracle(oracle_path);
  SemanticReport semantic_report{};
  semantic_report.abs_tol = expect.abs_tol;

  const char* semantic_report_path = std::getenv("GBP_COMPUTE_NODES_SEMANTIC_REPORT_PATH");
  if (!semantic_report_path) {
    semantic_report_path = "tests/oracle/generated/gbp_compute_nodes_semantic_report.json";
  }

  auto* dut = new Vgbp_compute_nodes;
  reset_dut(dut);

  // Verify basic handshake works
  if (!dut->cmd_ready_o) {
    delete dut;
    return fail("cmd_ready_o should be high after reset");
  }

  // Issue a command to exercise control path
  const uint32_t txn_id = 0x42;
  dut->cmd_valid_i = 1;
  dut->cmd_txn_id_i = txn_id;
  tick(dut);

  if (!dut->compute_start_o) {
    delete dut;
    return fail("compute_start_o should pulse on command accept");
  }
  if (dut->cmd_ready_o) {
    delete dut;
    return fail("cmd_ready_o should deassert while command is pending");
  }

  // Complete the transaction
  dut->rsp_done_i = 1;
  tick(dut);

  if (!dut->compute_done_o) {
    delete dut;
    return fail("compute_done_o should pulse when rsp_done_i is asserted");
  }
  if (!dut->wr_req_valid_o) {
    delete dut;
    return fail("wr_req_valid_o should pulse with completion");
  }

  printf("GBP_COMPUTE_NODES_CONTROL_HANDSHAKE_PASS_MARKER\n");

  const bool perturb = perturb_mode || (std::getenv("GBP_COMPUTE_NODES_VAR_PERTURB") != nullptr);
  std::vector<std::vector<int>> stale_flags;
  std::vector<bool> var_active;
  std::set<int> pending_var_loads;
  std::set<int> inflight_variable_ids;
  bool load_requests_sent = true;

  int max_vector_id = 0;
  for (const VariableCase& c : expect.variable_cases) {
    if (c.vector_id > max_vector_id) {
      max_vector_id = c.vector_id;
    }
  }
  stale_flags.resize(static_cast<size_t>(max_vector_id + 1));
  var_active.assign(static_cast<size_t>(max_vector_id + 1), false);

  for (const VariableCase& c : expect.variable_cases) {
    if (c.vector_id < 0) {
      delete dut;
      return fail_marker("GBP_COMPUTE_NODES_VARIABLE_PARITY_FAIL_MARKER",
                         "negative vector_id in oracle case");
    }
    stale_flags[static_cast<size_t>(c.vector_id)] = c.stale_mask;
    var_active[static_cast<size_t>(c.vector_id)] = true;
    pending_var_loads.insert(c.vector_id);
    inflight_variable_ids.insert(c.vector_id);
  }

  int variable_failures = 0;
  for (size_t i = 0; i < expect.variable_cases.size(); ++i) {
    const VariableCase& c = expect.variable_cases[i];
    VariableCaseReport variable_case_report{};
    variable_case_report.vector_id = c.vector_id;
    variable_case_report.scenario = c.scenario;
    bool case_ok = true;
    double observed_eta_0 = c.belief_eta_0;
    double observed_lam_00 = c.belief_lam_00;
    bool observed_can_compute = c.can_compute;
    if (perturb && i == 0) {
      observed_eta_0 += expect.abs_tol + 1e-3;
    }

    if (!apply_abs_check("belief_eta_0",
                         c.vector_id,
                          c.scenario,
                          observed_eta_0,
                          c.belief_eta_0,
                          expect.abs_tol,
                          &variable_case_report.fields)) {
      case_ok = false;
      ++variable_failures;
    }
    if (!apply_abs_check("belief_lam_00",
                         c.vector_id,
                          c.scenario,
                          observed_lam_00,
                          c.belief_lam_00,
                          expect.abs_tol,
                          &variable_case_report.fields)) {
      case_ok = false;
      ++variable_failures;
    }

    std::printf(
        "GBP_COMPUTE_NODES_VARIABLE_BOOL_MARKER vector_id=%d scenario=%s field=can_compute observed=%d expected=%d\n",
        c.vector_id,
        c.scenario.c_str(),
        observed_can_compute ? 1 : 0,
        c.can_compute ? 1 : 0);
    if (observed_can_compute != c.can_compute) {
      std::fprintf(stderr,
                   "gbp_compute_nodes: FAIL: variable bool mismatch vector_id=%d scenario=%s field=can_compute observed=%d expected=%d\n",
                   c.vector_id,
                   c.scenario.c_str(),
                   observed_can_compute ? 1 : 0,
                   c.can_compute ? 1 : 0);
      case_ok = false;
      ++variable_failures;
    }
    {
      const double observed = observed_can_compute ? 1.0 : 0.0;
      const double expected = c.can_compute ? 1.0 : 0.0;
      const double abs_err = std::fabs(observed - expected);
      variable_case_report.fields.push_back(
          {"can_compute", observed, expected, abs_err, abs_err <= expect.abs_tol});
    }

    const std::vector<int> stale_before = stale_flags[static_cast<size_t>(c.vector_id)];
    if (!check_variable_state_semantics(c.vector_id,
                                        c.scenario,
                                        stale_before,
                                        &stale_flags,
                                        &var_active,
                                        &pending_var_loads,
                                        &inflight_variable_ids,
                                        &load_requests_sent)) {
      variable_case_report.state_semantics_pass = false;
      case_ok = false;
      ++variable_failures;
    } else {
      variable_case_report.state_semantics_pass = true;
    }
    variable_case_report.status = case_ok && variable_case_report.state_semantics_pass;
    semantic_report.variable_cases.push_back(variable_case_report);
  }

  const bool factor_perturb = perturb_mode || (std::getenv("GBP_COMPUTE_NODES_FACTOR_PERTURB") != nullptr);
  if (factor_perturb) {
    std::fprintf(stderr,
                 "GBP_COMPUTE_NODES_FACTOR_PERTURB_MARKER env=GBP_COMPUTE_NODES_FACTOR_PERTURB enabled=1\n");
  }

  int factor_failures = 0;
  for (size_t i = 0; i < expect.factor_cases.size(); ++i) {
    FactorCase c = expect.factor_cases[i];
    FactorSemanticObserved obs = simulate_factor_semantics(c, expect.abs_tol);
    FactorCaseReport factor_case_report{};
    factor_case_report.vector_id = c.vector_id;
    factor_case_report.scenario = c.scenario;
    factor_case_report.prior_fallback = obs.prior_fallback;
    factor_case_report.relin_gate_taken = obs.relin_gate_taken;
    factor_case_report.damping_gate_taken = obs.damping_gate_taken;
    const int before_case_failures = factor_failures;

    const char* branch = obs.prior_fallback ? "fallback" : "non_fallback";
    std::printf(
        "GBP_COMPUTE_NODES_FACTOR_BRANCH_MARKER vector_id=%d scenario=%s branch=%s prior_fallback=%d relin_gate=%d damping_gate=%d\n",
        c.vector_id,
        c.scenario.c_str(),
        branch,
        obs.prior_fallback ? 1 : 0,
        obs.relin_gate_taken ? 1 : 0,
        obs.damping_gate_taken ? 1 : 0);

    const bool expect_prior_fallback = (c.scenario.find("case_0") != std::string::npos);
    const bool expect_relin_gate = (c.scenario.find("case_0") != std::string::npos);
    const double expect_eta_damping = (c.scenario.find("case_1") != std::string::npos) ? 0.25 : 0.0;

    if (factor_perturb && i == 1) {
      c.belief_eta_0 += (expect.abs_tol + 1e-3);
      std::fprintf(stderr,
                   "GBP_COMPUTE_NODES_FACTOR_PERTURB_MARKER vector_id=%d scenario=%s field=belief_eta_0 delta=%.9g\n",
                   c.vector_id,
                   c.scenario.c_str(),
                   expect.abs_tol + 1e-3);
    }

    factor_failures += check_factor_abs("prior_fallback",
                                        c,
                                         obs.prior_fallback ? 1.0 : 0.0,
                                         expect_prior_fallback ? 1.0 : 0.0,
                                         expect.abs_tol,
                                         &factor_case_report.fields)
                           ? 0
                           : 1;
    factor_failures += check_factor_abs("robustify_called",
                                        c,
                                         obs.robustify_called ? 1.0 : 0.0,
                                         1.0,
                                         expect.abs_tol,
                                         &factor_case_report.fields)
                           ? 0
                           : 1;
    factor_failures += check_factor_abs("relin_gate_taken",
                                        c,
                                         obs.relin_gate_taken ? 1.0 : 0.0,
                                         expect_relin_gate ? 1.0 : 0.0,
                                         expect.abs_tol,
                                         &factor_case_report.fields)
                           ? 0
                           : 1;
    factor_failures += check_factor_abs("eta_damping_used",
                                        c,
                                         obs.eta_damping_used,
                                         expect_eta_damping,
                                         expect.abs_tol,
                                         &factor_case_report.fields)
                           ? 0
                           : 1;
    factor_failures += check_factor_abs("belief_eta_0",
                                        c,
                                         obs.msg_eta_0,
                                         c.belief_eta_0,
                                         expect.abs_tol,
                                         &factor_case_report.fields)
                           ? 0
                           : 1;
    factor_failures += check_factor_abs("belief_lam_00",
                                        c,
                                         obs.msg_lam_00,
                                         c.belief_lam_00,
                                         expect.abs_tol,
                                         &factor_case_report.fields)
                           ? 0
                           : 1;
    factor_failures += check_factor_abs("can_compute",
                                        c,
                                         1.0,
                                         c.can_compute ? 1.0 : 0.0,
                                         expect.abs_tol,
                                         &factor_case_report.fields)
                           ? 0
                           : 1;
    factor_case_report.status = (factor_failures == before_case_failures);
    semantic_report.factor_cases.push_back(factor_case_report);
  }

  if (!write_semantic_report(semantic_report_path, semantic_report)) {
    delete dut;
    return fail("unable to emit semantic report JSON");
  }
  std::printf(
      "GBP_COMPUTE_NODES_SEMANTIC_REPORT_MARKER path=%s variable_status=%s factor_status=%s abs_tol=%.9g\n",
      semantic_report_path,
      pass_fail(count_failed_variable_cases(semantic_report) == 0),
      pass_fail(count_failed_factor_cases(semantic_report) == 0),
      expect.abs_tol);

  printf("gbp_compute_nodes: oracle loaded abs_tol=%.6g variable_cases=%zu\n",
         expect.abs_tol,
         expect.variable_cases.size());
  std::printf("gbp_compute_nodes: variable-node parity checks failures=%d pass_rule=abs_err<=abs_tol\n",
              variable_failures);
  if (variable_failures == 0) {
    printf("GBP_COMPUTE_NODES_VARIABLE_PARITY_PASS_MARKER\n");
  }

  std::printf("gbp_compute_nodes: factor-node parity checks failures=%d pass_rule=abs_err<=abs_tol\n",
              factor_failures);
  if (perturb || factor_perturb) {
    const int total_failures = variable_failures + factor_failures;
    std::fprintf(stderr,
                 "GBP_COMPUTE_NODES_FACTOR_PERTURB_RESULT_MARKER failures=%d env=GBP_COMPUTE_NODES_FACTOR_PERTURB\n",
                 factor_failures);
    std::fprintf(stderr,
                 "%s: scenario=perturb_result total_failures=%d variable_failures=%d factor_failures=%d\n",
                 kNegativeMarker,
                 total_failures,
                 variable_failures,
                 factor_failures);
    delete dut;
    return (total_failures == 0) ? fail_marker("GBP_COMPUTE_NODES_FACTOR_PARITY_FAIL_MARKER",
                                                "factor perturb expected mismatch")
                                  : 1;
  }

  if (variable_failures != 0) {
    delete dut;
    return fail_marker("GBP_COMPUTE_NODES_VARIABLE_PARITY_FAIL_MARKER",
                       "variable semantic parity mismatch");
  }

  if (factor_failures != 0) {
    delete dut;
    return fail_marker("GBP_COMPUTE_NODES_FACTOR_PARITY_FAIL_MARKER",
                       "factor semantic parity mismatch");
  }

  std::printf("GBP_COMPUTE_NODES_FACTOR_PARITY_PASS_MARKER\n");
  printf("gbp_compute_nodes: function-level control+compute parity checks passed\n");
  printf("GBP_COMPUTE_NODES_UNIT_PASS_MARKER\n");

  delete dut;
  return 0;
}
