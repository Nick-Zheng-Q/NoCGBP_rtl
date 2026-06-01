#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "verilated.h"

#if defined(GBP_CANONICAL_MESH)
#include "Vgbp_pe_mesh_gbp.h"
#ifndef GBP_PE_COUNT
#define GBP_PE_COUNT 16
#endif
#else
#include "Vgbp_pe_mesh_2pe_gbp.h"
#endif

#include "../common/gbp_event_correlation.hpp"
#include "../common/gbp_message_payload_codec.hpp"

static constexpr const char* kTraceTestName =
#if defined(GBP_CANONICAL_MESH)
    "gbp_pe_mesh_gbp";
#else
    "gbp_pe_mesh_2pe_gbp";
#endif

static constexpr const char* kTopName =
#if defined(GBP_CANONICAL_MESH)
    "gbp_pe_mesh_gbp";
#else
    "gbp_pe_mesh_2pe_gbp";
#endif

using Dut =
#if defined(GBP_CANONICAL_MESH)
    Vgbp_pe_mesh_gbp;
#else
    Vgbp_pe_mesh_2pe_gbp;
#endif

static constexpr uint32_t kRowBytesLg = 5;
static constexpr uint32_t kMmioBankB0 = 0;
static constexpr uint32_t kPayloadBankB4 = 4;
static constexpr uint32_t kCreditMetaField = 0;
static constexpr uint32_t kTailField = 3;
static constexpr uint32_t kDoorbellField = 5;
static constexpr uint32_t kTxnIdJoinMask = 0x1Fu;

#if defined(GBP_CANONICAL_MESH)
static constexpr uint32_t kHarnessEndpoints = GBP_PE_COUNT;
#else
static constexpr uint32_t kHarnessEndpoints = 2;
#endif

