// operand_window.cc
// Unit test for operand_window

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "verilated.h"
#include "Voperand_window_top.h"

static int error_count = 0;
static int test_count = 0;

static void check(bool condition, const char* msg) {
  test_count++;
  if (!condition) {
    error_count++;
    std::fprintf(stderr, "  [FAIL] %s\n", msg);
  } else {
    std::fprintf(stdout, "  [PASS] %s\n", msg);
  }
}

static uint32_t f2u(float f) {
  union { float f; uint32_t u; } c; c.f = f; return c.u;
}
static float u2f(uint32_t u) {
  union { float f; uint32_t u; } c; c.u = u; return c.f;
}

static void tick(Voperand_window_top* d) {
  d->clk_i = 0; d->eval();
  d->clk_i = 1; d->eval();
}

static void reset(Voperand_window_top* d) {
  d->reset_i = 1;
  for (int i = 0; i < 5; i++) tick(d);
  d->reset_i = 0;
  for (int i = 0; i < 3; i++) tick(d);
}

void test_operand_window(Voperand_window_top* d) {
  std::printf("\n=== operand_window ===\n");

  reset(d);

  // dim_i=1, dim_o=1: 5 scalars total = 1 beat
  d->ow_dim_i_i = 0; // DIM_1
  d->ow_dim_o_i = 0; // DIM_1

  // Start
  d->ow_start_i = 1;
  tick(d);
  d->ow_start_i = 0;
  tick(d);

  // Load one beat
  d->ow_load_valid_i = 1;
  d->ow_load_kind_i = 0; // OST_MSG_STATIC
  d->ow_load_last_i = 1;

  // Pack 5 scalars: eta[0]=1.0, L_ii[0]=2.0, L_io[0]=3.0, old_eta[0]=4.0, old_L[0]=5.0
  d->ow_load_data_flat_i[0] = f2u(1.0f);
  d->ow_load_data_flat_i[1] = f2u(2.0f);
  d->ow_load_data_flat_i[2] = f2u(3.0f);
  d->ow_load_data_flat_i[3] = f2u(4.0f);
  d->ow_load_data_flat_i[4] = f2u(5.0f);
  for (int i = 5; i < 16; i++) d->ow_load_data_flat_i[i] = 0;
  tick(d);

  d->ow_load_valid_i = 0;
  d->ow_load_last_i = 0;
  tick(d);
  tick(d);

  // Should be in ST_READY now
  check(d->ow_msg_static_valid_o == 1, "OW dim=1: valid after load");

  // Verify output values
  check(u2f(d->ow_msg_eta_flat_o[0]) == 1.0f, "OW dim=1: eta[0]=1.0");
  check(u2f(d->ow_msg_L_ii_flat_o[0]) == 2.0f, "OW dim=1: L_ii[0]=2.0");
  check(u2f(d->ow_msg_L_io_flat_o[0]) == 3.0f, "OW dim=1: L_io[0]=3.0");
  check(u2f(d->ow_msg_old_eta_flat_o[0]) == 4.0f, "OW dim=1: old_eta[0]=4.0");
  check(u2f(d->ow_msg_old_L_flat_o[0]) == 5.0f, "OW dim=1: old_L[0]=5.0");

  // Clear
  d->ow_clear_i = 1;
  tick(d);
  d->ow_clear_i = 0;
  tick(d);

  check(d->ow_msg_static_valid_o == 0, "OW dim=1: valid cleared");

  // Test dim=3: 27 scalars = 2 beats
  d->ow_dim_i_i = 1; // DIM_3
  d->ow_dim_o_i = 1; // DIM_3

  d->ow_start_i = 1;
  tick(d);
  d->ow_start_i = 0;
  tick(d);

  // Beat 0: 16 scalars
  d->ow_load_valid_i = 1;
  d->ow_load_kind_i = 0;
  d->ow_load_last_i = 0;
  for (int i = 0; i < 16; i++) d->ow_load_data_flat_i[i] = f2u((float)(i + 1));
  tick(d);

  // Beat 1: 11 scalars
  d->ow_load_last_i = 1;
  for (int i = 0; i < 11; i++) d->ow_load_data_flat_i[i] = f2u((float)(i + 17));
  for (int i = 11; i < 16; i++) d->ow_load_data_flat_i[i] = 0;
  tick(d);

  d->ow_load_valid_i = 0;
  d->ow_load_last_i = 0;
  tick(d);

  check(d->ow_msg_static_valid_o == 1, "OW dim=3: valid after load");

  // Verify eta values
  check(u2f(d->ow_msg_eta_flat_o[0]) == 1.0f, "OW dim=3: eta[0]=1.0");
  check(u2f(d->ow_msg_eta_flat_o[1]) == 2.0f, "OW dim=3: eta[1]=2.0");

  // Clear
  d->ow_clear_i = 1;
  tick(d);
  d->ow_clear_i = 0;
  tick(d);

  check(d->ow_msg_static_valid_o == 0, "OW dim=3: valid cleared");
}

// ============================================================
int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Voperand_window_top;

  std::printf("========================================\n");
  std::printf("operand_window unit tests\n");
  std::printf("========================================\n");

  test_operand_window(dut);

  std::printf("\n========================================\n");
  std::printf("Test Summary: %d tests, %d errors\n", test_count, error_count);
  if (error_count == 0)
    std::printf("ALL TESTS PASSED!\n");
  else
    std::printf("SOME TESTS FAILED!\n");
  std::printf("========================================\n");

  delete dut;
  return error_count > 0 ? 1 : 0;
}
