#include <cstdint>
#include <cstdio>

#include "verilated.h"
#include "Vcontrol_unit_top.h"

static void tick(Vcontrol_unit_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vcontrol_unit_top* dut) {
  dut->rst_n = 0;
  dut->i_dispatch_ready = 0;
  dut->i_compute_done = 0;
  dut->i_compute_cmd_ready = 1;
  dut->i_compute_rsp_done = 0;
  dut->i_read_occ = 0;
  dut->i_read_afull = 0;
  dut->i_read_meta_valid = 0;
  for (int i = 0; i < 8; ++i) {
    dut->i_read_meta_data[i] = 0;
  }
  dut->i_write_occ = 0;
  dut->i_write_afull = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  tick(dut);
}

static bool wait_dispatch(Vcontrol_unit_top* dut, int max_cycles) {
  for (int i = 0; i < max_cycles; ++i) {
    if (dut->o_dispatch_valid) {
      return true;
    }
    tick(dut);
  }
  return false;
}

static bool wait_dispatch_mode(Vcontrol_unit_top* dut, uint32_t mode, int max_cycles) {
  for (int i = 0; i < max_cycles; ++i) {
    if (dut->o_dispatch_valid && dut->o_dispatch_mode == mode) {
      return true;
    }
    tick(dut);
  }
  return false;
}

