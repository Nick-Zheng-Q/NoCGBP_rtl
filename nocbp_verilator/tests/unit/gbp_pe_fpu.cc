#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <limits>

#include "verilated.h"
#include "Vgbp_pe_fpu_top.h"

namespace {

// Convert IEEE-754 uint32 to float and return as double for printing
static inline double uint32_to_float(uint32_t bits) {
  static_assert(sizeof(float) == sizeof(uint32_t), "float size mismatch");
  float f;
  std::memcpy(&f, &bits, sizeof(float));
  return static_cast<double>(f);
}

// Print float in hex and decimal
static inline void print_float(const char* label, uint32_t bits) {
  double val = uint32_to_float(bits);
  std::fprintf(stdout, "%s: hex=0x%08x decimal=%.10g\n", label, bits, val);
}

void tick(Vgbp_pe_fpu_top* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

void reset_dut(Vgbp_pe_fpu_top* dut, int cycles = 2) {
  dut->rst_n = 0;
  dut->valid_i = 0;
  dut->op_i = 0;
  dut->a_i = 0;
  dut->b_i = 0;
  // Hold reset for several cycles
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
  // Wait for several cycles after reset to let FPU pipelines initialize
  for (int i = 0; i < 20; ++i) {
    tick(dut);
  }
}

void issue_op(Vgbp_pe_fpu_top* dut, uint32_t op, uint32_t a, uint32_t b) {
  dut->op_i = op;
  dut->a_i = a;
  dut->b_i = b;
  dut->valid_i = 1;
  // Hold valid high for several cycles to ensure FPU captures input
  tick(dut);
  tick(dut);
  tick(dut);
  dut->valid_i = 0;
}

bool wait_cycles(Vgbp_pe_fpu_top* dut, int cycles) {
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  return true;
}

// Wait for add/sub/mul result (3-cycle pipeline + some margin)
static void wait_for_addsub_mul(Vgbp_pe_fpu_top* dut) {
  // Add/sub/mul have 3 cycle latency, add some margin
  for (int i = 0; i < 10; ++i) {
    tick(dut);
  }
}

// Wait for div result (variable latency, typically 20-60 cycles for single precision)
static void wait_for_div(Vgbp_pe_fpu_top* dut) {
  for (int i = 0; i < 100; ++i) {
    tick(dut);
  }
}

int test_add(Vgbp_pe_fpu_top* dut, uint32_t a, uint32_t b, uint32_t expected) {
  std::fprintf(stdout, "--- ADD test ---\n");
  print_float("  a     ", a);
  print_float("  b     ", b);
  issue_op(dut, 0, a, b);
  wait_for_addsub_mul(dut);
  uint32_t result = dut->y_o;
  print_float("  result", result);
  print_float("  expected", expected);
  if (result == expected) {
    std::fprintf(stdout, "  PASS\n");
    return 0;
  } else {
    std::fprintf(stdout, "  FAIL\n");
    return 1;
  }
}

int test_sub(Vgbp_pe_fpu_top* dut, uint32_t a, uint32_t b, uint32_t expected) {
  std::fprintf(stdout, "--- SUB test ---\n");
  print_float("  a     ", a);
  print_float("  b     ", b);
  issue_op(dut, 1, a, b);
  wait_for_addsub_mul(dut);
  uint32_t result = dut->y_o;
  print_float("  result", result);
  print_float("  expected", expected);
  if (result == expected) {
    std::fprintf(stdout, "  PASS\n");
    return 0;
  } else {
    std::fprintf(stdout, "  FAIL\n");
    return 1;
  }
}

int test_mul(Vgbp_pe_fpu_top* dut, uint32_t a, uint32_t b, uint32_t expected) {
  std::fprintf(stdout, "--- MUL test ---\n");
  print_float("  a     ", a);
  print_float("  b     ", b);
  issue_op(dut, 2, a, b);
  wait_for_addsub_mul(dut);
  uint32_t result = dut->y_o;
  print_float("  result", result);
  print_float("  expected", expected);
  if (result == expected) {
    std::fprintf(stdout, "  PASS\n");
    return 0;
  } else {
    std::fprintf(stdout, "  FAIL\n");
    return 1;
  }
}

int test_div(Vgbp_pe_fpu_top* dut, uint32_t a, uint32_t b, uint32_t expected) {
  std::fprintf(stdout, "--- DIV test ---\n");
  print_float("  a     ", a);
  print_float("  b     ", b);
  issue_op(dut, 3, a, b);
  wait_for_div(dut);
  uint32_t result = dut->y_o;
  print_float("  result", result);
  print_float("  expected", expected);
  if (result == expected) {
    std::fprintf(stdout, "  PASS\n");
    return 0;
  } else {
    std::fprintf(stdout, "  FAIL\n");
    return 1;
  }
}

// Generate random uint32_t (biased towards normal float range)
static uint32_t random_float_bits() {
  // Generate sign, exponent, mantissa
  uint32_t sign = (std::rand() % 2) << 31;
  // Bias towards normal numbers (exponent 1-254)
  uint32_t exp = std::rand() % 255;  // 0-254 (will add 1 later)
  uint32_t mantissa = std::rand();
  return sign | (exp << 23) | (mantissa & 0x007FFFFF);
}

// Compute expected result using float (matching hardware behavior)
// Hardware FPU uses flush-to-zero (FTZ) for subnormal numbers.
// We simulate this behavior including signed zero for accurate comparison.
static uint32_t float_add(uint32_t a, uint32_t b) {
  float fa, fb, fr;
  std::memcpy(&fa, &a, sizeof(float));
  std::memcpy(&fb, &b, sizeof(float));
  fr = fa + fb;
  // Flush subnormal to zero
  if (std::fpclassify(fr) == FP_SUBNORMAL) {
    // 根据 IEEE-754 规则确定符号: 保留结果的符号
    uint32_t sign = (fr < 0) ? 0x80000000u : 0x00000000u;
    return sign;
  }
  uint32_t result;
  std::memcpy(&result, &fr, sizeof(uint32_t));
  return result;
}

static uint32_t float_sub(uint32_t a, uint32_t b) {
  float fa, fb, fr;
  std::memcpy(&fa, &a, sizeof(float));
  std::memcpy(&fb, &b, sizeof(float));
  fr = fa - fb;
  if (std::fpclassify(fr) == FP_SUBNORMAL) {
    uint32_t sign = (fr < 0) ? 0x80000000u : 0x00000000u;
    return sign;
  }
  uint32_t result;
  std::memcpy(&result, &fr, sizeof(uint32_t));
  return result;
}

static uint32_t float_mul(uint32_t a, uint32_t b) {
  float fa, fb, fr;
  std::memcpy(&fa, &a, sizeof(float));
  std::memcpy(&fb, &b, sizeof(float));
  fr = fa * fb;
  if (std::fpclassify(fr) == FP_SUBNORMAL) {
    // 乘法的符号 = sign(a) × sign(b)
    uint32_t sign_a = (a >> 31) & 1;
    uint32_t sign_b = (b >> 31) & 1;
    uint32_t sign = (sign_a ^ sign_b) ? 0x80000000u : 0x00000000u;
    return sign;
  }
  uint32_t result;
  std::memcpy(&result, &fr, sizeof(uint32_t));
  return result;
}

static uint32_t float_div(uint32_t a, uint32_t b) {
  float fa, fb, fr;
  std::memcpy(&fa, &a, sizeof(float));
  std::memcpy(&fb, &b, sizeof(float));
  fr = (fb == 0.0f) ? std::numeric_limits<float>::infinity() : fa / fb;
  if (std::fpclassify(fr) == FP_SUBNORMAL) {
    // 除法的符号 = sign(a) × sign(b)
    uint32_t sign_a = (a >> 31) & 1;
    uint32_t sign_b = (b >> 31) & 1;
    uint32_t sign = (sign_a ^ sign_b) ? 0x80000000u : 0x00000000u;
    return sign;
  }
  uint32_t result;
  std::memcpy(&result, &fr, sizeof(uint32_t));
  return result;
}

}  // namespace

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vgbp_pe_fpu_top;

