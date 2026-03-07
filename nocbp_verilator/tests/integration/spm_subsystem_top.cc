#include <cstdint>
#include <cstdio>

#include "verilated.h"
#include "Vspm_subsystem_top.h"

static void tick(Vspm_subsystem_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vspm_subsystem_top* dut) {
  dut->rst_n = 0;
  dut->i_wr0_valid = 0;
  dut->i_wr0_addr = 0;
  for (int i = 0; i < 8; i++) dut->i_wr0_data[i] = 0;
  dut->i_wr0_wstrb = 0;
  dut->i_rd0_valid = 0;
  dut->i_rd0_addr = 0;
  dut->i_rd0_bytes = 0;

  dut->i_wr1_valid = 0;
  dut->i_wr1_addr = 0;
  for (int i = 0; i < 8; i++) dut->i_wr1_data[i] = 0;
  dut->i_wr1_wstrb = 0;
  dut->i_rd1_valid = 0;
  dut->i_rd1_addr = 0;
  dut->i_rd1_bytes = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  tick(dut);
}

static void set_wide_data0(Vspm_subsystem_top* dut, uint32_t lo32) {
  for (int i = 0; i < 8; i++) dut->i_wr0_data[i] = 0;
  dut->i_wr0_data[0] = lo32;
}

static void set_wide_data1(Vspm_subsystem_top* dut, uint32_t lo32) {
  for (int i = 0; i < 8; i++) dut->i_wr1_data[i] = 0;
  dut->i_wr1_data[0] = lo32;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vspm_subsystem_top;
  reset_dut(dut);

  std::printf("SPM Subsystem Integration Test\n");

  const uint32_t addr0 = 0x1000;
  const uint32_t addr1 = 0x1100;
  const uint32_t wr_data0 = 0xDEADBEEF;
  const uint32_t wr_data1 = 0xCAFEBABE;

  std::printf("Test 1: two-source independent write/read sanity\n");
  dut->i_wr0_valid = 1;
  dut->i_wr0_addr = addr0;
  set_wide_data0(dut, wr_data0);
  dut->i_wr0_wstrb = 0xFFFFFFFFu;
  tick(dut);
  if (!dut->o_wr0_ready) {
    std::fprintf(stderr, "FAIL: expected wr0 ready high\n");
    delete dut;
    return 1;
  }
  dut->i_wr0_valid = 0;
  tick(dut);

  dut->i_wr1_valid = 1;
  dut->i_wr1_addr = addr1;
  set_wide_data1(dut, wr_data1);
  dut->i_wr1_wstrb = 0xFFFFFFFFu;
  tick(dut);
  if (!dut->o_wr1_ready) {
    std::fprintf(stderr, "FAIL: expected wr1 ready high\n");
    delete dut;
    return 1;
  }
  dut->i_wr1_valid = 0;
  tick(dut);

  dut->i_rd0_valid = 1;
  dut->i_rd0_addr = addr0;
  dut->i_rd0_bytes = 1;
  tick(dut);
  dut->i_rd0_valid = 0;

  bool got_rsp0 = false;
  for (int cyc = 0; cyc < 10; cyc++) {
    tick(dut);
    if (dut->o_rd0_rsp_valid) {
      got_rsp0 = true;
      if (dut->o_rd0_rsp_data[0] != wr_data0) {
        std::fprintf(stderr, "FAIL: rd0 rsp mismatch got 0x%x expected 0x%x\n", dut->o_rd0_rsp_data[0], wr_data0);
        delete dut;
        return 1;
      }
      break;
    }
  }
  if (!got_rsp0) {
    std::fprintf(stderr, "FAIL: missing rd0 response\n");
    delete dut;
    return 1;
  }

  dut->i_rd1_valid = 1;
  dut->i_rd1_addr = addr1;
  dut->i_rd1_bytes = 1;
  tick(dut);
  dut->i_rd1_valid = 0;

  bool got_rsp1 = false;
  for (int cyc = 0; cyc < 10; cyc++) {
    tick(dut);
    if (dut->o_rd1_rsp_valid) {
      got_rsp1 = true;
      if (dut->o_rd1_rsp_data[0] != wr_data1) {
        std::fprintf(stderr, "FAIL: rd1 rsp mismatch got 0x%x expected 0x%x\n", dut->o_rd1_rsp_data[0], wr_data1);
        delete dut;
        return 1;
      }
      break;
    }
  }
  if (!got_rsp1) {
    std::fprintf(stderr, "FAIL: missing rd1 response\n");
    delete dut;
    return 1;
  }

  std::printf("PASS: Test 1\n");

  std::printf("Test 2: RR (read-read contention same bank) observation\n");
  const uint32_t same_bank_addr = 0x2000;
  int rsp0 = 0;
  int rsp1 = 0;
  dut->i_rd0_valid = 1;
  dut->i_rd0_addr = same_bank_addr;
  dut->i_rd0_bytes = 1;
  dut->i_rd1_valid = 1;
  dut->i_rd1_addr = same_bank_addr;
  dut->i_rd1_bytes = 1;
  for (int cyc = 0; cyc < 8; cyc++) {
    tick(dut);
    if (dut->o_rd0_rsp_valid) rsp0++;
    if (dut->o_rd1_rsp_valid) rsp1++;
  }
  dut->i_rd0_valid = 0;
  dut->i_rd1_valid = 0;
  for (int cyc = 0; cyc < 4; cyc++) {
    tick(dut);
    if (dut->o_rd0_rsp_valid) rsp0++;
    if (dut->o_rd1_rsp_valid) rsp1++;
  }
  std::printf("  RR result: rd0_rsp=%d rd1_rsp=%d\n", rsp0, rsp1);

  std::printf("Test 3: WW (write-write contention same bank) observation\n");
  dut->i_wr0_valid = 1;
  dut->i_wr0_addr = same_bank_addr;
  set_wide_data0(dut, 0x11111111);
  dut->i_wr0_wstrb = 0xFFFFFFFFu;

  dut->i_wr1_valid = 1;
  dut->i_wr1_addr = same_bank_addr;
  set_wide_data1(dut, 0x22222222);
  dut->i_wr1_wstrb = 0xFFFFFFFFu;

  int wr0_ready = 0;
  int wr1_ready = 0;
  for (int cyc = 0; cyc < 4; cyc++) {
    tick(dut);
    if (dut->o_wr0_ready) wr0_ready++;
    if (dut->o_wr1_ready) wr1_ready++;
  }
  dut->i_wr0_valid = 0;
  dut->i_wr1_valid = 0;
  std::printf("  WW result: wr0_ready=%d wr1_ready=%d\n", wr0_ready, wr1_ready);

  std::printf("PASS: Test 2/3 observed (see RR/WW counters)\n");
  std::printf("All integration tests passed\n");
  delete dut;
  return 0;
}
