#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "verilated.h"
#include "Vgbp_pe_mesh_2pe_gbp.h"

using Dut = Vgbp_pe_mesh_2pe_gbp;

static constexpr uint32_t kRowBytesLg = 5;
static constexpr uint32_t kMmioBankB0 = 0;
static constexpr uint32_t kPayloadBankB4 = 4;
static constexpr uint32_t kCreditMetaField = 0;
static constexpr uint32_t kTailField = 3;
static constexpr uint32_t kDoorbellField = 5;
static constexpr int kFixedIters = 50;
static constexpr int kPostStopCycles = 64;

struct PartitionInfo {
  std::array<std::vector<int>, 2> fac_mapping;
  std::array<std::vector<int>, 2> var_mapping;
  std::vector<std::pair<int, int>> edges;
};

struct IterationTrace {
  int iteration = 0;
  uint32_t factor_phase_remote_ingress = 0;
  uint32_t factor_phase_cmd_accepted = 0;
  uint32_t variable_phase_remote_ingress = 0;
  uint32_t variable_phase_cmd_accepted = 0;
  uint32_t pe0_ingress_total = 0;
  uint32_t pe1_ingress_total = 0;
  uint32_t pe0_cmd_total = 0;
  uint32_t pe1_cmd_total = 0;
};

static void tick(Dut* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Dut* dut) {
  dut->rst_n = 0;
  dut->send0_v = 0;
  dut->send0_we = 0;
  dut->send0_addr = 0;
  dut->send0_data = 0;
  dut->send1_v = 0;
  dut->send1_we = 0;
  dut->send1_addr = 0;
  dut->send1_data = 0;
  tick(dut);
  tick(dut);
  dut->rst_n = 1;
}

static bool extract_balanced_region(const std::string& text,
                                    const std::string& key,
                                    char open_ch,
                                    char close_ch,
                                    std::string* out) {
  const size_t key_pos = text.find("\"" + key + "\"");
  if (key_pos == std::string::npos) {
    return false;
  }
  const size_t start = text.find(open_ch, key_pos);
  if (start == std::string::npos) {
    return false;
  }
  int depth = 0;
  size_t end = std::string::npos;
  for (size_t i = start; i < text.size(); ++i) {
    if (text[i] == open_ch) {
      ++depth;
    } else if (text[i] == close_ch) {
      --depth;
      if (depth == 0) {
        end = i;
        break;
      }
    }
  }
  if (end == std::string::npos || end < start) {
    return false;
  }
  *out = text.substr(start, end - start + 1);
  return true;
}

static std::vector<int> parse_int_list(const std::string& text) {
  std::vector<int> vals;
  std::regex int_re("-?[0-9]+");
  for (std::sregex_iterator it(text.begin(), text.end(), int_re), end; it != end; ++it) {
    vals.push_back(std::stoi((*it)[0]));
  }
  return vals;
}

static bool parse_two_pe_table(const std::string& section, std::array<std::vector<int>, 2>* out) {
  std::regex table_re("\\[\\s*\\[([^\\]]*)\\]\\s*,\\s*\\[([^\\]]*)\\]\\s*\\]");
  std::smatch m;
  if (!std::regex_search(section, m, table_re) || m.size() < 3) {
    return false;
  }
  (*out)[0] = parse_int_list(m[1].str());
  (*out)[1] = parse_int_list(m[2].str());
  return true;
}

static bool load_partition_info(const char* path, PartitionInfo* out) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open PARTITION JSON path=%s\n", path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  std::string fac_section;
  if (!extract_balanced_region(text, "fac_mapping_table", '[', ']', &fac_section) ||
      !parse_two_pe_table(fac_section, &out->fac_mapping)) {
    std::fprintf(stderr, "FAIL: malformed fac_mapping_table in PARTITION=%s\n", path);
    return false;
  }

  std::string var_section;
  if (!extract_balanced_region(text, "var_mapping_table", '[', ']', &var_section) ||
      !parse_two_pe_table(var_section, &out->var_mapping)) {
    std::fprintf(stderr, "FAIL: malformed var_mapping_table in PARTITION=%s\n", path);
    return false;
  }

  std::string edge_section;
  if (!extract_balanced_region(text, "factor_var_edges", '[', ']', &edge_section)) {
    std::fprintf(stderr, "FAIL: missing graph.factor_var_edges in PARTITION=%s\n", path);
    return false;
  }
  std::regex edge_re("\\[\\s*([0-9]+)\\s*,\\s*([0-9]+)\\s*\\]");
  for (std::sregex_iterator it(edge_section.begin(), edge_section.end(), edge_re), end; it != end;
       ++it) {
    out->edges.push_back({std::stoi((*it)[1].str()), std::stoi((*it)[2].str())});
  }
  if (out->edges.empty()) {
    std::fprintf(stderr, "FAIL: no factor_var_edges parsed from PARTITION=%s\n", path);
    return false;
  }

  return true;
}

