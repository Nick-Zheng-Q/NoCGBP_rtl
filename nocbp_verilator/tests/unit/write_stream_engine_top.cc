#include <cstdint>
#include <cstdio>

#include "verilated.h"
#include "Vwrite_stream_engine_top.h"

static void tick(Vwrite_stream_engine_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vwrite_stream_engine_top* dut) {
  dut->rst_n = 0;
  dut->i_desc_valid = 0;
  dut->i_desc_start = 0;
  dut->i_base_addr = 0;
  dut->i_xfer_bytes = 0;
  dut->i_addr_step_bytes = 0;
  dut->i_stream_valid = 0;
  dut->i_stream_data = 0;
  dut->i_spm_wr_req_ready = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  tick(dut);
}

static bool wait_for_wr_req(Vwrite_stream_engine_top* dut, int max_cycles) {
  for (int i = 0; i < max_cycles; ++i) {
    if (dut->o_spm_wr_req_valid) {
      return true;
    }
    tick(dut);
  }
  return false;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vwrite_stream_engine_top;
  reset_dut(dut);

  std::printf("Write Stream Engine Unit Test\n");

  std::printf("Test 1: descriptor + stream produces write request\n");
  dut->i_spm_wr_req_ready = 1;
  dut->i_desc_valid = 1;
  dut->i_desc_start = 1;
  dut->i_base_addr = 0x200;
  dut->i_xfer_bytes = 32;
  dut->i_addr_step_bytes = 32;
  dut->i_stream_valid = 1;
  dut->i_stream_data = 0xA5A55A5A;
  tick(dut);

  dut->i_desc_valid = 0;
  dut->i_desc_start = 0;

  if (!wait_for_wr_req(dut, 20)) {
    std::fprintf(stderr, "FAIL: expected write request\n");
    delete dut;
    return 1;
  }
  if (dut->o_spm_wr_req_addr != 0x200) {
    std::fprintf(stderr, "FAIL: expected wr addr 0x200 got 0x%x\n", dut->o_spm_wr_req_addr);
    delete dut;
    return 1;
  }
  if (dut->o_spm_wr_req_data[0] != 0xA5A55A5A) {
    std::fprintf(stderr, "FAIL: expected wr data[31:0]=0xA5A55A5A got 0x%x\n", dut->o_spm_wr_req_data[0]);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 1\n");

  std::printf("Test 2: backpressure holds request\n");
  reset_dut(dut);
  dut->i_spm_wr_req_ready = 0;
  dut->i_desc_valid = 1;
  dut->i_desc_start = 1;
  dut->i_base_addr = 0x280;
  dut->i_xfer_bytes = 32;
  dut->i_addr_step_bytes = 32;
  dut->i_stream_valid = 1;
  dut->i_stream_data = 0x11223344;
  tick(dut);
  dut->i_desc_valid = 0;
  dut->i_desc_start = 0;

  if (!wait_for_wr_req(dut, 20)) {
    std::fprintf(stderr, "FAIL: expected request while backpressured\n");
    delete dut;
    return 1;
  }
  if (dut->o_spm_wr_req_addr != 0x280) {
    std::fprintf(stderr, "FAIL: expected stalled addr 0x280 got 0x%x\n", dut->o_spm_wr_req_addr);
    delete dut;
    return 1;
  }

  for (int i = 0; i < 3; ++i) {
    tick(dut);
    if (!dut->o_spm_wr_req_valid) {
      std::fprintf(stderr, "FAIL: expected req_valid held under backpressure\n");
      delete dut;
      return 1;
    }
  }

  dut->i_spm_wr_req_ready = 1;
  tick(dut);
  tick(dut);
  if (dut->o_spm_wr_req_valid) {
    std::fprintf(stderr, "FAIL: expected req_valid drop after accept\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 2\n");

  std::printf("Test 3: no write request without stream data\n");
  reset_dut(dut);
  dut->i_spm_wr_req_ready = 1;
  dut->i_desc_valid = 1;
  dut->i_desc_start = 1;
  dut->i_base_addr = 0x300;
  dut->i_xfer_bytes = 32;
  dut->i_addr_step_bytes = 32;
  dut->i_stream_valid = 0;
  tick(dut);
  dut->i_desc_valid = 0;
  dut->i_desc_start = 0;

  bool saw_req = wait_for_wr_req(dut, 15);
  if (saw_req) {
    std::fprintf(stderr, "FAIL: unexpected write request without stream data\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 3\n");

  std::printf("All tests passed\n");
  delete dut;
  return 0;
}