  int failures = 0;

  // Seed random number generator
  std::srand(12345);

  std::fprintf(stdout, "========================================\n");
  std::fprintf(stdout, "gbp_pe_fpu unit test start\n");
  std::fprintf(stdout, "========================================\n\n");

  // Initialize: short reset, minimal wait (like simple test)
  dut->rst_n = 0;
  dut->valid_i = 0;
  dut->op_i = 0;
  dut->a_i = 0;
  dut->b_i = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
  // Minimal wait after reset (like simple test)
  for (int i = 0; i < 5; ++i) {
    tick(dut);
  }

  // === Basic tests with known values ===
  std::fprintf(stdout, "=== Basic Tests (Known Values) ===\n\n");

  // ADD: 1.5 + 2.5 = 4.0
  failures += test_add(dut, 0x3fc00000u, 0x40200000u, 0x40800000u);

  // SUB: 5.5 - 2.0 = 3.5
  failures += test_sub(dut, 0x40b00000u, 0x40000000u, 0x40600000u);

  // MUL: 1.25 * 3.0 = 3.75
  failures += test_mul(dut, 0x3fa00000u, 0x40400000u, 0x40700000u);

  // DIV: 9.0 / 2.0 = 4.5
  failures += test_div(dut, 0x41100000u, 0x40000000u, 0x40900000u);

