#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vgbp_pe_noc_bridge_top.h"

static constexpr uint32_t kRowBytesLg = 5;
static constexpr uint32_t kMmioBankB0 = 0;
static constexpr uint32_t kPayloadBankB4 = 4;
static constexpr uint32_t kTailField = 3;

static void tick(Vgbp_pe_noc_bridge_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe_noc_bridge_top* dut) {
  dut->rst_n = 0;
  dut->req_v = 0;
  dut->req_we = 0;
  dut->req_addr = 0;
  dut->req_data = 0;
  dut->ingress_ready_block = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
}

static int issue_req_cycles(Vgbp_pe_noc_bridge_top* dut,
                            bool we,
                            uint16_t addr,
                            uint32_t data,
                            int max_cycles);

static bool issue_req(Vgbp_pe_noc_bridge_top* dut,
                      bool we,
                      uint16_t addr,
                      uint32_t data,
                      int max_cycles) {
  return issue_req_cycles(dut, we, addr, data, max_cycles) >= 0;
}

static int issue_req_cycles(Vgbp_pe_noc_bridge_top* dut,
                            bool we,
                            uint16_t addr,
                            uint32_t data,
                            int max_cycles) {
  dut->req_we = we ? 1 : 0;
  dut->req_addr = addr;
  dut->req_data = data;
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    dut->req_v = 1;
    tick(dut);
    if (dut->req_yumi) {
      dut->req_v = 0;
      return cycle + 1;
    }
  }
  dut->req_v = 0;
  return -1;
}

static bool read_rsp(Vgbp_pe_noc_bridge_top* dut,
                     uint16_t addr,
                     uint32_t* rsp_data,
                     int max_cycles) {
  if (!issue_req(dut, false, addr, 0, max_cycles)) {
    return false;
  }
  if (dut->rsp_v) {
    *rsp_data = dut->rsp_data;
    return true;
  }
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    tick(dut);
    if (dut->rsp_v) {
      *rsp_data = dut->rsp_data;
      return true;
    }
  }
  return false;
}

static int run_invalid_decode_negative(Vgbp_pe_noc_bridge_top* dut) {
  reset_dut(dut);

  if (!issue_req(dut, true, dut->contract_invalid_addr, 0xA5A50001u, 8)) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: NEGATIVE_INVALID_SETUP_FAIL addr=0x%04x\n",
                 dut->contract_invalid_addr);
    return 1;
  }

  uint32_t status = 0;
  if (!read_rsp(dut, dut->contract_mmio_status_addr, &status, 8)) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: NEGATIVE_INVALID_STATUS_READ_FAIL addr=0x%04x\n",
                 dut->contract_mmio_status_addr);
    return 1;
  }

  const bool decode_error = ((status >> 3) & 0x1u) != 0;
  if (!decode_error) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: NEGATIVE_INVALID_DECODE_MISSING status=0x%08x\n",
                 status);
    return 1;
  }

  std::fprintf(stderr,
               "gbp_pe_noc_bridge: BRIDGE_MMIO_DECODE_ERROR addr=0x%04x status=0x%08x\n",
               dut->contract_invalid_addr,
               status);
  return 2;
}

