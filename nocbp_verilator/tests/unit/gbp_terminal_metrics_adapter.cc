#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "verilated.h"
#include "Vgbp_terminal_metrics_adapter.h"

#include "../common/gbp_terminal_metrics_adapter.hpp"
#include "../common/gbp_terminal_metrics_adapter.cpp"

static void tick(Vgbp_terminal_metrics_adapter* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  Vgbp_terminal_metrics_adapter* dut = new Vgbp_terminal_metrics_adapter;
  dut->rst_n = 0;
  tick(dut);
  dut->rst_n = 1;
  tick(dut);

  const char* dump_path_env = std::getenv("GBP_TERMINAL_METRICS_ADAPTER_DUMP");
  const std::string dump_path =
      (dump_path_env != nullptr)
          ? std::string(dump_path_env)
          : std::string("tests/unit/data/gbp_terminal_metrics_adapter_golden_dump.json");

  gbp_terminal_metrics_adapter::Metrics metrics;
  std::string error;
  if (!gbp_terminal_metrics_adapter::reconstruct_metrics_from_dump(dump_path, &metrics, &error)) {
    std::fprintf(stderr,
                 "gbp_terminal_metrics_adapter: FAIL: reconstruction error path=%s error=%s\n",
                 dump_path.c_str(),
                 error.c_str());
    delete dut;
    return 1;
  }

  const double expected_are = 0.06972;
  const double expected_energy = 0.044911;
  const double tol = 1e-9;
  if (std::fabs(metrics.are - expected_are) > tol ||
      std::fabs(metrics.energy - expected_energy) > tol) {
    std::fprintf(stderr,
                 "gbp_terminal_metrics_adapter: FAIL: metric mismatch are=%.12f energy=%.12f\n",
                 metrics.are,
                 metrics.energy);
    delete dut;
    return 1;
  }

  std::printf("GBP_TERMINAL_METRICS_ADAPTER_PASS_MARKER are=%.12f energy=%.12f\n",
              metrics.are,
              metrics.energy);

  delete dut;
  return 0;
}
