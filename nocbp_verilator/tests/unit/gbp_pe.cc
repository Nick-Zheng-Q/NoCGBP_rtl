#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

#include "verilated.h"
#include "Vgbp_pe.h"

static void tick(Vgbp_pe* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe* dut, int cycles = 2) {
  dut->rst_n = 0;
  dut->data_i = 0;
  dut->data_b_i = 0;
  dut->op_i = 0;
  dut->length_i = 0;
  for (int i = 0; i < cycles; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
}

static int fail(const char* msg) {
  std::fprintf(stderr, "gbp_pe: FAIL: %s\n", msg);
  return 1;
}

struct OracleExpect {
  uint32_t scenario_a_expected;
  uint32_t scenario_b_expected;
  double abs_tol;
  double rel_tol;
};

static bool extract_hex_field(const std::string& text, const char* key, uint32_t* out) {
  std::regex rx(std::string("\"") + key + "\"\\s*:\\s*\"0x([0-9a-fA-F]+)\"");
  std::smatch m;
  if (!std::regex_search(text, m, rx) || m.size() < 2) {
    return false;
  }
  *out = static_cast<uint32_t>(std::stoul(m[1].str(), nullptr, 16));
  return true;
}

static bool extract_double_field(const std::string& text, const char* key, double* out) {
  std::regex rx(std::string("\"") + key + "\"\\s*:\\s*([-+0-9.eE]+)");
  std::smatch m;
  if (!std::regex_search(text, m, rx) || m.size() < 2) {
    return false;
  }
  char* end = nullptr;
  const double parsed = std::strtod(m[1].str().c_str(), &end);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  *out = parsed;
  return true;
}

static OracleExpect load_oracle(const char* path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "gbp_pe: FAIL: unable to open oracle file: %s\n", path);
    std::exit(2);
  }
  std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  OracleExpect out{};
  if (!extract_hex_field(text, "scenario_a_expected_u32", &out.scenario_a_expected)) {
    std::fprintf(stderr, "gbp_pe: FAIL: oracle missing scenario_a_expected_u32\n");
    std::exit(2);
  }
  if (!extract_hex_field(text, "scenario_b_expected_u32", &out.scenario_b_expected)) {
    std::fprintf(stderr, "gbp_pe: FAIL: oracle missing scenario_b_expected_u32\n");
    std::exit(2);
  }
  if (!extract_double_field(text, "abs_tol_u32", &out.abs_tol)) {
    std::fprintf(stderr, "gbp_pe: FAIL: oracle missing abs_tol_u32\n");
    std::exit(2);
  }
  if (!extract_double_field(text, "rel_tol", &out.rel_tol)) {
    std::fprintf(stderr, "gbp_pe: FAIL: oracle missing rel_tol\n");
    std::exit(2);
  }
  return out;
}

static bool check_oracle_word(const char* scenario,
                              uint32_t observed,
                              uint32_t expected,
                              double abs_tol,
                              double rel_tol) {
  const double abs_err = std::fabs(static_cast<double>(observed) - static_cast<double>(expected));
  const double rel_err = abs_err / std::fmax(std::fabs(static_cast<double>(expected)), 1.0);
  if (abs_err <= abs_tol || rel_err <= rel_tol) {
    return true;
  }

  std::fprintf(stderr,
               "gbp_pe: FAIL: oracle mismatch scenario=%s observed=0x%08x expected=0x%08x abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g pass_rule=abs_err<=abs_tol||rel_err<=rel_tol\n",
               scenario,
               observed,
               expected,
               abs_err,
               rel_err,
               abs_tol,
               rel_tol);
  return false;
}

static uint32_t run_scenario(Vgbp_pe* dut, uint32_t data_i, uint32_t data_b_i, uint8_t op_i) {
  dut->data_i = data_i;
  dut->data_b_i = data_b_i;
  dut->op_i = op_i;
  dut->length_i = 1;

  for (int i = 0; i < 4; ++i) {
    tick(dut);
  }

  dut->length_i = 0;
  for (int i = 0; i < 8; ++i) {
    tick(dut);
  }

  return dut->data_o;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  const char* oracle_path = std::getenv("GBP_PE_ORACLE_PATH");
  if (!oracle_path) {
    oracle_path = "tests/oracle/gbp_pe_hw_oracle_phase1.json";
  }
  OracleExpect expect = load_oracle(oracle_path);

  if (std::getenv("GBP_PE_ORACLE_PERTURB")) {
    expect.scenario_a_expected += 1u;
  }

  auto* dut = new Vgbp_pe;
  reset_dut(dut);

  const uint32_t a_data_i = 0x3f800000u;
  const uint32_t a_data_b_i = 0x40000000u;
  const uint8_t a_op_i = 0u;

  const uint32_t b_data_i = 0x40400000u;
  const uint32_t b_data_b_i = 0x3f000000u;
  const uint8_t b_op_i = 2u;

  if (a_data_i == 0u || a_data_b_i == 0u || b_data_i == 0u || b_data_b_i == 0u) {
    delete dut;
    return fail("scenario stimulus must be non-zero");
  }

  if (a_data_i == b_data_i && a_data_b_i == b_data_b_i && a_op_i == b_op_i) {
    delete dut;
    return fail("scenario A/B stimuli must differ");
  }

  const uint32_t out_a = run_scenario(dut, a_data_i, a_data_b_i, a_op_i);
  const uint32_t out_b = run_scenario(dut, b_data_i, b_data_b_i, b_op_i);

  if (!check_oracle_word("A", out_a, expect.scenario_a_expected, expect.abs_tol, expect.rel_tol)) {
    delete dut;
    return 1;
  }

  if (!check_oracle_word("B", out_b, expect.scenario_b_expected, expect.abs_tol, expect.rel_tol)) {
    delete dut;
    return 1;
  }

  if (out_a == 0u && out_b == 0u) {
    delete dut;
    return fail("tie-off regression guard: both scenario outputs are zero");
  }

  if (out_a == out_b) {
    delete dut;
    return fail("tie-off regression guard: scenario outputs are identical");
  }

  std::printf("gbp_pe: oracle pass_rule=abs_err<=abs_tol||rel_err<=rel_tol abs_tol=%.9g rel_tol=%.9g\n",
              expect.abs_tol,
              expect.rel_tol);
  std::printf("gbp_pe: scenario A out=0x%08x scenario B out=0x%08x\n", out_a, out_b);
  std::printf("gbp_pe: stimulus-control checks passed\n");
  delete dut;
  return 0;
}