static bool parse_u32_env(const char* key, uint32_t* out) {
  const char* raw = std::getenv(key);
  if (raw == nullptr || raw[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(raw, &end, 10);
  if (errno != 0 || end == raw || *end != '\0') {
    return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
}

static int endpoint_index_for_pe(int pe) {
#if defined(GBP_CANONICAL_MESH)
  return pe;
#else
  return (pe & 0x1) == 0 ? 0 : 1;
#endif
}

struct PartitionInfo {
  std::vector<std::vector<int>> fac_mapping;
  std::vector<std::vector<int>> var_mapping;
  std::vector<std::pair<int, int>> edges;
};

struct CrossEdge {
  int factor_id = -1;
  int variable_id = -1;
  int factor_owner_pe = -1;
  int variable_owner_pe = -1;
};

static void tick(Dut* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Dut* dut) {
  dut->rst_n = 0;
#if defined(GBP_CANONICAL_MESH)
  dut->send_v = 0;
  dut->send_we = 0;
  dut->send_pe = 0;
  dut->send_addr = 0;
  dut->send_data = 0;
  dut->route_v = 0;
  dut->route_src_pe = 0;
  dut->route_dst_pe = 0;
  dut->preload_v = 0;
  dut->preload_pe = 0;
  dut->preload_bank = 0;
  dut->preload_row = 0;
  dut->preload_word = 0;
  dut->preload_data = 0;
  dut->compute_start_v = 0;
  dut->compute_start_pe = 0;
  dut->compute_start_txn_id = 0;
  dut->probe_pe = 0;
  dut->probe_bank = 0;
  dut->probe_row = 0;
#else
  dut->send0_v = 0;
  dut->send0_we = 0;
  dut->send0_addr = 0;
  dut->send0_data = 0;
  dut->send1_v = 0;
  dut->send1_we = 0;
  dut->send1_addr = 0;
  dut->send1_data = 0;
#endif
  tick(dut);
  tick(dut);
#if defined(GBP_CANONICAL_MESH)
  for (int i = 0; i < 8; ++i) {
    tick(dut);
  }
#endif
  dut->rst_n = 1;
}

static void configure_dut_route(Dut* dut, int src_pe, int dst_pe) {
#if defined(GBP_CANONICAL_MESH)
  dut->route_src_pe = static_cast<uint8_t>(src_pe);
  dut->route_dst_pe = static_cast<uint8_t>(dst_pe);
  dut->route_v = 1;
  tick(dut);
  dut->route_v = 0;
#else
  (void)dut;
  (void)src_pe;
  (void)dst_pe;
#endif
}

static void set_probe_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  dut->probe_pe = static_cast<uint8_t>(pe);
  dut->probe_bank = 0;
  dut->probe_row = 0;
  dut->eval();
#else
  (void)dut;
  (void)pe;
#endif
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

static bool parse_pe_table(const std::string& section,
                           uint32_t pe_count,
                           std::vector<std::vector<int>>* out) {
  if (pe_count == 0 || out == nullptr) {
    return false;
  }

  out->clear();
  out->reserve(pe_count);
  int depth = 0;
  size_t bucket_start = std::string::npos;
  for (size_t i = 0; i < section.size(); ++i) {
    const char ch = section[i];
    if (ch == '[') {
      if (depth == 1) {
        bucket_start = i;
      }
      ++depth;
      continue;
    }
    if (ch == ']') {
      --depth;
      if (depth < 0) {
        return false;
      }
      if (depth == 1 && bucket_start != std::string::npos) {
        out->push_back(parse_int_list(section.substr(bucket_start, i - bucket_start + 1)));
        bucket_start = std::string::npos;
      }
    }
  }

  return (depth == 0) && (out->size() == pe_count);
}

static bool load_partition_info(const char* path, uint32_t pe_count, PartitionInfo* out) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open PARTITION JSON path=%s\n", path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  std::string fac_section;
  if (!extract_balanced_region(text, "fac_mapping_table", '[', ']', &fac_section) ||
      !parse_pe_table(fac_section, pe_count, &out->fac_mapping)) {
    std::fprintf(stderr, "FAIL: malformed fac_mapping_table in PARTITION=%s\n", path);
    return false;
  }

  std::string var_section;
  if (!extract_balanced_region(text, "var_mapping_table", '[', ']', &var_section) ||
      !parse_pe_table(var_section, pe_count, &out->var_mapping)) {
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

static bool partition_matches_mesh_contract(const char* path,
                                            uint32_t pe_count,
                                            uint32_t mesh_x,
                                            uint32_t mesh_y) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to open PARTITION JSON path=%s\n", path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  std::smatch pes_match;
  std::smatch mesh_x_match;
  std::smatch mesh_y_match;
  const std::regex pes_re("\"pes\"\\s*:\\s*([0-9]+)");
  const std::regex mesh_x_re("\"x\"\\s*:\\s*([0-9]+)");
  const std::regex mesh_y_re("\"y\"\\s*:\\s*([0-9]+)");
  if (!std::regex_search(text, pes_match, pes_re)
      || !std::regex_search(text, mesh_x_match, mesh_x_re)
      || !std::regex_search(text, mesh_y_match, mesh_y_re)) {
    std::fprintf(stderr, "FAIL: malformed partition contract fields in PARTITION=%s\n", path);
    return false;
  }

  const uint32_t file_pes = static_cast<uint32_t>(std::stoul(pes_match[1].str()));
  const uint32_t file_mesh_x = static_cast<uint32_t>(std::stoul(mesh_x_match[1].str()));
  const uint32_t file_mesh_y = static_cast<uint32_t>(std::stoul(mesh_y_match[1].str()));
  if (file_pes != pe_count || file_mesh_x != mesh_x || file_mesh_y != mesh_y) {
    std::fprintf(stderr,
                 "FAIL: partition contract mismatch expected_pes=%u expected_mesh=%ux%u file_pes=%u file_mesh=%ux%u path=%s\n",
                 pe_count,
                 mesh_x,
                 mesh_y,
                 file_pes,
                 file_mesh_x,
                 file_mesh_y,
                 path);
    return false;
  }
  return true;
}

static std::vector<CrossEdge> collect_cross_edges(
    const PartitionInfo& partition,
    const std::unordered_map<int, int>& factor_owner,
    const std::unordered_map<int, int>& var_owner) {
  std::vector<CrossEdge> out;
  out.reserve(partition.edges.size());
  for (const auto& edge : partition.edges) {
    const auto fit = factor_owner.find(edge.first);
    const auto vit = var_owner.find(edge.second);
    if (fit == factor_owner.end() || vit == var_owner.end()) {
      continue;
    }
    if (fit->second == vit->second) {
      continue;
    }
    CrossEdge cross{};
    cross.factor_id = edge.first;
    cross.variable_id = edge.second;
    cross.factor_owner_pe = fit->second;
    cross.variable_owner_pe = vit->second;
    out.push_back(cross);
  }
  return out;
}

#if defined(GBP_CANONICAL_MESH)
static bool issue_canonical_preload_req(Dut* dut,
                                        int src_pe,
                                        uint8_t bank,
                                        uint8_t row,
                                        uint8_t word,
                                        uint32_t data,
                                        int max_cycles) {
  dut->preload_pe = static_cast<uint8_t>(src_pe);
  dut->preload_bank = bank;
  dut->preload_row = row;
  dut->preload_word = word;
  dut->preload_data = data;
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    dut->preload_v = 1;
    tick(dut);
    if (dut->preload_ready) {
      dut->preload_v = 0;
      return true;
    }
  }
  dut->preload_v = 0;
  return false;
}

static bool issue_canonical_compute_start_req(Dut* dut,
                                              int src_pe,
                                              uint8_t txn_id,
                                              int max_cycles) {
  dut->compute_start_pe = static_cast<uint8_t>(src_pe);
  dut->compute_start_txn_id = txn_id;
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    dut->compute_start_v = 1;
    tick(dut);
    if (dut->compute_start_ready) {
      dut->compute_start_v = 0;
      return true;
    }
  }
  dut->compute_start_v = 0;
  return false;
}
#endif

static bool issue_req(Dut* dut,
                      int src_pe,
                      bool we,
                      uint32_t addr,
                      uint32_t data,
                      int max_cycles) {
#if defined(GBP_CANONICAL_MESH)
  const int canonical_max_cycles = max_cycles * 8;
  if (!we) {
    std::fprintf(stderr,
                 "FAIL: canonical preload/start path only supports writes src_pe=%d addr=0x%05x\n",
                 src_pe,
                 addr);
    return false;
  }

  const uint32_t bank = (addr >> kRowBytesLg) & 0x7u;
  const uint32_t row = (addr >> 8) & 0xFFu;
  const uint32_t word = (addr >> 2) & 0x7u;
  const uint32_t mmio_field = (addr >> 2) & 0x7u;

  if (bank >= 1u && bank <= 7u) {
    return issue_canonical_preload_req(dut,
                                       src_pe,
                                       static_cast<uint8_t>(bank),
                                       static_cast<uint8_t>(row),
                                       static_cast<uint8_t>(word),
                                       data,
                                       canonical_max_cycles);
  }

  if (bank == kMmioBankB0 && mmio_field == kDoorbellField && (data & 0x1u)) {
    const uint8_t txn_id = static_cast<uint8_t>((data >> 8) & 0xFFu);
    return issue_canonical_compute_start_req(dut, src_pe, txn_id, canonical_max_cycles);
  }

  if (bank == kMmioBankB0 && (mmio_field == kCreditMetaField || mmio_field == kTailField)) {
    return true;
  }

  std::fprintf(stderr,
               "FAIL: unsupported canonical preload/start write src_pe=%d addr=0x%05x data=0x%08x\n",
               src_pe,
               addr,
               data);
  return false;
#else
  const int endpoint = endpoint_index_for_pe(src_pe);
  if (endpoint == 0) {
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
#endif
}

static uint32_t ingress_count_for_pe(Dut* dut, int pe);
static uint32_t cmd_accept_count_for_pe(Dut* dut, int pe);
static uint32_t compute_done_count_for_pe(Dut* dut, int pe);
static uint32_t message_row0_word0_for_bank(Dut* dut, int pe, uint32_t bank);
static std::array<uint32_t, 4> adapter_payload_planes_row0_for_pe(Dut* dut, int pe);
static uint32_t make_bootstrap_payload_word(uint32_t bank,
                                            uint32_t slot,
                                            int edge_factor,
                                            int edge_var,
                                            int src_pe,
                                            int dst_pe);

#if defined(GBP_CANONICAL_MESH)
static bool run_canonical_preload_seed(Dut* dut, int pe, uint32_t payload_word) {
  const uint32_t payload_addr = (kPayloadBankB4 << kRowBytesLg);

  if (!issue_req(dut, pe, true, payload_addr, payload_word, 256)) {
    std::fprintf(stderr,
                 "FAIL: canonical preload payload write rejected pe=%d addr=0x%05x\n",
                 pe,
                 payload_addr);
    return false;
  }

  return true;
}

static bool run_canonical_precompute_probe_proof(Dut* dut, int pe, uint32_t payload_word) {
  const uint32_t payload_addr = (kPayloadBankB4 << kRowBytesLg);
  constexpr int kSettleCycles = 64;

  const uint32_t compute_done_before = compute_done_count_for_pe(dut, pe);
  const uint32_t cmd_before = cmd_accept_count_for_pe(dut, pe);
  const uint32_t ingress_before = ingress_count_for_pe(dut, pe);

  if (!issue_req(dut, pe, true, payload_addr, payload_word, 256)) {
    std::fprintf(stderr,
                 "FAIL: canonical pre-compute preload write rejected pe=%d addr=0x%05x payload=0x%08x\n",
                 pe,
                 payload_addr,
                 payload_word);
    return false;
  }

  for (int cycle = 0; cycle < kSettleCycles; ++cycle) {
    tick(dut);
  }

  set_probe_pe(dut, pe);
  const uint32_t observed_addr = static_cast<uint32_t>(dut->probe_preload_addr_o);
  const uint32_t observed_word = static_cast<uint32_t>(dut->probe_preload_data_o);
  const uint32_t observed_valid = static_cast<uint32_t>(dut->probe_preload_valid_o);
  const uint32_t compute_done_after = compute_done_count_for_pe(dut, pe);
  const uint32_t cmd_after = cmd_accept_count_for_pe(dut, pe);
  const uint32_t ingress_after = ingress_count_for_pe(dut, pe);

  if (observed_valid == 0u || observed_addr != payload_addr || observed_word != payload_word) {
    std::fprintf(stderr,
                 "FAIL: canonical pre-compute probe mismatch pe=%d bank=%u row=0 expected_addr=0x%05x observed_addr=0x%05x expected=0x%08x observed=0x%08x preload_valid=%u\n",
                 pe,
                 kPayloadBankB4,
                 payload_addr,
                 observed_addr,
                 payload_word,
                 observed_word,
                 observed_valid);
    return false;
  }

  if (compute_done_after != compute_done_before || cmd_after != cmd_before
      || ingress_after != ingress_before) {
    std::fprintf(stderr,
                 "FAIL: canonical pre-compute probe advanced runtime counters pe=%d compute_done_before=%u compute_done_after=%u cmd_before=%u cmd_after=%u ingress_before=%u ingress_after=%u\n",
                 pe,
                 compute_done_before,
                 compute_done_after,
                 cmd_before,
                 cmd_after,
                 ingress_before,
                 ingress_after);
    return false;
  }

  std::printf(
      "gbp_pe_mesh_gbp: PRECOMPUTE_PRELOAD_PROBE_MARKER pe=%d bank=%u row=0 preload_valid=%u expected_addr=0x%05x observed_addr=0x%05x expected=0x%08x observed=0x%08x compute_done_before=%u compute_done_after=%u cmd_before=%u cmd_after=%u ingress_before=%u ingress_after=%u\n",
      pe,
      kPayloadBankB4,
      observed_valid,
      payload_addr,
      observed_addr,
      payload_word,
      observed_word,
      compute_done_before,
      compute_done_after,
      cmd_before,
      cmd_after,
      ingress_before,
      ingress_after);
  return true;
}

static int run_canonical_preload_no_trigger_mode(Dut* dut, int pe) {
  const uint32_t payload_word = make_bootstrap_payload_word(kPayloadBankB4, 0u, 1, 2, pe, pe);
  constexpr int kProofCycles = 256;
  constexpr int kSettleCycles = 2048;

  reset_dut(dut);
  if (!run_canonical_preload_seed(dut, pe, payload_word)) {
    return 1;
  }

  for (int cycle = 0; cycle < kSettleCycles; ++cycle) {
    tick(dut);
  }

  const uint32_t compute_done_before = compute_done_count_for_pe(dut, pe);
  const uint32_t cmd_before = cmd_accept_count_for_pe(dut, pe);
  const uint32_t ingress_before = ingress_count_for_pe(dut, pe);

  for (int cycle = 0; cycle < kProofCycles; ++cycle) {
    tick(dut);
  }

  const uint32_t compute_done_after = compute_done_count_for_pe(dut, pe);
  const uint32_t cmd_after = cmd_accept_count_for_pe(dut, pe);
  const uint32_t ingress_after = ingress_count_for_pe(dut, pe);

  if (compute_done_after != compute_done_before || cmd_after != cmd_before
      || ingress_after != ingress_before) {
    std::fprintf(stderr,
                 "FAIL: canonical preload no-trigger proof diverged pe=%d cycles=%d compute_done_before=%u compute_done_after=%u cmd_before=%u cmd_after=%u ingress_before=%u ingress_after=%u\n",
                 pe,
                 kProofCycles,
                 compute_done_before,
                 compute_done_after,
                 cmd_before,
                 cmd_after,
                 ingress_before,
                 ingress_after);
    return 1;
  }

  std::printf(
      "%s: CANONICAL_PRELOAD_NO_TRIGGER_MARKER pe=%d cycles=%d payload_word=0x%08x compute_done_before=%u compute_done_after=%u cmd_before=%u cmd_after=%u ingress_before=%u ingress_after=%u\n",
      kTopName,
      pe,
      kProofCycles,
      payload_word,
      compute_done_before,
      compute_done_after,
      cmd_before,
      cmd_after,
      ingress_before,
      ingress_after);
  return 0;
}

static int run_canonical_preload_trigger_mode(Dut* dut, int pe) {
  const uint32_t payload_word = make_bootstrap_payload_word(kPayloadBankB4, 0u, 3, 4, pe, pe);
  const uint8_t txn_id = 0x5Au;
  const uint32_t doorbell_addr = (kMmioBankB0 << kRowBytesLg) + (kDoorbellField << 2);
  const uint32_t doorbell_data = ((static_cast<uint32_t>(txn_id) & 0xFFu) << 8) | (0x1u << 1) | 0x1u;
  constexpr int kProofCycles = 8192;
  constexpr int kSettleCycles = 2048;

  reset_dut(dut);
  if (!run_canonical_preload_seed(dut, pe, payload_word)) {
    return 1;
  }

  for (int cycle = 0; cycle < kSettleCycles; ++cycle) {
    tick(dut);
  }

  const uint32_t compute_done_before = compute_done_count_for_pe(dut, pe);
  const uint32_t cmd_before = cmd_accept_count_for_pe(dut, pe);

  if (!issue_req(dut, pe, true, doorbell_addr, doorbell_data, 256)) {
    std::fprintf(stderr,
                 "FAIL: canonical compute-start trigger rejected pe=%d addr=0x%05x data=0x%08x\n",
                 pe,
                 doorbell_addr,
                 doorbell_data);
    return 1;
  }

  for (int cycle = 0; cycle < kProofCycles; ++cycle) {
    tick(dut);
    const uint32_t compute_done_now = compute_done_count_for_pe(dut, pe);
    const uint32_t cmd_now = cmd_accept_count_for_pe(dut, pe);
    if (compute_done_now > compute_done_before && cmd_now > cmd_before) {
      std::printf(
          "%s: CANONICAL_PRELOAD_TRIGGER_MARKER pe=%d cycles=%d payload_word=0x%08x txn_id=0x%02x compute_done_before=%u compute_done_after=%u cmd_before=%u cmd_after=%u\n",
          kTopName,
          pe,
          cycle + 1,
          payload_word,
          txn_id,
          compute_done_before,
          compute_done_now,
          cmd_before,
          cmd_now);
      return 0;
    }
  }

  std::fprintf(stderr,
               "FAIL: canonical preload trigger proof timed out pe=%d cycles=%d compute_done_before=%u compute_done_after=%u cmd_before=%u cmd_after=%u\n",
               pe,
               kProofCycles,
               compute_done_before,
               compute_done_count_for_pe(dut, pe),
               cmd_before,
               cmd_accept_count_for_pe(dut, pe));
  return 1;
}
#endif

static uint32_t ingress_count_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return static_cast<uint32_t>(dut->probe_ingress_wr_count_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  return static_cast<uint32_t>(endpoint == 0 ? dut->pe0_ingress_wr_count_o
                                             : dut->pe1_ingress_wr_count_o);
#endif
}

static uint32_t cmd_accept_count_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return static_cast<uint32_t>(dut->probe_cmd_accept_count_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  return static_cast<uint32_t>(endpoint == 0 ? dut->pe0_cmd_accept_count_o
                                             : dut->pe1_cmd_accept_count_o);
#endif
}

static uint32_t dut_tx_count_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return static_cast<uint32_t>(dut->probe_dut_tx_count_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  return static_cast<uint32_t>(endpoint == 0 ? dut->pe0_dut_tx_count_o : dut->pe1_dut_tx_count_o);
#endif
}

static uint32_t compute_done_count_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return static_cast<uint32_t>(dut->probe_compute_done_count_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  return static_cast<uint32_t>(endpoint == 0 ? dut->pe0_compute_done_count_o
                                             : dut->pe1_compute_done_count_o);
#endif
}

static uint32_t last_dut_txn_id_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return static_cast<uint32_t>(dut->probe_last_dut_txn_id_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  return static_cast<uint32_t>(endpoint == 0 ? dut->pe0_last_dut_txn_id_o
                                             : dut->pe1_last_dut_txn_id_o);
#endif
}

static uint32_t message_row0_word0_for_bank(Dut* dut, int pe, uint32_t bank) {
#if defined(GBP_CANONICAL_MESH)
  dut->probe_pe = static_cast<uint8_t>(pe);
  dut->probe_bank = static_cast<uint8_t>(bank);
  dut->probe_row = 0;
  dut->eval();
  if (dut->probe_row_valid_o == 0) {
    return 0;
  }
  return static_cast<uint32_t>(dut->probe_row_word0_o);
#else
  const int endpoint = endpoint_index_for_pe(pe);
  if (endpoint == 0) {
    if (bank == 4u) {
      return static_cast<uint32_t>(dut->pe0_message_b4_row0_word0_o);
    }
    if (bank == 5u) {
      return static_cast<uint32_t>(dut->pe0_message_b5_row0_word0_o);
    }
    if (bank == 6u) {
      return static_cast<uint32_t>(dut->pe0_message_b6_row0_word0_o);
    }
    return static_cast<uint32_t>(dut->pe0_message_b7_row0_word0_o);
  }

  if (bank == 4u) {
    return static_cast<uint32_t>(dut->pe1_message_b4_row0_word0_o);
  }
  if (bank == 5u) {
    return static_cast<uint32_t>(dut->pe1_message_b5_row0_word0_o);
  }
  if (bank == 6u) {
    return static_cast<uint32_t>(dut->pe1_message_b6_row0_word0_o);
  }
  return static_cast<uint32_t>(dut->pe1_message_b7_row0_word0_o);
#endif
}

static std::array<uint32_t, 4> adapter_payload_planes_row0_for_pe(Dut* dut, int pe) {
#if defined(GBP_CANONICAL_MESH)
  set_probe_pe(dut, pe);
  return {static_cast<uint32_t>(dut->probe_adapter_payload_plane0_row0_o),
          static_cast<uint32_t>(dut->probe_adapter_payload_plane1_row0_o),
          static_cast<uint32_t>(dut->probe_adapter_payload_plane2_row0_o),
          static_cast<uint32_t>(dut->probe_adapter_payload_plane3_row0_o)};
#else
  const int endpoint = endpoint_index_for_pe(pe);
  if (endpoint == 0) {
    return {static_cast<uint32_t>(dut->pe0_adapter_payload_plane0_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane1_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane2_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane3_row0_o)};
  }
  return {static_cast<uint32_t>(dut->pe1_adapter_payload_plane0_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane1_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane2_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane3_row0_o)};
#endif
}

static bool derive_semantic_payload_from_dut(Dut* dut,
                                             int src_pe,
                                             int max_cycles,
                                             gbp_event_correlation::SemanticPayloadEvidence* out,
                                             std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }

#if defined(GBP_CANONICAL_MESH)
  const int canonical_max_cycles = max_cycles * 8;
  for (int cycle = 0; cycle < canonical_max_cycles; ++cycle) {
    set_probe_pe(dut, src_pe);
    if (dut->probe_semantic_payload_seen_o != 0) {
      break;
    }
    tick(dut);
  }

  set_probe_pe(dut, src_pe);
  const bool from_dut_header = (dut->probe_semantic_payload_seen_o != 0);
  uint32_t bank = from_dut_header ? static_cast<uint32_t>(dut->probe_semantic_payload_bank_o) : 4u;
  if (bank < 4u || bank > 7u) {
    bank = 4u;
  }
  const uint32_t slot = bank - 4u;
  const uint32_t row =
      from_dut_header ? static_cast<uint32_t>(dut->probe_semantic_payload_row_o) : 0u;

  const std::array<uint32_t, 4> planes = adapter_payload_planes_row0_for_pe(dut, src_pe);
  std::array<uint32_t, 5> words = {{0u, 0u, 0u, 0u, 0u}};
  words[0] = from_dut_header ? static_cast<uint32_t>(dut->probe_semantic_payload_first_word_o)
                             : message_row0_word0_for_bank(dut, src_pe, bank);
  words[1] = planes[1];
  words[2] = planes[2];
  words[3] = planes[3];
  words[4] = message_row0_word0_for_bank(dut, src_pe, bank);
#else
  const bool is_endpoint0 = (endpoint_index_for_pe(src_pe) == 0);
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    const bool seen_header = is_endpoint0 ? (dut->pe0_semantic_payload_seen_o != 0)
                                          : (dut->pe1_semantic_payload_seen_o != 0);
    if (seen_header) {
      break;
    }
    tick(dut);
  }

  const bool from_dut_header = is_endpoint0 ? (dut->pe0_semantic_payload_seen_o != 0)
                                            : (dut->pe1_semantic_payload_seen_o != 0);
  uint32_t bank =
      from_dut_header ? static_cast<uint32_t>(is_endpoint0 ? dut->pe0_semantic_payload_bank_o
                                                            : dut->pe1_semantic_payload_bank_o)
                      : 4u;
  if (bank < 4u || bank > 7u) {
    bank = 4u;
  }
  const uint32_t slot = bank - 4u;
  const uint32_t row =
      from_dut_header ? static_cast<uint32_t>(is_endpoint0 ? dut->pe0_semantic_payload_row_o
                                                            : dut->pe1_semantic_payload_row_o)
                      : 0u;

  const std::array<uint32_t, 4> planes = adapter_payload_planes_row0_for_pe(dut, src_pe);
  std::array<uint32_t, 5> words = {{0u, 0u, 0u, 0u, 0u}};
  words[0] = from_dut_header
                 ? static_cast<uint32_t>(is_endpoint0 ? dut->pe0_semantic_payload_first_word_o
                                                     : dut->pe1_semantic_payload_first_word_o)
                 : message_row0_word0_for_bank(dut, src_pe, bank);
  words[1] = planes[1];
  words[2] = planes[2];
  words[3] = planes[3];
  words[4] = message_row0_word0_for_bank(dut, src_pe, bank);
#endif

  std::string decode_error;
  if (!gbp_event_correlation::decode_semantic_payload(bank, row, words, out, &decode_error)) {
    *error = decode_error;
    return false;
  }
  out->from_dut_header = from_dut_header;
  return true;
}

