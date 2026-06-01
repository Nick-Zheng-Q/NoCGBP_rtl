// staging_buffer_test.cc
// Verilator-based unit test for staging_buffer

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Vstaging_buffer.h"
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

// Set a bit in a 16-bit value
void set_bit16(uint16_t& val, int bit, bool on) {
  if (on) val |= (1 << bit);
  else val &= ~(1 << bit);
}

// Pack 8 values into VlWide<8> (256-bit)
void set_wide_256(VlWide<8>& wide, uint32_t v0, uint32_t v1, uint32_t v2, uint32_t v3,
                  uint32_t v4, uint32_t v5, uint32_t v6, uint32_t v7) {
  wide.m_storage[0] = v0;
  wide.m_storage[1] = v1;
  wide.m_storage[2] = v2;
  wide.m_storage[3] = v3;
  wide.m_storage[4] = v4;
  wide.m_storage[5] = v5;
  wide.m_storage[6] = v6;
  wide.m_storage[7] = v7;
}

// Get 32-bit value from VlWide<8>
uint32_t get_wide_32(const VlWide<8>& wide, int idx) {
  return wide.m_storage[idx];
}

// Set address in VlWide<3> (96-bit for 16 lanes x 6 bits each)
void set_simd_addr(VlWide<3>& wide, int lane, uint8_t addr) {
  // Each address is 6 bits (for 64-entry depth)
  // Packed into 96 bits (16 * 6 = 96)
  int bit_start = lane * 6;
  int word_idx = bit_start / 32;
  int bit_offset = bit_start % 32;
  
  // Clear the 6 bits
  uint32_t mask = ~(0x3F << bit_offset);
  wide.m_storage[word_idx] &= mask;
  
  // Set the new value
  wide.m_storage[word_idx] |= (addr & 0x3F) << bit_offset;
  
  // Handle overflow to next word
  if (bit_offset > 26) {
    int overflow_bits = bit_offset - 26;
    wide.m_storage[word_idx + 1] &= ~(0x3F >> (6 - overflow_bits));
    wide.m_storage[word_idx + 1] |= (addr & 0x3F) >> (6 - overflow_bits);
  }
}

// Set data in VlWide<16> (512-bit for 16 lanes x 32 bits each)
void set_simd_data(VlWide<16>& wide, int lane, uint32_t data) {
  wide.m_storage[lane] = data;
}

// Get data from VlWide<16>
uint32_t get_simd_data(const VlWide<16>& wide, int lane) {
  return wide.m_storage[lane];
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  
  auto* dut = new Vstaging_buffer;
  
  std::printf("========================================\n");
  std::printf("Staging Buffer Unit Test\n");
  std::printf("========================================\n\n");
  
  // Initialize
  dut->clk_i = 0;
  dut->reset_i = 1;
  dut->stream_wr_valid = 0;
  dut->stream_rd_valid = 0;
  dut->simd_wr_valid = 0;
  
  // Reset sequence
  std::printf("[Reset] Applying reset...\n");
  for (int i = 0; i < 10; i++) {
    dut->clk_i = 0;
    dut->eval();
    dut->clk_i = 1;
    dut->eval();
  }
  dut->reset_i = 0;
  for (int i = 0; i < 5; i++) {
    dut->clk_i = 0;
    dut->eval();
    dut->clk_i = 1;
    dut->eval();
  }
  dut->reset_i = 1;
  std::printf("[Reset] Done.\n\n");
  
  // Test 1: Stream Write/Read at address 0
  std::printf("[Test 1] Stream Write/Read at address 0\n");
  {
    uint32_t test_pattern[8] = {
      0x3F800000, 0x40000000, 0x40400000, 0x40800000,
      0x40A00000, 0x40C00000, 0x40E00000, 0x41000000
    };
    
    dut->stream_wr_valid = 1;
    dut->stream_wr_addr = 0;
    set_wide_256(dut->stream_wr_data, 
                 test_pattern[0], test_pattern[1], test_pattern[2], test_pattern[3],
                 test_pattern[4], test_pattern[5], test_pattern[6], test_pattern[7]);
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    dut->stream_wr_valid = 0;
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    dut->stream_rd_valid = 1;
    dut->stream_rd_addr = 0;
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    for (int i = 0; i < 8; i++) {
      uint32_t read_val = get_wide_32(dut->stream_rd_data, i);
      char msg[256];
      std::snprintf(msg, sizeof(msg), "Stream read word %d", i);
      check(read_val == test_pattern[i], msg);
    }
    dut->stream_rd_valid = 0;
  }
  std::printf("\n");
  
  // Test 2: SIMD Write single lane, then read
  std::printf("[Test 2] SIMD Write single lane, read\n");
  {
    // Use lane 0 only to simplify
    dut->simd_wr_valid = 0x0001;  // Only lane 0
    set_simd_addr(dut->simd_wr_addr, 0, 20);  // Address 20
    set_simd_data(dut->simd_wr_data, 0, 0xDEADBEEF);
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    dut->simd_wr_valid = 0;
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    // Read back via stream at address 20
    dut->stream_rd_valid = 1;
    dut->stream_rd_addr = 20;
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    uint32_t read_val = get_wide_32(dut->stream_rd_data, 0);
    char msg[256];
    std::snprintf(msg, sizeof(msg), "SIMD single write: expected 0xDEADBEEF, got 0x%08X", read_val);
    check(read_val == 0xDEADBEEF, msg);
    
    dut->stream_rd_valid = 0;
  }
  std::printf("\n");
  
  // Test 3: SIMD Write multiple lanes
  std::printf("[Test 3] SIMD Write multiple lanes\n");
  {
    // Write 8 lanes to addresses 32-39
    dut->simd_wr_valid = 0x00FF;  // Lanes 0-7
    for (int i = 0; i < 8; i++) {
      set_simd_addr(dut->simd_wr_addr, i, 32 + i);
      set_simd_data(dut->simd_wr_data, i, 0xA0000000 + i);
    }
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    dut->simd_wr_valid = 0;
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    // Read back via stream at address 32
    dut->stream_rd_valid = 1;
    dut->stream_rd_addr = 32;
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    for (int i = 0; i < 8; i++) {
      uint32_t expected = 0xA0000000 + i;
      uint32_t read_val = get_wide_32(dut->stream_rd_data, i);
      char msg[256];
      std::snprintf(msg, sizeof(msg), "Multi-lane write word %d", i);
      check(read_val == expected, msg);
    }
    dut->stream_rd_valid = 0;
  }
  std::printf("\n");
  
  // Test 4: SIMD Read
  std::printf("[Test 4] SIMD Read\n");
  {
    // First, populate addresses 48-55 via stream
    dut->stream_wr_valid = 1;
    dut->stream_wr_addr = 48;
    set_wide_256(dut->stream_wr_data,
                 0xC0000000, 0xC0000001, 0xC0000002, 0xC0000003,
                 0xC0000004, 0xC0000005, 0xC0000006, 0xC0000007);
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    dut->stream_wr_valid = 0;
    
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    // Set up SIMD read addresses (8 lanes)
    for (int i = 0; i < 8; i++) {
      set_simd_addr(dut->simd_rd_addr_a, i, 48 + i);
    }
    dut->clk_i = 0; dut->eval();
    dut->clk_i = 1; dut->eval();
    
    // Check results
    for (int i = 0; i < 8; i++) {
      uint32_t expected = 0xC0000000 + i;
      uint32_t read_val = get_simd_data(dut->simd_rd_data_a, i);
      char msg[256];
      std::snprintf(msg, sizeof(msg), "SIMD read lane %d", i);
      check(read_val == expected, msg);
    }
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
