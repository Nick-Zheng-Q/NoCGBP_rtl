// gbp_control_fsm_test.cc
// Unit test for gbp_control_fsm - GBP node computation control

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "Vgbp_control_fsm.h"
#include "verilated.h"

int error_count = 0;
int test_count = 0;

void check(bool condition, const char* msg) {
  test_count++;
  if (!condition) {
    error_count++;
    std::fprintf(stderr, "  [FAIL] %s\n", msg);
  } else {
    std::fprintf(stdout, "  [PASS] %s\n", msg);
  }
}

// Float/uint32 conversion
uint32_t f2u(float f) {
  union { float f; uint32_t u; } conv;
  conv.f = f;
  return conv.u;
}

float u2f(uint32_t u) {
  union { float f; uint32_t u; } conv;
  conv.u = u;
  return conv.f;
}

void tick(Vgbp_control_fsm* dut) {
  dut->clk_i = 0; dut->eval();
  dut->clk_i = 1; dut->eval();
}

void pulse_mat_done(Vgbp_control_fsm* dut) {
  dut->mat_done = 1;
  tick(dut);
  dut->mat_done = 0;
  tick(dut);
}

bool observe_done_pulse(Vgbp_control_fsm* dut, int max_ticks) {
  if (dut->done_o) return true;
  for (int i = 0; i < max_ticks; i++) {
    tick(dut);
    if (dut->done_o) return true;
  }
  return false;
}

