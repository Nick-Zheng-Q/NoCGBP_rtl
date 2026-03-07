#include "oracle_contract.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace oracle_contract {

namespace {

std::string trim(const std::string& in) {
  size_t begin = 0;
  while (begin < in.size() && std::isspace(static_cast<unsigned char>(in[begin])) != 0) {
    ++begin;
  }
  size_t end = in.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(in[end - 1])) != 0) {
    --end;
  }
  return in.substr(begin, end - begin);
}

std::string strip_inline_comment(const std::string& in) {
  const size_t comment_pos = in.find('#');
  if (comment_pos == std::string::npos) {
    return in;
  }
  return in.substr(0, comment_pos);
}

bool parse_bool_value(const std::string& value, bool* out) {
  if (value == "true" || value == "1") {
    *out = true;
    return true;
  }
  if (value == "false" || value == "0") {
    *out = false;
    return true;
  }
  return false;
}

bool get_required_string(const std::unordered_map<std::string, std::string>& values,
                         const std::string& key,
                         std::string* out,
                         std::string* error) {
  const auto it = values.find(key);
  if (it == values.end()) {
    *error = "missing required field: " + key;
    return false;
  }
  *out = it->second;
  return true;
}

bool get_required_int(const std::unordered_map<std::string, std::string>& values,
                      const std::string& key,
                      int* out,
                      std::string* error) {
  std::string value;
  if (!get_required_string(values, key, &value, error)) {
    return false;
  }
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    *error = "invalid integer field: " + key + "='" + value + "'";
    return false;
  }
  *out = static_cast<int>(parsed);
  return true;
}

bool get_required_double(const std::unordered_map<std::string, std::string>& values,
                         const std::string& key,
                         double* out,
                         std::string* error) {
  std::string value;
  if (!get_required_string(values, key, &value, error)) {
    return false;
  }
  char* end = nullptr;
  const double parsed = std::strtod(value.c_str(), &end);
  if (end == nullptr || *end != '\0') {
    *error = "invalid floating field: " + key + "='" + value + "'";
    return false;
  }
  *out = parsed;
  return true;
}

bool get_required_bool(const std::unordered_map<std::string, std::string>& values,
                       const std::string& key,
                       bool* out,
                       std::string* error) {
  const auto it = values.find(key);
  if (it == values.end()) {
    *error = "missing required field: " + key;
    return false;
  }
  std::string value = it->second;
  if (!parse_bool_value(value, out)) {
    *error = "invalid boolean field: " + key + "='" + value + "'";
    return false;
  }
  return true;
}

bool get_optional_string(const std::unordered_map<std::string, std::string>& values,
                        const std::string& key,
                        std::string* out) {
  const auto it = values.find(key);
  if (it != values.end()) {
    *out = it->second;
    return true;
  }
  return false;
}

bool get_optional_double(const std::unordered_map<std::string, std::string>& values,
                         const std::string& key,
                         double* out) {
  const auto it = values.find(key);
  if (it != values.end()) {
    char* end = nullptr;
    const double parsed = std::strtod(it->second.c_str(), &end);
    if (end != nullptr && *end == '\0') {
      *out = parsed;
      return true;
    }
  }
  return false;
}

bool get_optional_bool(const std::unordered_map<std::string, std::string>& values,
                       const std::string& key,
                       bool* out) {
  const auto it = values.find(key);
  if (it != values.end()) {
    if (parse_bool_value(it->second, out)) {
      return true;
    }
  }
  return false;
}

bool get_optional_int(const std::unordered_map<std::string, std::string>& values,
                      const std::string& key,
                      int* out) {
  const auto it = values.find(key);
  if (it != values.end()) {
    char* end = nullptr;
    const long parsed = std::strtol(it->second.c_str(), &end, 10);
    if (end != nullptr && *end == '\0') {
      *out = static_cast<int>(parsed);
      return true;
    }
  }
  return false;
}

bool expect_exact_int(const char* key, int actual, int expected, std::string* error) {
  if (actual == expected) {
    return true;
  }
  std::ostringstream oss;
  oss << "frozen value mismatch for " << key << ": expected " << expected << " got " << actual;
  *error = oss.str();
  return false;
}

bool expect_exact_double(const char* key, double actual, double expected, std::string* error) {
  if (actual == expected) {
    return true;
  }
  std::ostringstream oss;
  oss << "frozen value mismatch for " << key << ": expected " << expected << " got " << actual;
  *error = oss.str();
  return false;
}

bool expect_exact_string(const char* key,
                         const std::string& actual,
                         const std::string& expected,
                         std::string* error) {
  if (actual == expected) {
    return true;
  }
  std::ostringstream oss;
  oss << "frozen value mismatch for " << key << ": expected '" << expected << "' got '" << actual
      << "'";
  *error = oss.str();
  return false;
}

}