static bool issue_req(Dut* dut,
                      int src_pe,
                      bool we,
                      uint32_t addr,
                      uint32_t data,
                      int max_cycles) {
  if (src_pe == 0) {
    dut->send0_we = we ? 1 : 0;
    dut->send0_addr = addr;
    dut->send0_data = data;
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
      dut->send0_v = 1;
      tick(dut);
      if (dut->send0_ready) {
        dut->send0_v = 0;
        return true;
      }
    }
    dut->send0_v = 0;
    return false;
  }

  dut->send1_we = we ? 1 : 0;
  dut->send1_addr = addr;
  dut->send1_data = data;
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    dut->send1_v = 1;
    tick(dut);
    if (dut->send1_ready) {
      dut->send1_v = 0;
      return true;
    }
  }
  dut->send1_v = 0;
  return false;
}

static uint32_t ingress_count_for_pe(Dut* dut, int pe) {
  return static_cast<uint32_t>(pe == 0 ? dut->pe0_ingress_wr_count_o : dut->pe1_ingress_wr_count_o);
}

static uint32_t cmd_accept_count_for_pe(Dut* dut, int pe) {
  return static_cast<uint32_t>(pe == 0 ? dut->pe0_cmd_accept_count_o : dut->pe1_cmd_accept_count_o);
}

static bool run_remote_message(Dut* dut,
                               int src_pe,
                               int dst_pe,
                               uint8_t qid,
                               uint8_t txn_id,
                               uint32_t payload,
                               uint32_t* sent_reqs,
                               uint32_t* remote_ingress_delta,
                               uint32_t* remote_cmd_delta) {
  const uint32_t payload_addr = (kPayloadBankB4 << kRowBytesLg) + (static_cast<uint32_t>(qid) << 8);
  const uint32_t meta_addr =
      (kMmioBankB0 << kRowBytesLg) + (static_cast<uint32_t>(qid) << 8) + (kCreditMetaField << 2);
  const uint32_t tail_addr =
      (kMmioBankB0 << kRowBytesLg) + (static_cast<uint32_t>(qid) << 8) + (kTailField << 2);
  const uint32_t doorbell_addr =
      (kMmioBankB0 << kRowBytesLg) + (static_cast<uint32_t>(qid) << 8) + (kDoorbellField << 2);
  const uint32_t tail_data = 0x1u;
  const uint32_t doorbell_data = ((txn_id & 0xFFu) << 8) | (0x1u << 1) | 0x1u;

  const uint32_t ingress_before = ingress_count_for_pe(dut, dst_pe);
  const uint32_t cmd_before = cmd_accept_count_for_pe(dut, dst_pe);

  if (!issue_req(dut, src_pe, true, meta_addr, 0x1u, 256)) {
    std::fprintf(stderr,
                 "FAIL: credit_meta not accepted src_pe=%d dst_pe=%d addr=0x%05x\n",
                 src_pe,
                 dst_pe,
                 meta_addr);
    return false;
  }
  *sent_reqs += 1;

  if (!issue_req(dut, src_pe, true, payload_addr, payload, 256)) {
    std::fprintf(stderr,
                 "FAIL: payload not accepted src_pe=%d dst_pe=%d addr=0x%05x payload=0x%08x\n",
                 src_pe,
                 dst_pe,
                 payload_addr,
                 payload);
    return false;
  }
  *sent_reqs += 1;

  if (!issue_req(dut, src_pe, true, tail_addr, tail_data, 256)) {
    std::fprintf(stderr,
                 "FAIL: tail not accepted src_pe=%d dst_pe=%d addr=0x%05x\n",
                 src_pe,
                 dst_pe,
                 tail_addr);
    return false;
  }
  *sent_reqs += 1;

  if (!issue_req(dut, src_pe, true, doorbell_addr, doorbell_data, 256)) {
    std::fprintf(stderr,
                 "FAIL: doorbell not accepted src_pe=%d dst_pe=%d addr=0x%05x\n",
                 src_pe,
                 dst_pe,
                 doorbell_addr);
    return false;
  }
  *sent_reqs += 1;

  for (int cycle = 0; cycle < 1024; ++cycle) {
    tick(dut);
    const uint32_t ingress_now = ingress_count_for_pe(dut, dst_pe);
    const uint32_t cmd_now = cmd_accept_count_for_pe(dut, dst_pe);
    if (ingress_now > ingress_before && cmd_now > cmd_before) {
      *remote_ingress_delta = ingress_now - ingress_before;
      *remote_cmd_delta = cmd_now - cmd_before;
      return true;
    }
  }

  std::fprintf(
      stderr,
      "FAIL: remote accept timeout src_pe=%d dst_pe=%d ingress_before=%u ingress_after=%u cmd_before=%u cmd_after=%u\n",
      src_pe,
      dst_pe,
      ingress_before,
      ingress_count_for_pe(dut, dst_pe),
      cmd_before,
      cmd_accept_count_for_pe(dut, dst_pe));
  return false;
}