void reset(Vgbp_control_fsm* dut) {
  dut->reset_i = 1;
  for (int i = 0; i < 10; i++) tick(dut);
  dut->reset_i = 0;
  for (int i = 0; i < 5; i++) tick(dut);
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_control_fsm;
  
  std::printf("========================================\n");
  std::printf("GBP Control FSM Unit Test\n");
  std::printf("========================================\n\n");
  
  // Initialize all inputs
  dut->clk_i = 0;
  dut->reset_i = 1;
  dut->cmd_valid = 0;
  dut->cmd_is_factor = 0;
  dut->cmd_node_idx = 0;
  dut->cmd_dofs = 0;
  dut->cmd_adj_count = 0;
  dut->cmd_msg_count = 0;
  dut->stream_grant = 0;
  dut->stream_done = 0;
  dut->stream_in_valid = 0;
  dut->stream_out_ready = 0;
  dut->mat_cmd_ready = 1;
  dut->mat_done = 0;
  
  // Reset
  std::printf("[Reset]...\n");
  reset(dut);
  std::printf("[Reset] Done.\n\n");
  
  // Test 1: Idle state - check cmd_ready
  std::printf("[Test 1] Idle state - cmd_ready assertion\n");
  {
    check(dut->cmd_ready == 1, "cmd_ready should be 1 in IDLE");
    check(dut->done_o == 0, "done_o should be 0 in IDLE");
  }
  std::printf("\n");
  
  // Test 2: Variable Node command acceptance (2 DOF, 2 adjacent)
  std::printf("[Test 2] Variable Node command (2 DOF, 2 adjacent)\n");
  {
    dut->cmd_valid = 1;
    dut->cmd_is_factor = 0;  // Variable node
    dut->cmd_dofs = 2;
    dut->cmd_adj_count = 2;
    dut->cmd_msg_count = 2;
    tick(dut);
    dut->cmd_valid = 0;
    
    // Should transition to S_VAR_LOAD_DATA
    check(dut->stream_req_state == 1, "stream_req_state should be 1 in VAR_LOAD_DATA");
    check(dut->stream_xfer_bytes == 256, "stream_xfer_bytes should be 256");
    check(dut->stream_in_ready == 1, "stream_in_ready should be 1");
  }
  std::printf("\n");
  
  // Test 3: Complete Variable Node flow
  std::printf("[Test 3] Variable Node full flow (stream_done)\n");
  {
    // Trigger stream_done to move to S_VAR_ACCUMULATE
    dut->stream_done = 1;
    tick(dut);
    dut->stream_done = 0;
    tick(dut);
    tick(dut);
    
    // Should be in S_VAR_ACCUMULATE
    check(dut->mat_cmd_valid == 1, "mat_cmd_valid should be 1 in VAR_ACCUMULATE");
    check(dut->mat_cmd_op == 0, "mat_cmd_op should be MAT_ADD (0)");
    check(dut->mat_cmd_m == 1, "mat_cmd_m should be 1");
    check(dut->mat_cmd_n == 2, "mat_cmd_n should match dofs (2)");
    
    // Simulate matrix operation complete (accum_count_r = 0, adj_count_r = 2)
    // Need one more accumulation
    pulse_mat_done(dut);
    
    // Second accumulation
    pulse_mat_done(dut);
    
    // Should transition to S_VAR_INVERT_LAM
    tick(dut);
    check(dut->mat_cmd_op == 3, "mat_cmd_op should be MAT_INV (3) in VAR_INVERT_LAM");
    check(dut->mat_cmd_m == 2, "mat_cmd_m should be 2 in VAR_INVERT_LAM");
    check(dut->mat_cmd_n == 2, "mat_cmd_n should be 2 in VAR_INVERT_LAM");
    
    // Complete inversion
    pulse_mat_done(dut);
    
    // Should be in S_VAR_MVMUL
    tick(dut);
    check(dut->mat_cmd_op == 4, "mat_cmd_op should be MAT_VEC_MUL (4) in VAR_MVMUL");
    
    // Complete MVMUL
    pulse_mat_done(dut);
    
    // Should be in S_VAR_STORE_RESULT
    tick(dut);
    check(dut->stream_out_valid == 1, "stream_out_valid should be 1 in VAR_STORE_RESULT");
    
    // Complete store
    dut->stream_done = 1;
    tick(dut);
    dut->stream_done = 0;
    
    // Should observe a DONE pulse
    check(observe_done_pulse(dut, 3), "done_o should be 1 in VAR_DONE");
    
    // Return to IDLE
    tick(dut);
    check(dut->cmd_ready == 1, "Should return to IDLE (cmd_ready=1)");
  }
  std::printf("\n");
  
  // Test 4: Factor Node command acceptance (3 DOF, 3 adjacent)
  std::printf("[Test 4] Factor Node command (3 DOF, 3 adjacent)\n");
  {
    reset(dut);
    
    dut->cmd_valid = 1;
    dut->cmd_is_factor = 1;  // Factor node
    dut->cmd_dofs = 3;
    dut->cmd_adj_count = 3;
    dut->cmd_msg_count = 3;
    tick(dut);
    dut->cmd_valid = 0;
    
    // Should transition to S_FAC_LOAD_DATA
    check(dut->stream_req_state == 1, "stream_req_state should be 1 in FAC_LOAD_DATA");
    check(dut->stream_req_messages == 1, "stream_req_messages should be 1");
    check(dut->stream_xfer_bytes == 512, "stream_xfer_bytes should be 512");
  }
  std::printf("\n");
  
  // Test 5: Factor Node cavity accumulation
  std::printf("[Test 5] Factor Node cavity accumulation\n");
  {
    // Complete load
    dut->stream_done = 1;
    tick(dut);
    dut->stream_done = 0;
    
    // Should be in S_FAC_LOOP_INIT -> S_FAC_CAVITY_ACCUM
    tick(dut);  // S_FAC_LOOP_INIT
    tick(dut);  // S_FAC_CAVITY_ACCUM valid stable
    
    // S_FAC_CAVITY_ACCUM: accumulate messages (skip current adjacent)
    // For 3 adjacent, need to accumulate 2 messages (skip current)
    check(dut->mat_cmd_valid == 1, "mat_cmd_valid should be 1 in FAC_CAVITY_ACCUM");
    check(dut->mat_cmd_op == 0, "mat_cmd_op should be MAT_ADD (0)");
    
    // First accumulation
    pulse_mat_done(dut);
    
    // Second accumulation
    pulse_mat_done(dut);
    
    // Should transition to S_FAC_EXTRACT_BLOCKS
    tick(dut);
    
    // Then S_FAC_INVERT_LNONO
    check(dut->mat_cmd_op == 3, "mat_cmd_op should be MAT_INV (3)");
    
    pulse_mat_done(dut);
    
    // S_FAC_COMPUTE_MESSAGE
    tick(dut);
    check(dut->mat_cmd_op == 2, "mat_cmd_op should be MAT_MUL (2)");
  }
  std::printf("\n");
  
  // Test 6: Buffer write during stream input
  std::printf("[Test 6] Buffer write during stream input\n");
  {
    reset(dut);
    
    dut->cmd_valid = 1;
    dut->cmd_is_factor = 0;
    dut->cmd_dofs = 2;
    dut->cmd_adj_count = 1;
    dut->cmd_msg_count = 1;
    tick(dut);
    dut->cmd_valid = 0;
    
    // In S_VAR_LOAD_DATA
    dut->stream_in_valid = 1;
    dut->stream_in_ready = 1;
    // stream_in_data is a wide signal, but we're testing control logic
    tick(dut);
    
    check(dut->buf_wr_valid == 1, "buf_wr_valid should be 1 when stream_in_valid && stream_in_ready");
    
    dut->stream_in_valid = 0;
    tick(dut);
  }
  std::printf("\n");
  
  // Test 7: Factor Node next adjacent iteration
  std::printf("[Test 7] Factor Node adjacent iteration\n");
  {
    reset(dut);
    
    dut->cmd_valid = 1;
    dut->cmd_is_factor = 1;
    dut->cmd_dofs = 2;
    dut->cmd_adj_count = 2;  // 2 adjacent variables
    dut->cmd_msg_count = 2;
    tick(dut);
    dut->cmd_valid = 0;
    
    // Fast forward through first adjacent processing
    dut->stream_done = 1; tick(dut); dut->stream_done = 0;
    tick(dut);  // S_FAC_LOOP_INIT
    
    // S_FAC_CAVITY_ACCUM (0 accumulations for 2 adjacent with skip)
    // Actually for 2 adjacent: adj_count_r - 2 = 0, so skip immediately
    tick(dut);
    tick(dut);  // S_FAC_EXTRACT_BLOCKS
    pulse_mat_done(dut);  // S_FAC_INVERT_LNONO
    pulse_mat_done(dut);  // S_FAC_COMPUTE_MESSAGE
    dut->stream_done = 1; tick(dut); dut->stream_done = 0;  // S_FAC_STORE_MESSAGE
    
    // S_FAC_NEXT_ADJACENT - should increment current_adj_r and loop back
    tick(dut);
    
    // Should go back to S_FAC_LOOP_INIT for second adjacent
    tick(dut);
    check(dut->mat_cmd_valid == 1, "Should be processing second adjacent (S_FAC_CAVITY_ACCUM)");
  }
  std::printf("\n");
  
  // Test 8: Different DOF values
  std::printf("[Test 8] Different DOF values (6 DOF)\n");
  {
    reset(dut);
    
    dut->cmd_valid = 1;
    dut->cmd_is_factor = 0;
    dut->cmd_dofs = 6;  // Maximum DOF
    dut->cmd_adj_count = 1;
    dut->cmd_msg_count = 1;
    tick(dut);
    dut->cmd_valid = 0;
    
    // Complete load
    dut->stream_done = 1;
    tick(dut);
    dut->stream_done = 0;
    
    // In S_VAR_ACCUMULATE
    // msg_size = 6 + 6*7/2 = 6 + 21 = 27
    // lam_size = 6 * 6 = 36
    pulse_mat_done(dut);
    
    // S_VAR_INVERT_LAM should use 6x6
    tick(dut);
    check(dut->mat_cmd_m == 6, "mat_cmd_m should be 6 for 6 DOF");
    check(dut->mat_cmd_n == 6, "mat_cmd_n should be 6 for 6 DOF");
  }
  std::printf("\n");
  
  // Test 9: cmd_ready only in IDLE
  std::printf("[Test 9] cmd_ready only asserted in IDLE\n");
  {
    reset(dut);
    check(dut->cmd_ready == 1, "cmd_ready=1 in IDLE");
    
    // Start Variable Node
    dut->cmd_valid = 1;
    dut->cmd_is_factor = 0;
    dut->cmd_dofs = 2;
    dut->cmd_adj_count = 1;
    dut->cmd_msg_count = 1;
    tick(dut);
    dut->cmd_valid = 0;
    
    check(dut->cmd_ready == 0, "cmd_ready=0 when not in IDLE");
  }
  std::printf("\n");
  
  // Test 10: done_o pulse only in DONE states
  std::printf("[Test 10] done_o only in DONE states\n");
  {
    // Continue from previous test
    // Fast forward to completion
    dut->stream_done = 1; tick(dut); dut->stream_done = 0;
    pulse_mat_done(dut);  // ACCUM -> INVERT
    pulse_mat_done(dut);  // INVERT -> MVMUL
    pulse_mat_done(dut);  // MVMUL -> STORE
    dut->stream_done = 1; tick(dut); dut->stream_done = 0;  // STORE -> DONE
    
    check(observe_done_pulse(dut, 3), "done_o=1 in VAR_DONE");
    
    tick(dut);  // Return to IDLE
    check(dut->done_o == 0, "done_o=0 after returning to IDLE");
  }
  std::printf("\n");
  
  // Summary
  std::printf("========================================\n");
  std::printf("Test Summary: %d tests, %d errors\n", test_count, error_count);
  if (error_count == 0) {
    std::printf("ALL TESTS PASSED!\n");
  } else {
    std::printf("SOME TESTS FAILED!\n");
  }
  std::printf("========================================\n");
  
  delete dut;
  return error_count > 0 ? 1 : 0;
}
