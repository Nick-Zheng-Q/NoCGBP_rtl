#include <cstdio>

#include "verilated.h"
#include "Vendpoint_noc_top.h"

static void tick(Vendpoint_noc_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vendpoint_noc_top* dut, int cycles = 3) {
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
  auto* dut = new Vendpoint_noc_top;

  reset_dut(dut);

  dut->send_we = 1;
  dut->send_addr = 0x0020;
  dut->send_data = 0xA5A55A5A;

  bool accepted = false;
  for (int i = 0; i < 64; ++i) {
    dut->send_v = accepted ? 0 : 1;
    tick(dut);
    if (dut->send_ready) {
      accepted = true;
    }
    if (dut->recv_seen) {
      break;
    }
  }

  if (!accepted) {
    std::fprintf(stderr, "endpoint_noc: sender never got ready\n");
    delete dut;
    return 1;
  }
  if (!dut->recv_seen) {
    std::fprintf(stderr, "endpoint_noc: no receive observed through NoC path\n");
    delete dut;
    return 1;
  }
  if (!dut->recv_we) {
    std::fprintf(stderr, "endpoint_noc: expected write receive\n");
    delete dut;
    return 1;
  }
  if (dut->recv_addr != 0x0020) {
    std::fprintf(stderr, "endpoint_noc: addr mismatch got 0x%04x\n", dut->recv_addr);
    delete dut;
    return 1;
  }
  if (dut->recv_data != 0xA5A55A5Au) {
    std::fprintf(stderr, "endpoint_noc: data mismatch got 0x%08x\n", dut->recv_data);
    delete dut;
    return 1;
  }

  std::printf("endpoint_noc: PASS recv addr=0x%04x data=0x%08x\n", dut->recv_addr, dut->recv_data);
  delete dut;
  return 0;
}
