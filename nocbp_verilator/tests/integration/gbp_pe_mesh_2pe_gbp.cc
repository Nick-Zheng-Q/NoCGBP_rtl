#include <array>
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
#include "Vgbp_pe_mesh_2pe_gbp.h"

#include "../common/gbp_message_payload_codec.hpp"

static constexpr uint32_t kRowBytesLg = 5;
static constexpr uint32_t kMmioBankB0 = 0;
static constexpr uint32_t kPayloadBankB4 = 4;
static constexpr uint32_t kCreditMetaField = 0;
static constexpr uint32_t kTailField = 3;
static constexpr uint32_t kDoorbellField = 5;

struct SemanticPayloadSource {
  uint32_t bank = 4;
  uint32_t slot = 0;
  uint32_t row = 0;
  std::array<uint32_t, 5> payload_words{{0u, 0u, 0u, 0u, 0u}};
  std::vector<float> eta;
  std::vector<float> lam;
  bool from_dut_header = false;
};

struct PartitionInfo {
  std::array<std::vector<int>, 2> fac_mapping;
  std::array<std::vector<int>, 2> var_mapping;
  std::vector<std::pair<int, int>> edges;
};

struct CrossEdge {
  int factor_id = -1;
  int variable_id = -1;
  int factor_owner_pe = -1;
  int variable_owner_pe = -1;
};

struct CrossEdgeSemanticPayload {
  CrossEdge edge;
  SemanticPayloadSource semantic;
};