bool load_contract(const std::string& path, Contract* out, std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }

  std::ifstream in(path);
  if (!in.is_open()) {
    *error = "cannot open contract file: " + path;
    return false;
  }

  std::unordered_map<std::string, std::string> values;
  std::string line;
  int line_number = 0;
  while (std::getline(in, line)) {
    ++line_number;
    const std::string compact = trim(strip_inline_comment(line));
    if (compact.empty()) {
      continue;
    }
    const size_t colon = compact.find(':');
    if (colon == std::string::npos) {
      std::ostringstream oss;
      oss << "invalid contract line " << line_number << ": missing ':'";
      *error = oss.str();
      return false;
    }
    const std::string key = trim(compact.substr(0, colon));
    const std::string value = trim(compact.substr(colon + 1));
    if (key.empty()) {
      std::ostringstream oss;
      oss << "invalid contract line " << line_number << ": empty key";
      *error = oss.str();
      return false;
    }
    if (value.empty()) {
      std::ostringstream oss;
      oss << "invalid contract line " << line_number << ": empty value for key " << key;
      *error = oss.str();
      return false;
    }
    values[key] = value;
  }

  Contract parsed;
  get_optional_string(values, "contract_type", &parsed.contract_type);
  get_optional_double(values, "abs_tol", &parsed.abs_tol);
  get_optional_bool(values, "abs_only", &parsed.abs_only);
  get_optional_int(values, "generator_seed", &parsed.generator_seed);
  get_optional_int(values, "function_count", &parsed.function_count);

  bool is_function_level = (parsed.contract_type == "function_level_parity");

  if (!get_required_int(values, "contract_version", &parsed.contract_version, error) ||
      !get_required_string(values, "simulator_config_path", &parsed.simulator_config_path, error)) {
    return false;
  }

  if (is_function_level) {
    if (parsed.abs_tol == 0.0) {
      *error = "function-level contract requires abs_tol to be set";
      return false;
    }
  } else {
    if (!get_required_string(values, "workload_a_name", &parsed.workload_a.name, error) ||
        !get_required_int(values, "workload_a_nodes", &parsed.workload_a.nodes, error) ||
        !get_required_double(values, "workload_a_spacing", &parsed.workload_a.spacing, error) ||
        !get_required_int(values, "workload_a_seed", &parsed.workload_a.seed, error) ||
        !get_required_string(values, "workload_b_name", &parsed.workload_b.name, error) ||
        !get_required_int(values, "workload_b_rows", &parsed.workload_b.rows, error) ||
        !get_required_int(values, "workload_b_cols", &parsed.workload_b.cols, error) ||
        !get_required_double(values, "workload_b_spacing", &parsed.workload_b.spacing, error) ||
        !get_required_int(values, "workload_b_seed", &parsed.workload_b.seed, error) ||
        !get_required_int(values, "max_iters", &parsed.max_iters, error) ||
        !get_required_int(values, "max_cycles", &parsed.max_cycles, error) ||
        !get_required_double(values,
                             "threshold_state_message_abs_err",
                             &parsed.state_message_threshold.abs_err,
                             error) ||
        !get_required_double(values,
                             "threshold_state_message_rel_err",
                             &parsed.state_message_threshold.rel_err,
                             error) ||
        !get_required_double(values,
                             "threshold_are_energy_abs_err",
                             &parsed.are_energy_threshold.abs_err,
                             error) ||
        !get_required_double(values,
                             "threshold_are_energy_rel_err",
                             &parsed.are_energy_threshold.rel_err,
                             error) ||
        !get_required_bool(values, "nan_mismatch_fail", &parsed.nan_mismatch_fail, error) ||
        !get_required_bool(values, "inf_mismatch_fail", &parsed.inf_mismatch_fail, error) ||
        !get_required_bool(values, "signed_zero_equivalent", &parsed.signed_zero_equivalent, error)) {
      return false;
    }
  }

  *out = parsed;
  return true;
}

bool compare_abs_only(const char* scenario, double observed, double expected, double abs_tol,
                      std::string* mismatch_output) {
  double abs_err = std::fabs(observed - expected);
  if (abs_err <= abs_tol) {
    return true;
  }
  std::ostringstream oss;
  oss << "oracle_mismatch: scenario=" << scenario << " observed=" << observed << " expected=" << expected
      << " abs_err=" << abs_err << " abs_tol=" << abs_tol;
  *mismatch_output = oss.str();
  return false;
}

bool validate_frozen_contract(const Contract& contract, std::string* error) {
  if (error == nullptr) {
    return false;
  }

  if (!expect_exact_int("contract_version", contract.contract_version, 1, error) ||
      !expect_exact_string("simulator_config_path", contract.simulator_config_path, "config.yml", error) ||
      !expect_exact_string("workload_a_name", contract.workload_a.name, "synthetic_line", error) ||
      !expect_exact_int("workload_a_nodes", contract.workload_a.nodes, 16, error) ||
      !expect_exact_double("workload_a_spacing", contract.workload_a.spacing, 1.0, error) ||
      !expect_exact_int("workload_a_seed", contract.workload_a.seed, 12345, error) ||
      !expect_exact_string("workload_b_name", contract.workload_b.name, "synthetic_lattice", error) ||
      !expect_exact_int("workload_b_rows", contract.workload_b.rows, 4, error) ||
      !expect_exact_int("workload_b_cols", contract.workload_b.cols, 4, error) ||
      !expect_exact_double("workload_b_spacing", contract.workload_b.spacing, 1.0, error) ||
      !expect_exact_int("workload_b_seed", contract.workload_b.seed, 12345, error) ||
      !expect_exact_int("max_iters", contract.max_iters, 50, error) ||
      !expect_exact_int("max_cycles", contract.max_cycles, 5000, error) ||
      !expect_exact_double("threshold_state_message_abs_err", contract.state_message_threshold.abs_err, 1e-4, error) ||
      !expect_exact_double("threshold_state_message_rel_err", contract.state_message_threshold.rel_err, 1e-3, error) ||
      !expect_exact_double("threshold_are_energy_abs_err", contract.are_energy_threshold.abs_err, 1e-3, error) ||
      !expect_exact_double("threshold_are_energy_rel_err", contract.are_energy_threshold.rel_err, 1e-2, error)) {
    return false;
  }

  if (!contract.nan_mismatch_fail || !contract.inf_mismatch_fail || !contract.signed_zero_equivalent) {
    *error = "frozen value mismatch for floating-point policy booleans";
    return false;
  }

  return true;
}

}
