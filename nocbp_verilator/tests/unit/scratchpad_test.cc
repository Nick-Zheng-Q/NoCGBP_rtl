#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include "verilated.h"
#include "Vscratchpad_test_top.h"

namespace {

constexpr int NUM_ADDRS = 16;  // 4-bit address space (0-15)
constexpr int NUM_TESTS = 100;  // Number of random operations

void tick(Vscratchpad_test_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

void reset_dut(Vscratchpad_test_top* dut, int cycles = 2) {
  dut->rst_n = 0;
  dut->w_v_i = 0;
  dut->w_addr_i = 0;
  dut->w_data_i = 0;
  dut->r_v_i = 0;
  dut->r_addr_i = 0;
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
  tick(dut);
}

// Write to scratchpad
void write_mem(Vscratchpad_test_top* dut, uint32_t addr, uint32_t data) {
  dut->w_v_i = 1;
  dut->w_addr_i = addr & 0xF;
  dut->w_data_i = data;
  tick(dut);
  dut->w_v_i = 0;
}

// Read from scratchpad (returns data after 1 cycle)
uint32_t read_mem(Vscratchpad_test_top* dut, uint32_t addr) {
  dut->r_v_i = 1;
  dut->r_addr_i = addr & 0xF;
  tick(dut);
  dut->r_v_i = 0;
  return dut->r_data_o;
}

// Generate random address in valid range
uint32_t random_addr() {
  return std::rand() % NUM_ADDRS;
}

// Generate random 32-bit data
uint32_t rand_data() {
  return static_cast<uint32_t>(std::rand()) |
         (static_cast<uint32_t>(std::rand()) << 16);
}

}  // namespace

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vscratchpad_test_top;

  int failures = 0;

  // Seed random number generator
  std::srand(static_cast<unsigned>(time(nullptr)));

  std::fprintf(stdout, "========================================\n");
  std::fprintf(stdout, "Scratchpad Random Address Test\n");
  std::fprintf(stdout, "========================================\n\n");

  // Initialize
  reset_dut(dut, 5);

  std::fprintf(stdout, "=== Test 1: Random Write/Read (100 iterations) ===\n\n");

  // Store expected values in an array
  uint32_t memory[NUM_ADDRS];
  for (int i = 0; i < NUM_ADDRS; ++i) {
    memory[i] = 0;
  }

  // Initialize counters
  int write_count = 0;
  int read_count = 0;
  int addr_hits[NUM_ADDRS] = {0};

  // Perform random write/read operations
  for (int i = 0; i < NUM_TESTS; ++i) {
    uint32_t addr = random_addr();
    uint32_t data = rand_data();

    if (std::rand() % 2 == 0) {
      // Write
      write_mem(dut, addr, data);
      memory[addr] = data;
      write_count++;
    } else {
      // Read
      uint32_t read_data = read_mem(dut, addr);
      if (read_data != memory[addr]) {
        std::fprintf(stdout, "FAIL: addr=%d exp=0x%08x got=0x%08x\n",
                     addr, memory[addr], read_data);
        failures++;
      }
      read_count++;
    }
  }

   std::fprintf(stdout, "Random write/read: %d/%d passed\n\n", NUM_TESTS - failures, NUM_TESTS);

   // Test 2: Random Bank Access Pattern (mixed read/write)
   std::fprintf(stdout, "=== Test 2: Random Bank Access Pattern ===\n\n");

   // Initialize counters for Test 2
   write_count = 0;
   read_count = 0;

   for (int i = 0; i < NUM_TESTS; ++i) {
     uint32_t addr = random_addr();
     uint32_t data = rand_data();

     if (std::rand() % 2 == 0) {
       // Write
       write_mem(dut, addr, data);
       memory[addr] = data;
       write_count++;
     } else {
       // Read
       uint32_t read_data = read_mem(dut, addr);
       if (read_data != memory[addr]) {
         std::fprintf(stdout, "FAIL: addr=%d exp=0x%08x got=0x%08x\n",
                      addr, memory[addr], read_data);
         failures++;
       }
       read_count++;
     }
   }

   std::fprintf(stdout, "Mixed write/read: %d writes, %d reads\n\n", write_count, read_count);

   // Test 3: Address Space Coverage
   std::fprintf(stdout, "=== Test 3: Address Space Coverage ===\n\n");

   // Reset addr_hits for Test 3
   for (int i = 0; i < NUM_ADDRS; ++i) {
     addr_hits[i] = 0;
   }

   // Write to random addresses
   for (int i = 0; i < NUM_TESTS; ++i) {
     uint32_t addr = random_addr();
     uint32_t data = rand_data();
     write_mem(dut, addr, data);
     memory[addr] = data;
     addr_hits[addr]++;
   }

   // Verify all addresses were accessed
   int missed_addrs = 0;
   for (int i = 0; i < NUM_ADDRS; ++i) {
     if (addr_hits[i] == 0) {
       missed_addrs++;
       std::fprintf(stdout, "  Note: addr %d was never accessed\n", i);
     }
   }

   std::fprintf(stdout, "Address coverage: %d/%d addresses accessed\n\n",
                NUM_ADDRS - missed_addrs, NUM_ADDRS);

   // Test 4: Read After Write
   std::fprintf(stdout, "=== Test 4: Read After Write (random order) ===\n\n");

  // First write to all addresses
  for (int i = 0; i < NUM_ADDRS; ++i) {
    uint32_t data = rand_data();
    write_mem(dut, i, data);
    memory[i] = data;
  }

  // Then read in random order
  bool all_read_ok = true;
  for (int i = 0; i < NUM_TESTS; ++i) {
    uint32_t addr = random_addr();
    uint32_t read_data = read_mem(dut, addr);
    if (read_data != memory[addr]) {
      std::fprintf(stdout, "FAIL: addr=%d exp=0x%08x got=0x%08x\n",
                   addr, memory[addr], read_data);
      failures++;
      all_read_ok = false;
    }
  }

  if (all_read_ok) {
    std::fprintf(stdout, "Read-after-write: all %d reads correct\n", NUM_TESTS);
  }

  delete dut;

  std::fprintf(stdout, "\n========================================\n");
  if (failures == 0) {
    std::fprintf(stdout, "Scratchpad random test: ALL PASSED\n");
  } else {
    std::fprintf(stdout, "Scratchpad random test: %d FAILURES\n", failures);
  }
  std::fprintf(stdout, "========================================\n");

  return failures ? 1 : 0;
}
