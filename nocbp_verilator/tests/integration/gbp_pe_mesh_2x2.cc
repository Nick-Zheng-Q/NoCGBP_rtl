#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vgbp_pe_mesh_2x2.h"

static constexpr uint32_t kRowBytesLg = 5;
static constexpr uint32_t kMmioBankB0 = 0;
static constexpr uint32_t kPayloadBankB4 = 4;
static constexpr uint32_t kCreditMetaField = 0;
static constexpr uint32_t kTailField = 3;
static constexpr uint32_t kDoorbellField = 5;

static void tick(Vgbp_pe_mesh_2x2* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe_mesh_2x2* dut) {
  dut->rst_n = 0;
  dut->send_v = 0;
  dut->send_we = 0;
  dut->send_addr = 0;
  dut->send_data = 0;
  dut->force_stall_i = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
}

static bool issue_req(Vgbp_pe_mesh_2x2* dut,
                      bool we,
                      uint32_t addr,
                      uint32_t data,
                      int max_cycles,
                      bool* stalled_before_accept) {
  dut->send_we = we ? 1 : 0;
  dut->send_addr = addr;
  dut->send_data = data;
  bool stalled = false;
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    dut->send_v = 1;
    tick(dut);
    if (dut->send_ready) {
      dut->send_v = 0;
      if (stalled_before_accept != nullptr) {
        *stalled_before_accept = stalled;
      }
      return true;
    }
    stalled = true;
  }
  dut->send_v = 0;
  if (stalled_before_accept != nullptr) {
    *stalled_before_accept = stalled;
  }
  return false;
}

static bool wait_for_decode_error(Vgbp_pe_mesh_2x2* dut, int max_cycles) {
  if (dut->decode_error_seen_o) {
    return true;
  }
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    tick(dut);
    if (dut->decode_error_seen_o) {
      return true;
    }
  }
  return false;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_pe_mesh_2x2;

  const char* negative_env = std::getenv("GBP_PE_MESH_EXPECT_ORDER_ERROR");
  const bool expect_order_error =
      (negative_env != nullptr) && (negative_env[0] != '\0') && (negative_env[0] != '0');
  const char* stall_env = std::getenv("GBP_PE_MESH_FORCE_STALL");
  const bool force_stall =
      (stall_env != nullptr) && (stall_env[0] != '\0') && (stall_env[0] != '0');

  reset_dut(dut);
  dut->force_stall_i = force_stall ? 1 : 0;

  const uint32_t mmio_meta_addr = (kMmioBankB0 << kRowBytesLg) + (kCreditMetaField << 2);
  const uint32_t mmio_meta_data = 0x00000001u;
  const uint32_t payload_addr = (kPayloadBankB4 << kRowBytesLg);
  const uint32_t payload_data = 0xC0FFEE11u;
  const uint32_t mmio_tail_addr = (kMmioBankB0 << kRowBytesLg) + (kTailField << 2);
  const uint32_t mmio_tail_data = 0x00000001u;
  const uint32_t mmio_doorbell_addr = (kMmioBankB0 << kRowBytesLg) + (kDoorbellField << 2);
  const uint32_t mmio_doorbell_data = (0x51u << 8) | (0x1u << 1) | 0x1u;
  bool stalled_before_accept = false;
  bool saw_stall_recovery = false;

  if (!issue_req(dut, true, mmio_meta_addr, mmio_meta_data, 256, &stalled_before_accept)) {
    std::fprintf(stderr,
                 "gbp_pe_mesh_2x2: RED_PHASE_MISSING_INTEGRATION src=(0,0) dst=(1,1) step=credit_meta addr=0x%05x\n",
                 mmio_meta_addr);
    delete dut;
    return 1;
  }
  saw_stall_recovery = saw_stall_recovery || stalled_before_accept;

  if (!issue_req(dut, true, payload_addr, payload_data, 256, &stalled_before_accept)) {
    std::fprintf(stderr,
                 "gbp_pe_mesh_2x2: RED_PHASE_MISSING_INTEGRATION src=(0,0) dst=(1,1) step=payload addr=0x%05x\n",
                 payload_addr);
    delete dut;
    return 1;
  }
  saw_stall_recovery = saw_stall_recovery || stalled_before_accept;

  if (!expect_order_error) {
    if (!issue_req(dut, true, mmio_tail_addr, mmio_tail_data, 256, &stalled_before_accept)) {
      std::fprintf(stderr,
                   "gbp_pe_mesh_2x2: RED_PHASE_MISSING_INTEGRATION src=(0,0) dst=(1,1) step=tail addr=0x%05x\n",
                   mmio_tail_addr);
      delete dut;
      return 1;
    }
    saw_stall_recovery = saw_stall_recovery || stalled_before_accept;
  }

  if (!issue_req(dut, true, mmio_doorbell_addr, mmio_doorbell_data, 256, &stalled_before_accept)) {
    std::fprintf(stderr,
                 "gbp_pe_mesh_2x2: RED_PHASE_MISSING_INTEGRATION src=(0,0) dst=(1,1) step=doorbell addr=0x%05x\n",
                 mmio_doorbell_addr);
    delete dut;
    return 1;
  }
  saw_stall_recovery = saw_stall_recovery || stalled_before_accept;

  for (int i = 0; i < 32; ++i) {
    tick(dut);
  }

  if (expect_order_error) {
    if (wait_for_decode_error(dut, 96)) {
      std::fprintf(stderr,
                   "gbp_pe_mesh_2x2: ORDERING_ERROR_MARKER doorbell_before_tail src=(0,0) dst=(1,1)\n");
      delete dut;
      return 2;
    }
    std::fprintf(stderr,
                 "gbp_pe_mesh_2x2: FAIL expected_decode_error_not_observed src=(0,0) dst=(1,1)\n");
    delete dut;
    return 1;
  }

  if (!dut->link_activity_o || !dut->horizontal_activity_o || !dut->vertical_activity_o) {
    std::fprintf(stderr,
                 "gbp_pe_mesh_2x2: RED_PHASE_MISSING_INTEGRATION src=(0,0) dst=(1,1) reason=missing_non_neighbor_mesh_hops\n");
    delete dut;
    return 1;
  }

  if (force_stall && !saw_stall_recovery) {
    std::fprintf(stderr,
                 "gbp_pe_mesh_2x2: FAIL forced_stall_did_not_backpressure_src=(0,0)_dst=(1,1)\n");
    delete dut;
    return 1;
  }

  if (force_stall) {
    std::printf("gbp_pe_mesh_2x2: PASS src=(0,0) dst=(1,1) qid=0 data=0x%08x recovered_from_stall=1\n",
                payload_data);
  } else {
    std::printf("gbp_pe_mesh_2x2: PASS src=(0,0) dst=(1,1) qid=0 data=0x%08x\n", payload_data);
  }
  delete dut;
  return 0;
}