static uint32_t set_bank(uint32_t addr, uint32_t bank) {
  return (addr & ~(0x7u << 5)) | ((bank & 0x7u) << 5);
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vcontrol_unit_top;
  reset_dut(dut);

  std::printf("control_unit unit test\n");

  dut->i_dispatch_ready = 1;
  if (!wait_dispatch(dut, 8)) {
    std::fprintf(stderr, "FAIL: expected META dispatch valid\n");
    delete dut;
    return 1;
  }
  if (dut->o_dispatch_mode != 0) {
    std::fprintf(stderr, "FAIL: expected META mode=0\n");
    delete dut;
    return 1;
  }
  if (dut->o_dispatch_node_address != 0x00000) {
    std::fprintf(stderr, "FAIL: expected META addr 0x00000 got 0x%x\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 1\n");

  dut->i_read_meta_data[0] = 0x0101002Au;
  dut->i_read_meta_data[1] = 0x00403000u;
  dut->i_read_meta_data[2] = 0x00805000u;
  dut->i_read_meta_data[3] = 0x00400020u;
  dut->i_read_meta_data[4] = 0x20200000u;
  dut->i_read_meta_data[5] = 0x00020003u;
  dut->i_read_meta_data[6] = 0x00000001u;
  dut->i_read_meta_data[7] = 0x00000000u;
  dut->i_read_meta_valid = 1;
  if (!wait_dispatch_mode(dut, 1, 16)) {
    std::fprintf(stderr, "FAIL: expected followup dispatch mode=1\n");
    delete dut;
    return 1;
  }
  dut->i_read_meta_valid = 0;
  if (dut->o_dispatch_node_address != 0x0423) {
    std::fprintf(stderr, "FAIL: expected STATE addr 0x0423 got 0x%x\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  if (dut->o_dispatch_xfer_bytes != 64 || dut->o_dispatch_addr_step_bytes != 32) {
    std::fprintf(stderr, "FAIL: expected STATE xfer/step 64/32 got %u/%u\n", dut->o_dispatch_xfer_bytes,
                 dut->o_dispatch_addr_step_bytes);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 2\n");

  tick(dut);
  if (!wait_dispatch_mode(dut, 2, 16)) {
    std::fprintf(stderr, "FAIL: expected second followup dispatch mode=2\n");
    delete dut;
    return 1;
  }
  if (dut->o_dispatch_node_address != 0x0885) {
    std::fprintf(stderr, "FAIL: expected MESSAGE addr 0x0885 got 0x%x\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  if (dut->o_dispatch_xfer_bytes != 32 || dut->o_dispatch_addr_step_bytes != 32) {
    std::fprintf(stderr, "FAIL: expected MESSAGE xfer/step 32/32 got %u/%u\n", dut->o_dispatch_xfer_bytes,
                 dut->o_dispatch_addr_step_bytes);
    delete dut;
    return 1;
  }

  tick(dut);
  if (!dut->o_compute_start || !dut->o_compute_cmd_valid) {
    std::fprintf(stderr, "FAIL: expected compute command valid/start after followups\n");
    delete dut;
    return 1;
  }

  dut->i_compute_cmd_ready = 0;
  const uint32_t stalled_cmd_kind = dut->o_compute_cmd_kind;
  const uint32_t stalled_cmd_node_idx = dut->o_compute_cmd_node_idx;
  const uint32_t stalled_cmd_iter0 = dut->o_compute_cmd_iter0;
  const uint32_t stalled_cmd_txn_id = dut->o_compute_cmd_txn_id;
  tick(dut);
  tick(dut);
  if (!dut->o_compute_cmd_valid || !dut->o_compute_start) {
    std::fprintf(stderr, "FAIL: expected command to remain pending while cmd_ready=0\n");
    delete dut;
    return 1;
  }
  if (dut->o_compute_cmd_kind != stalled_cmd_kind || dut->o_compute_cmd_node_idx != stalled_cmd_node_idx ||
      dut->o_compute_cmd_iter0 != stalled_cmd_iter0 || dut->o_compute_cmd_txn_id != stalled_cmd_txn_id) {
    std::fprintf(stderr,
                 "FAIL[RED_CMD_PERSIST_BACKPRESSURE_MARKER]: cmd payload changed while cmd_ready=0 (kind/node/iter0/txn %u/%u/%u/%u -> %u/%u/%u/%u)\n",
                 stalled_cmd_kind, stalled_cmd_node_idx, stalled_cmd_iter0, stalled_cmd_txn_id,
                 dut->o_compute_cmd_kind, dut->o_compute_cmd_node_idx, dut->o_compute_cmd_iter0,
                 dut->o_compute_cmd_txn_id);
    delete dut;
    return 1;
  }

  dut->i_dispatch_ready = 0;
  tick(dut);
  dut->i_dispatch_ready = 1;
  tick(dut);
  if (!dut->o_dispatch_valid) {
    std::fprintf(stderr,
                 "FAIL[RED_INVALID_DISPATCH_EDGE_MARKER]: expected invalid dispatch_ready edge to be flagged by dispatch_valid\n");
    delete dut;
    return 1;
  }

  dut->i_compute_done = 1;
  tick(dut);
  dut->i_compute_done = 0;
  if (wait_dispatch_mode(dut, 0, 4)) {
    std::fprintf(stderr, "FAIL: legacy done must not trigger next META dispatch\n");
    delete dut;
    return 1;
  }

  dut->i_compute_cmd_ready = 1;
  tick(dut);
  if (dut->o_compute_cmd_valid || dut->o_compute_start) {
    std::fprintf(stderr, "FAIL: expected command valid/start to clear after cmd handshake\n");
    delete dut;
    return 1;
  }
  if (wait_dispatch_mode(dut, 0, 4)) {
    std::fprintf(stderr,
                 "FAIL[RED_NO_PROGRESS_BEFORE_RSP_DONE_MARKER]: expected no META dispatch before rsp_done\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 3\n");

  dut->i_compute_rsp_done = 1;
  tick(dut);
  dut->i_compute_rsp_done = 0;
  if (!wait_dispatch_mode(dut, 0, 8)) {
    std::fprintf(stderr, "FAIL: expected META dispatch after rsp_done\n");
    delete dut;
    return 1;
  }

  std::printf("PASS: Test 4\n");

  // Test 5: Zero/edge transfer case - minimal legal field values
  // meta_word0: txn_id=0x00
  // meta_word1: state_addr=0x00000 (zero), bank_hint=0
  // meta_word2: message_addr=0x00000 (zero), bank_hint=0
  // meta_word3: state_xfer_bytes=0x0000, message_xfer_bytes=0x0000 (zero edge)
  // meta_word4: state_step_bytes=0x00, message_step_bytes=0x00 (zero edge)
  dut->i_read_meta_data[0] = 0x00000000u;
  dut->i_read_meta_data[1] = 0x00000000u;
  dut->i_read_meta_data[2] = 0x00000000u;
  dut->i_read_meta_data[3] = 0x00000000u;
  dut->i_read_meta_data[4] = 0x00000000u;
  dut->i_read_meta_data[5] = 0x00000000u;
  dut->i_read_meta_data[6] = 0x00000000u;
  dut->i_read_meta_data[7] = 0x00000000u;
  dut->i_read_meta_valid = 1;
  if (!wait_dispatch_mode(dut, 1, 16)) {
    std::fprintf(stderr, "FAIL: expected followup dispatch mode=1 (zero case)\n");
    delete dut;
    return 1;
  }
  dut->i_read_meta_valid = 0;
  // Zero base addr maps to bank 1 after force (default bank_hint=0 -> bank 1)
  if (dut->o_dispatch_node_address != 0x00020) {
    std::fprintf(stderr, "FAIL: expected zero STATE addr 0x00020 got 0x%x\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  // Zero transfer/step bytes
  if (dut->o_dispatch_xfer_bytes != 0 || dut->o_dispatch_addr_step_bytes != 0) {
    std::fprintf(stderr, "FAIL: expected zero xfer/step 0/0 got %u/%u\n", dut->o_dispatch_xfer_bytes,
                 dut->o_dispatch_addr_step_bytes);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 5\n");

  // Advance to MESSAGE followup for zero case
  tick(dut);
  if (!wait_dispatch_mode(dut, 2, 16)) {
    std::fprintf(stderr, "FAIL: expected MESSAGE followup mode=2 (zero case)\n");
    delete dut;
    return 1;
  }
  if (dut->o_dispatch_node_address != 0x00080) {
    std::fprintf(stderr, "FAIL: expected zero MESSAGE addr 0x00080 got 0x%x\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  if (dut->o_dispatch_xfer_bytes != 0 || dut->o_dispatch_addr_step_bytes != 0) {
    std::fprintf(stderr, "FAIL: expected zero MESSAGE xfer/step 0/0 got %u/%u\n", dut->o_dispatch_xfer_bytes,
                 dut->o_dispatch_addr_step_bytes);
    delete dut;
    return 1;
  }

  // Wait for compute and rsp_done before next test
  tick(dut);
  if (!dut->o_compute_start || !dut->o_compute_cmd_valid) {
    std::fprintf(stderr, "FAIL: expected compute command valid/start after zero followups\n");
    delete dut;
    return 1;
  }
  dut->i_compute_cmd_ready = 1;
  tick(dut);
  if (dut->o_compute_cmd_valid || dut->o_compute_start) {
    std::fprintf(stderr, "FAIL: expected command to clear after handshake (zero case)\n");
    delete dut;
    return 1;
  }
  dut->i_compute_rsp_done = 1;
  tick(dut);
  dut->i_compute_rsp_done = 0;
  if (!wait_dispatch_mode(dut, 0, 8)) {
    std::fprintf(stderr, "FAIL: expected META dispatch after rsp_done (zero case)\n");
    delete dut;
    return 1;
  }

  // Test 6: Max-range legal field case
  // meta_word0: txn_id=0xFF
  // meta_word1: state_addr=0xFFFFF (max 20-bit), bank_hint=0
  // meta_word2: message_addr=0xFFFFF (max 20-bit), bank_hint=0
  // meta_word3: state_xfer_bytes=0xFFFF, message_xfer_bytes=0xFFFF (max 16-bit)
  // meta_word4: state_step_bytes=0xFF, message_step_bytes=0xFF (max 8-bit)
  dut->i_read_meta_data[0] = 0x000000FFu;
  dut->i_read_meta_data[1] = 0xFFFFF000u;  // [31:12]=0xFFFFF, [11:9]=0x0 (bank_hint=0)
  dut->i_read_meta_data[2] = 0xFFFFF000u;  // [31:12]=0xFFFFF, [11:9]=0x0 (bank_hint=0)
  dut->i_read_meta_data[3] = 0xFFFFFFFFu; // state_xfer=0xFFFF, message_xfer=0xFFFF
  dut->i_read_meta_data[4] = 0xFFFF0000u;  // state_step=0xFF, message_step=0xFF
  dut->i_read_meta_data[5] = 0x00000000u;
  dut->i_read_meta_data[6] = 0x00000000u;
  dut->i_read_meta_data[7] = 0x00000000u;
  dut->i_read_meta_valid = 1;
  if (!wait_dispatch_mode(dut, 1, 16)) {
    std::fprintf(stderr, "FAIL: expected followup dispatch mode=1 (max case)\n");
    delete dut;
    return 1;
  }
  dut->i_read_meta_valid = 0;
  // Check max address: base 0xFFFFF with bank force to 1 (bank_hint=0 maps to bank 1)
  if (dut->o_dispatch_node_address != 0xfff3f) {
    std::fprintf(stderr, "FAIL: expected max STATE addr 0xfff3f got 0x%x\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  // Max transfer/step bytes
  if (dut->o_dispatch_xfer_bytes != 0xFFFF || dut->o_dispatch_addr_step_bytes != 0xFF) {
    std::fprintf(stderr, "FAIL: expected max xfer/step 65535/255 got %u/%u\n", dut->o_dispatch_xfer_bytes,
                 dut->o_dispatch_addr_step_bytes);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 6\n");

  reset_dut(dut);
  dut->i_dispatch_ready = 1;

  // Test 7: STATE bank mapping - bank_hint=1 maps to bank 2
  // meta_word1: state_addr=0x00000, bank_hint=1
  dut->i_read_meta_data[0] = 0x00000000u;
  dut->i_read_meta_data[1] = 0x00000200u;  // [31:12]=0x00000, [11:9]=1 (bank_hint=1)
  dut->i_read_meta_data[2] = 0x00000000u;
  dut->i_read_meta_data[3] = 0x00000000u;
  dut->i_read_meta_data[4] = 0x00000000u;
  dut->i_read_meta_data[5] = 0x00000000u;
  dut->i_read_meta_data[6] = 0x00000000u;
  dut->i_read_meta_data[7] = 0x00000000u;
  dut->i_read_meta_valid = 1;
  if (!wait_dispatch_mode(dut, 1, 16)) {
    std::fprintf(stderr, "FAIL: expected STATE followup dispatch mode=1 (bank_hint=1)\n");
    delete dut;
    return 1;
  }
  dut->i_read_meta_valid = 0;
  // bank_hint=1 maps to bank 2 -> address 0x00040
  if (dut->o_dispatch_node_address != 0x00040) {
    std::fprintf(stderr, "FAIL: expected STATE bank 2 addr 0x00040 got 0x%x (bank_hint=1)\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 7a\n");

  reset_dut(dut);
  dut->i_dispatch_ready = 1;

  // Test 7b: STATE bank mapping - bank_hint=2 maps to bank 3
  dut->i_read_meta_data[0] = 0x00000000u;
  dut->i_read_meta_data[1] = 0x00000400u;  // [31:12]=0x00000, [11:9]=2 (bank_hint=2)
  dut->i_read_meta_data[2] = 0x00000000u;
  dut->i_read_meta_data[3] = 0x00000000u;
  dut->i_read_meta_data[4] = 0x00000000u;
  dut->i_read_meta_data[5] = 0x00000000u;
  dut->i_read_meta_data[6] = 0x00000000u;
  dut->i_read_meta_data[7] = 0x00000000u;
  dut->i_read_meta_valid = 1;
  if (!wait_dispatch_mode(dut, 1, 16)) {
    std::fprintf(stderr, "FAIL: expected STATE followup dispatch mode=1 (bank_hint=2)\n");
    delete dut;
    return 1;
  }
  dut->i_read_meta_valid = 0;
  // bank_hint=2 maps to bank 3 -> address 0x00060
  if (dut->o_dispatch_node_address != 0x00060) {
    std::fprintf(stderr, "FAIL: expected STATE bank 3 addr 0x00060 got 0x%x (bank_hint=2)\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 7b\n");

  reset_dut(dut);
  dut->i_dispatch_ready = 1;

  // Test 7c: STATE bank mapping - bank_hint=0 maps to bank 1 (default)
  dut->i_read_meta_data[0] = 0x00000000u;
  dut->i_read_meta_data[1] = 0x00000000u;  // [31:12]=0x00000, [11:9]=0 (bank_hint=0)
  dut->i_read_meta_data[2] = 0x00000000u;
  dut->i_read_meta_data[3] = 0x00000000u;
  dut->i_read_meta_data[4] = 0x00000000u;
  dut->i_read_meta_data[5] = 0x00000000u;
  dut->i_read_meta_data[6] = 0x00000000u;
  dut->i_read_meta_data[7] = 0x00000000u;
  dut->i_read_meta_valid = 1;
  if (!wait_dispatch_mode(dut, 1, 16)) {
    std::fprintf(stderr, "FAIL: expected STATE followup dispatch mode=1 (bank_hint=0)\n");
    delete dut;
    return 1;
  }
  dut->i_read_meta_valid = 0;
  // bank_hint=0 maps to bank 1 -> address 0x00020
  if (dut->o_dispatch_node_address != 0x00020) {
    std::fprintf(stderr, "FAIL: expected STATE bank 1 addr 0x00020 got 0x%x (bank_hint=0)\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 7c\n");

  // Advance through MESSAGE followup and compute to reach next test
  tick(dut);
  if (!wait_dispatch_mode(dut, 2, 16)) {
    std::fprintf(stderr, "FAIL: expected MESSAGE followup after STATE (bank test)\n");
    delete dut;
    return 1;
  }
  tick(dut);
  if (!dut->o_compute_start || !dut->o_compute_cmd_valid) {
    std::fprintf(stderr, "FAIL: expected compute command after STATE/MESSAGE followups\n");
    delete dut;
    return 1;
  }
  dut->i_compute_cmd_ready = 1;
  tick(dut);
  dut->i_compute_rsp_done = 1;
  tick(dut);
  dut->i_compute_rsp_done = 0;
  if (!wait_dispatch_mode(dut, 0, 8)) {
    std::fprintf(stderr, "FAIL: expected META dispatch after rsp_done (bank test)\n");
    delete dut;
    return 1;
  }

  // Test 8: MESSAGE bank mapping - low2 hints (bank_hint[1:0])
  // Test 8a: bank_hint[1:0]=1 maps to bank 5
  dut->i_read_meta_data[0] = 0x00000000u;
  dut->i_read_meta_data[1] = 0x00000000u;  // state_addr=0, bank_hint=0
  dut->i_read_meta_data[2] = 0x00000200u;  // [31:12]=0x00000, [11:9]=1 (bank_hint=1 -> low2=1)
  dut->i_read_meta_data[3] = 0x00000000u;
  dut->i_read_meta_data[4] = 0x00000000u;
  dut->i_read_meta_data[5] = 0x00000000u;
  dut->i_read_meta_data[6] = 0x00000000u;
  dut->i_read_meta_data[7] = 0x00000000u;
  dut->i_read_meta_valid = 1;
  if (!wait_dispatch_mode(dut, 1, 16)) {
    std::fprintf(stderr, "FAIL: expected STATE followup dispatch mode=1 (msg bank test)\n");
    delete dut;
    return 1;
  }
  dut->i_read_meta_valid = 0;
  // Skip STATE check, advance to MESSAGE
  tick(dut);
  if (!wait_dispatch_mode(dut, 2, 16)) {
    std::fprintf(stderr, "FAIL: expected MESSAGE followup mode=2 (low2=1)\n");
    delete dut;
    return 1;
  }
  // low2=1 maps to bank 5 -> address 0x000A0 (5*0x20)
  if (dut->o_dispatch_node_address != 0x000a0) {
    std::fprintf(stderr, "FAIL: expected MESSAGE bank 5 addr 0x000a0 got 0x%x (low2=1)\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 8a\n");

  // Test 8b: bank_hint[1:0]=2 maps to bank 6
  // Need to advance to META first
  tick(dut);
  if (!dut->o_compute_start || !dut->o_compute_cmd_valid) {
    std::fprintf(stderr, "FAIL: expected compute command valid\n");
    delete dut;
    return 1;
  }
  dut->i_compute_cmd_ready = 1;
  tick(dut);
  dut->i_compute_rsp_done = 1;
  tick(dut);
  dut->i_compute_rsp_done = 0;
  if (!wait_dispatch_mode(dut, 0, 8)) {
    std::fprintf(stderr, "FAIL: expected META dispatch after rsp_done\n");
    delete dut;
    return 1;
  }

  dut->i_read_meta_data[0] = 0x00000000u;
  dut->i_read_meta_data[1] = 0x00000000u;
  dut->i_read_meta_data[2] = 0x00000400u;  // bank_hint=2 -> low2=2
  dut->i_read_meta_data[3] = 0x00000000u;
  dut->i_read_meta_data[4] = 0x00000000u;
  dut->i_read_meta_data[5] = 0x00000000u;
  dut->i_read_meta_data[6] = 0x00000000u;
  dut->i_read_meta_data[7] = 0x00000000u;
  dut->i_read_meta_valid = 1;
  if (!wait_dispatch_mode(dut, 1, 16)) {
    std::fprintf(stderr, "FAIL: expected STATE followup dispatch mode=1\n");
    delete dut;
    return 1;
  }
  dut->i_read_meta_valid = 0;
  tick(dut);
  if (!wait_dispatch_mode(dut, 2, 16)) {
    std::fprintf(stderr, "FAIL: expected MESSAGE followup mode=2 (low2=2)\n");
    delete dut;
    return 1;
  }
  // low2=2 maps to bank 6 -> address 0x000C0 (6*0x20)
  if (dut->o_dispatch_node_address != 0x000c0) {
    std::fprintf(stderr, "FAIL: expected MESSAGE bank 6 addr 0x000c0 got 0x%x (low2=2)\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 8b\n");

  // Test 8c: bank_hint[1:0]=3 maps to bank 7
  // Advance to META
  tick(dut);
  if (!dut->o_compute_start || !dut->o_compute_cmd_valid) {
    std::fprintf(stderr, "FAIL: expected compute command valid\n");
    delete dut;
    return 1;
  }
  dut->i_compute_cmd_ready = 1;
  tick(dut);
  dut->i_compute_rsp_done = 1;
  tick(dut);
  dut->i_compute_rsp_done = 0;
  if (!wait_dispatch_mode(dut, 0, 8)) {
    std::fprintf(stderr, "FAIL: expected META dispatch after rsp_done\n");
    delete dut;
    return 1;
  }

  dut->i_read_meta_data[0] = 0x00000000u;
  dut->i_read_meta_data[1] = 0x00000000u;
  dut->i_read_meta_data[2] = 0x00000600u;  // bank_hint=3 -> low2=3
  dut->i_read_meta_data[3] = 0x00000000u;
  dut->i_read_meta_data[4] = 0x00000000u;
  dut->i_read_meta_data[5] = 0x00000000u;
  dut->i_read_meta_data[6] = 0x00000000u;
  dut->i_read_meta_data[7] = 0x00000000u;
  dut->i_read_meta_valid = 1;
  if (!wait_dispatch_mode(dut, 1, 16)) {
    std::fprintf(stderr, "FAIL: expected STATE followup dispatch mode=1\n");
    delete dut;
    return 1;
  }
  dut->i_read_meta_valid = 0;
  tick(dut);
  if (!wait_dispatch_mode(dut, 2, 16)) {
    std::fprintf(stderr, "FAIL: expected MESSAGE followup mode=2 (low2=3)\n");
    delete dut;
    return 1;
  }
  // low2=3 maps to bank 7 -> address 0x000E0 (7*0x20)
  if (dut->o_dispatch_node_address != 0x000e0) {
    std::fprintf(stderr, "FAIL: expected MESSAGE bank 7 addr 0x000e0 got 0x%x (low2=3)\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 8c\n");

  // Test 8d: bank_hint[1:0]=0 maps to bank 4 (default)
  // Advance to META
  tick(dut);
  if (!dut->o_compute_start || !dut->o_compute_cmd_valid) {
    std::fprintf(stderr, "FAIL: expected compute command valid\n");
    delete dut;
    return 1;
  }
  dut->i_compute_cmd_ready = 1;
  tick(dut);
  dut->i_compute_rsp_done = 1;
  tick(dut);
  dut->i_compute_rsp_done = 0;
  if (!wait_dispatch_mode(dut, 0, 8)) {
    std::fprintf(stderr, "FAIL: expected META dispatch after rsp_done\n");
    delete dut;
    return 1;
  }

  dut->i_read_meta_data[0] = 0x00000000u;
  dut->i_read_meta_data[1] = 0x00000000u;
  dut->i_read_meta_data[2] = 0x00000000u;  // bank_hint=0 -> low2=0
  dut->i_read_meta_data[3] = 0x00000000u;
  dut->i_read_meta_data[4] = 0x00000000u;
  dut->i_read_meta_data[5] = 0x00000000u;
  dut->i_read_meta_data[6] = 0x00000000u;
  dut->i_read_meta_data[7] = 0x00000000u;
  dut->i_read_meta_valid = 1;
  if (!wait_dispatch_mode(dut, 1, 16)) {
    std::fprintf(stderr, "FAIL: expected STATE followup dispatch mode=1\n");
    delete dut;
    return 1;
  }
  dut->i_read_meta_valid = 0;
  tick(dut);
  if (!wait_dispatch_mode(dut, 2, 16)) {
    std::fprintf(stderr, "FAIL: expected MESSAGE followup mode=2 (low2=0)\n");
    delete dut;
    return 1;
  }
  // low2=0 maps to bank 4 -> address 0x00080 (4*0x20)
  if (dut->o_dispatch_node_address != 0x00080) {
    std::fprintf(stderr, "FAIL: expected MESSAGE bank 4 addr 0x00080 got 0x%x (low2=0)\n", dut->o_dispatch_node_address);
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 8d\n");

  std::printf("All tests passed\n");
  delete dut;
  return 0;
}
