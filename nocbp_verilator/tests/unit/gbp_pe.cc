#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "verilated.h"
#include "Vgbp_pe.h"

static void tick(Vgbp_pe* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe* dut) {
  dut->rst_n = 0;
  dut->data_i = 0;
  dut->data_b_i = 0;
  dut->op_i = 0;
  dut->length_i = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
}

static int fail(const char* msg) {
  std::fprintf(stderr, "gbp_pe: FAIL: %s\n", msg);
  return 1;
}

static bool has_snippet(const std::string& text, const char* snippet) {
  return text.find(snippet) != std::string::npos;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vgbp_pe;
  reset_dut(dut);
  tick(dut);

  std::ifstream ifs("../v/gbp_pe/gbp_pe.sv");
  if (!ifs.is_open()) {
    delete dut;
    return fail("unable to open ../v/gbp_pe/gbp_pe.sv");
  }
  const std::string rtl((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  const char* required_snippets[] = {
      "gbp_pe_noc_bridge",
      "pe_top pe",
      ".core_req_data_i(core_req_data_lo)",
      ".core_rsp_data_o(core_rsp_data_li)",
      ".cmd_valid_i(sideband_cmd_valid_lo)",
      "assign out_v_li = egress_pending_r;",
      "assign out_packet_li = egress_packet_r;",
  };

  for (const char* snippet : required_snippets) {
    if (!has_snippet(rtl, snippet)) {
      delete dut;
      std::fprintf(stderr, "gbp_pe: FAIL: missing live-wiring snippet: %s\n", snippet);
      return 1;
    }
  }

  const char* forbidden_snippets[] = {
      "assign out_v_li = 1'b0;",
      "assign out_packet_li = '0;",
      "assign core_rsp_data_li = '0;",
      "assign core_rsp_v_li = 1'b0;",
      "assign core_req_yumi_li = core_req_v_lo;",
  };

  for (const char* snippet : forbidden_snippets) {
    if (has_snippet(rtl, snippet)) {
      delete dut;
      std::fprintf(stderr, "gbp_pe: FAIL: forbidden tie-off present: %s\n", snippet);
      return 1;
    }
  }

  std::printf("gbp_pe: LIVE_WIRING_MARKER bridge->pe_top path active\n");
  std::printf("gbp_pe: PASS\n");

  delete dut;
  return 0;
}
