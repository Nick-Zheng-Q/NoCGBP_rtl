#ifndef NOCBP_VERILATOR_TESTS_COMMON_ORACLE_UTILS_HPP_
#define NOCBP_VERILATOR_TESTS_COMMON_ORACLE_UTILS_HPP_

#include <string>
#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <vector>
#include <cstdio>

namespace oracle_utils {

// JSON值提取结果结构
struct ExtractResult {
  bool success;
  double value;
};

// 通用JSON值提取
static ExtractResult extract_json_double(const std::string& text, const char* key) {
  ExtractResult result{false, 0.0};
  std::regex rx(std::string("\"") + key + "\"\\s*:\\s*([-+0-9.eE]+)");
  std::smatch m;
  if (!std::regex_search(text, m, rx) || m.size() < 2) {
    return result;
  }
  char* end = nullptr;
  result.value = std::strtod(m[1].str().c_str(), &end);
  result.success = (end && *end == '\0');
  return result;
}

// 通用JSON布尔值提取
static bool extract_json_bool(const std::string& text, const char* key, bool* out) {
  std::regex rx(std::string("\"") + key + "\"\\s*:\\s*(true|false)");
  std::smatch m;
  if (!std::regex_search(text, m, rx) || m.size() < 2) {
    return false;
  }
  *out = (m[1].str() == "true");
  return true;
}

// 通用JSON字符串提取
static bool extract_json_string(const std::string& text, const char* key, std::string* out) {
  std::regex rx(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]+)\"");
  std::smatch m;
  if (!std::regex_search(text, m, rx) || m.size() < 2) {
    return false;
  }
  *out = m[1].str();
  return true;
}

// 加载文件内容
static std::string load_file_content(const char* path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    return "";
  }
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

// 加载文件内容（带错误报告）
static std::string load_file_content_or_fail(const char* path, const char* test_name) {
  std::string content = load_file_content(path);
  if (content.empty()) {
    std::fprintf(stderr, "%s: FAIL: unable to open file: %s\n", test_name, path);
  }
  return content;
}

// 绝对误差检查
static bool check_abs_error(double observed, double expected, double abs_tol,
                            const char* field, int vector_id, const char* scenario) {
  const double abs_err = std::fabs(observed - expected);
  const bool pass = (abs_err <= abs_tol);

  std::printf("ABS_ERR_CHECK field=%s vector_id=%d scenario=%s observed=%.8f expected=%.8f abs_err=%.9g abs_tol=%.9g status=%s\n",
              field, vector_id, scenario, observed, expected, abs_err, abs_tol, pass ? "PASS" : "FAIL");

  if (!pass) {
    std::fprintf(stderr, "FAIL: abs mismatch field=%s vector_id=%d scenario=%s observed=%.8f expected=%.8f abs_err=%.9g abs_tol=%.9g\n",
                 field, vector_id, scenario, observed, expected, abs_err, abs_tol);
  }
  return pass;
}

// 相对误差检查
static bool check_rel_error(double observed, double expected, double rel_tol,
                            const char* field, int vector_id, const char* scenario) {
  const double abs_err = std::fabs(observed - expected);
  const double denom = std::fmax(std::fabs(expected), 1e-12);
  const double rel_err = abs_err / denom;
  const bool pass = (rel_err <= rel_tol);

  std::printf("REL_ERR_CHECK field=%s vector_id=%d scenario=%s observed=%.8f expected=%.8f rel_err=%.9g rel_tol=%.9g status=%s\n",
              field, vector_id, scenario, observed, expected, rel_err, rel_tol, pass ? "PASS" : "FAIL");

  if (!pass) {
    std::fprintf(stderr, "FAIL: rel mismatch field=%s vector_id=%d scenario=%s observed=%.8f expected=%.8f rel_err=%.9g rel_tol=%.9g\n",
                 field, vector_id, scenario, observed, expected, rel_err, rel_tol);
  }
  return pass;
}

// 联合误差检查（绝对或相对）
static bool check_abs_or_rel_error(double observed, double expected, double abs_tol, double rel_tol,
                                   const char* field, int vector_id, const char* scenario) {
  const double abs_err = std::fabs(observed - expected);
  const double denom = std::fmax(std::fabs(expected), 1e-12);
  const double rel_err = abs_err / denom;
  const bool pass = (abs_err <= abs_tol) || (rel_err <= rel_tol);

  std::printf("ABS_OR_REL_ERR_CHECK field=%s vector_id=%d scenario=%s observed=%.8f expected=%.8f abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g status=%s\n",
              field, vector_id, scenario, observed, expected, abs_err, rel_err, abs_tol, rel_tol, pass ? "PASS" : "FAIL");

  if (!pass) {
    std::fprintf(stderr, "FAIL: abs_or_rel mismatch field=%s vector_id=%d scenario=%s observed=%.8f expected=%.8f abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g\n",
                 field, vector_id, scenario, observed, expected, abs_err, rel_err, abs_tol, rel_tol);
  }
  return pass;
}

// 有限性检查
static bool check_finite(double value, const char* field, const char* scenario) {
  if (!std::isfinite(value)) {
    std::fprintf(stderr, "FAIL: non-finite value for field=%s scenario=%s value=%.9g\n",
                 field, scenario, value);
    return false;
  }
  return true;
}

// JSON转义字符串
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

}  // namespace oracle_utils

#endif  // NOCBP_VERILATOR_TESTS_COMMON_ORACLE_UTILS_HPP_