#include <cstdio>
#include <cstdint>
#include <cstdlib>

#include "verilated.h"
#include "Vendpoint_noc.h"

static bool tick(Vendpoint_noc* dut) {
  bool saw_core_req = false;
  dut->clk = 0;
  dut->eval();
  saw_core_req = saw_core_req || static_cast<bool>(dut->rx_core_req_v_observe);
  dut->clk = 1;
  dut->eval();
  saw_core_req = saw_core_req || static_cast<bool>(dut->rx_core_req_v_observe);
  return saw_core_req;
}

static void reset_dut(Vendpoint_noc* dut, int cycles = 3) {
  dut->rst_n = 0;
  dut->send_v = 0;
  dut->send_we = 0;
  dut->send_addr = 0;
  dut->send_data = 0;
  for (int i = 0; i < cycles; ++i) {
    (void)tick(dut);
  }
  dut->rst_n = 1;
}

static bool run_write_case(Vendpoint_noc* dut,
                           uint16_t addr,
                           uint32_t data,
                           bool expect_core_forward,
                           const char* case_name) {
  dut->send_we = 1;
  dut->send_addr = addr;
  dut->send_data = data;

  bool accepted = false;
  bool saw_core_req = false;
  for (int i = 0; i < 64; ++i) {
    dut->send_v = accepted ? 0 : 1;
    saw_core_req = saw_core_req || tick(dut);
    if (dut->send_ready) {
      accepted = true;
    }
    if (dut->recv_seen) {
      break;
    }
  }

  if (!accepted) {
    std::fprintf(stderr, "endpoint_noc[%s]: sender never got ready\n", case_name);
    return false;
  }
  if (!dut->recv_seen) {
    std::fprintf(stderr, "endpoint_noc[%s]: no receive observed through NoC path\n", case_name);
    return false;
  }
  if (!dut->recv_we) {
    std::fprintf(stderr, "endpoint_noc[%s]: expected write receive\n", case_name);
    return false;
  }
  if (dut->recv_addr != addr) {
    std::fprintf(stderr,
                 "endpoint_noc[%s]: addr mismatch got 0x%04x expected 0x%04x\n",
                 case_name,
                 dut->recv_addr,
                 addr);
    return false;
  }
  if (dut->recv_data != data) {
    std::fprintf(stderr,
                 "endpoint_noc[%s]: data mismatch got 0x%08x expected 0x%08x\n",
                 case_name,
                 dut->recv_data,
                 data);
    return false;
  }
  if ((saw_core_req || static_cast<bool>(dut->recv_via_core_req)) != expect_core_forward) {
    std::fprintf(stderr,
                 "endpoint_noc[%s]: recv_via_core_req mismatch got %d expected %d\n",
                 case_name,
                 (saw_core_req || static_cast<bool>(dut->recv_via_core_req)) ? 1 : 0,
                 expect_core_forward ? 1 : 0);
    return false;
  }
  std::printf("endpoint_noc[%s]: PASS addr=0x%04x data=0x%08x\n", case_name, addr, data);
  return true;
}

static int run_invalid_class_negative_case(Vendpoint_noc* dut,
                                           uint16_t invalid_class_addr,
                                           uint32_t data) {
  dut->send_we = 1;
  dut->send_addr = invalid_class_addr;
  dut->send_data = data;

  bool accepted = false;
  bool saw_core_req = false;
  for (int i = 0; i < 64; ++i) {
    dut->send_v = accepted ? 0 : 1;
    saw_core_req = saw_core_req || tick(dut);
    if (dut->send_ready) {
      accepted = true;
    }
  }

  if (!accepted) {
    std::fprintf(stderr,
                 "endpoint_noc: NEGATIVE_INVALID_CLASS_SETUP_FAIL sender_not_ready addr=0x%04x\n",
                 invalid_class_addr);
    return 1;
  }

  const bool core_forwarded = saw_core_req || static_cast<bool>(dut->recv_via_core_req);
  std::fprintf(stderr,
               "endpoint_noc: INVALID_CLASS_REJECTED addr=0x%04x core_forwarded=%d recv_seen=%d\n",
               invalid_class_addr,
               core_forwarded ? 1 : 0,
               dut->recv_seen ? 1 : 0);
  return 2;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vendpoint_noc;

  reset_dut(dut);

  const uint16_t mmio_addr = dut->contract_mmio_addr;
  const uint16_t invalid_class_addr = dut->contract_invalid_class_addr;
  const char* negative_env = std::getenv("GBP_ENDPOINT_NOC_NEGATIVE_INVALID_CLASS");
  const bool negative_invalid_class_mode =
      (negative_env != nullptr) && (negative_env[0] != '\0') && (negative_env[0] != '0');

  if (negative_invalid_class_mode) {
    const int rc = run_invalid_class_negative_case(dut, invalid_class_addr, 0xA5A55A5Au);
    delete dut;
    return rc;
  }

  if (!run_write_case(dut, mmio_addr, 0x11223344u, false, "mmio_b0_local")) {
    delete dut;
    return 1;
  }

  std::printf("BASELINE_PASS endpoint_noc: mmio=0x%04x invalid_class_probe=0x%04x\n",
               mmio_addr,
               invalid_class_addr);
  delete dut;
  return 0;
}
