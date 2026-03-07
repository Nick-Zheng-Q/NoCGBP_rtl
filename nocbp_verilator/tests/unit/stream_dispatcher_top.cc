#include <cstdint>
#include <cstdio>

#include "verilated.h"
#include "Vstream_dispatcher_top.h"

static constexpr uint32_t kRowBytesLg = 5;

static uint32_t bank_id_from_addr(uint32_t addr) {
  return (addr >> kRowBytesLg) & 0x7u;
}

static bool mapping_allowed(uint32_t mode, uint32_t bank_id) {
  if (mode == 0) {
    return bank_id == 0;
  }
  if (mode == 1) {
    return bank_id >= 1 && bank_id <= 3;
  }
  if (mode == 2) {
    return bank_id >= 4 && bank_id <= 7;
  }
  return false;
}

static void tick(Vstream_dispatcher_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vstream_dispatcher_top* dut) {
  dut->rst_n = 0;
  dut->i_valid = 0;
  dut->i_mode = 0;
  dut->i_node_address = 0;
  dut->i_xfer_bytes = 0;
  dut->i_addr_step_bytes = 0;
  dut->i_read_ready = 0;
  dut->i_write_ready = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  tick(dut);
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Vstream_dispatcher_top;
  reset_dut(dut);

  std::printf("stream_dispatcher unit test\n");

  std::printf("Test 1: read path forwards descriptor\n");
  dut->i_read_ready = 1;
  dut->i_write_ready = 0;
  dut->i_valid = 1;
  dut->i_mode = 0;
  dut->i_node_address = 0x1234;
  dut->i_xfer_bytes = 64;
  dut->i_addr_step_bytes = 32;
  tick(dut);

  if (!dut->o_read_valid) {
    std::fprintf(stderr, "FAIL: expected read valid\n");
    delete dut;
    return 1;
  }
  if (dut->o_write_valid) {
    std::fprintf(stderr, "FAIL: write valid should be zero\n");
    delete dut;
    return 1;
  }
  if (!dut->o_ready) {
    std::fprintf(stderr, "FAIL: ready should follow read_ready\n");
    delete dut;
    return 1;
  }
  if (dut->o_read_base_addr != 0x1234) {
    std::fprintf(stderr, "FAIL: base_addr mismatch\n");
    delete dut;
    return 1;
  }
  if (dut->o_read_xfer_bytes != 64 || dut->o_read_addr_step_bytes != 32) {
    std::fprintf(stderr, "FAIL: transfer fields mismatch\n");
    delete dut;
    return 1;
  }
  if (dut->o_read_op != 0) {
    std::fprintf(stderr, "FAIL: op should be OP_READ(0)\n");
    delete dut;
    return 1;
  }
  if ((dut->o_read_operand_id & 0x3) != 0) {
    std::fprintf(stderr, "FAIL: expected operand class STREAM_META(0)\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 1\n");

  std::printf("Test 2: mode=1 still uses read path (current policy)\n");
  dut->i_mode = 1;
  dut->i_read_ready = 1;
  dut->i_write_ready = 1;
  dut->i_valid = 1;
  dut->i_node_address = 0x2222;
  tick(dut);

  if (!dut->o_read_valid || dut->o_write_valid) {
    std::fprintf(stderr, "FAIL: expected read-only dispatch behavior\n");
    delete dut;
    return 1;
  }
  if (dut->o_read_base_addr != 0x2222) {
    std::fprintf(stderr, "FAIL: mode=1 base_addr mismatch\n");
    delete dut;
    return 1;
  }
  if ((dut->o_read_operand_id & 0x3) != 1) {
    std::fprintf(stderr, "FAIL: expected operand class STREAM_VEC(1)\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 2\n");

  std::printf("Test 3: mode=2 maps operand class to STREAM_MESSAGE\n");
  dut->i_mode = 2;
  dut->i_valid = 1;
  dut->i_node_address = 0x3333;
  tick(dut);

  if (!dut->o_read_valid || dut->o_write_valid) {
    std::fprintf(stderr, "FAIL: expected read-only dispatch behavior for mode=2\n");
    delete dut;
    return 1;
  }
  if ((dut->o_read_operand_id & 0x3) != 2) {
    std::fprintf(stderr, "FAIL: expected operand class STREAM_MESSAGE(2)\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 3\n");

  std::printf("Test 4: mode=3 (invalid) issues no descriptor, remains non-blocking\n");
  dut->i_mode = 3;  // Invalid: 2'b11 outside valid META(0)/VEC(1)/MESSAGE(2)
  dut->i_valid = 1;
  dut->i_node_address = 0x4444;
  dut->i_read_ready = 0;  // Read channel not ready, but should not matter
  tick(dut);

  if (dut->o_read_valid) {
    std::fprintf(stderr, "FAIL: invalid mode should not issue read descriptor\n");
    delete dut;
    return 1;
  }
  if (dut->o_write_valid) {
    std::fprintf(stderr, "FAIL: invalid mode should not issue write descriptor\n");
    delete dut;
    return 1;
  }
  if (!dut->o_ready) {
    std::fprintf(stderr, "FAIL: invalid mode should set ready=1 (non-blocking)\n");
    delete dut;
    return 1;
  }
  std::printf("PASS: Test 4\n");

  std::printf("Test 5: allowed class->bank mapping vectors pass invariants\n");
  struct MappingVector {
    uint32_t mode;
    uint32_t addr;
  };
  const MappingVector allowed_vectors[] = {
      {0, 0x00000},
      {1, 0x00020},
      {1, 0x00060},
      {2, 0x00080},
      {2, 0x000E0},
  };

  for (const auto& vec : allowed_vectors) {
    dut->i_mode = vec.mode;
    dut->i_node_address = vec.addr;
    dut->i_valid = 1;
    dut->i_read_ready = 1;
    tick(dut);

    if (!dut->o_read_valid) {
      std::fprintf(stderr, "FAIL: expected read valid for allowed vector mode=%u addr=0x%05x\n", vec.mode, vec.addr);
      delete dut;
      return 1;
    }
    const uint32_t bank_id = bank_id_from_addr(dut->o_read_base_addr);
    if (!mapping_allowed(vec.mode, bank_id)) {
      std::fprintf(stderr,
                   "FAIL: allowed vector violated bank policy mode=%u addr=0x%05x bank=%u\n",
                   vec.mode,
                   dut->o_read_base_addr,
                   bank_id);
      delete dut;
      return 1;
    }
  }
  std::printf("PASS: Test 5\n");

  std::printf("Test 6: forbidden class->bank mapping vectors are detected\n");
  const MappingVector forbidden_vectors[] = {
      {0, 0x00080},
      {1, 0x00000},
      {2, 0x00040},
  };

  for (const auto& vec : forbidden_vectors) {
    dut->i_mode = vec.mode;
    dut->i_node_address = vec.addr;
    dut->i_valid = 1;
    dut->i_read_ready = 1;
    tick(dut);

    if (!dut->o_read_valid) {
      std::fprintf(stderr,
                   "FAIL: expected read valid while checking forbidden vector mode=%u addr=0x%05x\n",
                   vec.mode,
                   vec.addr);
      delete dut;
      return 1;
    }
    const uint32_t bank_id = bank_id_from_addr(dut->o_read_base_addr);
    if (mapping_allowed(vec.mode, bank_id)) {
      std::fprintf(stderr,
                   "FAIL: forbidden vector unexpectedly satisfies policy mode=%u addr=0x%05x bank=%u\n",
                   vec.mode,
                   dut->o_read_base_addr,
                   bank_id);
      delete dut;
      return 1;
    }
  }
  std::printf("PASS: Test 6\n");

  std::printf("All tests passed\n");
  delete dut;
  return 0;
}
