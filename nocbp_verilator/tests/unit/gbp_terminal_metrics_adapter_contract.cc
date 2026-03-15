#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "verilated.h"
#include "Vgbp_terminal_metrics_adapter_contract.h"

#include "../common/gbp_terminal_metrics_adapter.hpp"
#include "../common/gbp_terminal_metrics_adapter.cpp"

static void tick(Vgbp_terminal_metrics_adapter_contract* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static bool write_text(const std::string& path, const std::string& text) {
  std::ofstream out(path);
  if (!out.is_open()) {
    return false;
  }
  out << text;
  return true;
}

static bool read_text(const std::string& path, std::string* out) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return false;
  }
  *out = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return true;
}

static bool replace_once(std::string* text,
                         const std::string& needle,
                         const std::string& replacement,
                         const char* case_name) {
  const size_t pos = text->find(needle);
  if (pos == std::string::npos) {
    std::fprintf(stderr,
                 "gbp_terminal_metrics_adapter_contract: FAIL: fixture mutation missing needle case=%s needle=%s\n",
                 case_name,
                 needle.c_str());
    return false;
  }
  text->replace(pos, needle.size(), replacement);
  return true;
}

static bool expect_fail(const std::string& path, const char* marker, const char* case_name) {
  gbp_terminal_metrics_adapter::Metrics metrics;
  std::string error;
  const bool ok =
      gbp_terminal_metrics_adapter::reconstruct_metrics_from_dump(path, &metrics, &error);
  if (ok) {
    std::fprintf(stderr,
                 "gbp_terminal_metrics_adapter_contract: FAIL: expected failure case=%s marker=%s path=%s\n",
                 case_name,
                 marker,
                 path.c_str());
    return false;
  }
  if (error.find(marker) == std::string::npos) {
    std::fprintf(stderr,
                 "gbp_terminal_metrics_adapter_contract: FAIL: unexpected error case=%s marker=%s observed=%s\n",
                 case_name,
                 marker,
                 error.c_str());
    return false;
  }
  std::printf("GBP_TERMINAL_METRICS_ADAPTER_CONTRACT_EXPECTED_FAIL case=%s marker=%s error=%s\n",
              case_name,
              marker,
              error.c_str());
  return true;
}

static bool write_and_expect_fail(const std::string& case_name,
                                  const std::string& text,
                                  const char* marker) {
  const std::string path =
      "build/unit/gbp_terminal_metrics_adapter_contract/" + case_name + "_dump.json";
  if (!write_text(path, text)) {
    std::fprintf(stderr,
                 "gbp_terminal_metrics_adapter_contract: FAIL: cannot write case=%s path=%s\n",
                 case_name.c_str(),
                 path.c_str());
    return false;
  }
  return expect_fail(path, marker, case_name.c_str());
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  Vgbp_terminal_metrics_adapter_contract* dut = new Vgbp_terminal_metrics_adapter_contract;
  dut->rst_n = 0;
  tick(dut);
  dut->rst_n = 1;
  tick(dut);

  std::string base_fixture;
  if (!read_text("tests/unit/data/gbp_terminal_metrics_adapter_golden_dump.json", &base_fixture)) {
    std::fprintf(stderr,
                 "gbp_terminal_metrics_adapter_contract: FAIL: cannot read base fixture path=%s\n",
                 "tests/unit/data/gbp_terminal_metrics_adapter_golden_dump.json");
    delete dut;
    return 1;
  }

  {
    std::string text = base_fixture;
    if (!replace_once(&text,
                      "\"required_message_banks\": [4, 5, 6, 7]",
                      "\"required_message_banks\": [4, 5, 6, 7, 8]",
                      "missing_required_rows_coverage")) {
      delete dut;
      return 1;
    }
    if (!write_and_expect_fail("missing_required_rows_coverage",
                               text,
                               "missing required message bank coverage in required_rows")) {
      delete dut;
      return 1;
    }
  }

  {
    std::string text = base_fixture;
    if (!replace_once(&text,
                      "  \"iterations\": 50,\n",
                      "",
                      "missing_required_field_iterations")) {
      delete dut;
      return 1;
    }
    if (!write_and_expect_fail("missing_required_field_iterations",
                               text,
                               "missing required field: iterations")) {
      delete dut;
      return 1;
    }
  }

  {
    std::string text = base_fixture;
    if (!replace_once(&text, "\"state_bank\": 1", "\"state_bank\": 0", "bank_class_violation")) {
      delete dut;
      return 1;
    }
    if (!write_and_expect_fail("bank_class_violation",
                               text,
                               "state bank outside B1-B3")) {
      delete dut;
      return 1;
    }
  }

  {
    std::string text = base_fixture;
    if (!replace_once(&text,
                      "\"beats\": [0,0,0,0,0,0,0,0]",
                      "\"beats\": [0,0,0,0,0,0,0]",
                      "beat_contract_violation")) {
      delete dut;
      return 1;
    }
    if (!write_and_expect_fail("beat_contract_violation",
                               text,
                               "required_rows beat count mismatch with coverage_contract")) {
      delete dut;
      return 1;
    }
  }

  {
    std::string text = base_fixture;
    if (!replace_once(&text,
                      "\"msg_eta\": [1.401298464e-45, 0.0],\n          \"msg_lam\": [[0.0, 0.0], [0.0, 0.0]]",
                      "\"direction\": \"factor_to_var\",\n          \"payload_words\": [1065353216, 0, 0, 1065353216]",
                      "heuristic_direction_decode_rejected")) {
      delete dut;
      return 1;
    }
    if (!write_and_expect_fail("heuristic_direction_decode_rejected",
                               text,
                               "missing required field: msg_eta")) {
      delete dut;
      return 1;
    }
  }

  {
    std::string text = base_fixture;
    if (!replace_once(&text,
                      "\"msg_eta\": [1.401298464e-45, 0.0],\n          \"msg_lam\": [[0.0, 0.0], [0.0, 0.0]]",
                      "\"schema_version\": 99,\n          \"payload_words\": [1065353216, 0, 0, 1065353216]",
                      "heuristic_version_decode_rejected")) {
      delete dut;
      return 1;
    }
    if (!write_and_expect_fail("heuristic_version_decode_rejected",
                               text,
                               "missing required field: msg_eta")) {
      delete dut;
      return 1;
    }
  }

  {
    std::string text = base_fixture;
    if (!replace_once(&text,
                      "\"prior_eta\": [0.0, 0.0],\n      \"prior_lam\": [[1.0, 0.0], [0.0, 1.0]],",
                      "\"prior_fallback\": true,",
                      "heuristic_prior_fallback_rejected")) {
      delete dut;
      return 1;
    }
    if (!write_and_expect_fail("heuristic_prior_fallback_rejected",
                               text,
                               "missing required field: prior_eta")) {
      delete dut;
      return 1;
    }
  }

  std::printf("GBP_TERMINAL_METRICS_ADAPTER_CONTRACT_PASS_MARKER\n");
  delete dut;
  return 0;
}
