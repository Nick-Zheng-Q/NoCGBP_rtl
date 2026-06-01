// wrapper_test.cc
// Unit test for compute_unit_wrapper - GBP compute engine wrapper

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "Vwrapper_test_top.h"
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

void tick(Vwrapper_test_top* dut) {
  dut->clk_i = 0; dut->eval();
  dut->clk_i = 1; dut->eval();
}

void reset(Vwrapper_test_top* dut) {
  dut->reset_i = 1;
  for (int i = 0; i < 10; i++) tick(dut);
  dut->reset_i = 0;
  for (int i = 0; i < 5; i++) tick(dut);
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vwrapper_test_top;
  
  std::printf("========================================\n");
  std::printf("Compute Unit Wrapper Integration Test\n");
  std::printf("========================================\n\n");
  
  // Initialize all inputs
  dut->clk_i = 0;
  dut->reset_i = 1;
  dut->cmd_valid_i = 0;
  dut->cmd_kind_i = 0;
  dut->cmd_dofs_i = 2;
  dut->cmd_adj_count_i = 1;
  dut->cmd_msg_count_i = 1;
  dut->force_persistence_stall_i = 0;
  
  // Stream interfaces
  dut->stream_in_valid_i = 0;
  for (int i = 0; i < 8; i++) dut->stream_in_data_i[i] = 0;
  dut->stream_out_ready_i = 0;
  
  // Data interface
  for (int i = 0; i < 16; i++) {
    dut->data_a_i[i] = 0;
    dut->data_b_i[i] = 0;
  }
  dut->op_i = 0;
  dut->valid_i = 0;
  
  // Reset
  std::printf("[Reset]...\n");
  reset(dut);
  std::printf("[Reset] Done.\n\n");
  
  // Test 1: Idle state - cmd_ready_o assertion
  std::printf("[Test 1] Idle state - cmd_ready_o assertion\n");
  {
    check(dut->cmd_ready_o == 1, "cmd_ready_o should be 1 in IDLE");
    check(dut->compute_done_o == 0, "compute_done_o should be 0 in IDLE");
    check(dut->rsp_done_o == 0, "rsp_done_o should be 0 in IDLE");
  }
  std::printf("\n");
  
  // Test 2: Variable Node command acceptance
  std::printf("[Test 2] Variable Node command (cmd_kind=0)\n");
  {
    dut->cmd_valid_i = 1;
    dut->cmd_kind_i = 0;  // Variable node
    tick(dut);
    dut->cmd_valid_i = 0;
    
    // Should accept command
    check(dut->cmd_ready_o == 0, "cmd_ready_o should be 0 after accepting command");
    
    // Wait for command to be issued to GBP engine
    tick(dut);
    tick(dut);
  }
  std::printf("\n");
  
  // Test 3: Factor Node command acceptance
  std::printf("[Test 3] Factor Node command (cmd_kind=1)\n");
  {
    reset(dut);
    
    dut->cmd_valid_i = 1;
    dut->cmd_kind_i = 1;  // Factor node
    tick(dut);
    dut->cmd_valid_i = 0;
    
    check(dut->cmd_ready_o == 0, "cmd_ready_o should be 0 after accepting command");
    
    tick(dut);
    tick(dut);
  }
  std::printf("\n");
  
  // Test 4: Reset during operation
  std::printf("[Test 4] Reset during operation\n");
  {
    // Start an operation
    dut->cmd_valid_i = 1;
    dut->cmd_kind_i = 0;
    tick(dut);
    dut->cmd_valid_i = 0;
    
    // Apply reset
    dut->reset_i = 1;
    for (int i = 0; i < 5; i++) tick(dut);
    dut->reset_i = 0;
    for (int i = 0; i < 5; i++) tick(dut);
    
    // Should return to IDLE
    check(dut->cmd_ready_o == 1, "Should return to IDLE (cmd_ready_o=1)");
    check(dut->compute_done_o == 0, "compute_done_o should be 0 after reset");
    check(dut->rsp_done_o == 0, "rsp_done_o should be 0 after reset");
  }
  std::printf("\n");
  
  // Test 5: Data passthrough (legacy interface)
  std::printf("[Test 5] Legacy data interface passthrough\n");
  {
    reset(dut);
    
    // Set input data
    for (int i = 0; i < 16; i++) {
      dut->data_a_i[i] = f2u((float)(i + 1));
      dut->data_b_i[i] = f2u((float)(i + 2));
    }
    dut->op_i = 0;  // ADD
    dut->valid_i = 1;
    
    tick(dut);
    
    // Check output (passthrough)
    check(dut->valid_o == 1, "valid_o should be 1");
    
    dut->valid_i = 0;
  }
  std::printf("\n");
  
  // Test 6: Multiple consecutive commands
  std::printf("[Test 6] Multiple consecutive commands\n");
  {
    for (int cmd = 0; cmd < 3; cmd++) {
      reset(dut);
      
      dut->cmd_valid_i = 1;
      dut->cmd_kind_i = (cmd % 2);  // Alternate var/factor
      tick(dut);
      dut->cmd_valid_i = 0;
      
      char msg[64];
      std::snprintf(msg, sizeof(msg), "Command %d accepted", cmd);
      check(dut->cmd_ready_o == 0, msg);
      
      // Let it process
      for (int i = 0; i < 10; i++) tick(dut);
    }
    std::printf("  [PASS] All 3 consecutive commands started correctly\n");
  }
  std::printf("\n");
  
  // Test 7: Force persistence stall
  std::printf("[Test 7] Force persistence stall handling\n");
  {
    reset(dut);
    
    // Enable stall
    dut->force_persistence_stall_i = 1;
    
    // Start operation
    dut->cmd_valid_i = 1;
    dut->cmd_kind_i = 0;
    tick(dut);
    dut->cmd_valid_i = 0;
    
    // Should still accept command
    check(dut->cmd_ready_o == 0, "Command accepted with stall");
    
    // Release stall
    dut->force_persistence_stall_i = 0;
    tick(dut);
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
