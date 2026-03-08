#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "Vgbp_pe_compute_done_egress.h"

static void tick(Vgbp_pe_compute_done_egress* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe_compute_done_egress* dut, bool stall_enabled) {
  dut->rst_n = 0;
  dut->noc_ready_i = stall_enabled ? 0 : 1;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_pe_compute_done_egress;

  const char* stall_env = std::getenv("GBP_PE_EGRESS_FORCE_NOC_STALL");
  const bool force_stall =
      (stall_env != nullptr) && (stall_env[0] != '\0') && (stall_env[0] != '0');
  const char* mismatch_env = std::getenv("GBP_PE_EGRESS_EXPECT_MISMATCH");
  const bool expect_mismatch =
      (mismatch_env != nullptr) && (mismatch_env[0] != '\0') && (mismatch_env[0] != '0');

  reset_dut(dut, force_stall);

  static constexpr int kMaxCycles = 2000;
  static constexpr int kStallHoldCyclesAfterDone = 8;

  bool prev_compute_done = false;
  bool target_done_seen = false;
  bool released_from_stall = !force_stall;
  bool accepted_while_stalled = false;
  bool accepted_after_release = false;
  int stall_hold_cycles = 0;
  uint32_t target_txn_id = 0;
  int target_packet_accepts = 0;
  bool completion_window_active = false;
  bool completion_window_closed = false;

  for (int cycle = 0; cycle < kMaxCycles; ++cycle) {
    tick(dut);

    const bool compute_done = static_cast<bool>(dut->compute_done_o);
    const bool compute_done_pulse = compute_done && !prev_compute_done;
    prev_compute_done = compute_done;

    if (!target_done_seen && compute_done_pulse) {
      target_done_seen = true;
      target_txn_id = static_cast<uint32_t>(dut->compute_txn_id_o);
      completion_window_active = true;
    } else if (completion_window_active && compute_done_pulse) {
      completion_window_active = false;
      completion_window_closed = true;
    }

    if (force_stall && target_done_seen && !released_from_stall) {
      dut->noc_ready_i = 0;
      stall_hold_cycles++;
      if (stall_hold_cycles >= kStallHoldCyclesAfterDone) {
        dut->noc_ready_i = 1;
        released_from_stall = true;
      }
    }

    const bool accepted = static_cast<bool>(dut->egress_v_o) && static_cast<bool>(dut->noc_ready_i);
    if (accepted && completion_window_active) {
      const uint32_t accepted_txn_id = static_cast<uint32_t>(dut->egress_txn_id_o);
      if (!released_from_stall) {
        accepted_while_stalled = true;
      }
      if (released_from_stall) {
        accepted_after_release = true;
      }
      if (accepted_txn_id == target_txn_id) {
        target_packet_accepts++;
      }
    }

    if (target_packet_accepts > 1) {
      std::fprintf(stderr,
                   "gbp_pe_compute_done_egress: FAIL txn_id=0x%02x packets=%d expected=1\n",
                   static_cast<unsigned int>(target_txn_id),
                   target_packet_accepts);
      delete dut;
      return 1;
    }

    if (completion_window_closed) {
      break;
    }
  }

  if (!target_done_seen) {
    std::fprintf(stderr, "gbp_pe_compute_done_egress: FAIL no_compute_done_pulse\n");
    delete dut;
    return 1;
  }

  if (!completion_window_closed) {
    std::fprintf(stderr,
                 "gbp_pe_compute_done_egress: FAIL completion_window_not_closed txn_id=0x%02x packets=%d\n",
                 static_cast<unsigned int>(target_txn_id),
                 target_packet_accepts);
    delete dut;
    return 1;
  }

  if (target_packet_accepts != 1) {
    std::fprintf(stderr,
                 "gbp_pe_compute_done_egress: FAIL txn_id=0x%02x packets=%d expected=1\n",
                 static_cast<unsigned int>(target_txn_id),
                 target_packet_accepts);
    delete dut;
    return 1;
  }

   if (expect_mismatch) {
    std::fprintf(stderr,
                 "gbp_pe_compute_done_egress: PACKET_COUNT_MISMATCH_MARKER txn_id=0x%02x packets=%d expected=2\n",
                 static_cast<unsigned int>(target_txn_id),
                 target_packet_accepts);
    delete dut;
    return 2;
  }

  if (force_stall) {
    if (accepted_while_stalled) {
      std::fprintf(stderr,
                   "gbp_pe_compute_done_egress: FAIL packet_accepted_while_stalled txn_id=0x%02x\n",
                   static_cast<unsigned int>(target_txn_id));
      delete dut;
      return 1;
    }
    if (!accepted_after_release) {
      std::fprintf(stderr,
                   "gbp_pe_compute_done_egress: FAIL no_packet_after_stall_release txn_id=0x%02x\n",
                   static_cast<unsigned int>(target_txn_id));
      delete dut;
      return 1;
    }
    std::printf(
        "gbp_pe_compute_done_egress: PASS txn_id=0x%02x packets=1 recovered_from_stall=1\n",
        static_cast<unsigned int>(target_txn_id));
  } else {
    std::printf("gbp_pe_compute_done_egress: PASS txn_id=0x%02x packets=1\n",
                static_cast<unsigned int>(target_txn_id));
  }

  delete dut;
  return 0;
}
