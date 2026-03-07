#include <cstdio>

#include "verilated.h"
#include "Vendpoint_rx_top.h"

static void tick(Vendpoint_rx_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vendpoint_rx_top* dut, int cycles = 2) {
  dut->rst_n = 0;
  dut->send_v = 0;
  dut->send_we = 0;
  dut->send_addr = 0;
  dut->send_data = 0;
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vendpoint_rx_top;

  reset_dut(dut);

  dut->send_we = 1;
  dut->send_addr = 0x20;
  dut->send_data = 0x1234ABCD;

  bool accepted = false;
  for (int i = 0; i < 20; ++i) {
    dut->send_v = accepted ? 0 : 1;
    tick(dut);
    if (dut->send_ready) {
      accepted = true;
    }
    if (dut->recv_seen) {
      break;
    }
  }

  if (!dut->recv_seen) {
    std::fprintf(stderr, "endpoint_rx: no receive observed\n");
    delete dut;
    return 1;
  }
  if (!dut->recv_we) {
    std::fprintf(stderr, "endpoint_rx: expected write receive\n");
    delete dut;
    return 1;
  }
  if (dut->recv_addr != 0x20) {
    std::fprintf(stderr, "endpoint_rx: addr mismatch got 0x%04x\n", dut->recv_addr);
    delete dut;
    return 1;
  }
  if (dut->recv_data != 0x1234ABCDu) {
    std::fprintf(stderr, "endpoint_rx: data mismatch got 0x%08x\n", dut->recv_data);
    delete dut;
    return 1;
  }

  std::printf("endpoint_rx: PASS recv addr=0x%04x data=0x%08x\n", dut->recv_addr, dut->recv_data);
  delete dut;
  return 0;
}
