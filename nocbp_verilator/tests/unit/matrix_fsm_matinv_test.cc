// matrix_fsm_matinv_test.cc
// Unit test for matrix_fsm MatInv operation

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "Vmatrix_fsm.h"
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

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vmatrix_fsm;
  
  std::printf("========================================\n");
  std::printf("Matrix FSM MatInv Unit Test\n");
  std::printf("========================================\n\n");
  
  // Initialize
  dut->clk_i = 0;
  dut->reset_i = 1;
  dut->cmd_valid = 0;
  dut->simd_valid = 0;
  for (int i = 0; i < 16; i++) {
    dut->buf_rd_data_a[i] = 0;
    dut->buf_rd_data_b[i] = 0;
    dut->simd_result[i] = 0;
  }
  
  // Reset
  for (int i = 0; i < 10; i++) {
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
  }
  dut->reset_i = 0;
  for (int i = 0; i < 5; i++) {
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
  }
  std::printf("[Reset] Done.\n\n");
  
  // Test 1: MatInv 2x2 - verify command execution and completion
  std::printf("[Test 1] MatInv 2x2 command execution\n");
  {
    float mat_a[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    
    dut->cmd_valid = 1;
    dut->cmd_op = 3;  // MatInv
    dut->cmd_base_a = 0;
    dut->cmd_base_b = 0;
    dut->cmd_base_dest = 32;
    dut->cmd_m = 2;
    dut->cmd_n = 2;
    dut->cmd_k = 2;
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    dut->cmd_valid = 0;
    
    int cycle = 0;
    int max_cycles = 200;
    int write_count = 0;
    int matinv_started = 0;
    int matinv_done = 0;
    
    while (cycle++ < max_cycles && !dut->done_o) {
      // Provide matrix data
      for (int lane = 0; lane < 4; lane++) {
        uint8_t addr_a = dut->buf_rd_addr_a[lane] & 0x3F;
        if (addr_a < 4) dut->buf_rd_data_a[lane] = f2u(mat_a[addr_a]);
      }
      
      // Provide SIMD results
      if (dut->simd_op_mul & 0x1) {
        dut->simd_valid = 0x1;
        dut->simd_result[0] = f2u(1.0f);
      } else if (dut->simd_op_sub & 0x1) {
        dut->simd_valid = 0x1;
        dut->simd_result[0] = f2u(0.0f);
      } else if (dut->simd_op_add & 0x1) {
        dut->simd_valid = 0x1;
        dut->simd_result[0] = f2u(1.0f);
      } else {
        dut->simd_valid = 0;
      }
      
      // Track writes
      for (int lane = 0; lane < 16; lane++) {
        if (dut->buf_wr_valid & (1 << lane)) {
          write_count++;
        }
      }
      
      dut->clk_i = 0; dut->eval();
      dut->clk_i = 1; dut->eval();
    }
    
    std::printf("  Cycles: %d, Writes: %d\n", cycle, write_count);
    check(cycle < max_cycles, "MatInv completed within timeout");
    check(dut->done_o, "MatInv done_o asserted");
    check(write_count > 0, "MatInv produced at least one write");
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
