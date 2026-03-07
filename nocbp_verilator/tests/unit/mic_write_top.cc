#include <cstdint>
#include <cstdio>

#include "verilated.h"
#include "Vmic_write_top.h"

static void tick(Vmic_write_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void set_data32(Vmic_write_top* dut, uint32_t value) {
  for (int i = 0; i < 8; ++i) {
    dut->i_data_data[i] = 0;
  }
  dut->i_data_data[0] = value;
}

static void reset_dut(Vmic_write_top* dut, int cycles = 2) {
  dut->rst_n = 0;
  dut->i_addr_valid = 0;
  dut->i_addr_data = 0;
  dut->i_data_valid = 0;
  dut->i_spm_wr_req_ready = 0;
  set_data32(dut, 0);
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
  tick(dut);
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vmic_write_top;
  reset_dut(dut);

  std::printf("MIC Write Unit Test\n");

  std::printf("Test 1: basic FSM write flow\n");
  dut->i_spm_wr_req_ready = 1;
  dut->i_addr_valid = 1;
  dut->i_addr_data = 0x1200;
  dut->i_data_valid = 1;
  set_data32(dut, 0xA5A55A5A);
  tick(dut);

  if (!dut->o_spm_wr_req_valid) {
    std::fprintf(stderr, "FAIL: expected req_valid=1\n");
    delete dut;
    return 1;
  }
  if (dut->o_spm_wr_req_addr != 0x1200) {
    std::fprintf(stderr, "FAIL: expected req_addr=0x1200 got 0x%x\n", dut->o_spm_wr_req_addr);
    delete dut;
    return 1;
  }
  if (dut->o_spm_wr_req_data[0] != 0xA5A55A5A) {
    std::fprintf(stderr, "FAIL: expected req_data[31:0]=0xA5A55A5A got 0x%x\n", dut->o_spm_wr_req_data[0]);
    delete dut;
    return 1;
  }
  if (dut->o_spm_wr_req_wstrb != 0xFFFFFFFFu) {
    std::fprintf(stderr, "FAIL: expected full wstrb got 0x%x\n", dut->o_spm_wr_req_wstrb);
    delete dut;
    return 1;
  }

  dut->i_addr_valid = 0;
  dut->i_data_valid = 0;
  tick(dut);
  if (dut->o_spm_wr_req_valid) {
    std::fprintf(stderr, "FAIL: expected req_valid=0 after handshake\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 1\n");

  std::printf("Test 2: backpressure hold\n");
  reset_dut(dut);
  dut->i_spm_wr_req_ready = 0;
  dut->i_addr_valid = 1;
  dut->i_addr_data = 0x2200;
  dut->i_data_valid = 1;
  set_data32(dut, 0x11223344);
  tick(dut);

  if (!dut->o_spm_wr_req_valid) {
    std::fprintf(stderr, "FAIL: expected req_valid=1 under backpressure\n");
    delete dut;
    return 1;
  }
  if (dut->o_spm_wr_req_addr != 0x2200 || dut->o_spm_wr_req_data[0] != 0x11223344) {
    std::fprintf(stderr, "FAIL: expected latched request payload\n");
    delete dut;
    return 1;
  }

  dut->i_addr_data = 0x22F0;
  set_data32(dut, 0x99AA55CC);
  tick(dut);
  if (!dut->o_spm_wr_req_valid) {
    std::fprintf(stderr, "FAIL: expected req_valid held during backpressure\n");
    delete dut;
    return 1;
  }
  if (dut->o_spm_wr_req_addr != 0x2200 || dut->o_spm_wr_req_data[0] != 0x11223344) {
    std::fprintf(stderr, "FAIL: request payload changed while stalled\n");
    delete dut;
    return 1;
  }

  dut->i_spm_wr_req_ready = 1;
  tick(dut);
  if (dut->o_spm_wr_req_valid) {
    std::fprintf(stderr, "FAIL: expected req_valid=0 after stalled request accepted\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 2\n");

  std::printf("Test 3: missing input validity\n");
  reset_dut(dut);
  dut->i_spm_wr_req_ready = 1;

  dut->i_addr_valid = 1;
  dut->i_data_valid = 0;
  dut->i_addr_data = 0x3300;
  set_data32(dut, 0x55667788);
  tick(dut);
  if (dut->o_spm_wr_req_valid) {
    std::fprintf(stderr, "FAIL: expected no request when data_valid=0\n");
    delete dut;
    return 1;
  }

  dut->i_addr_valid = 0;
  dut->i_data_valid = 1;
  tick(dut);
  if (dut->o_spm_wr_req_valid) {
    std::fprintf(stderr, "FAIL: expected no request when addr_valid=0\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 3\n");

  std::printf("All tests passed\n");
  delete dut;
  return 0;
}
