#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "verilated.h"
#include "Vcompute_unit_top.h"

namespace {

constexpr const char* kMismatchMarker = "OP_MATRIX_MISMATCH_MARKER";
constexpr const char* kPassMarker = "OP_MATRIX_PASS_MARKER";
constexpr const char* kNegativeMarker = "COMPUTE_UNIT_GREEN_NEGATIVE_MARKER";

struct OpCase {
  const char* name;
  uint8_t raw_op_vector;
  uint32_t a[4];
  uint32_t b[4];
  uint32_t expected[4];
  int max_cycles;
};

static void tick(Vcompute_unit_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vcompute_unit_top* dut, int cycles = 4) {
  dut->rst_n = 0;
  dut->i_cmd_valid = 0;
  dut->i_cmd_kind = 0;
  dut->i_valid = 0;
  dut->i_op = 0;
  dut->i_write_ready = 1;
  dut->i_a0 = 0;
  dut->i_a1 = 0;
  dut->i_a2 = 0;
  dut->i_a3 = 0;
  dut->i_b0 = 0;
  dut->i_b1 = 0;
  dut->i_b2 = 0;
  dut->i_b3 = 0;
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
  for (int i = 0; i < 8; ++i) {
    tick(dut);
  }
}

static bool accept_command(Vcompute_unit_top* dut) {
  for (int i = 0; i < 8; ++i) {
    if (dut->o_cmd_ready) {
      dut->i_cmd_valid = 1;
      tick(dut);
      dut->i_cmd_valid = 0;
      return true;
    }
    tick(dut);
  }
  return false;
}

static void drive_lane_vectors(Vcompute_unit_top* dut, const OpCase& tc) {
  dut->i_op = static_cast<uint8_t>(tc.raw_op_vector & 0x3u);
  dut->i_a0 = tc.a[0];
  dut->i_a1 = tc.a[1];
  dut->i_a2 = tc.a[2];
  dut->i_a3 = tc.a[3];
  dut->i_b0 = tc.b[0];
  dut->i_b1 = tc.b[1];
  dut->i_b2 = tc.b[2];
  dut->i_b3 = tc.b[3];
}

static bool wait_for_valid(Vcompute_unit_top* dut, int max_cycles) {
  for (int i = 0; i < max_cycles; ++i) {
    if (dut->o_valid) {
      return true;
    }
    tick(dut);
  }
  return dut->o_valid;
}

static int check_case(Vcompute_unit_top* dut, const OpCase& tc) {
  reset_dut(dut);
  if (!accept_command(dut)) {
    std::fprintf(stderr, "%s: case=%s reason=command_not_accepted\n", kMismatchMarker, tc.name);
    return 1;
  }

  drive_lane_vectors(dut, tc);
  dut->i_valid = 1;
  tick(dut);
  tick(dut);
  dut->i_valid = 0;

  if (!wait_for_valid(dut, tc.max_cycles)) {
    std::fprintf(stderr, "%s: case=%s reason=timeout_waiting_for_valid\n", kMismatchMarker, tc.name);
    return 1;
  }

  const uint32_t observed[4] = {dut->o_y0, dut->o_y1, dut->o_y2, dut->o_y3};
  int mismatches = 0;
  for (int lane = 0; lane < 4; ++lane) {
    if (observed[lane] != tc.expected[lane]) {
      std::fprintf(stderr,
                   "%s: case=%s lane=%d op_raw=0x%02x op_eff=0x%01x observed=0x%08x expected=0x%08x\n",
                   kMismatchMarker,
                   tc.name,
                   lane,
                   static_cast<unsigned>(tc.raw_op_vector),
                   static_cast<unsigned>(tc.raw_op_vector & 0x3u),
                   observed[lane],
                   tc.expected[lane]);
      ++mismatches;
    }
  }

  if (dut->o_wr_valid && dut->o_wr_data != observed[0]) {
    std::fprintf(stderr,
                 "%s: case=%s lane0_writeback observed=0x%08x write_data=0x%08x\n",
                 kMismatchMarker,
                 tc.name,
                 observed[0],
                 dut->o_wr_data);
    ++mismatches;
  }

  return mismatches;
}

}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vcompute_unit_top;
  const char* perturb_env = std::getenv("COMPUTE_UNIT_TOP_PERTURB");
  const bool perturb_mode = (perturb_env != nullptr) && (std::strcmp(perturb_env, "1") == 0);

  const OpCase cases[] = {
      {
          "add",
          0x00,
          {0x3f800000u, 0x40000000u, 0x40800000u, 0x3f000000u},
          {0x40000000u, 0x40400000u, 0x40a00000u, 0x3f000000u},
          {0x40400000u, 0x40a00000u, 0x41100000u, 0x3f800000u},
          64,
      },
      {
          "sub",
          0x01,
          {0x40a00000u, 0x40e00000u, 0x41200000u, 0x3f800000u},
          {0x3fc00000u, 0x40000000u, 0x40400000u, 0x3f000000u},
          {0x40600000u, 0x40a00000u, 0x40e00000u, 0x3f000000u},
          64,
      },
      {
          "mul",
          0x02,
          {0x40000000u, 0x40400000u, 0x40800000u, 0x3f800000u},
          {0x40400000u, 0x40800000u, 0x40a00000u, 0xbf800000u},
          {0x40c00000u, 0x41400000u, 0x41a00000u, 0xbf800000u},
          64,
      },
      {
          "div-edge",
          0x03,
          {0x3f800000u, 0x40000000u, 0xbf800000u, 0x40800000u},
          {0x00000000u, 0x3f800000u, 0x00000000u, 0x00000000u},
          {0x7f800000u, 0x40000000u, 0xff800000u, 0x7f800000u},
          256,
      },
  };

  int failures = 0;

  for (const OpCase& tc : cases) {
    OpCase expected_case = tc;
    if (perturb_mode && std::strcmp(tc.name, "sub") == 0) {
      const uint32_t old_expected = expected_case.expected[2];
      expected_case.expected[2] ^= 0x1u;
      std::fprintf(stderr,
                   "%s: enabled env=COMPUTE_UNIT_TOP_PERTURB case=%s lane=2 expected_from=0x%08x expected_to=0x%08x\n",
                   kNegativeMarker,
                   tc.name,
                   old_expected,
                   expected_case.expected[2]);
    }

    const int mismatches = check_case(dut, expected_case);
    if (mismatches != 0) {
      failures += mismatches;
    }
  }

  std::printf("compute_unit_top: op-matrix executed add/sub/mul/div\n");
  std::printf("compute_unit_top: failures=%d\n", failures);

  delete dut;

  if (perturb_mode) {
    std::fprintf(stderr,
                 "%s: result failures=%d env=COMPUTE_UNIT_TOP_PERTURB\n",
                 kNegativeMarker,
                 failures);
    if (failures == 0) {
      return 1;
    }
  }

  if (failures == 0) {
    std::fprintf(stderr, "%s: add/sub/mul/div matrix clean\n", kPassMarker);
    return 0;
  }

  return 1;
}
