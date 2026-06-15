#ifndef NOCBP_VERILATOR_TESTS_COMMON_TEST_UTILS_HPP_
#define NOCBP_VERILATOR_TESTS_COMMON_TEST_UTILS_HPP_

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>

#ifdef TRACE_VCD
#include "verilated_vcd_c.h"
#endif

namespace test_utils {

#ifdef TRACE_VCD
static VerilatedVcdC* g_tfp = nullptr;
static uint64_t g_sim_time = 0;
#endif

// 通用tick函数模板（需要针对具体DUT类型特化）
template<typename DUT>
static void tick(DUT* dut) {
  dut->clk = 0;
  dut->eval();
#ifdef TRACE_VCD
  if (g_tfp) g_tfp->dump(g_sim_time++);
#endif
  dut->clk = 1;
  dut->eval();
#ifdef TRACE_VCD
  if (g_tfp) g_tfp->dump(g_sim_time++);
#endif
}

// 通用reset函数模板
template<typename DUT>
static void reset_dut(DUT* dut, int cycles = 2) {
  dut->rst_n = 0;
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
}

// VCD trace 初始化
#ifdef TRACE_VCD
template<typename DUT>
static void trace_init(DUT* dut, const char* filename = "dump.vcd", int levels = 99) {
  Verilated::traceEverOn(true);
  g_tfp = new VerilatedVcdC;
  dut->trace(g_tfp, levels);
  g_tfp->open(filename);
  g_sim_time = 0;
}

static void trace_close() {
  if (g_tfp) {
    g_tfp->close();
    delete g_tfp;
    g_tfp = nullptr;
  }
}
#else
template<typename DUT>
static void trace_init(DUT*, const char* = "dump.vcd", int = 99) {}
static void trace_close() {}
#endif

// 通用失败报告函数
static int fail(const char* test_name, const char* msg) {
  std::fprintf(stderr, "%s: FAIL: %s\n", test_name, msg);
  return 1;
}

// 带标记的失败报告函数
static int fail_marker(const char* test_name, const char* marker, const char* msg) {
  std::fprintf(stderr, "%s\n", marker);
  return fail(test_name, msg);
}

// 环境变量检查工具
static bool env_flag(const char* name) {
  const char* val = std::getenv(name);
  return val != nullptr && val[0] != '\0' && val[0] != '0';
}

// 环境变量获取工具（带默认值）
static const char* env_value(const char* name, const char* default_value = nullptr) {
  const char* val = std::getenv(name);
  return (val != nullptr && val[0] != '\0') ? val : default_value;
}

// 通用等待信号函数模板
template<typename DUT, typename SignalFunc>
static bool wait_signal(DUT* dut, SignalFunc signal_check, int max_cycles) {
  for (int i = 0; i < max_cycles; ++i) {
    if (signal_check(dut)) {
      return true;
    }
    tick(dut);
  }
  return false;
}

// 打印测试开始标记
static void print_test_start(const char* test_name) {
  std::printf("%s: START\n", test_name);
  std::fflush(stdout);
}

// 打印测试通过标记
static void print_test_pass(const char* test_name) {
  std::printf("%s: PASS\n", test_name);
  std::fflush(stdout);
}

// 打印自定义标记
static void print_marker(const char* marker) {
  std::printf("%s\n", marker);
  std::fflush(stdout);
}

}  // namespace test_utils

#endif  // NOCBP_VERILATOR_TESTS_COMMON_TEST_UTILS_HPP_