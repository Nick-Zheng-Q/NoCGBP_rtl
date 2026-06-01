// gbp_compute_engine_test.cc
// Unit test for gbp_compute_engine - Top-level GBP compute engine
// Tests end-to-end computation flows

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "Vgbp_compute_engine.h"
#include "Vgbp_compute_engine___024root.h"
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

void tick(Vgbp_compute_engine* dut) {
  dut->clk_i = 0; dut->eval();
  dut->clk_i = 1; dut->eval();
}

void reset(Vgbp_compute_engine* dut) {
  dut->reset_i = 1;
  for (int i = 0; i < 10; i++) tick(dut);
  dut->reset_i = 0;
  for (int i = 0; i < 5; i++) tick(dut);
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vgbp_compute_engine;
  
  std::printf("========================================\n");
  std::printf("GBP Compute Engine Unit Test\n");
  std::printf("========================================\n\n");
  
  // Initialize all inputs
  dut->clk_i = 0;
  dut->reset_i = 1;
  dut->cmd_valid_i = 0;
  dut->cmd_is_factor_i = 0;
  dut->cmd_node_idx_i = 0;
  dut->cmd_dofs_i = 0;
  dut->cmd_adj_count_i = 0;
  dut->cmd_msg_count_i = 0;
  dut->stream_in_valid = 0;
  dut->stream_out_ready = 0;
  dut->damping_factor_i = f2u(0.4f);  // Default damping 0.4
  
  // Reset
  std::printf("[Reset]...\n");
  reset(dut);
  std::printf("[Reset] Done.\n\n");
  
  // Test 1: Idle state - check cmd_ready_o
  std::printf("[Test 1] Idle state - cmd_ready_o assertion\n");
  {
    check(dut->cmd_ready_o == 1, "cmd_ready_o should be 1 in IDLE");
    check(dut->compute_done_o == 0, "compute_done_o should be 0 in IDLE");
    check(dut->rsp_done_o == 0, "rsp_done_o should be 0 in IDLE");
  }
  std::printf("\n");
  
  // Test 2: Variable Node command acceptance
  std::printf("[Test 2] Variable Node command (2 DOF)\n");
  {
    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 0;  // Variable node
    dut->cmd_dofs_i = 2;
    dut->cmd_adj_count_i = 1;
    dut->cmd_msg_count_i = 1;
    tick(dut);
    dut->cmd_valid_i = 0;
    
    // Should accept command and start processing
    check(dut->cmd_ready_o == 0, "cmd_ready_o should be 0 after accepting command");
    check(dut->stream_in_ready == 1, "stream_in_ready should be 1 for data loading");
  }
  std::printf("\n");
  
  // Test 3: Stream data input to staging buffer
  std::printf("[Test 3] Stream data input (256-bit beat)\n");
  {
    // Prepare test data: 8 floats
    uint32_t test_data[8] = {
      f2u(1.0f), f2u(2.0f), f2u(3.0f), f2u(4.0f),
      f2u(5.0f), f2u(6.0f), f2u(7.0f), f2u(8.0f)
    };
    
    dut->stream_in_valid = 1;
    for (int i = 0; i < 8; i++) {
      dut->stream_in_data[i] = test_data[i];
    }
    tick(dut);
    dut->stream_in_valid = 0;
    
    // Data should be written to staging buffer
    // stream_in_ready may go to 0 if loading is complete - that's OK
    check(dut->cmd_ready_o == 0, "Command still being processed");
    
    // Let the stream complete
    tick(dut);
    tick(dut);
  }
  std::printf("\n");
  
  // Test 4: Factor Node command acceptance
  std::printf("[Test 4] Factor Node command (3 DOF)\n");
  {
    reset(dut);
    
    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 1;  // Factor node
    dut->cmd_dofs_i = 3;
    dut->cmd_adj_count_i = 2;
    dut->cmd_msg_count_i = 2;
    tick(dut);
    dut->cmd_valid_i = 0;
    
    check(dut->cmd_ready_o == 0, "cmd_ready_o should be 0 after accepting command");
    check(dut->stream_in_ready == 1, "stream_in_ready should be 1 for factor node data loading");
  }
  std::printf("\n");
  
  // Test 5: Reset during operation
  std::printf("[Test 5] Reset during operation\n");
  {
    // Start an operation
    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 0;
    dut->cmd_dofs_i = 2;
    dut->cmd_adj_count_i = 1;
    dut->cmd_msg_count_i = 1;
    tick(dut);
    dut->cmd_valid_i = 0;
    
    // Apply reset
    dut->reset_i = 1;
    for (int i = 0; i < 5; i++) tick(dut);
    dut->reset_i = 0;
    for (int i = 0; i < 5; i++) tick(dut);
    
    // Should return to IDLE
    check(dut->cmd_ready_o == 1, "Should return to IDLE after reset (cmd_ready_o=1)");
    check(dut->compute_done_o == 0, "compute_done_o should be 0 after reset");
  }
  std::printf("\n");
  
  // Test 6: Stream output ready handling
  std::printf("[Test 6] Stream output ready handling\n");
  {
    reset(dut);
    
    // Start Variable Node operation
    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 0;
    dut->cmd_dofs_i = 2;
    dut->cmd_adj_count_i = 0;  // No adjacent - simpler flow
    dut->cmd_msg_count_i = 0;
    tick(dut);
    dut->cmd_valid_i = 0;
    
    // Provide input data
    dut->stream_in_valid = 1;
    for (int i = 0; i < 8; i++) {
      dut->stream_in_data[i] = f2u((float)(i + 1));
    }
    tick(dut);
    dut->stream_in_valid = 0;
    
    // Wait for processing and output
    dut->stream_out_ready = 1;
    for (int i = 0; i < 100 && !dut->stream_out_valid; i++) {
      tick(dut);
    }
    
    // May or may not have output depending on internal state
    std::printf("  [INFO] stream_out_valid = %d\n", dut->stream_out_valid);
  }
  std::printf("\n");
  
  // Test 7: Different damping factors
  std::printf("[Test 7] Damping factor configuration\n");
  {
    reset(dut);
    
    // Test with damping = 0.0
    dut->damping_factor_i = f2u(0.0f);
    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 0;
    dut->cmd_dofs_i = 2;
    dut->cmd_adj_count_i = 0;
    dut->cmd_msg_count_i = 0;
    tick(dut);
    dut->cmd_valid_i = 0;
    
    check(dut->cmd_ready_o == 0, "Command accepted with damping=0.0");
    
    reset(dut);
    
    // Test with damping = 1.0
    dut->damping_factor_i = f2u(1.0f);
    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 0;
    dut->cmd_dofs_i = 2;
    dut->cmd_adj_count_i = 0;
    dut->cmd_msg_count_i = 0;
    tick(dut);
    dut->cmd_valid_i = 0;
    
    check(dut->cmd_ready_o == 0, "Command accepted with damping=1.0");
    
    // Restore default
    dut->damping_factor_i = f2u(0.4f);
  }
  std::printf("\n");
  
  // Test 8: Maximum DOF (6) with Variable Node
  std::printf("[Test 8] Maximum DOF (6) Variable Node\n");
  {
    reset(dut);
    
    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 0;
    dut->cmd_dofs_i = 6;  // Maximum
    dut->cmd_adj_count_i = 1;
    dut->cmd_msg_count_i = 1;
    tick(dut);
    dut->cmd_valid_i = 0;
    
    check(dut->cmd_ready_o == 0, "Command accepted for 6 DOF");
    check(dut->stream_in_ready == 1, "stream_in_ready asserted for 6 DOF");
    
    // Provide data for 6 DOF (larger data requirement)
    dut->stream_in_valid = 1;
    for (int i = 0; i < 8; i++) {
      dut->stream_in_data[i] = f2u((float)(i + 1));
    }
    tick(dut);
    dut->stream_in_valid = 0;
    
    tick(dut);
    tick(dut);
  }
  std::printf("\n");
  
  // Test 9: Stream interface handshaking
  std::printf("[Test 9] Stream input handshaking\n");
  {
    reset(dut);
    
    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 0;
    dut->cmd_dofs_i = 2;
    dut->cmd_adj_count_i = 1;
    dut->cmd_msg_count_i = 1;
    tick(dut);
    dut->cmd_valid_i = 0;
    
    // Test ready/valid handshaking
    check(dut->stream_in_ready == 1, "stream_in_ready should be 1 when expecting data");
    
    // Send data
    dut->stream_in_valid = 1;
    for (int i = 0; i < 8; i++) {
      dut->stream_in_data[i] = f2u(1.0f);
    }
    tick(dut);
    dut->stream_in_valid = 0;
    
    // After data transfer, command is still being processed
    check(dut->cmd_ready_o == 0, "Command still being processed after data transfer");
  }
  std::printf("\n");
  
  // Test 10: Multiple consecutive operations
  std::printf("[Test 10] Multiple consecutive operations\n");
  {
    for (int op = 0; op < 3; op++) {
      reset(dut);
      
      dut->cmd_valid_i = 1;
      dut->cmd_is_factor_i = (op % 2);  // Alternate between var and factor
      dut->cmd_dofs_i = 2 + (op % 3);   // Vary DOF
      dut->cmd_adj_count_i = op;
      dut->cmd_msg_count_i = op;
      tick(dut);
      dut->cmd_valid_i = 0;
      
      char msg[64];
      std::snprintf(msg, sizeof(msg), "Command accepted in operation %d", op);
      check(dut->cmd_ready_o == 0, msg);
      
      // Quick data input
      dut->stream_in_valid = 1;
      for (int i = 0; i < 8; i++) {
        dut->stream_in_data[i] = f2u((float)(op + 1));
      }
      tick(dut);
      dut->stream_in_valid = 0;
      
      // Let it process a bit
      for (int i = 0; i < 20; i++) tick(dut);
    }
    std::printf("  [PASS] All 3 consecutive operations started correctly\n");
  }
  std::printf("\n");

  // Test 11: 3DOF Variable Node with non-zero message count
  std::printf("[Test 11] 3DOF Variable Node command (msg_count=3)\n");
  {
    reset(dut);

    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 0;
    dut->cmd_dofs_i = 3;
    dut->cmd_adj_count_i = 3;
    dut->cmd_msg_count_i = 3;
    tick(dut);
    dut->cmd_valid_i = 0;

    check(dut->cmd_ready_o == 0, "3DOF variable command accepted");
    check(dut->stream_in_ready == 1, "3DOF variable command requests input stream");

    dut->stream_in_valid = 1;
    for (int i = 0; i < 8; i++) {
      dut->stream_in_data[i] = f2u((float)(10 + i));
    }
    tick(dut);
    dut->stream_in_valid = 0;
    tick(dut);

    check(dut->cmd_ready_o == 0, "3DOF variable command remains in-flight after first beat");
  }
  std::printf("\n");

  // Test 12: 3DOF Variable Node completes after state + 3 messages (8 beats total)
  std::printf("[Test 12] 3DOF Variable completion with 8 input beats\n");
  {
    reset(dut);

    dut->stream_out_ready = 1;
    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 0;
    dut->cmd_dofs_i = 3;
    dut->cmd_adj_count_i = 3;
    dut->cmd_msg_count_i = 3;
    tick(dut);
    dut->cmd_valid_i = 0;

    check(dut->cmd_ready_o == 0, "3DOF variable completion test command accepted");

    for (int beat = 0; beat < 8; ++beat) {
      dut->stream_in_valid = 1;
      for (int i = 0; i < 8; i++) {
        dut->stream_in_data[i] = f2u((float)(100 + beat * 8 + i));
      }
      tick(dut);
    }
    dut->stream_in_valid = 0;

    bool saw_compute_done = false;
    bool saw_rsp_done = false;
    for (int i = 0; i < 200; ++i) {
      tick(dut);
      saw_compute_done = saw_compute_done || (dut->compute_done_o != 0);
      saw_rsp_done = saw_rsp_done || (dut->rsp_done_o != 0);
      if (saw_compute_done && saw_rsp_done) break;
    }

    if (!saw_compute_done || !saw_rsp_done) {
      auto* root = dut->rootp;
      std::printf(
          "  [INFO] timeout: fsm=%u matrix=%u matinv=%u beats_in=%u beats_out=%u active=%u dir_out=%u "
          "dofs_r=%u msg_count_r=%u rsp_done=%u compute_done=%u mat_done_r=%u "
          "stream_in_ready=%u stream_in_valid=%u stream_out_valid=%u stream_out_ready=%u\n",
          (unsigned)root->gbp_compute_engine__DOT__u_gbp_control_fsm__DOT__state_r,
          (unsigned)root->gbp_compute_engine__DOT__u_matrix_fsm__DOT__state_r,
          (unsigned)root->gbp_compute_engine__DOT__u_matrix_fsm__DOT__u_mat_inv__DOT__state_r,
          (unsigned)root->gbp_compute_engine__DOT__stream_in_beats_r,
          (unsigned)root->gbp_compute_engine__DOT__stream_out_beats_r,
          (unsigned)root->gbp_compute_engine__DOT__stream_active_r,
          (unsigned)root->gbp_compute_engine__DOT__stream_dir_out_r,
          (unsigned)root->gbp_compute_engine__DOT__cmd_dofs_r,
          (unsigned)root->gbp_compute_engine__DOT__cmd_msg_count_r,
          (unsigned)root->rsp_done_o,
          (unsigned)root->compute_done_o,
          (unsigned)root->gbp_compute_engine__DOT__mat_done_r,
          (unsigned)root->stream_in_ready,
          (unsigned)root->stream_in_valid,
          (unsigned)root->stream_out_valid,
          (unsigned)root->stream_out_ready);
    }

    check(saw_compute_done, "3DOF variable should assert compute_done_o after 8 beats");
    check(saw_rsp_done, "3DOF variable should assert rsp_done_o after 8 beats");
  }
  std::printf("\n");

  // Test 13: 单拍输出在首次 ready 握手时即可完成
  std::printf("[Test 13] Single-beat output completes on first ready pulse\n");
  {
    reset(dut);

    dut->stream_out_ready = 0;
    dut->cmd_valid_i = 1;
    dut->cmd_is_factor_i = 0;
    dut->cmd_dofs_i = 2;
    dut->cmd_adj_count_i = 0;
    dut->cmd_msg_count_i = 0;
    tick(dut);
    dut->cmd_valid_i = 0;

    dut->stream_in_valid = 1;
    for (int i = 0; i < 8; ++i) {
      dut->stream_in_data[i] = f2u((float)(200 + i));
    }
    tick(dut);
    dut->stream_in_valid = 0;

    bool saw_stream_out_valid = false;
    bool saw_rsp_done = false;
    bool ready_pulsed = false;
    for (int i = 0; i < 80; ++i) {
      if (!ready_pulsed && dut->stream_out_valid) {
        saw_stream_out_valid = true;
        dut->stream_out_ready = 1;
        tick(dut);
        dut->stream_out_ready = 0;
        ready_pulsed = true;
      } else {
        tick(dut);
      }
      saw_rsp_done = saw_rsp_done || (dut->rsp_done_o != 0);
      if (ready_pulsed && saw_rsp_done) {
        break;
      }
    }

    if (!saw_rsp_done) {
      auto* root = dut->rootp;
      std::printf(
          "  [INFO] single-beat timeout: fsm=%u beats_in=%u beats_out=%u active=%u dir_out=%u "
          "out_valid=%u out_ready=%u rsp_done=%u compute_done=%u\n",
          (unsigned)root->gbp_compute_engine__DOT__u_gbp_control_fsm__DOT__state_r,
          (unsigned)root->gbp_compute_engine__DOT__stream_in_beats_r,
          (unsigned)root->gbp_compute_engine__DOT__stream_out_beats_r,
          (unsigned)root->gbp_compute_engine__DOT__stream_active_r,
          (unsigned)root->gbp_compute_engine__DOT__stream_dir_out_r,
          (unsigned)root->stream_out_valid,
          (unsigned)root->stream_out_ready,
          (unsigned)root->rsp_done_o,
          (unsigned)root->compute_done_o);
    }

    check(saw_stream_out_valid, "2DOF variable should produce single-beat output");
    check(saw_rsp_done, "Single-beat output should complete on first ready pulse");
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