static int run_ingress_backpressure_negative(Vgbp_pe_noc_bridge_top* dut) {
  static constexpr uint32_t kRowBytesLg = 5;
  static constexpr uint32_t kMmioBankB0 = 0;
  static constexpr uint32_t kPayloadBankB4 = 4;
  static constexpr uint32_t kTailField = 3;

  reset_dut(dut);

  const uint32_t payload_addr = (kPayloadBankB4 << kRowBytesLg);
  const uint32_t payload_data = 0xABCD0011u;
  const uint32_t mmio_tail_addr = (kMmioBankB0 << kRowBytesLg) + (kTailField << 2);
  const uint32_t mmio_tail_data = 0x1u;
  const uint32_t cmd_payload = (0x44u << 8) | (0x1u << 1) | 0x1u;

  if (!issue_req(dut, true, payload_addr, payload_data, 8)) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: INGRESS_BACKPRESSURE_PAYLOAD_SETUP_FAIL payload_addr=0x%04x\n",
                 payload_addr);
    return 1;
  }

  if (!issue_req(dut, true, mmio_tail_addr, mmio_tail_data, 8)) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: INGRESS_BACKPRESSURE_TAIL_SETUP_FAIL tail_addr=0x%04x\n",
                 mmio_tail_addr);
    return 1;
  }

  dut->ingress_ready_block = 1;
  const int blocked_cycles = issue_req_cycles(dut, true, dut->contract_mmio_cmd_addr, cmd_payload, 16);
  const bool ingress_block_observed = !dut->ingress_intent_v_observe;
  dut->ingress_ready_block = 0;

  if (blocked_cycles >= 0) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: INGRESS_BACKPRESSURE_MISSING_BLOCK blocked_cycles=%d\n",
                 blocked_cycles);
    return 1;
  }

  const int retry_cycles = issue_req_cycles(dut, true, dut->contract_mmio_cmd_addr, cmd_payload, 8);
  if (retry_cycles < 0 || !dut->sideband_cmd_seen_observe || !dut->ingress_intent_seen_observe) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: INGRESS_BACKPRESSURE_RETRY_FAIL retry_cycles=%d cmd_seen=%d ingress_seen=%d\n",
                 retry_cycles,
                 dut->sideband_cmd_seen_observe ? 1 : 0,
                 dut->ingress_intent_seen_observe ? 1 : 0);
    return 1;
  }

  if (dut->decode_error_observe) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: INGRESS_BACKPRESSURE_DECODE_ERROR\n");
    return 1;
  }

  std::printf(
      "gbp_pe_noc_bridge: INGRESS_BACKPRESSURE_MARKER blocked_cycles=16 retry_cycles=%d ingress_block_observed=%d\n",
      retry_cycles,
      ingress_block_observed ? 1 : 0);
  return 0;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_pe_noc_bridge_top;

  const char* negative_env = std::getenv("GBP_PE_NOC_BRIDGE_NEGATIVE_INVALID_DECODE");
  const bool negative_mode =
      (negative_env != nullptr) && (negative_env[0] != '\0') && (negative_env[0] != '0');
  const char* backpressure_env = std::getenv("GBP_PE_NOC_BRIDGE_NEGATIVE_INGRESS_STALL");
  const bool backpressure_mode =
      (backpressure_env != nullptr) && (backpressure_env[0] != '\0') && (backpressure_env[0] != '0');

  if (negative_mode) {
    const int rc = run_invalid_decode_negative(dut);
    delete dut;
    return rc;
  }

  if (backpressure_mode) {
    const int rc = run_ingress_backpressure_negative(dut);
    delete dut;
    return rc;
  }

  reset_dut(dut);

  const uint32_t payload_addr = (kPayloadBankB4 << kRowBytesLg);
  const uint32_t payload_data = 0xC001D00Du;
  const uint32_t mmio_tail_addr = (kMmioBankB0 << kRowBytesLg) + (kTailField << 2);
  const uint32_t mmio_tail_data = 0x1u;
  const uint32_t cmd_payload = (0x3Cu << 8) | (0x1u << 1) | 0x1u;

  if (!issue_req(dut, true, payload_addr, payload_data, 8)) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: MMIO_PAYLOAD_SETUP_FAIL payload_addr=0x%04x\n",
                 payload_addr);
    delete dut;
    return 1;
  }

  if (!issue_req(dut, true, mmio_tail_addr, mmio_tail_data, 8)) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: MMIO_TAIL_SETUP_FAIL tail_addr=0x%04x\n",
                 mmio_tail_addr);
    delete dut;
    return 1;
  }

  if (!issue_req(dut, true, dut->contract_mmio_cmd_addr, cmd_payload, 8)) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: MMIO_SETUP_FAIL cmd_addr=0x%04x\n",
                 dut->contract_mmio_cmd_addr);
    delete dut;
    return 1;
  }

  bool saw_cmd_launch = static_cast<bool>(dut->sideband_cmd_seen_observe);
  for (int cycle = 0; cycle < 4 && !saw_cmd_launch; ++cycle) {
    tick(dut);
    saw_cmd_launch = static_cast<bool>(dut->sideband_cmd_seen_observe);
  }
  if (!saw_cmd_launch) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: MMIO_CMD_NOT_ACCEPTED kind=0x%x txn=0x%02x\n",
                 static_cast<unsigned int>(dut->sideband_cmd_kind_observe),
                 static_cast<unsigned int>(dut->sideband_cmd_txn_observe));
    delete dut;
    return 1;
  }
  if (!dut->ingress_intent_seen_observe) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: MMIO_INGRESS_INTENT_NOT_OBSERVED payload_addr=0x%04x\n",
                 payload_addr);
    delete dut;
    return 1;
  }
  std::printf("gbp_pe_noc_bridge: BRIDGE_MMIO_COMMAND_ACCEPTED kind=0x%x txn=0x%02x\n",
               static_cast<unsigned int>(dut->sideband_cmd_kind_observe),
               static_cast<unsigned int>(dut->sideband_cmd_txn_observe));

  tick(dut);
  tick(dut);

  uint32_t status = 0;
  if (!read_rsp(dut, dut->contract_mmio_status_addr, &status, 8)) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: MMIO_STATUS_READ_FAIL status_addr=0x%04x\n",
                 dut->contract_mmio_status_addr);
    delete dut;
    return 1;
  }

  const bool accepted = ((status >> 0) & 0x1u) != 0;
  const bool done = ((status >> 1) & 0x1u) != 0;
  const bool decode_error = ((status >> 3) & 0x1u) != 0;
  if (!accepted || !done || decode_error) {
    std::fprintf(stderr,
                 "gbp_pe_noc_bridge: MMIO_STATUS_INVALID status=0x%08x accepted=%d done=%d decode_error=%d\n",
                 status,
                 accepted ? 1 : 0,
                 done ? 1 : 0,
                 decode_error ? 1 : 0);
    delete dut;
    return 1;
  }

  std::printf("gbp_pe_noc_bridge: BRIDGE_MMIO_STATUS_READABLE status=0x%08x\n", status);

  delete dut;
  return 0;
}