  // === Additional edge cases ===
  std::fprintf(stdout, "\n=== Edge Case Tests ===\n\n");

  // Add: 0.0 + 0.0 = 0.0
  failures += test_add(dut, 0x00000000u, 0x00000000u, 0x00000000u);

  // Sub: 1.0 - 1.0 = 0.0
  failures += test_sub(dut, 0x3f800000u, 0x3f800000u, 0x00000000u);

  // Mul: 2.0 * 0.5 = 1.0
  failures += test_mul(dut, 0x40000000u, 0x3f000000u, 0x3f800000u);

  // Div: 1.0 / 1.0 = 1.0
  failures += test_div(dut, 0x3f800000u, 0x3f800000u, 0x3f800000u);

  // === Random tests ===
  std::fprintf(stdout, "\n=== Random Tests (10 iterations each) ===\n\n");

  // Random ADD tests
  std::fprintf(stdout, "Random ADD tests:\n");
  for (int i = 0; i < 10; ++i) {
    uint32_t a = random_float_bits();
    uint32_t b = random_float_bits();
    failures += test_add(dut, a, b, float_add(a, b));
  }

  // Random SUB tests
  std::fprintf(stdout, "\nRandom SUB tests:\n");
  for (int i = 0; i < 10; ++i) {
    uint32_t a = random_float_bits();
    uint32_t b = random_float_bits();
    failures += test_sub(dut, a, b, float_sub(a, b));
  }

  // Random MUL tests
  std::fprintf(stdout, "\nRandom MUL tests:\n");
  for (int i = 0; i < 10; ++i) {
    uint32_t a = random_float_bits();
    uint32_t b = random_float_bits();
    failures += test_mul(dut, a, b, float_mul(a, b));
  }

  // Random DIV tests
  std::fprintf(stdout, "\nRandom DIV tests:\n");
  for (int i = 0; i < 10; ++i) {
    uint32_t a = random_float_bits();
    uint32_t b;
    do {
      b = random_float_bits();
    } while (b == 0x00000000u);  // Avoid division by zero
    failures += test_div(dut, a, b, float_div(a, b));
  }

  delete dut;

  std::fprintf(stdout, "\n========================================\n");
  if (failures == 0) {
    std::fprintf(stdout, "gbp_pe_fpu unit test: ALL PASSED (%d failures)\n", failures);
  } else {
    std::fprintf(stdout, "gbp_pe_fpu unit test: %d FAILURES\n", failures);
  }
  std::fprintf(stdout, "========================================\n");
  return failures ? 1 : 0;
}