static void tick(Vgbp_pe_mesh_2pe_gbp* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static void reset_dut(Vgbp_pe_mesh_2pe_gbp* dut) {
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

static bool issue_req(Vgbp_pe_mesh_2pe_gbp* dut,
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

static uint32_t ingress_count_for_pe(Vgbp_pe_mesh_2pe_gbp* dut, int pe) {
  return static_cast<uint32_t>(pe == 0 ? dut->pe0_ingress_wr_count_o : dut->pe1_ingress_wr_count_o);
}

static uint32_t cmd_accept_count_for_pe(Vgbp_pe_mesh_2pe_gbp* dut, int pe) {
  return static_cast<uint32_t>(pe == 0 ? dut->pe0_cmd_accept_count_o : dut->pe1_cmd_accept_count_o);
}

static uint32_t dut_tx_count_for_pe(Vgbp_pe_mesh_2pe_gbp* dut, int pe) {
  return static_cast<uint32_t>(pe == 0 ? dut->pe0_dut_tx_count_o : dut->pe1_dut_tx_count_o);
}

static uint32_t message_row0_word0_for_bank(Vgbp_pe_mesh_2pe_gbp* dut, int pe, uint32_t bank) {
  if (pe == 0) {
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
}

static std::array<uint32_t, 4> adapter_payload_planes_row0_for_pe(Vgbp_pe_mesh_2pe_gbp* dut, int pe) {
  if (pe == 0) {
    return {static_cast<uint32_t>(dut->pe0_adapter_payload_plane0_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane1_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane2_row0_o),
            static_cast<uint32_t>(dut->pe0_adapter_payload_plane3_row0_o)};
  }
  return {static_cast<uint32_t>(dut->pe1_adapter_payload_plane0_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane1_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane2_row0_o),
          static_cast<uint32_t>(dut->pe1_adapter_payload_plane3_row0_o)};
}

static bool derive_semantic_payload_from_dut(Vgbp_pe_mesh_2pe_gbp* dut,
                                             int src_pe,
                                             int max_cycles,
                                             SemanticPayloadSource* out,
                                             std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }

  const bool is_pe0 = (src_pe == 0);
  for (int cycle = 0; cycle < max_cycles; ++cycle) {
    const bool seen_header = is_pe0 ? (dut->pe0_semantic_payload_seen_o != 0)
                                    : (dut->pe1_semantic_payload_seen_o != 0);
    if (seen_header) {
      break;
    }
    tick(dut);
  }

  const bool from_dut_header = is_pe0 ? (dut->pe0_semantic_payload_seen_o != 0)
                                      : (dut->pe1_semantic_payload_seen_o != 0);
  uint32_t bank = from_dut_header ? static_cast<uint32_t>(is_pe0 ? dut->pe0_semantic_payload_bank_o
                                                                  : dut->pe1_semantic_payload_bank_o)
                                  : 4u;
  if (bank < 4u || bank > 7u) {
    bank = 4u;
  }
  const uint32_t slot = bank - 4u;
  const uint32_t row = from_dut_header ? static_cast<uint32_t>(is_pe0 ? dut->pe0_semantic_payload_row_o
                                                                       : dut->pe1_semantic_payload_row_o)
                                       : 0u;

  const std::array<uint32_t, 4> planes = adapter_payload_planes_row0_for_pe(dut, src_pe);
  std::array<uint32_t, 5> words = {{0u, 0u, 0u, 0u, 0u}};
  words[0] = from_dut_header ? static_cast<uint32_t>(is_pe0 ? dut->pe0_semantic_payload_first_word_o
                                                             : dut->pe1_semantic_payload_first_word_o)
                             : message_row0_word0_for_bank(dut, src_pe, bank);
  words[1] = planes[1];
  words[2] = planes[2];
  words[3] = planes[3];
  words[4] = message_row0_word0_for_bank(dut, src_pe, bank);

  gbp_message_payload_codec::EncodedPayload encoded;
  encoded.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
  encoded.bank = bank;
  encoded.slot = slot;
  encoded.direction = gbp_message_payload_codec::direction_for_slot(slot);
  encoded.dim = 2;
  encoded.eta_len = 2;
  encoded.lam_len = 3;
  gbp_message_payload_codec::Segment segment;
  segment.segment_idx = 0;
  segment.segment_count = 1;
  segment.segment_payload_words = 5;
  segment.words.assign(words.begin(), words.end());
  encoded.segments.push_back(segment);

  gbp_message_payload_codec::DecodedPayload decoded;
  size_t words_consumed = 0;
  std::string codec_error;
  if (!gbp_message_payload_codec::decode(encoded, &decoded, &codec_error, &words_consumed)) {
    *error = codec_error;
    return false;
  }
  if (words_consumed != 5u) {
    *error = "payload_words_consumed_mismatch";
    return false;
  }

  out->bank = bank;
  out->slot = slot;
  out->row = row;
  out->payload_words = words;
  out->eta = decoded.eta;
  out->lam = decoded.lam;
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

static bool run_remote_message(Vgbp_pe_mesh_2pe_gbp* dut,
                               int src_pe,
                               int dst_pe,
                               uint32_t payload_bank,
                               uint8_t qid,
                               uint8_t txn_id,
                               uint32_t payload,
                               bool require_cmd_accept,
                               uint32_t* sent_reqs,
                               uint32_t* remote_ingress_delta,
                               uint32_t* remote_cmd_delta,
                               uint32_t* dst_dut_tx_delta) {
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

  for (int cycle = 0; cycle < 512; ++cycle) {
    tick(dut);
    const uint32_t ingress_now = ingress_count_for_pe(dut, dst_pe);
    const uint32_t cmd_now = cmd_accept_count_for_pe(dut, dst_pe);
    const uint32_t dst_dut_tx_now = dut_tx_count_for_pe(dut, dst_pe);
    const bool ingress_ok = ingress_now > ingress_before;
    const bool cmd_ok = (!require_cmd_accept) || (cmd_now > cmd_before);
    const bool dut_tx_ok = dst_dut_tx_now > dst_dut_tx_before;
    if (ingress_ok && cmd_ok && dut_tx_ok) {
      *remote_ingress_delta = ingress_now - ingress_before;
      *remote_cmd_delta = cmd_now - cmd_before;
      *dst_dut_tx_delta = dst_dut_tx_now - dst_dut_tx_before;
      return true;
    }
  }

  std::fprintf(stderr,
               "FAIL: remote accept timeout src_pe=%d dst_pe=%d ingress_before=%u ingress_after=%u cmd_before=%u cmd_after=%u dst_dut_tx_before=%u dst_dut_tx_after=%u\n",
               src_pe,
               dst_pe,
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
  auto* dut = new Vgbp_pe_mesh_2pe_gbp;

  const char* workload = std::getenv("WORKLOAD");
  if (workload == nullptr || workload[0] == '\0') {
    workload = "synthetic_line";
  }

  const char* partition_path = std::getenv("PARTITION");
  if (partition_path == nullptr || partition_path[0] == '\0') {
    std::fprintf(stderr, "FAIL: PARTITION env var is required\n");
    delete dut;
    return 1;
  }

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

  const std::vector<CrossEdge> cross_edges = collect_cross_edges(partition, factor_owner, var_owner);
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

  reset_dut(dut);

  uint32_t pe_sent[2] = {0, 0};
  uint32_t pe_received[2] = {0, 0};
  uint32_t pe_consumed[2] = {0, 0};
  uint32_t factor_to_var_remote = 0;
  uint32_t var_to_factor_remote = 0;
  uint32_t factor_to_var_dut_tx = 0;
  uint32_t var_to_factor_dut_tx = 0;
  uint32_t factor_to_var_cmd_accepted = 0;
  uint32_t var_to_factor_cmd_accepted = 0;

  std::vector<CrossEdgeSemanticPayload> factor_to_var_payload_sources;
  factor_to_var_payload_sources.reserve(cross_edges.size());
  uint32_t txn_cursor = 0x51u;
  for (size_t i = 0; i < cross_edges.size(); ++i) {
    const CrossEdge& edge = cross_edges[i];
    const uint32_t payload_word = make_bootstrap_payload_word(4u,
                                                              0u,
                                                              edge.factor_id,
                                                              edge.variable_id,
                                                              edge.factor_owner_pe,
                                                              edge.variable_owner_pe);
    uint32_t ingress_delta = 0;
    uint32_t cmd_delta = 0;
    uint32_t dst_dut_tx_delta = 0;
    if (!run_remote_message(dut,
                            edge.factor_owner_pe,
                            edge.variable_owner_pe,
                            4u,
                            0,
                            static_cast<uint8_t>(txn_cursor & 0xFFu),
                            payload_word,
                            true,
                            &pe_sent[edge.factor_owner_pe],
                            &ingress_delta,
                            &cmd_delta,
                            &dst_dut_tx_delta)) {
      delete dut;
      return 1;
    }
    txn_cursor += 1u;
    factor_to_var_remote += ingress_delta;
    factor_to_var_dut_tx += dst_dut_tx_delta;
    factor_to_var_cmd_accepted += cmd_delta;
    pe_received[edge.variable_owner_pe] += ingress_delta;
    pe_consumed[edge.variable_owner_pe] += cmd_delta;

    CrossEdgeSemanticPayload payload{};
    payload.edge = edge;
    std::string payload_error;
    if (!derive_semantic_payload_from_dut(dut,
                                          edge.variable_owner_pe,
                                          256,
                                          &payload.semantic,
                                          &payload_error)) {
      std::fprintf(stderr,
                   "FAIL: canonical semantic payload export failed phase=factor_to_var factor_id=%d variable_id=%d pe=%d error=%s\n",
                   edge.factor_id,
                   edge.variable_id,
                   edge.variable_owner_pe,
                   payload_error.c_str());
      delete dut;
      return 1;
    }
    factor_to_var_payload_sources.push_back(payload);
  }

  reset_dut(dut);

  std::vector<CrossEdgeSemanticPayload> var_to_factor_payload_sources;
  var_to_factor_payload_sources.reserve(cross_edges.size());
  txn_cursor = 0x61u;
  for (size_t i = 0; i < cross_edges.size(); ++i) {
    const CrossEdge& edge = cross_edges[i];
    const uint32_t payload_word = make_bootstrap_payload_word(5u,
                                                              1u,
                                                              edge.factor_id,
                                                              edge.variable_id,
                                                              edge.variable_owner_pe,
                                                              edge.factor_owner_pe);
    uint32_t ingress_delta = 0;
    uint32_t cmd_delta = 0;
    uint32_t dst_dut_tx_delta = 0;
    if (!run_remote_message(dut,
                            edge.variable_owner_pe,
                            edge.factor_owner_pe,
                            5u,
                            0,
                            static_cast<uint8_t>(txn_cursor & 0xFFu),
                            payload_word,
                            true,
                            &pe_sent[edge.variable_owner_pe],
                            &ingress_delta,
                            &cmd_delta,
                            &dst_dut_tx_delta)) {
      delete dut;
      return 1;
    }
    txn_cursor += 1u;
    var_to_factor_remote += ingress_delta;
    var_to_factor_dut_tx += dst_dut_tx_delta;
    var_to_factor_cmd_accepted += cmd_delta;
    pe_received[edge.factor_owner_pe] += ingress_delta;
    pe_consumed[edge.factor_owner_pe] += cmd_delta;

    CrossEdgeSemanticPayload payload{};
    payload.edge = edge;
    std::string payload_error;
    if (!derive_semantic_payload_from_dut(dut,
                                          edge.factor_owner_pe,
                                          256,
                                          &payload.semantic,
                                          &payload_error)) {
      std::fprintf(stderr,
                   "FAIL: canonical semantic payload export failed phase=var_to_factor factor_id=%d variable_id=%d pe=%d error=%s\n",
                   edge.factor_id,
                   edge.variable_id,
                   edge.factor_owner_pe,
                   payload_error.c_str());
      delete dut;
      return 1;
    }
    var_to_factor_payload_sources.push_back(payload);
  }

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
      std::string("build/integration/gbp_pe_mesh_2pe_gbp/") + workload + "_distributed_trace.json";
  std::ofstream trace_ofs(trace_path);
  if (!trace_ofs.is_open()) {
    std::fprintf(stderr, "FAIL: unable to write trace file path=%s\n", trace_path.c_str());
    delete dut;
    return 1;
  }

  auto emit_payload_source = [&trace_ofs](const CrossEdgeSemanticPayload& payload) {
    const SemanticPayloadSource& src = payload.semantic;
    trace_ofs << "    {\"factor_id\": " << payload.edge.factor_id
              << ", \"variable_id\": " << payload.edge.variable_id
              << ", \"factor_owner_pe\": " << payload.edge.factor_owner_pe
              << ", \"variable_owner_pe\": " << payload.edge.variable_owner_pe
              << ", \"bank\": " << src.bank
              << ", \"slot\": " << src.slot
              << ", \"row\": " << src.row
              << ", \"source\": \""
              << (src.from_dut_header ? "dut_ingress_first_flit" : "dut_payload_snapshot")
              << "\", \"schema_version\": " << gbp_message_payload_codec::kPhase1SchemaVersion
              << ", \"direction\": \""
              << (gbp_message_payload_codec::direction_for_slot(src.slot)
                          == gbp_message_payload_codec::Direction::kFactorToVar
                      ? "factor_to_var"
                      : "var_to_factor")
              << "\", \"dim\": 2, \"eta_len\": 2, \"lam_len\": 3, \"segment_idx\": 0"
              << ", \"segment_count\": 1, \"segment_payload_words\": 5"
              << ", \"payload_words\": [" << src.payload_words[0] << ", " << src.payload_words[1]
              << ", " << src.payload_words[2] << ", " << src.payload_words[3] << ", "
              << src.payload_words[4] << "]"
              << ", \"decoded\": {\"eta\": [" << src.eta[0] << ", " << src.eta[1]
              << "], \"lam\": [" << src.lam[0] << ", " << src.lam[1] << ", " << src.lam[2]
              << "]}}";
  };

  trace_ofs << "{\n"
            << "  \"test\": \"gbp_pe_mesh_2pe_gbp\",\n"
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
            << "  \"factor_to_var_payload_semantic\": [\n";
  for (size_t i = 0; i < factor_to_var_payload_sources.size(); ++i) {
    emit_payload_source(factor_to_var_payload_sources[i]);
    trace_ofs << (i + 1 == factor_to_var_payload_sources.size() ? "\n" : ",\n");
  }
  trace_ofs << "  ],\n"
            << "  \"var_to_factor_payload_semantic\": [\n";
  for (size_t i = 0; i < var_to_factor_payload_sources.size(); ++i) {
    emit_payload_source(var_to_factor_payload_sources[i]);
    trace_ofs << (i + 1 == var_to_factor_payload_sources.size() ? "\n" : ",\n");
  }
  trace_ofs << "  ],\n"
            << "  \"factor_to_var_remote\": " << factor_to_var_remote << ",\n"
            << "  \"var_to_factor_remote\": " << var_to_factor_remote << ",\n"
            << "  \"factor_to_var_dut_tx\": " << factor_to_var_dut_tx << ",\n"
            << "  \"var_to_factor_dut_tx\": " << var_to_factor_dut_tx << ",\n"
            << "  \"factor_to_var_cmd_accepted\": " << factor_to_var_cmd_accepted << ",\n"
            << "  \"var_to_factor_cmd_accepted\": " << var_to_factor_cmd_accepted << ",\n"
            << "  \"pe_counts\": [\n"
            << "    {\"pe\": 0, \"sent\": " << pe_sent[0] << ", \"received\": " << pe_received[0]
            << ", \"consumed\": " << pe_consumed[0] << "},\n"
            << "    {\"pe\": 1, \"sent\": " << pe_sent[1] << ", \"received\": " << pe_received[1]
            << ", \"consumed\": " << pe_consumed[1] << "}\n"
            << "  ]\n"
            << "}\n";

  std::printf(
      "gbp_pe_mesh_2pe_gbp: PASS workload=%s factor_to_var_remote>=1 var_to_factor_remote>=1 factor_to_var_dut_tx>=1 var_to_factor_dut_tx>=1 factor_to_var_remote=%u var_to_factor_remote=%u factor_to_var_dut_tx=%u var_to_factor_dut_tx=%u trace_json=%s\n",
      workload,
      factor_to_var_remote,
      var_to_factor_remote,
      factor_to_var_dut_tx,
      var_to_factor_dut_tx,
      trace_path.c_str());

  delete dut;
  return 0;
}
