#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vscratchpad_top.h"

static void tick(Vscratchpad_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vscratchpad_top* dut, int cycles = 2) {
  dut->rst_n = 0;
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
}

static void sb_write(Vscratchpad_top* dut, uint8_t addr, uint32_t data) {
  dut->sb_w_addr = addr;
  dut->sb_w_data = data;
  dut->sb_w_v = 1;
  tick(dut);
  dut->sb_w_v = 0;
}

static uint32_t sb_read(Vscratchpad_top* dut, uint8_t addr) {
  dut->sb_r_addr = addr;
  dut->sb_r_v = 1;
  tick(dut);
  dut->sb_r_v = 0;
  tick(dut);
  if (dut->sb_r_v != 1) {
    std::fprintf(stderr, "FAIL: sb_r_v not asserted\n");
    return 0;
  }
  return dut->sb_r_data;
}

static void mb_write(Vscratchpad_top* dut, uint8_t addr, uint32_t data) {
  dut->mb_w_addr = addr;
  dut->mb_w_data = data;
  dut->mb_w_v = 1;
  tick(dut);
  dut->mb_w_v = 0;
}

static uint32_t mb_read(Vscratchpad_top* dut, uint8_t addr) {
  dut->mb_r_addr = addr;
  dut->mb_r_v = 1;
  tick(dut);
  dut->mb_r_v = 0;
  tick(dut);
  if (dut->mb_r_v != 1) {
    std::fprintf(stderr, "FAIL: mb_r_v not asserted\n");
    return 0;
  }
  return dut->mb_r_data;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vscratchpad_top;
  reset_dut(dut);

  // Single bank test
  sb_write(dut, 0x1, 0xA5A5A5A5);
  sb_write(dut, 0x2, 0x12345678);
  auto sb_r1 = sb_read(dut, 0x1);
  if (sb_r1 != 0xA5A5A5A5) {
    std::fprintf(stderr, "FAIL: sb_r1=0x%08x\n", sb_r1);
    delete dut;
    return 1;
  }
  auto sb_r2 = sb_read(dut, 0x2);
  if (sb_r2 != 0x12345678) {
    std::fprintf(stderr, "FAIL: sb_r2=0x%08x\n", sb_r2);
    delete dut;
    return 1;
  }

  // Multi-bank interleaving test
  for (uint8_t addr = 0; addr < 8; ++addr) {
    mb_write(dut, addr, 0xCAFE0000u | addr);
  }
  for (uint8_t addr = 0; addr < 8; ++addr) {
    auto data = mb_read(dut, addr);
    uint32_t expected = 0xCAFE0000u | addr;
    if (data != expected) {
      std::fprintf(stderr, "FAIL: mb addr %u data=0x%08x\n", addr, data);
      delete dut;
      return 1;
    }
  }

  std::printf("scratchpad: ok\n");
  delete dut;
  return 0;
}
