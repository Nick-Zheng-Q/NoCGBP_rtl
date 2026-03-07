#include <cstdio>
#include <cstdint>

#include "verilated.h"
#include "Vpe_unit.h"

static void tick(Vpe_unit* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vpe_unit* dut, int cycles = 2) {
  dut->rst_n = 0;
  dut->cmd_valid_i = 0;
  dut->rsp_done_i = 0;
  dut->cmd_txn_id_i = 0;
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
}

static int fail(const char* msg) {
  std::fprintf(stderr, "pe_unit: FAIL: %s\n", msg);
  return 1;
}

static int fail_marker(const char* marker, const char* msg) {
  std::fprintf(stderr, "%s\n", marker);
  return fail(msg);
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vpe_unit;
  reset_dut(dut);

  if (!dut->cmd_ready_o) {
    delete dut;
    return fail("cmd_ready_o should be high after reset");
  }

  const uint32_t txn_a = 0x12;
  const uint32_t txn_b = 0x2A;

  dut->cmd_valid_i = 1;
  dut->cmd_txn_id_i = txn_a;
  tick(dut);

  if (!dut->compute_start_o) {
    delete dut;
    return fail("compute_start_o should pulse on command accept");
  }
  if (dut->cmd_ready_o) {
    delete dut;
    return fail("cmd_ready_o should deassert while command is pending");
  }
  if (dut->cmd_txn_id_o != txn_a) {
    delete dut;
    return fail("cmd_txn_id_o mismatch after accept");
  }

  std::printf("PE_UNIT_COUPLED_SEQ_ACCEPT_MARKER\n");

  dut->cmd_txn_id_i = txn_b;
  tick(dut);
  if (dut->compute_start_o) {
    delete dut;
    return fail_marker(
        "PE_UNIT_COUPLED_BACKPRESSURE_RESTART_MARKER",
        "compute_start_o should not retrigger while command is pending");
  }
  if (dut->compute_done_o) {
    delete dut;
    return fail_marker(
        "PE_UNIT_COUPLED_EARLY_DONE_MARKER",
        "compute_done_o must stay low before rsp_done_i");
  }
  if (dut->wr_req_valid_o) {
    delete dut;
    return fail_marker(
        "PE_UNIT_COUPLED_EARLY_WR_REQ_MARKER",
        "wr_req_valid_o must stay low before rsp_done_i");
  }
  if (dut->cmd_txn_id_o != txn_a) {
    delete dut;
    return fail_marker(
        "PE_UNIT_COUPLED_CMD_STABILITY_MARKER",
        "pending command txn should remain stable under backpressure");
  }

  for (int i = 0; i < 3; ++i) {
    tick(dut);
    if (dut->compute_done_o) {
      delete dut;
      return fail_marker(
          "PE_UNIT_COUPLED_EARLY_DONE_MARKER",
          "compute_done_o must stay low until rsp_done_i handshake");
    }
    if (dut->wr_req_valid_o) {
      delete dut;
      return fail_marker(
          "PE_UNIT_COUPLED_EARLY_WR_REQ_MARKER",
          "wr_req_valid_o must stay low until rsp_done_i handshake");
    }
    if (dut->cmd_txn_id_o != txn_a) {
      delete dut;
      return fail_marker(
          "PE_UNIT_COUPLED_CMD_STABILITY_MARKER",
          "pending transaction id changed before completion");
    }
  }

  std::printf("PE_UNIT_COUPLED_BACKPRESSURE_STABLE_MARKER\n");

  if (std::getenv("PE_UNIT_HOLD_DONE")) {
    for (int i = 0; i < 20; ++i) {
      tick(dut);
    }
    if (dut->compute_done_o) {
      delete dut;
      return fail_marker(
          "PE_UNIT_WITHHELD_DONE_EARLY_DONE_MARKER",
          "compute_done_o should not assert when rsp_done_i is withheld");
    }
    delete dut;
    return fail_marker(
        "PE_UNIT_WITHHELD_DONE_MARKER",
        "timeout: rsp_done_i withheld while command pending");
  }

  dut->rsp_done_i = 1;
  tick(dut);
  if (!dut->compute_done_o) {
    delete dut;
    return fail("compute_done_o should pulse when rsp_done_i is asserted");
  }
  if (!dut->wr_req_valid_o) {
    delete dut;
    return fail("wr_req_valid_o should pulse with completion");
  }
  if (dut->wr_txn_id_o != txn_a) {
    delete dut;
    return fail("wr_txn_id_o should match accepted transaction");
  }

  std::printf("PE_UNIT_COUPLED_COMPLETION_MARKER\n");

  dut->rsp_done_i = 0;
  dut->cmd_valid_i = 0;
  tick(dut);
  if (dut->compute_done_o) {
    delete dut;
    return fail_marker(
        "PE_UNIT_COUPLED_DONE_PULSE_WIDTH_MARKER",
        "compute_done_o should pulse for one cycle only");
  }
  if (dut->wr_req_valid_o) {
    delete dut;
    return fail_marker(
        "PE_UNIT_COUPLED_WR_PULSE_WIDTH_MARKER",
        "wr_req_valid_o should pulse for one cycle only");
  }
  if (!dut->cmd_ready_o) {
    delete dut;
    return fail("cmd_ready_o should reassert after completion");
  }

  std::printf("PE_UNIT_COUPLED_PASS_MARKER\n");
  std::printf("pe_unit: function checks passed\n");
  delete dut;
  return 0;
}
