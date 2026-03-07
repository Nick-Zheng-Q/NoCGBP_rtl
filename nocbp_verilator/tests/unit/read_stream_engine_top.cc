#include <cstdint>
#include <cstdio>

#include "verilated.h"
#include "Vread_stream_engine_top.h"

static void tick(Vread_stream_engine_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void set_rsp32(Vread_stream_engine_top* dut, uint32_t value) {
  for (int i = 0; i < 8; ++i) {
    dut->i_spm_rd_rsp_data[i] = 0;
  }
  dut->i_spm_rd_rsp_data[0] = value;
}

static void reset_dut(Vread_stream_engine_top* dut) {
  dut->rst_n = 0;
  dut->i_desc_valid = 0;
  dut->i_desc_start = 0;
  dut->i_base_addr = 0;
  dut->i_xfer_bytes = 0;
  dut->i_addr_step_bytes = 0;
  dut->i_spm_rd_rsp_valid = 0;
  set_rsp32(dut, 0);
  dut->i_stream_ready = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  tick(dut);
}

static bool wait_for_req(Vread_stream_engine_top* dut, int max_cycles) {
  for (int i = 0; i < max_cycles; ++i) {
    if (dut->o_spm_rd_req_valid) {
      return true;
    }
    tick(dut);
  }
  return false;
}

static bool wait_for_stream_valid(Vread_stream_engine_top* dut, int max_cycles) {
  for (int i = 0; i < max_cycles; ++i) {
    if (dut->o_stream_valid) {
      return true;
    }
    tick(dut);
  }
  return false;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vread_stream_engine_top;
  reset_dut(dut);

  std::printf("Read Stream Engine Unit Test\n");

  std::printf("Test 1: descriptor accepted and read request issued\n");
  dut->i_desc_valid = 1;
  dut->i_desc_start = 1;
  dut->i_base_addr = 0x100;
  dut->i_xfer_bytes = 32;
  dut->i_addr_step_bytes = 32;
  dut->i_stream_ready = 0;
  dut->i_spm_rd_rsp_valid = 0;
  tick(dut);

  dut->i_desc_valid = 0;
  dut->i_desc_start = 0;

  if (!wait_for_req(dut, 20)) {
    std::fprintf(stderr, "FAIL: expected o_spm_rd_req_valid=1\n");
    delete dut;
    return 1;
  }
  if (dut->o_spm_rd_req_addr != 0x100) {
    std::fprintf(stderr, "FAIL: expected req addr 0x100 got 0x%x\n", dut->o_spm_rd_req_addr);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 1\n");

  std::printf("Test 2: response forwarded to stream output\n");
  for (int i = 0; i < 3; ++i) {
    dut->i_spm_rd_rsp_valid = 0;
    tick(dut);
  }
  dut->i_spm_rd_rsp_valid = 1;
  set_rsp32(dut, 0xDEADBEEF);
  tick(dut);
  dut->i_spm_rd_rsp_valid = 0;

  if (!wait_for_stream_valid(dut, 30)) {
    std::fprintf(stderr, "FAIL: expected stream valid after response\n");
    delete dut;
    return 1;
  }
  if (dut->o_stream_data != 0xDEADBEEF) {
    std::fprintf(stderr, "FAIL: expected stream data 0xDEADBEEF got 0x%x\n", dut->o_stream_data);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 2\n");

  std::printf("Test 3: stream consume clears valid\n");
  dut->i_stream_ready = 1;
  for (int i = 0; i < 5; ++i) {
    tick(dut);
  }

  if (dut->o_stream_valid != 0) {
    std::fprintf(stderr, "FAIL: expected stream valid drop after consume\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 3\n");

  std::printf("All tests passed\n");
  delete dut;
  return 0;
}