static uint32_t phase_payload(int iter, bool factor_phase, int edge_factor, int edge_var) {
  const uint32_t phase_tag = factor_phase ? 0xF0000000u : 0xA0000000u;
  const uint32_t iter_tag = (static_cast<uint32_t>(iter) & 0xFFu) << 16;
  const uint32_t edge_tag = ((static_cast<uint32_t>(edge_factor) & 0xFFu) << 8)
      | (static_cast<uint32_t>(edge_var) & 0xFFu);
  return phase_tag | iter_tag | edge_tag;
}

static bool parse_optional_int_env(const char* env_name, int* out, bool* present) {
  const char* raw = std::getenv(env_name);
  if (raw == nullptr || raw[0] == '\0') {
    *present = false;
    return true;
  }
  char* end = nullptr;
  const long parsed = std::strtol(raw, &end, 10);
  if (end == nullptr || *end != '\0' || parsed < 0 || parsed > 255) {
    std::fprintf(stderr, "FAIL: invalid %s=%s expected integer in [0,255]\n", env_name, raw);
    return false;
  }
  *present = true;
  *out = static_cast<int>(parsed);
  return true;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Dut;

  const char* workload = std::getenv("WORKLOAD");
  if (workload == nullptr || workload[0] == '\0') {
    workload = "synthetic_line";
  }

  const std::string default_partition =
      std::string("tests/oracle/generated/") + workload + "_partition_2pe_factor_variable.json";
  const char* partition_env = std::getenv("PARTITION");
  const char* partition_path =
      (partition_env == nullptr || partition_env[0] == '\0') ? default_partition.c_str() : partition_env;

  PartitionInfo partition{};
  if (!load_partition_info(partition_path, &partition)) {
    delete dut;
    return 1;
  }

  std::unordered_map<int, int> factor_owner;
  std::unordered_map<int, int> var_owner;
  for (int pe = 0; pe < 2; ++pe) {
    for (int f : partition.fac_mapping[pe]) {
      factor_owner[f] = pe;
    }
    for (int v : partition.var_mapping[pe]) {
      var_owner[v] = pe;
    }
  }

  bool found_cross_edge = false;
  int edge_factor = -1;
  int edge_var = -1;
  int fac_pe = -1;
  int var_pe = -1;
  for (const auto& e : partition.edges) {
    const auto fit = factor_owner.find(e.first);
    const auto vit = var_owner.find(e.second);
    if (fit == factor_owner.end() || vit == var_owner.end()) {
      continue;
    }
    if (fit->second != vit->second) {
      found_cross_edge = true;
      edge_factor = e.first;
      edge_var = e.second;
      fac_pe = fit->second;
      var_pe = vit->second;
      break;
    }
  }

  if (!found_cross_edge) {
    std::fprintf(stderr,
                 "FAIL: no cross-PE factor_var_edges found in PARTITION=%s\n",
                 partition_path);
    delete dut;
    return 1;
  }

  if (!((fac_pe == 0 && var_pe == 1) || (fac_pe == 1 && var_pe == 0))) {
    std::fprintf(stderr,
                 "FAIL: unsupported owner mapping for 2-PE mesh fac_pe=%d var_pe=%d\n",
                 fac_pe,
                 var_pe);
    delete dut;
    return 1;
  }

  bool premature_stop_mode = false;
  int premature_stop_iter = -1;
  if (!parse_optional_int_env("GBP_PE_MESH_PREMATURE_STOP_ITER",
                              &premature_stop_iter,
                              &premature_stop_mode)) {
    delete dut;
    return 1;
  }

  if (premature_stop_mode && (premature_stop_iter >= kFixedIters)) {
    std::fprintf(stderr,
                 "FAIL: GBP_PE_MESH_PREMATURE_STOP_ITER=%d must be < %d\n",
                 premature_stop_iter,
                 kFixedIters);
    delete dut;
    return 1;
  }

  reset_dut(dut);

  uint32_t pe_sent[2] = {0, 0};
  uint32_t pe_received[2] = {0, 0};
  uint32_t pe_consumed[2] = {0, 0};

  std::vector<IterationTrace> trace;
  trace.reserve(kFixedIters);

  for (int iter = 0; iter < kFixedIters; ++iter) {
    if (premature_stop_mode && iter == premature_stop_iter) {
      std::fprintf(stderr,
                   "PREMATURE_STOP_ERROR iteration=%d requested_stop_before_fixed_iters=%d mode=env_gbp_pe_mesh_premature_stop_iter\n",
                   iter,
                   kFixedIters);
      delete dut;
      return 2;
    }

    IterationTrace row{};
    row.iteration = iter;

    uint32_t ingress_delta = 0;
    uint32_t cmd_delta = 0;
    if (!run_remote_message(dut,
                            fac_pe,
                            var_pe,
                            0,
                            static_cast<uint8_t>((0x40 + iter) & 0xFF),
                            phase_payload(iter, true, edge_factor, edge_var),
                            &pe_sent[fac_pe],
                            &ingress_delta,
                            &cmd_delta)) {
      delete dut;
      return 1;
    }
    row.factor_phase_remote_ingress = ingress_delta;
    row.factor_phase_cmd_accepted = cmd_delta;
    pe_received[var_pe] += ingress_delta;
    pe_consumed[var_pe] += cmd_delta;

    ingress_delta = 0;
    cmd_delta = 0;
    if (!run_remote_message(dut,
                            var_pe,
                            fac_pe,
                            0,
                            static_cast<uint8_t>((0x80 + iter) & 0xFF),
                            phase_payload(iter, false, edge_factor, edge_var),
                            &pe_sent[var_pe],
                            &ingress_delta,
                            &cmd_delta)) {
      delete dut;
      return 1;
    }
    row.variable_phase_remote_ingress = ingress_delta;
    row.variable_phase_cmd_accepted = cmd_delta;
    pe_received[fac_pe] += ingress_delta;
    pe_consumed[fac_pe] += cmd_delta;

    if (row.factor_phase_remote_ingress == 0 || row.factor_phase_cmd_accepted == 0
        || row.variable_phase_remote_ingress == 0 || row.variable_phase_cmd_accepted == 0) {
      std::fprintf(stderr,
                   "FAIL: zero-progress superstep iteration=%d factor_ingress=%u factor_cmd=%u variable_ingress=%u variable_cmd=%u\n",
                   iter,
                   row.factor_phase_remote_ingress,
                   row.factor_phase_cmd_accepted,
                   row.variable_phase_remote_ingress,
                   row.variable_phase_cmd_accepted);
      delete dut;
      return 1;
    }

    row.pe0_ingress_total = ingress_count_for_pe(dut, 0);
    row.pe1_ingress_total = ingress_count_for_pe(dut, 1);
    row.pe0_cmd_total = cmd_accept_count_for_pe(dut, 0);
    row.pe1_cmd_total = cmd_accept_count_for_pe(dut, 1);
    trace.push_back(row);
  }

  const uint32_t pre_stop_pe0_ingress = ingress_count_for_pe(dut, 0);
  const uint32_t pre_stop_pe1_ingress = ingress_count_for_pe(dut, 1);
  const uint32_t pre_stop_pe0_cmd = cmd_accept_count_for_pe(dut, 0);
  const uint32_t pre_stop_pe1_cmd = cmd_accept_count_for_pe(dut, 1);
  for (int i = 0; i < kPostStopCycles; ++i) {
    tick(dut);
  }
  const uint32_t post_stop_pe0_ingress = ingress_count_for_pe(dut, 0);
  const uint32_t post_stop_pe1_ingress = ingress_count_for_pe(dut, 1);
  const uint32_t post_stop_pe0_cmd = cmd_accept_count_for_pe(dut, 0);
  const uint32_t post_stop_pe1_cmd = cmd_accept_count_for_pe(dut, 1);

  const bool post_stop_quiet = pre_stop_pe0_ingress == post_stop_pe0_ingress
      && pre_stop_pe1_ingress == post_stop_pe1_ingress && pre_stop_pe0_cmd == post_stop_pe0_cmd
      && pre_stop_pe1_cmd == post_stop_pe1_cmd;
  if (!post_stop_quiet) {
    std::fprintf(stderr,
                 "FAIL: post-stop traffic detected pre=[%u,%u,%u,%u] post=[%u,%u,%u,%u]\n",
                 pre_stop_pe0_ingress,
                 pre_stop_pe1_ingress,
                 pre_stop_pe0_cmd,
                 pre_stop_pe1_cmd,
                 post_stop_pe0_ingress,
                 post_stop_pe1_ingress,
                 post_stop_pe0_cmd,
                 post_stop_pe1_cmd);
    delete dut;
    return 1;
  }

  const std::string trace_path =
      std::string("build/integration/gbp_pe_mesh_2pe_superstep/") + workload + "_superstep_trace.json";
  std::ofstream trace_ofs(trace_path);
  if (!trace_ofs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to write trace file path=%s\n", trace_path.c_str());
    delete dut;
    return 1;
  }

  trace_ofs << "{\n"
            << "  \"test\": \"gbp_pe_mesh_2pe_superstep\",\n"
            << "  \"workload\": \"" << workload << "\",\n"
            << "  \"partition\": \"" << partition_path << "\",\n"
            << "  \"iterations\": " << kFixedIters << ",\n"
            << "  \"stop_reason\": \"fixed_iters\",\n"
            << "  \"cross_edge\": {\n"
            << "    \"factor\": " << edge_factor << ",\n"
            << "    \"variable\": " << edge_var << ",\n"
            << "    \"factor_owner_pe\": " << fac_pe << ",\n"
            << "    \"variable_owner_pe\": " << var_pe << "\n"
            << "  },\n"
            << "  \"supersteps\": [\n";

  for (size_t i = 0; i < trace.size(); ++i) {
    const IterationTrace& row = trace[i];
    trace_ofs << "    {\"iteration\": " << row.iteration
              << ", \"factor_phase_remote_ingress\": " << row.factor_phase_remote_ingress
              << ", \"factor_phase_cmd_accepted\": " << row.factor_phase_cmd_accepted
              << ", \"variable_phase_remote_ingress\": " << row.variable_phase_remote_ingress
              << ", \"variable_phase_cmd_accepted\": " << row.variable_phase_cmd_accepted
              << ", \"pe0_ingress_total\": " << row.pe0_ingress_total
              << ", \"pe1_ingress_total\": " << row.pe1_ingress_total
              << ", \"pe0_cmd_total\": " << row.pe0_cmd_total
              << ", \"pe1_cmd_total\": " << row.pe1_cmd_total << "}";
    trace_ofs << (i + 1 == trace.size() ? "\n" : ",\n");
  }

  trace_ofs << "  ],\n"
            << "  \"post_stop_traffic\": {\n"
            << "    \"cycles_checked\": " << kPostStopCycles << ",\n"
            << "    \"quiet\": " << (post_stop_quiet ? "true" : "false") << ",\n"
            << "    \"pre_stop\": {\"pe0_ingress\": " << pre_stop_pe0_ingress
            << ", \"pe1_ingress\": " << pre_stop_pe1_ingress << ", \"pe0_cmd\": "
            << pre_stop_pe0_cmd << ", \"pe1_cmd\": " << pre_stop_pe1_cmd << "},\n"
            << "    \"post_stop\": {\"pe0_ingress\": " << post_stop_pe0_ingress
            << ", \"pe1_ingress\": " << post_stop_pe1_ingress << ", \"pe0_cmd\": "
            << post_stop_pe0_cmd << ", \"pe1_cmd\": " << post_stop_pe1_cmd << "}\n"
            << "  },\n"
            << "  \"pe_counts\": [\n"
            << "    {\"pe\": 0, \"sent\": " << pe_sent[0] << ", \"received\": " << pe_received[0]
            << ", \"consumed\": " << pe_consumed[0] << "},\n"
            << "    {\"pe\": 1, \"sent\": " << pe_sent[1] << ", \"received\": " << pe_received[1]
            << ", \"consumed\": " << pe_consumed[1] << "}\n"
            << "  ]\n"
            << "}\n";

  std::printf("gbp_pe_mesh_2pe_superstep: PASS iterations=%d stop_reason=fixed_iters trace_json=%s\n",
              kFixedIters,
              trace_path.c_str());

  delete dut;
  return 0;
}
