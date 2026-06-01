// simd_array_test.cc
// Verilator-based unit test for simd_array

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "Vsimd_array.h"
#include "verilated.h"

// Test counters
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

// Float <-> uint32 conversion
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

// Set data in VlWide<16> (512-bit for 16 lanes)
void set_lane_data(VlWide<16>& wide, int lane, uint32_t data) {
  wide.m_storage[lane] = data;
}

// Get data from VlWide<16>
uint32_t get_lane_data(const VlWide<16>& wide, int lane) {
  return wide.m_storage[lane];
}

// Set 2-bit control for each lane in a 32-bit value (16 lanes x 2 bits)
void set_lane_ctrl(uint32_t& val, int lane, uint8_t ctrl) {
  int bit_start = lane * 2;
  uint32_t mask = ~(0x3 << bit_start);
  val &= mask;
  val |= (ctrl & 0x3) << bit_start;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  
  auto* dut = new Vsimd_array;
  
  std::printf("========================================\n");
  std::printf("SIMD Array Unit Test\n");
  std::printf("========================================\n\n");
  
  // Initialize
  dut->clk_i = 0;
  dut->reset_i = 1;
  dut->op_add_en = 0;
  dut->op_sub_en = 0;
  dut->op_mul_en = 0;
  dut->op_div_en = 0;
  dut->op_mac_en = 0;
  dut->src_a_sel = 0;
  dut->src_b_sel = 0;
  
  for (int i = 0; i < 16; i++) {
    set_lane_data(dut->data_a_i, i, 0);
    set_lane_data(dut->data_b_i, i, 0);
    set_lane_data(dut->const_val, i, 0);
  }
  
  // Reset
  std::printf("[Reset] Applying reset...\n");
  for (int i = 0; i < 10; i++) {
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
  }
  dut->reset_i = 0;
  for (int i = 0; i < 5; i++) {
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
  }
  dut->reset_i = 1;
  std::printf("[Reset] Done.\n\n");
  
  // Test 1: FP Addition
  std::printf("[Test 1] FP Addition (1.0 + 2.0 = 3.0)\n");
  {
    // Setup inputs
    set_lane_data(dut->data_a_i, 0, f2u(1.0f));
    set_lane_data(dut->data_b_i, 0, f2u(2.0f));
    
    // Select buffer sources
    set_lane_ctrl(dut->src_a_sel, 0, 0);  // data_a
    set_lane_ctrl(dut->src_b_sel, 0, 1);  // data_b
    
    // Enable add
    dut->op_add_en = 0x0001;  // Only lane 0
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    // Check result (should be available immediately for add)
    float result = u2f(get_lane_data(dut->result_o, 0));
    char msg[256];
    std::snprintf(msg, sizeof(msg), "1.0 + 2.0 = %f (expected 3.0)", result);
    check(std::fabs(result - 3.0f) < 0.001f, msg);
    
    dut->op_add_en = 0;
  }
  std::printf("\n");
  
  // Test 2: FP Subtraction
  std::printf("[Test 2] FP Subtraction (5.0 - 2.0 = 3.0)\n");
  {
    set_lane_data(dut->data_a_i, 0, f2u(5.0f));
    set_lane_data(dut->data_b_i, 0, f2u(2.0f));
    set_lane_ctrl(dut->src_a_sel, 0, 0);
    set_lane_ctrl(dut->src_b_sel, 0, 1);
    
    dut->op_sub_en = 0x0001;
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    float result = u2f(get_lane_data(dut->result_o, 0));
    char msg[256];
    std::snprintf(msg, sizeof(msg), "5.0 - 2.0 = %f (expected 3.0)", result);
    check(std::fabs(result - 3.0f) < 0.001f, msg);
    
    dut->op_sub_en = 0;
  }
  std::printf("\n");
  
  // Test 3: FP Multiplication
  std::printf("[Test 3] FP Multiplication (3.0 * 4.0 = 12.0)\n");
  {
    set_lane_data(dut->data_a_i, 0, f2u(3.0f));
    set_lane_data(dut->data_b_i, 0, f2u(4.0f));
    set_lane_ctrl(dut->src_a_sel, 0, 0);
    set_lane_ctrl(dut->src_b_sel, 0, 1);
    
    dut->op_mul_en = 0x0001;
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    float result = u2f(get_lane_data(dut->result_o, 0));
    char msg[256];
    std::snprintf(msg, sizeof(msg), "3.0 * 4.0 = %f (expected 12.0)", result);
    check(std::fabs(result - 12.0f) < 0.001f, msg);
    
    dut->op_mul_en = 0;
  }
  std::printf("\n");
  
  // Test 4: Multi-lane parallel operations
  std::printf("[Test 4] Multi-lane parallel addition\n");
  {
    float inputs_a[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    float inputs_b[8] = {0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f};
    
    for (int i = 0; i < 8; i++) {
      set_lane_data(dut->data_a_i, i, f2u(inputs_a[i]));
      set_lane_data(dut->data_b_i, i, f2u(inputs_b[i]));
      set_lane_ctrl(dut->src_a_sel, i, 0);
      set_lane_ctrl(dut->src_b_sel, i, 1);
    }
    
    dut->op_add_en = 0x00FF;  // Lanes 0-7
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    for (int i = 0; i < 8; i++) {
      float result = u2f(get_lane_data(dut->result_o, i));
      float expected = inputs_a[i] + inputs_b[i];
      char msg[256];
      std::snprintf(msg, sizeof(msg), "Lane %d: %f + %f = %f", 
                    i, inputs_a[i], inputs_b[i], result);
      check(std::fabs(result - expected) < 0.001f, msg);
    }
    
    dut->op_add_en = 0;
  }
  std::printf("\n");
  
  // Test 5: Constant input
  std::printf("[Test 5] Constant input (x + 10.0)\n");
  {
    set_lane_data(dut->data_a_i, 0, f2u(5.0f));
    set_lane_data(dut->const_val, 0, f2u(10.0f));
    set_lane_ctrl(dut->src_a_sel, 0, 0);  // data_a
    set_lane_ctrl(dut->src_b_sel, 0, 3);  // const
    
    dut->op_add_en = 0x0001;
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    float result = u2f(get_lane_data(dut->result_o, 0));
    char msg[256];
    std::snprintf(msg, sizeof(msg), "5.0 + 10.0 = %f (expected 15.0)", result);
    check(std::fabs(result - 15.0f) < 0.001f, msg);
    
    dut->op_add_en = 0;
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
