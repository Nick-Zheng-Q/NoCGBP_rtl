#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vmic_read_top.h"

static void tick(Vmic_read_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void clear_rsp(Vmic_read_top* dut) {
  dut->i_spm_rd_rsp_valid = 0;
  for (int i = 0; i < 8; ++i) {
    dut->i_spm_rd_rsp_data[i] = 0;
  }
}

static void set_rsp32(Vmic_read_top* dut, uint32_t value) {
  clear_rsp(dut);
  dut->i_spm_rd_rsp_valid = 1;
  dut->i_spm_rd_rsp_data[0] = value;
}

static void reset_dut(Vmic_read_top* dut, int cycles = 2) {
  dut->rst_n = 0;
  dut->i_addr_valid = 0;
  dut->i_addr_data = 0;
  dut->i_data_ready = 0;
  clear_rsp(dut);
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
  tick(dut);
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vmic_read_top;
  reset_dut(dut);

  std::printf("MIC Read Unit Test\n");

  std::printf("Test 1: basic request-response flow\n");
  dut->i_data_ready = 1;
  dut->i_addr_valid = 1;
  dut->i_addr_data = 0x1000;
  clear_rsp(dut);
  tick(dut);
  if (dut->o_spm_rd_req_addr != 0x1000) {
    std::fprintf(stderr, "FAIL: expected req_addr=0x1000 got 0x%x\n", dut->o_spm_rd_req_addr);
    delete dut;
    return 1;
  }

  dut->i_addr_valid = 0;
  tick(dut);
  set_rsp32(dut, 0xDEADBEEF);
  tick(dut);
  if (!dut->o_data_valid) {
    std::fprintf(stderr, "FAIL: expected o_data_valid after response\n");
    delete dut;
    return 1;
  }

  clear_rsp(dut);
  tick(dut);
  if (dut->o_data_valid) {
    std::fprintf(stderr, "FAIL: expected o_data_valid=0 after consume\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 1\n");

  std::printf("Test 2: no request when addr invalid\n");
  reset_dut(dut);
  dut->i_data_ready = 1;
  dut->i_addr_valid = 0;
  clear_rsp(dut);
  tick(dut);
  if (dut->o_spm_rd_req_valid || dut->o_addr_unqueue) {
    std::fprintf(stderr, "FAIL: expected no request/unqueue in IDLE with invalid addr\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 2\n");

  std::printf("Test 3: fifo backpressure hold\n");
  reset_dut(dut);
  dut->i_data_ready = 0;
  dut->i_addr_valid = 1;
  dut->i_addr_data = 0x2000;
  clear_rsp(dut);
  tick(dut);

  dut->i_addr_valid = 0;
  tick(dut);
  set_rsp32(dut, 0xCAFEBABE);
  tick(dut);
  if (!dut->o_data_valid) {
    std::fprintf(stderr, "FAIL: expected o_data_valid entering stall\n");
    delete dut;
    return 1;
  }

  clear_rsp(dut);
  tick(dut);
  if (!dut->o_data_valid) {
    std::fprintf(stderr, "FAIL: expected o_data_valid held during stall\n");
    delete dut;
    return 1;
  }

  dut->i_data_ready = 1;
  tick(dut);
  if (dut->o_data_valid) {
    std::fprintf(stderr, "FAIL: expected o_data_valid drop after ready\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 3\n");

  std::printf("All tests passed\n");
  delete dut;
  return 0;
}
