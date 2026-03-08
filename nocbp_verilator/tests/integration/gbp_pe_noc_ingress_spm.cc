#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vgbp_pe_noc_ingress_spm.h"

static constexpr uint32_t kRowBytesLg = 5;
static constexpr uint32_t kMmioBankB0 = 0;
static constexpr uint32_t kPayloadBankB4 = 4;
static constexpr uint32_t kTailField = 3;
static constexpr uint32_t kDoorbellField = 5;

static void tick(Vgbp_pe_noc_ingress_spm* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe_noc_ingress_spm* dut) {
  dut->rst_n = 0;
  dut->send_v = 0;
  dut->send_we = 0;
  dut->send_addr = 0;
  dut->send_data = 0;
  dut->ingress_stall = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
}

static bool issue_req(Vgbp_pe_noc_ingress_spm* dut,
                      bool we,
                      uint32_t addr,
                      uint32_t data,
                      int max_cycles) {
  dut->send_we = we ? 1 : 0;
  dut->send_addr = addr;
  dut->send_data = data;
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    dut->send_v = 1;
    tick(dut);
    if (dut->send_ready) {
      dut->send_v = 0;
      return true;
    }
  }
  dut->send_v = 0;
  return false;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_pe_noc_ingress_spm;

  const char* negative_env = std::getenv("GBP_PE_NOC_INGRESS_ORDER_NEGATIVE");
  const bool negative_mode =
      (negative_env != nullptr) && (negative_env[0] != '\0') && (negative_env[0] != '0');

  reset_dut(dut);

  const uint32_t mmio_tail_addr = (kMmioBankB0 << kRowBytesLg) + (kTailField << 2);
  const uint32_t mmio_tail_data = 0x1u;
  const uint32_t mmio_cmd_addr = (kMmioBankB0 << kRowBytesLg) + (kDoorbellField << 2);
  const uint32_t mmio_cmd_data = (0x51u << 8) | (0x1u << 1) | 0x1u;
  const uint32_t ingress_addr = (kPayloadBankB4 << kRowBytesLg);
  const uint32_t ingress_data = 0xC0FFEE11u;

  if (!issue_req(dut, true, ingress_addr, ingress_data, 64)) {
    std::fprintf(stderr,
                 "FAIL: ingress payload request not accepted over real gbp_pe path addr=0x%05x data=0x%08x\n",
                 ingress_addr,
                 ingress_data);
    delete dut;
    return 1;
  }

  if (!negative_mode) {
    if (!issue_req(dut, true, mmio_tail_addr, mmio_tail_data, 64)) {
      std::fprintf(stderr,
                    "FAIL: queue tail write not accepted over real gbp_pe path addr=0x%05x data=0x%08x\n",
                    mmio_tail_addr,
                    mmio_tail_data);
      delete dut;
      return 1;
    }
  }

  if (!issue_req(dut, true, mmio_cmd_addr, mmio_cmd_data, 64)) {
    std::fprintf(stderr,
                 "FAIL: doorbell write request not accepted over real gbp_pe path addr=0x%05x\n",
                 mmio_cmd_addr);
    delete dut;
    return 1;
  }

  bool lane1_seen = false;
  uint32_t lane1_addr = 0;
  uint32_t lane1_data = 0;
  for (int cycle = 0; cycle < 128; ++cycle) {
    tick(dut);
    if (dut->lane1_wr_req_valid) {
      lane1_seen = true;
      lane1_addr = dut->lane1_wr_req_addr;
      lane1_data = dut->lane1_wr_req_data;
      break;
    }
  }

  if (negative_mode) {
    if (!dut->decode_error) {
      std::fprintf(stderr,
                   "FAIL: missing decode_error for doorbell-before-tail ordering violation\n");
      delete dut;
      return 1;
    }
    if (lane1_seen) {
      std::fprintf(stderr,
                   "FAIL: lane1 write observed for invalid ordering addr=0x%05x data=0x%08x\n",
                   lane1_addr,
                   lane1_data);
      delete dut;
      return 1;
    }
    std::fprintf(stderr,
                  "gbp_pe_noc_ingress_spm: ORDERING_ERROR_MARKER doorbell_before_tail decode_error=1 real_gbp_pe=1\n");
    delete dut;
    return 2;
  }

  if (!lane1_seen) {
    std::fprintf(stderr,
                 "FAIL: dedicated ingress lane1 ordered write was not observed\n");
    delete dut;
    return 1;
  }

  if (lane1_addr != ingress_addr || lane1_data != ingress_data) {
    std::fprintf(stderr,
                 "FAIL: lane1 proof mismatch addr=0x%05x data=0x%08x expected_addr=0x%05x expected_data=0x%08x\n",
                 lane1_addr,
                 lane1_data,
                 ingress_addr,
                 ingress_data);
    delete dut;
    return 1;
  }

  if (dut->decode_error) {
    std::fprintf(stderr,
                 "FAIL: decode_error asserted during valid ordering sequence\n");
    delete dut;
    return 1;
  }

  std::printf(
      "gbp_pe_noc_ingress_spm: ORDERED_WRITE_MARKER bank=%u lane1_addr=0x%05x lane1_data=0x%08x real_gbp_pe=1\n",
      static_cast<unsigned int>(dut->ingress_bank),
      lane1_addr,
      lane1_data);

  delete dut;
  return 0;
}
