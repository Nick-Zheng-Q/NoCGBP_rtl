#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vexample_top.h"

static void tick(Vexample_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vexample_top;
  dut->rst_n = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;

  for (int i = 0; i < 5; ++i) {
    tick(dut);
  }

  const uint8_t expected = 5;
  if (dut->out != expected) {
    std::fprintf(stderr, "example: expected %u got %u\n", expected, dut->out);
    delete dut;
    return 1;
  }

  delete dut;
  return 0;
}