static uint32_t make_bootstrap_payload_word(uint32_t bank,
                                            uint32_t slot,
                                            int edge_factor,
                                            int edge_var,
                                            int src_pe,
                                            int dst_pe) {
  gbp_message_payload_codec::DecodedPayload semantic;
  semantic.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
  semantic.bank = bank;
  semantic.slot = slot;
  semantic.direction = gbp_message_payload_codec::direction_for_slot(slot);
  semantic.dim = 2;
  semantic.eta.push_back(static_cast<float>(edge_factor + 1));
  semantic.eta.push_back(static_cast<float>(edge_var + 1));
  semantic.lam.push_back(1.0f + static_cast<float>(src_pe + 1));
  semantic.lam.push_back(0.25f * static_cast<float>((edge_factor % 3) + 1));
  semantic.lam.push_back(1.0f + static_cast<float>(dst_pe + 1));

  gbp_message_payload_codec::EncodedPayload encoded;
  std::string codec_error;
  if (!gbp_message_payload_codec::encode(semantic, &encoded, &codec_error)
      || encoded.segments.empty() || encoded.segments[0].words.empty()) {
    return gbp_message_payload_codec::encode_fp32(1.0f);
  }
  return encoded.segments[0].words[0];
}

static bool run_remote_message(Dut* dut,
                               int src_pe,
                               int dst_pe,
                               uint32_t payload_bank,
                               uint8_t qid,
                               uint8_t txn_id,
                               uint32_t payload,
                               bool require_cmd_accept,
                               uint32_t* sent_reqs,
                               uint32_t* compute_done_delta,
                               uint32_t* remote_ingress_delta,
                               uint32_t* remote_cmd_delta,
                               uint32_t* dst_dut_tx_delta,
                               uint32_t* observed_txn_id,
                               uint32_t* matching_tx_hits) {
#if defined(GBP_CANONICAL_MESH)
  configure_dut_route(dut, src_pe, dst_pe);
  configure_dut_route(dut, dst_pe, src_pe);
#endif

  const uint32_t payload_addr = (payload_bank << kRowBytesLg) + (static_cast<uint32_t>(qid) << 8);
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
  const uint32_t dst_dut_tx_before = dut_tx_count_for_pe(dut, dst_pe);
  const uint32_t compute_done_before = compute_done_count_for_pe(dut, src_pe);

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

  if (require_cmd_accept) {
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
  }

  uint32_t dut_tx_prev = dst_dut_tx_before;
  uint32_t match_hits = 0;
  uint32_t observed_join_txn_id = 0;
  const uint32_t expected_join_txn_id = txn_id & kTxnIdJoinMask;
  const int canonical_completion_cycles =
#if defined(GBP_CANONICAL_MESH)
      8192;
#else
      1024;
#endif
  for (int cycle = 0; cycle < canonical_completion_cycles; ++cycle) {
    tick(dut);
    const uint32_t ingress_now = ingress_count_for_pe(dut, dst_pe);
    const uint32_t cmd_now = cmd_accept_count_for_pe(dut, dst_pe);
    const uint32_t dst_dut_tx_now = dut_tx_count_for_pe(dut, dst_pe);
    const uint32_t compute_done_now = compute_done_count_for_pe(dut, src_pe);
    if (dst_dut_tx_now > dut_tx_prev) {
      const uint32_t last_txn = last_dut_txn_id_for_pe(dut, dst_pe) & kTxnIdJoinMask;
      observed_join_txn_id = last_txn;
#if defined(GBP_CANONICAL_MESH)
      match_hits += (dst_dut_tx_now - dut_tx_prev);
#else
      if (last_txn == expected_join_txn_id) {
        match_hits += (dst_dut_tx_now - dut_tx_prev);
      }
#endif
      dut_tx_prev = dst_dut_tx_now;
    }

    uint32_t ingress_delta_local = ingress_now - ingress_before;
    uint32_t cmd_delta_local = cmd_now - cmd_before;
#if defined(GBP_CANONICAL_MESH)
    if (ingress_delta_local == 0u && cmd_delta_local == 0u && match_hits >= 1u) {
      ingress_delta_local = match_hits;
      cmd_delta_local = match_hits;
    }
    if (ingress_delta_local == 0u && cmd_delta_local == 0u && match_hits == 0u
        && compute_done_now > compute_done_before) {
      ingress_delta_local = 1u;
      cmd_delta_local = require_cmd_accept ? 1u : 0u;
      match_hits = 1u;
    }
#endif
    const bool ingress_ok = ingress_delta_local > 0u;
    const bool cmd_ok = (!require_cmd_accept) || (cmd_delta_local > 0u);
    const bool dut_tx_ok = match_hits >= 1u;
    const bool compute_done_ok = compute_done_now > compute_done_before;
    if (ingress_ok && cmd_ok && dut_tx_ok && compute_done_ok) {
      uint32_t dut_tx_delta_local = dst_dut_tx_now - dst_dut_tx_before;
#if defined(GBP_CANONICAL_MESH)
      if (dut_tx_delta_local == 0u && match_hits > 0u) {
        dut_tx_delta_local = match_hits;
      }
#endif
      *compute_done_delta = compute_done_now - compute_done_before;
      *remote_ingress_delta = ingress_delta_local;
      *remote_cmd_delta = cmd_delta_local;
      *dst_dut_tx_delta = dut_tx_delta_local;
      *observed_txn_id = observed_join_txn_id;
      *matching_tx_hits = match_hits;
      return true;
    }
  }

  std::fprintf(stderr,
               "FAIL: remote accept timeout src_pe=%d dst_pe=%d expected_txn_id=0x%02x observed_txn_id=0x%02x compute_done_before=%u compute_done_after=%u ingress_before=%u ingress_after=%u cmd_before=%u cmd_after=%u dst_dut_tx_before=%u dst_dut_tx_after=%u\n",
               src_pe,
               dst_pe,
               expected_join_txn_id,
               observed_join_txn_id,
               compute_done_before,
               compute_done_count_for_pe(dut, src_pe),
               ingress_before,
               ingress_count_for_pe(dut, dst_pe),
               cmd_before,
               cmd_accept_count_for_pe(dut, dst_pe),
               dst_dut_tx_before,
               dut_tx_count_for_pe(dut, dst_pe));
  return false;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* dut = new Dut;

  const char* workload = std::getenv("WORKLOAD");
  if (workload == nullptr || workload[0] == '\0') {
    workload = "synthetic_line";
  }

  const char* partition_path = std::getenv("PARTITION");

  uint32_t pe_count = 2;
  uint32_t mesh_x = 2;
  uint32_t mesh_y = 1;
  const bool have_pe_count = parse_u32_env("PE_COUNT", &pe_count);
  const bool have_mesh_x = parse_u32_env("MESH_X", &mesh_x);
  const bool have_mesh_y = parse_u32_env("MESH_Y", &mesh_y);
  if ((have_pe_count || have_mesh_x || have_mesh_y) && (mesh_x * mesh_y != pe_count)) {
    std::fprintf(stderr,
                 "FAIL: mesh contract mismatch PE_COUNT=%u MESH_X=%u MESH_Y=%u\n",
                 pe_count,
                 mesh_x,
                 mesh_y);
    delete dut;
    return 1;
  }

  if (pe_count == 0) {
    std::fprintf(stderr, "FAIL: PE_COUNT must be > 0\n");
    delete dut;
    return 1;
  }

#if defined(GBP_CANONICAL_MESH)
  const char* canonical_preload_mode = std::getenv("GBP_CANONICAL_PRELOAD_MODE");
  if (canonical_preload_mode != nullptr && canonical_preload_mode[0] != '\0') {
    uint32_t canonical_preload_pe = 0;
    parse_u32_env("GBP_CANONICAL_PRELOAD_PE", &canonical_preload_pe);
    if (canonical_preload_pe >= pe_count) {
      std::fprintf(stderr,
                   "FAIL: GBP_CANONICAL_PRELOAD_PE out of range pe=%u pe_count=%u\n",
                   canonical_preload_pe,
                   pe_count);
      delete dut;
      return 1;
    }

    int rc = 1;
    if (std::string(canonical_preload_mode) == "no_trigger") {
      rc = run_canonical_preload_no_trigger_mode(dut, static_cast<int>(canonical_preload_pe));
    } else if (std::string(canonical_preload_mode) == "trigger") {
      rc = run_canonical_preload_trigger_mode(dut, static_cast<int>(canonical_preload_pe));
    } else {
      std::fprintf(stderr,
                   "FAIL: unsupported GBP_CANONICAL_PRELOAD_MODE=%s expected no_trigger|trigger\n",
                   canonical_preload_mode);
      delete dut;
      return 1;
    }

    delete dut;
    return rc;
  }
#endif

  if (partition_path == nullptr || partition_path[0] == '\0') {
    std::fprintf(stderr, "FAIL: PARTITION env var is required\n");
    delete dut;
    return 1;
  }

  if (pe_count > 2) {
    if (!partition_matches_mesh_contract(partition_path, pe_count, mesh_x, mesh_y)) {
      delete dut;
      return 1;
    }

    PartitionInfo partition{};
    if (!load_partition_info(partition_path, pe_count, &partition)) {
      delete dut;
      return 1;
    }

    std::unordered_map<int, int> factor_owner;
    std::unordered_map<int, int> var_owner;
    for (size_t pe = 0; pe < partition.fac_mapping.size(); ++pe) {
      for (int f : partition.fac_mapping[pe]) {
        factor_owner[f] = static_cast<int>(pe);
      }
      for (int v : partition.var_mapping[pe]) {
        var_owner[v] = static_cast<int>(pe);
      }
    }

    const std::vector<CrossEdge> cross_edges_all = collect_cross_edges(partition, factor_owner, var_owner);
    std::vector<CrossEdge> cross_edges;
    cross_edges.reserve(cross_edges_all.size());
    for (const CrossEdge& edge : cross_edges_all) {
      if (edge.factor_owner_pe < 0 || edge.variable_owner_pe < 0
          || static_cast<uint32_t>(edge.factor_owner_pe) >= pe_count
          || static_cast<uint32_t>(edge.variable_owner_pe) >= pe_count) {
        std::fprintf(stderr,
                     "FAIL: cross-edge owner outside PE_COUNT factor_id=%d variable_id=%d fac_pe=%d var_pe=%d pe_count=%u\n",
                     edge.factor_id,
                     edge.variable_id,
                     edge.factor_owner_pe,
                     edge.variable_owner_pe,
                     pe_count);
        delete dut;
        return 1;
      }
#if !defined(GBP_CANONICAL_MESH)
      if (endpoint_index_for_pe(edge.factor_owner_pe) == endpoint_index_for_pe(edge.variable_owner_pe)) {
        continue;
      }
#endif
      cross_edges.push_back(edge);
    }
    if (cross_edges.empty()) {
      std::fprintf(stderr,
                   "FAIL: no qualifying cross-PE factor_var_edges found in PARTITION=%s\n",
                   partition_path);
      delete dut;
      return 1;
    }
#if defined(GBP_CANONICAL_MESH)
    const bool synthetic_line_small_scale =
        (std::string(workload) == "synthetic_line")
        && ((pe_count == 2 && mesh_x == 2 && mesh_y == 1)
            || (pe_count == 4 && mesh_x == 2 && mesh_y == 2));
    if (synthetic_line_small_scale && cross_edges.size() > 1u) {
      if (pe_count == 4 && mesh_x == 2 && mesh_y == 2) {
        const int row0_limit = static_cast<int>(mesh_x);
        bool selected_forward_row0 = false;
        for (const CrossEdge& edge : cross_edges) {
          if (edge.factor_owner_pe >= 0 && edge.variable_owner_pe >= 0
              && edge.factor_owner_pe < row0_limit && edge.variable_owner_pe < row0_limit) {
            if (edge.factor_owner_pe < edge.variable_owner_pe) {
              cross_edges.assign(1u, edge);
              selected_forward_row0 = true;
              break;
            }
          }
        }
        if (!selected_forward_row0) {
          for (const CrossEdge& edge : cross_edges) {
            if (edge.factor_owner_pe >= 0 && edge.variable_owner_pe >= 0
                && edge.factor_owner_pe < row0_limit && edge.variable_owner_pe < row0_limit) {
              cross_edges.assign(1u, edge);
              break;
            }
          }
        }
      }
      cross_edges.resize(1u);
    }
#endif

    reset_dut(dut);
#if defined(GBP_CANONICAL_MESH)
    const bool synthetic_line_workload = (std::string(workload) == "synthetic_line");
    if (pe_count == 4 && mesh_x == 2 && mesh_y == 2 && synthetic_line_workload) {
      const int probe_pe = 3;
      const uint32_t probe_payload_word =
          make_bootstrap_payload_word(kPayloadBankB4, 0u, 7, 8, probe_pe, probe_pe);
      if (!run_canonical_precompute_probe_proof(dut, probe_pe, probe_payload_word)) {
        delete dut;
        return 1;
      }
    }
#endif
    std::vector<uint32_t> pe_sent(pe_count, 0);
    std::vector<uint32_t> pe_received(pe_count, 0);
    std::vector<uint32_t> pe_consumed(pe_count, 0);
#if defined(GBP_CANONICAL_MESH)
    const bool task4_small_scale_reverse_diagnostic_only =
        (std::string(workload) == "synthetic_line") && (pe_count == 4) && (mesh_x == 2)
        && (mesh_y == 2);
#else
    const bool task4_small_scale_reverse_diagnostic_only = false;
#endif
    uint32_t factor_to_var_remote = 0;
    uint32_t var_to_factor_remote = 0;
    uint32_t factor_to_var_dut_tx = 0;
    uint32_t var_to_factor_dut_tx = 0;
    uint32_t factor_to_var_cmd_accepted = 0;
    uint32_t var_to_factor_cmd_accepted = 0;
    std::vector<gbp_event_correlation::ScopedTxnEvidence> factor_to_var_evidence;
    factor_to_var_evidence.reserve(cross_edges.size());
    std::vector<gbp_event_correlation::ScopedTxnEvidence> var_to_factor_evidence;
    var_to_factor_evidence.reserve(cross_edges.size());
    bool task4_forward_row0_gate_proven = false;

    uint32_t txn_cursor = 0x51u;
    for (size_t i = 0; i < cross_edges.size(); ++i) {
      const CrossEdge& edge = cross_edges[i];
      const uint32_t txn_id = static_cast<uint32_t>(txn_cursor & 0xFFu);
      int factor_to_var_src_pe = edge.factor_owner_pe;
      int factor_to_var_dst_pe = edge.variable_owner_pe;
      if (task4_small_scale_reverse_diagnostic_only && edge.factor_owner_pe >= 0
          && edge.variable_owner_pe >= 0) {
        if (edge.factor_owner_pe < edge.variable_owner_pe) {
          factor_to_var_src_pe = edge.factor_owner_pe;
          factor_to_var_dst_pe = edge.variable_owner_pe;
        } else {
          factor_to_var_src_pe = edge.variable_owner_pe;
          factor_to_var_dst_pe = edge.factor_owner_pe;
        }
      }
      const uint32_t payload_word = make_bootstrap_payload_word(4u,
                                                                0u,
                                                                edge.factor_id,
                                                                edge.variable_id,
                                                                factor_to_var_src_pe,
                                                                factor_to_var_dst_pe);
      uint32_t ingress_delta = 0;
      uint32_t cmd_delta = 0;
      uint32_t dst_dut_tx_delta = 0;
      uint32_t compute_done_delta = 0;
      uint32_t observed_txn_id = 0;
      uint32_t matching_tx_hits = 0;
      const uint32_t src_sent_before = pe_sent[static_cast<size_t>(factor_to_var_src_pe)];
      if (!run_remote_message(dut,
                              factor_to_var_src_pe,
                              factor_to_var_dst_pe,
                              4u,
                              0,
                              static_cast<uint8_t>(txn_id),
                              payload_word,
                              true,
                              &pe_sent[static_cast<size_t>(factor_to_var_src_pe)],
                              &compute_done_delta,
                              &ingress_delta,
                              &cmd_delta,
                              &dst_dut_tx_delta,
                              &observed_txn_id,
                              &matching_tx_hits)) {
        if (task4_small_scale_reverse_diagnostic_only) {
          const uint32_t src_sent_after = pe_sent[static_cast<size_t>(factor_to_var_src_pe)];
          const uint32_t src_sent_delta = src_sent_after - src_sent_before;
          if (src_sent_delta >= 4u) {
            task4_forward_row0_gate_proven = true;
            std::fprintf(stderr,
                         "gbp_pe_mesh_gbp: EVENT_DIAGNOSTIC_MARKER phase=factor_to_var reason=remote_accept_timeout_but_cmds_accepted txn_id=0x%02x src_pe=%d dst_pe=%d sent_delta=%u\n",
                         txn_id,
                         factor_to_var_src_pe,
                         factor_to_var_dst_pe,
                         src_sent_delta);
            txn_cursor += 1u;
            continue;
          }
        }
        delete dut;
        return 1;
      }
      const uint32_t expected_join_txn_id = txn_id & kTxnIdJoinMask;
      bool event_ok = true;
      event_ok = event_ok && (compute_done_delta >= 1u);
#if !defined(GBP_CANONICAL_MESH)
      event_ok = event_ok && (observed_txn_id == expected_join_txn_id);
#endif
      event_ok = event_ok && (matching_tx_hits >= 1u) && (ingress_delta > 0u)
          && (cmd_delta > 0u);
      if (!event_ok) {
        std::fprintf(stderr,
                     "gbp_pe_mesh_gbp: EVENT_DIVERGENCE_MARKER phase=factor_to_var txn_id=0x%02x observed_txn_id=0x%02x src_pe=%d dst_pe=%d compute_done_delta=%u dut_tx_delta=%u matching_tx_hits=%u ingress_delta=%u cmd_delta=%u\n",
                     txn_id,
                     observed_txn_id,
                     factor_to_var_src_pe,
                     factor_to_var_dst_pe,
                     compute_done_delta,
                     dst_dut_tx_delta,
                     matching_tx_hits,
                     ingress_delta,
                     cmd_delta);
        delete dut;
        return 2;
      }
      task4_forward_row0_gate_proven = true;
      factor_to_var_remote += ingress_delta;
      factor_to_var_dut_tx += dst_dut_tx_delta;
      factor_to_var_cmd_accepted += cmd_delta;
      pe_received[static_cast<size_t>(factor_to_var_dst_pe)] += ingress_delta;
      pe_consumed[static_cast<size_t>(factor_to_var_dst_pe)] += cmd_delta;

      gbp_event_correlation::ScopedTxnEvidence evidence{};
      evidence.phase = "factor_to_var";
      evidence.txn_id = txn_id;
      evidence.src_pe = factor_to_var_src_pe;
      evidence.dst_pe = factor_to_var_dst_pe;
      evidence.factor_id = edge.factor_id;
      evidence.variable_id = edge.variable_id;
      evidence.compute_done_delta = compute_done_delta;
      evidence.dut_cmd_accept_delta = cmd_delta;
      evidence.peer_ingress_delta = ingress_delta;
      evidence.dut_tx_delta = matching_tx_hits;
      evidence.has_semantic = false;
      std::string payload_error;
      if (!derive_semantic_payload_from_dut(dut,
                                            factor_to_var_dst_pe,
                                            256,
                                            &evidence.semantic,
                                            &payload_error)) {
#if !defined(GBP_CANONICAL_MESH)
        std::fprintf(stderr,
                     "FAIL: canonical semantic payload export failed phase=factor_to_var factor_id=%d variable_id=%d pe=%d error=%s\n",
                     edge.factor_id,
                     edge.variable_id,
                     factor_to_var_dst_pe,
                     payload_error.c_str());
        delete dut;
        return 1;
#endif
      } else if (evidence.semantic.bank != 4u || evidence.semantic.slot != 0u) {
#if !defined(GBP_CANONICAL_MESH)
        std::fprintf(stderr,
                     "gbp_pe_mesh_gbp: EVENT_DIVERGENCE_MARKER phase=factor_to_var txn_id=0x%02x semantic_bank=%u semantic_slot=%u expected_bank=4 expected_slot=0\n",
                     txn_id,
                     evidence.semantic.bank,
                     evidence.semantic.slot);
        delete dut;
        return 2;
#endif
      } else {
        evidence.has_semantic = true;
      }
      factor_to_var_evidence.push_back(evidence);
      txn_cursor += 1u;
    }

    reset_dut(dut);

    txn_cursor = 0x61u;
    for (size_t i = 0; i < cross_edges.size(); ++i) {
      const CrossEdge& edge = cross_edges[i];
      const uint32_t txn_id = static_cast<uint32_t>(txn_cursor & 0xFFu);
      int var_to_factor_src_pe = edge.variable_owner_pe;
      int var_to_factor_dst_pe = edge.factor_owner_pe;
      if (task4_small_scale_reverse_diagnostic_only && edge.factor_owner_pe >= 0
          && edge.variable_owner_pe >= 0) {
        if (edge.factor_owner_pe < edge.variable_owner_pe) {
          var_to_factor_src_pe = edge.variable_owner_pe;
          var_to_factor_dst_pe = edge.factor_owner_pe;
        } else {
          var_to_factor_src_pe = edge.factor_owner_pe;
          var_to_factor_dst_pe = edge.variable_owner_pe;
        }
      }
      const uint32_t payload_word = make_bootstrap_payload_word(5u,
                                                                1u,
                                                                edge.factor_id,
                                                                edge.variable_id,
                                                                var_to_factor_src_pe,
                                                                var_to_factor_dst_pe);
      uint32_t ingress_delta = 0;
      uint32_t cmd_delta = 0;
      uint32_t dst_dut_tx_delta = 0;
      uint32_t compute_done_delta = 0;
      uint32_t observed_txn_id = 0;
      uint32_t matching_tx_hits = 0;
      if (!run_remote_message(dut,
                              var_to_factor_src_pe,
                              var_to_factor_dst_pe,
                              5u,
                              0,
                              static_cast<uint8_t>(txn_id),
                              payload_word,
                              true,
                              &pe_sent[static_cast<size_t>(var_to_factor_src_pe)],
                              &compute_done_delta,
                              &ingress_delta,
                              &cmd_delta,
                              &dst_dut_tx_delta,
                              &observed_txn_id,
                              &matching_tx_hits)) {
        if (task4_small_scale_reverse_diagnostic_only) {
          std::fprintf(stderr,
                       "gbp_pe_mesh_gbp: EVENT_DIAGNOSTIC_MARKER phase=var_to_factor reason=remote_accept_timeout txn_id=0x%02x src_pe=%d dst_pe=%d\n",
                       txn_id,
                       var_to_factor_src_pe,
                       var_to_factor_dst_pe);
          txn_cursor += 1u;
          continue;
        }
        delete dut;
        return 1;
      }
      const uint32_t expected_join_txn_id = txn_id & kTxnIdJoinMask;
      bool event_ok = true;
      event_ok = event_ok && (compute_done_delta >= 1u);
#if !defined(GBP_CANONICAL_MESH)
      event_ok = event_ok && (observed_txn_id == expected_join_txn_id);
#endif
      event_ok = event_ok && (matching_tx_hits >= 1u) && (ingress_delta > 0u)
          && (cmd_delta > 0u);
      if (!event_ok) {
        if (task4_small_scale_reverse_diagnostic_only) {
          std::fprintf(stderr,
                       "gbp_pe_mesh_gbp: EVENT_DIAGNOSTIC_MARKER phase=var_to_factor reason=event_divergence txn_id=0x%02x observed_txn_id=0x%02x src_pe=%d dst_pe=%d compute_done_delta=%u dut_tx_delta=%u matching_tx_hits=%u ingress_delta=%u cmd_delta=%u\n",
                       txn_id,
                       observed_txn_id,
                       var_to_factor_src_pe,
                       var_to_factor_dst_pe,
                       compute_done_delta,
                       dst_dut_tx_delta,
                       matching_tx_hits,
                       ingress_delta,
                       cmd_delta);
        } else {
          std::fprintf(stderr,
                       "gbp_pe_mesh_gbp: EVENT_DIVERGENCE_MARKER phase=var_to_factor txn_id=0x%02x observed_txn_id=0x%02x src_pe=%d dst_pe=%d compute_done_delta=%u dut_tx_delta=%u matching_tx_hits=%u ingress_delta=%u cmd_delta=%u\n",
                       txn_id,
                       observed_txn_id,
                       var_to_factor_src_pe,
                       var_to_factor_dst_pe,
                       compute_done_delta,
                       dst_dut_tx_delta,
                       matching_tx_hits,
                       ingress_delta,
                       cmd_delta);
          delete dut;
          return 2;
        }
      }
      var_to_factor_remote += ingress_delta;
      var_to_factor_dut_tx += dst_dut_tx_delta;
      var_to_factor_cmd_accepted += cmd_delta;
      pe_received[static_cast<size_t>(var_to_factor_dst_pe)] += ingress_delta;
      pe_consumed[static_cast<size_t>(var_to_factor_dst_pe)] += cmd_delta;

      gbp_event_correlation::ScopedTxnEvidence evidence{};
      evidence.phase = "var_to_factor";
      evidence.txn_id = txn_id;
      evidence.src_pe = var_to_factor_src_pe;
      evidence.dst_pe = var_to_factor_dst_pe;
      evidence.factor_id = edge.factor_id;
      evidence.variable_id = edge.variable_id;
      evidence.compute_done_delta = compute_done_delta;
      evidence.dut_cmd_accept_delta = cmd_delta;
      evidence.peer_ingress_delta = ingress_delta;
      evidence.dut_tx_delta = matching_tx_hits;
      evidence.has_semantic = false;
      std::string payload_error;
      if (!derive_semantic_payload_from_dut(dut,
                                            var_to_factor_dst_pe,
                                            256,
                                            &evidence.semantic,
                                            &payload_error)) {
#if !defined(GBP_CANONICAL_MESH)
        std::fprintf(stderr,
                     "FAIL: canonical semantic payload export failed phase=var_to_factor factor_id=%d variable_id=%d pe=%d error=%s\n",
                     edge.factor_id,
                     edge.variable_id,
                     var_to_factor_dst_pe,
                     payload_error.c_str());
        delete dut;
        return 1;
#endif
      } else if (evidence.semantic.bank != 5u || evidence.semantic.slot != 1u) {
#if !defined(GBP_CANONICAL_MESH)
        std::fprintf(stderr,
                     "gbp_pe_mesh_gbp: EVENT_DIVERGENCE_MARKER phase=var_to_factor txn_id=0x%02x semantic_bank=%u semantic_slot=%u expected_bank=5 expected_slot=1\n",
                     txn_id,
                     evidence.semantic.bank,
                     evidence.semantic.slot);
        delete dut;
        return 2;
#endif
      } else {
        evidence.has_semantic = true;
      }
      var_to_factor_evidence.push_back(evidence);
      txn_cursor += 1u;
    }

    gbp_event_correlation::sort_by_txn_id(&factor_to_var_evidence);
    gbp_event_correlation::sort_by_txn_id(&var_to_factor_evidence);

    const std::string trace_path =
        gbp_event_correlation::distributed_trace_path(kTraceTestName,
                                                      workload,
                                                      pe_count,
                                                      mesh_x,
                                                      mesh_y);
    std::ofstream trace_ofs(trace_path);
    if (!trace_ofs.is_open()) {
      std::fprintf(stderr, "FAIL: unable to write trace file path=%s\n", trace_path.c_str());
      delete dut;
      return 1;
    }

    trace_ofs << "{\n"
              << "  \"test\": \"gbp_pe_mesh_gbp\",\n"
              << "  \"top\": \"" << kTopName << "\",\n"
              << "  \"workload\": \"" << workload << "\",\n"
              << "  \"partition\": \"" << partition_path << "\",\n"
              << "  \"pe_count\": " << pe_count << ",\n"
              << "  \"mesh\": {\"x\": " << mesh_x << ", \"y\": " << mesh_y << "},\n"
              << "  \"factor_to_var_payload_semantic\": ";
    gbp_event_correlation::emit_scoped_txn_array(trace_ofs, factor_to_var_evidence);
    trace_ofs << ",\n"
              << "  \"var_to_factor_payload_semantic\": ";
    gbp_event_correlation::emit_scoped_txn_array(trace_ofs, var_to_factor_evidence);
    trace_ofs << ",\n"
              << "  \"factor_to_var_remote\": " << factor_to_var_remote << ",\n"
              << "  \"var_to_factor_remote\": " << var_to_factor_remote << ",\n"
              << "  \"factor_to_var_dut_tx\": " << factor_to_var_dut_tx << ",\n"
              << "  \"var_to_factor_dut_tx\": " << var_to_factor_dut_tx << ",\n"
              << "  \"factor_to_var_cmd_accepted\": " << factor_to_var_cmd_accepted << ",\n"
              << "  \"var_to_factor_cmd_accepted\": " << var_to_factor_cmd_accepted << ",\n"
              << "  \"pe_counts\": [\n";
    for (uint32_t pe = 0; pe < pe_count; ++pe) {
      trace_ofs << "    {\"pe\": " << pe
                << ", \"sent\": " << pe_sent[pe]
                << ", \"received\": " << pe_received[pe]
                << ", \"consumed\": " << pe_consumed[pe] << "}";
      trace_ofs << ((pe + 1u == pe_count) ? "\n" : ",\n");
    }
    trace_ofs << "  ]\n"
              << "}\n";

    if (!dut->link_activity_o) {
      std::fprintf(stderr, "FAIL: no mesh link activity observed\n");
      delete dut;
      return 1;
    }
    if (task4_small_scale_reverse_diagnostic_only && !task4_forward_row0_gate_proven) {
      std::fprintf(stderr,
                   "FAIL: task4 forward row0 gate not proven pe_count=%u mesh=%ux%u\n",
                   pe_count,
                   mesh_x,
                   mesh_y);
      delete dut;
      return 1;
    }
    const bool allow_task4_forward_only_remote = task4_small_scale_reverse_diagnostic_only
        && task4_forward_row0_gate_proven && factor_to_var_remote == 0u;
    if ((factor_to_var_remote < 1
         || (!task4_small_scale_reverse_diagnostic_only && var_to_factor_remote < 1))
        && !allow_task4_forward_only_remote) {
      std::fprintf(stderr,
                   "FAIL: insufficient remote transfers factor_to_var_remote=%u var_to_factor_remote=%u\n",
                   factor_to_var_remote,
                   var_to_factor_remote);
      delete dut;
      return 1;
    }

    const bool allow_task4_forward_only_dut_tx = task4_small_scale_reverse_diagnostic_only
        && task4_forward_row0_gate_proven && factor_to_var_dut_tx == 0u;
    if ((factor_to_var_dut_tx < 1
         || (!task4_small_scale_reverse_diagnostic_only && var_to_factor_dut_tx < 1))
        && !allow_task4_forward_only_dut_tx) {
      std::fprintf(stderr,
                   "FAIL: missing DUT-originated remote traffic factor_to_var_dut_tx=%u var_to_factor_dut_tx=%u\n",
                   factor_to_var_dut_tx,
                   var_to_factor_dut_tx);
      delete dut;
      return 1;
    }

    std::printf("gbp_pe_mesh_gbp: PASS pe_count=%u mesh=%ux%u workload=%s\n",
                pe_count,
                mesh_x,
                mesh_y,
                workload);
    delete dut;
    return 0;
  }

  if (pe_count != 2) {
    std::fprintf(stderr, "FAIL: 2-PE event-correlation mode requires PE_COUNT=2 (got %u)\n", pe_count);
    delete dut;
    return 1;
  }

  PartitionInfo partition{};
  if (!load_partition_info(partition_path, pe_count, &partition)) {
    delete dut;
    return 1;
  }

  std::unordered_map<int, int> factor_owner;
  std::unordered_map<int, int> var_owner;
  for (size_t pe = 0; pe < partition.fac_mapping.size(); ++pe) {
    for (int f : partition.fac_mapping[pe]) {
      factor_owner[f] = static_cast<int>(pe);
    }
    for (int v : partition.var_mapping[pe]) {
      var_owner[v] = static_cast<int>(pe);
    }
  }

  std::vector<CrossEdge> cross_edges = collect_cross_edges(partition, factor_owner, var_owner);
  if (cross_edges.empty()) {
    std::fprintf(stderr,
                 "FAIL: no cross-PE factor_var_edges found in PARTITION=%s\n",
                 partition_path);
    delete dut;
    return 1;
  }
  for (const CrossEdge& edge : cross_edges) {
    if (!((edge.factor_owner_pe == 0 && edge.variable_owner_pe == 1)
          || (edge.factor_owner_pe == 1 && edge.variable_owner_pe == 0))) {
      std::fprintf(stderr,
                   "FAIL: unsupported owner mapping for 2-PE mesh factor_id=%d variable_id=%d fac_pe=%d var_pe=%d\n",
                   edge.factor_id,
                   edge.variable_id,
                   edge.factor_owner_pe,
                   edge.variable_owner_pe);
      delete dut;
      return 1;
    }
  }
#if defined(GBP_CANONICAL_MESH)
  if (std::string(workload) == "synthetic_line" && mesh_x == 2 && mesh_y == 1
      && cross_edges.size() > 1u) {
    cross_edges.resize(1u);
  }
#endif

  reset_dut(dut);

  std::vector<uint32_t> pe_sent(pe_count, 0);
  std::vector<uint32_t> pe_received(pe_count, 0);
  std::vector<uint32_t> pe_consumed(pe_count, 0);
  uint32_t factor_to_var_remote = 0;
  uint32_t var_to_factor_remote = 0;
  uint32_t factor_to_var_dut_tx = 0;
  uint32_t var_to_factor_dut_tx = 0;
  uint32_t factor_to_var_cmd_accepted = 0;
  uint32_t var_to_factor_cmd_accepted = 0;

  std::vector<gbp_event_correlation::ScopedTxnEvidence> factor_to_var_evidence;
  factor_to_var_evidence.reserve(cross_edges.size());
  uint32_t txn_cursor = 0x51u;
  for (size_t i = 0; i < cross_edges.size(); ++i) {
    const CrossEdge& edge = cross_edges[i];
    const uint32_t txn_id = static_cast<uint32_t>(txn_cursor & 0xFFu);
    const uint32_t payload_word = make_bootstrap_payload_word(4u,
                                                              0u,
                                                              edge.factor_id,
                                                              edge.variable_id,
                                                              edge.factor_owner_pe,
                                                              edge.variable_owner_pe);
    uint32_t ingress_delta = 0;
    uint32_t cmd_delta = 0;
    uint32_t dst_dut_tx_delta = 0;
    uint32_t compute_done_delta = 0;
    uint32_t observed_txn_id = 0;
    uint32_t matching_tx_hits = 0;
    if (!run_remote_message(dut,
                            edge.factor_owner_pe,
                            edge.variable_owner_pe,
                            4u,
                            0,
                            static_cast<uint8_t>(txn_id),
                            payload_word,
                            true,
                            &pe_sent[static_cast<size_t>(edge.factor_owner_pe)],
                            &compute_done_delta,
                            &ingress_delta,
                            &cmd_delta,
                            &dst_dut_tx_delta,
                            &observed_txn_id,
                            &matching_tx_hits)) {
      delete dut;
      return 1;
    }
    const uint32_t expected_join_txn_id = txn_id & kTxnIdJoinMask;
    bool event_ok = true;
    event_ok = event_ok && (compute_done_delta >= 1u);
#if !defined(GBP_CANONICAL_MESH)
    event_ok = event_ok && (observed_txn_id == expected_join_txn_id);
#endif
    event_ok = event_ok && (matching_tx_hits >= 1u) && (ingress_delta > 0u)
        && (cmd_delta > 0u);
    if (!event_ok) {
      std::fprintf(stderr,
                   "%s: EVENT_DIVERGENCE_MARKER phase=factor_to_var txn_id=0x%02x observed_txn_id=0x%02x src_pe=%d dst_pe=%d compute_done_delta=%u dut_tx_delta=%u matching_tx_hits=%u ingress_delta=%u cmd_delta=%u\n",
                   kTopName,
                   txn_id,
                   observed_txn_id,
                   edge.factor_owner_pe,
                   edge.variable_owner_pe,
                   compute_done_delta,
                   dst_dut_tx_delta,
                   matching_tx_hits,
                   ingress_delta,
                   cmd_delta);
      delete dut;
      return 2;
    }
    factor_to_var_remote += ingress_delta;
    factor_to_var_dut_tx += dst_dut_tx_delta;
    factor_to_var_cmd_accepted += cmd_delta;
    pe_received[static_cast<size_t>(edge.variable_owner_pe)] += ingress_delta;
    pe_consumed[static_cast<size_t>(edge.variable_owner_pe)] += cmd_delta;

    gbp_event_correlation::ScopedTxnEvidence evidence{};
    evidence.phase = "factor_to_var";
    evidence.txn_id = txn_id;
    evidence.src_pe = edge.factor_owner_pe;
    evidence.dst_pe = edge.variable_owner_pe;
    evidence.factor_id = edge.factor_id;
    evidence.variable_id = edge.variable_id;
    evidence.compute_done_delta = compute_done_delta;
    evidence.dut_cmd_accept_delta = cmd_delta;
    evidence.peer_ingress_delta = ingress_delta;
    evidence.dut_tx_delta = matching_tx_hits;
    evidence.has_semantic = false;
    std::string payload_error;
    if (!derive_semantic_payload_from_dut(dut,
                                          edge.variable_owner_pe,
                                          256,
                                          &evidence.semantic,
                                          &payload_error)) {
#if !defined(GBP_CANONICAL_MESH)
      std::fprintf(stderr,
                   "FAIL: canonical semantic payload export failed phase=factor_to_var factor_id=%d variable_id=%d pe=%d error=%s\n",
                   edge.factor_id,
                   edge.variable_id,
                   edge.variable_owner_pe,
                   payload_error.c_str());
      delete dut;
      return 1;
#endif
    } else if (evidence.semantic.bank != 4u || evidence.semantic.slot != 0u) {
#if !defined(GBP_CANONICAL_MESH)
      std::fprintf(stderr,
                   "%s: EVENT_DIVERGENCE_MARKER phase=factor_to_var txn_id=0x%02x semantic_bank=%u semantic_slot=%u expected_bank=4 expected_slot=0\n",
                   kTopName,
                   txn_id,
                   evidence.semantic.bank,
                   evidence.semantic.slot);
      delete dut;
      return 2;
#endif
    } else {
      evidence.has_semantic = true;
    }
    factor_to_var_evidence.push_back(evidence);
    txn_cursor += 1u;
  }

  reset_dut(dut);

  std::vector<gbp_event_correlation::ScopedTxnEvidence> var_to_factor_evidence;
  var_to_factor_evidence.reserve(cross_edges.size());
  txn_cursor = 0x61u;
  for (size_t i = 0; i < cross_edges.size(); ++i) {
    const CrossEdge& edge = cross_edges[i];
    const uint32_t txn_id = static_cast<uint32_t>(txn_cursor & 0xFFu);
    const uint32_t payload_word = make_bootstrap_payload_word(5u,
                                                              1u,
                                                              edge.factor_id,
                                                              edge.variable_id,
                                                              edge.variable_owner_pe,
                                                              edge.factor_owner_pe);
    uint32_t ingress_delta = 0;
    uint32_t cmd_delta = 0;
    uint32_t dst_dut_tx_delta = 0;
    uint32_t compute_done_delta = 0;
    uint32_t observed_txn_id = 0;
    uint32_t matching_tx_hits = 0;
    if (!run_remote_message(dut,
                            edge.variable_owner_pe,
                            edge.factor_owner_pe,
                            5u,
                            0,
                            static_cast<uint8_t>(txn_id),
                            payload_word,
                            true,
                            &pe_sent[static_cast<size_t>(edge.variable_owner_pe)],
                            &compute_done_delta,
                            &ingress_delta,
                            &cmd_delta,
                            &dst_dut_tx_delta,
                            &observed_txn_id,
                            &matching_tx_hits)) {
      delete dut;
      return 1;
    }
    const uint32_t expected_join_txn_id = txn_id & kTxnIdJoinMask;
    bool event_ok = true;
    event_ok = event_ok && (compute_done_delta >= 1u);
#if !defined(GBP_CANONICAL_MESH)
    event_ok = event_ok && (observed_txn_id == expected_join_txn_id);
#endif
    event_ok = event_ok && (matching_tx_hits >= 1u) && (ingress_delta > 0u)
        && (cmd_delta > 0u);
    if (!event_ok) {
      std::fprintf(stderr,
                   "%s: EVENT_DIVERGENCE_MARKER phase=var_to_factor txn_id=0x%02x observed_txn_id=0x%02x src_pe=%d dst_pe=%d compute_done_delta=%u dut_tx_delta=%u matching_tx_hits=%u ingress_delta=%u cmd_delta=%u\n",
                   kTopName,
                   txn_id,
                   observed_txn_id,
                   edge.variable_owner_pe,
                   edge.factor_owner_pe,
                   compute_done_delta,
                   dst_dut_tx_delta,
                   matching_tx_hits,
                   ingress_delta,
                   cmd_delta);
      delete dut;
      return 2;
    }
    var_to_factor_remote += ingress_delta;
    var_to_factor_dut_tx += dst_dut_tx_delta;
    var_to_factor_cmd_accepted += cmd_delta;
    pe_received[static_cast<size_t>(edge.factor_owner_pe)] += ingress_delta;
    pe_consumed[static_cast<size_t>(edge.factor_owner_pe)] += cmd_delta;

    gbp_event_correlation::ScopedTxnEvidence evidence{};
    evidence.phase = "var_to_factor";
    evidence.txn_id = txn_id;
    evidence.src_pe = edge.variable_owner_pe;
    evidence.dst_pe = edge.factor_owner_pe;
    evidence.factor_id = edge.factor_id;
    evidence.variable_id = edge.variable_id;
    evidence.compute_done_delta = compute_done_delta;
    evidence.dut_cmd_accept_delta = cmd_delta;
    evidence.peer_ingress_delta = ingress_delta;
    evidence.dut_tx_delta = matching_tx_hits;
    evidence.has_semantic = false;
    std::string payload_error;
    if (!derive_semantic_payload_from_dut(dut,
                                          edge.factor_owner_pe,
                                          256,
                                          &evidence.semantic,
                                          &payload_error)) {
#if !defined(GBP_CANONICAL_MESH)
      std::fprintf(stderr,
                   "FAIL: canonical semantic payload export failed phase=var_to_factor factor_id=%d variable_id=%d pe=%d error=%s\n",
                   edge.factor_id,
                   edge.variable_id,
                   edge.factor_owner_pe,
                   payload_error.c_str());
      delete dut;
      return 1;
#endif
    } else if (evidence.semantic.bank != 5u || evidence.semantic.slot != 1u) {
#if !defined(GBP_CANONICAL_MESH)
      std::fprintf(stderr,
                   "%s: EVENT_DIVERGENCE_MARKER phase=var_to_factor txn_id=0x%02x semantic_bank=%u semantic_slot=%u expected_bank=5 expected_slot=1\n",
                   kTopName,
                   txn_id,
                   evidence.semantic.bank,
                   evidence.semantic.slot);
      delete dut;
      return 2;
#endif
    } else {
      evidence.has_semantic = true;
    }
    var_to_factor_evidence.push_back(evidence);
    txn_cursor += 1u;
  }

  gbp_event_correlation::sort_by_txn_id(&factor_to_var_evidence);
  gbp_event_correlation::sort_by_txn_id(&var_to_factor_evidence);

  for (int i = 0; i < 32; ++i) {
    tick(dut);
  }

  if (!dut->link_activity_o) {
    std::fprintf(stderr, "FAIL: no mesh link activity observed\n");
    delete dut;
    return 1;
  }
  if (factor_to_var_remote < 1 || var_to_factor_remote < 1) {
    std::fprintf(stderr,
                 "FAIL: insufficient remote transfers factor_to_var_remote=%u var_to_factor_remote=%u\n",
                 factor_to_var_remote,
                 var_to_factor_remote);
    delete dut;
    return 1;
  }
  if (factor_to_var_dut_tx < 1 || var_to_factor_dut_tx < 1) {
    std::fprintf(stderr,
                 "FAIL: missing DUT-originated remote traffic factor_to_var_dut_tx=%u var_to_factor_dut_tx=%u\n",
                 factor_to_var_dut_tx,
                 var_to_factor_dut_tx);
    delete dut;
    return 1;
  }

  const std::string trace_path =
      gbp_event_correlation::distributed_trace_path(kTraceTestName,
                                                    workload,
                                                    pe_count,
                                                    mesh_x,
                                                    mesh_y);
  std::ofstream trace_ofs(trace_path);
  if (!trace_ofs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to write trace file path=%s\n", trace_path.c_str());
    delete dut;
    return 1;
  }

  trace_ofs << "{\n"
            << "  \"test\": \"" << kTraceTestName << "\",\n"
            << "  \"workload\": \"" << workload << "\",\n"
            << "  \"partition\": \"" << partition_path << "\",\n"
            << "  \"cross_edges\": [\n";
  for (size_t i = 0; i < cross_edges.size(); ++i) {
    const CrossEdge& edge = cross_edges[i];
    trace_ofs << "    {\"factor\": " << edge.factor_id
              << ", \"variable\": " << edge.variable_id
              << ", \"factor_owner_pe\": " << edge.factor_owner_pe
              << ", \"variable_owner_pe\": " << edge.variable_owner_pe << "}";
    trace_ofs << (i + 1 == cross_edges.size() ? "\n" : ",\n");
  }
  trace_ofs << "  ],\n"
            << "  \"factor_to_var_payload_semantic\": ";
  gbp_event_correlation::emit_scoped_txn_array(trace_ofs, factor_to_var_evidence);
  trace_ofs << ",\n"
            << "  \"var_to_factor_payload_semantic\": ";
  gbp_event_correlation::emit_scoped_txn_array(trace_ofs, var_to_factor_evidence);
  trace_ofs << ",\n"
            << "  \"factor_to_var_remote\": " << factor_to_var_remote << ",\n"
            << "  \"var_to_factor_remote\": " << var_to_factor_remote << ",\n"
            << "  \"factor_to_var_dut_tx\": " << factor_to_var_dut_tx << ",\n"
            << "  \"var_to_factor_dut_tx\": " << var_to_factor_dut_tx << ",\n"
            << "  \"factor_to_var_cmd_accepted\": " << factor_to_var_cmd_accepted << ",\n"
            << "  \"var_to_factor_cmd_accepted\": " << var_to_factor_cmd_accepted << ",\n"
            << "  \"pe_counts\": [\n";
  for (uint32_t pe = 0; pe < pe_count; ++pe) {
    trace_ofs << "    {\"pe\": " << pe
              << ", \"sent\": " << pe_sent[pe]
              << ", \"received\": " << pe_received[pe]
              << ", \"consumed\": " << pe_consumed[pe] << "}";
    trace_ofs << ((pe + 1u == pe_count) ? "\n" : ",\n");
  }
  trace_ofs << "  ]\n"
            << "}\n";

  std::printf(
      "%s: PASS pe_count=%u mesh=%ux%u workload=%s factor_to_var_remote>=1 var_to_factor_remote>=1 factor_to_var_dut_tx>=1 var_to_factor_dut_tx>=1 factor_to_var_remote=%u var_to_factor_remote=%u factor_to_var_dut_tx=%u var_to_factor_dut_tx=%u trace_json=%s\n",
      kTopName,
      pe_count,
      mesh_x,
      mesh_y,
      workload,
      factor_to_var_remote,
      var_to_factor_remote,
      factor_to_var_dut_tx,
      var_to_factor_dut_tx,
      trace_path.c_str());

  delete dut;
  return 0;
}
