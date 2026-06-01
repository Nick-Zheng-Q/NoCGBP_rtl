#ifndef NOCBP_VERILATOR_TESTS_COMMON_VERILATOR_UTILS_HPP_
#define NOCBP_VERILATOR_TESTS_COMMON_VERILATOR_UTILS_HPP_

#include "verilated.h"
#include <cstdio>
#include <cstdlib>

namespace verilator_utils {

// Verilator初始化工具
static void verilator_init(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
}

// 通用测试入口宏
#define DEFINE_TEST_ENTRY(test_name) \
  int run_test(int argc, char** argv)

// 测试结果枚举
enum TestResult {
  TEST_PASS = 0,
  TEST_FAIL = 1,
  TEST_ERROR = 2,
  TEST_TIMEOUT = 124
};

// 打印Verilator版本信息
static void print_verilator_version() {
  std::printf("Verilator version: %s\n", Verilated::version());
  std::fflush(stdout);
}

// 检查Verilator覆盖率支持
static bool has_coverage_support() {
  return VM_COVERAGE;
}

// 检查Verilator跟踪支持
static bool has_trace_support() {
  return VM_TRACE;
}

// 打印Verilator配置信息
static void print_verilator_config() {
  std::printf("Verilator config:\n");
  std::printf("  Coverage: %s\n", has_coverage_support() ? "enabled" : "disabled");
  std::printf("  Trace: %s\n", has_trace_support() ? "enabled" : "disabled");
  std::fflush(stdout);
}

// 环境变量工具函数
namespace env {

// 获取环境变量（带默认值）
static const char* get(const char* name, const char* default_value = nullptr) {
  const char* val = std::getenv(name);
  return (val != nullptr && val[0] != '\0') ? val : default_value;
}

// 检查环境变量标志
static bool flag(const char* name) {
  const char* val = std::getenv(name);
  return val != nullptr && val[0] != '\0' && val[0] != '0';
}

// 获取整数环境变量
static int get_int(const char* name, int default_value = 0) {
  const char* val = std::getenv(name);
  if (val == nullptr || val[0] == '\0') {
    return default_value;
  }
  char* end = nullptr;
  long result = std::strtol(val, &end, 10);
  return (end && *end == '\0') ? static_cast<int>(result) : default_value;
}

// 获取浮点环境变量
static double get_double(const char* name, double default_value = 0.0) {
  const char* val = std::getenv(name);
  if (val == nullptr || val[0] == '\0') {
    return default_value;
  }
  char* end = nullptr;
  double result = std::strtod(val, &end);
  return (end && *end == '\0') ? result : default_value;
}

}  // namespace env

}  // namespace verilator_utils

#endif  // NOCBP_VERILATOR_TESTS_COMMON_VERILATOR_UTILS_HPP_