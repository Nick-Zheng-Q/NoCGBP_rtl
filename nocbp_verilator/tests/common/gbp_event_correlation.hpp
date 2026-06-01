#ifndef GBP_EVENT_CORRELATION_HPP_
#define GBP_EVENT_CORRELATION_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "gbp_message_payload_codec.hpp"
#include "gbp_terminal_metrics_adapter.hpp"

namespace gbp_event_correlation {

struct SemanticPayloadEvidence {
  uint32_t bank = 4;
  uint32_t slot = 0;
  uint32_t row = 0;
  std::array<uint32_t, 5> payload_words{{0u, 0u, 0u, 0u, 0u}};
  std::vector<float> eta;
  std::vector<float> lam;
  bool from_dut_header = false;
};

struct ScopedTxnEvidence {
  std::string phase;
  uint32_t txn_id = 0;
  int src_pe = -1;
  int dst_pe = -1;
  int factor_id = -1;
  int variable_id = -1;
  uint32_t compute_done_delta = 0;
  uint32_t dut_cmd_accept_delta = 0;
  uint32_t peer_ingress_delta = 0;
  uint32_t dut_tx_delta = 0;
  bool has_semantic = false;
  SemanticPayloadEvidence semantic;
};

struct TerminalMetricsEvidence {
  std::string dump_path;
  gbp_terminal_metrics_adapter::Metrics reconstructed;
};

inline bool decode_semantic_payload(uint32_t bank,
                                    uint32_t row,
                                    const std::array<uint32_t, 5>& payload_words,
                                    SemanticPayloadEvidence* out,
                                    std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  if (bank < gbp_message_payload_codec::kMessageBankLo
      || bank > gbp_message_payload_codec::kMessageBankHi) {
    *error = "bad_bank_slot";
    return false;
  }

  gbp_message_payload_codec::EncodedPayload encoded;
  encoded.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
  encoded.bank = bank;
  encoded.slot = bank - gbp_message_payload_codec::kMessageBankLo;
  encoded.direction = gbp_message_payload_codec::direction_for_slot(encoded.slot);
  encoded.dim = 2;
  encoded.eta_len = 2;
  encoded.lam_len = 3;
  gbp_message_payload_codec::Segment segment;
  segment.segment_idx = 0;
  segment.segment_count = 1;
  segment.segment_payload_words = 5;
  segment.words.assign(payload_words.begin(), payload_words.end());
  encoded.segments.push_back(segment);

  gbp_message_payload_codec::DecodedPayload decoded;
  size_t words_consumed = 0;
  if (!gbp_message_payload_codec::decode(encoded, &decoded, error, &words_consumed)) {
    return false;
  }
  if (decoded.eta.size() != 2u || decoded.lam.size() != 3u || words_consumed != 5u) {
    *error = "semantic_payload_shape_mismatch";
    return false;
  }

  out->bank = bank;
  out->slot = encoded.slot;
  out->row = row;
  out->payload_words = payload_words;
  out->eta = decoded.eta;
  out->lam = decoded.lam;
  return true;
}

inline bool reconstruct_terminal_metrics(const std::string& dump_path,
                                         TerminalMetricsEvidence* out,
                                         std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  gbp_terminal_metrics_adapter::Metrics reconstructed{};
  if (!gbp_terminal_metrics_adapter::reconstruct_metrics_from_dump(dump_path,
                                                                    &reconstructed,
                                                                    error)) {
    return false;
  }
  out->dump_path = dump_path;
  out->reconstructed = reconstructed;
  return true;
}

inline void sort_by_txn_id(std::vector<ScopedTxnEvidence>* evidence) {
  std::sort(evidence->begin(),
            evidence->end(),
            [](const ScopedTxnEvidence& lhs, const ScopedTxnEvidence& rhs) {
              if (lhs.txn_id != rhs.txn_id) {
                return lhs.txn_id < rhs.txn_id;
              }
              if (lhs.src_pe != rhs.src_pe) {
                return lhs.src_pe < rhs.src_pe;
              }
              return lhs.dst_pe < rhs.dst_pe;
            });
}

inline void emit_scoped_txn_array(std::ostream& os, const std::vector<ScopedTxnEvidence>& evidence) {
  os << "[\n";
  for (size_t i = 0; i < evidence.size(); ++i) {
    const ScopedTxnEvidence& row = evidence[i];
    os << "    {\"phase\": \"" << row.phase
       << "\", \"txn_id\": " << row.txn_id
       << ", \"src_pe\": " << row.src_pe
       << ", \"dst_pe\": " << row.dst_pe
       << ", \"factor_id\": " << row.factor_id
       << ", \"variable_id\": " << row.variable_id
       << ", \"compute_done_delta\": " << row.compute_done_delta
       << ", \"dut_cmd_accept_delta\": " << row.dut_cmd_accept_delta
       << ", \"peer_ingress_delta\": " << row.peer_ingress_delta
       << ", \"dut_tx_delta\": " << row.dut_tx_delta;
    if (row.has_semantic) {
      os << ", \"semantic\": {\"bank\": " << row.semantic.bank
         << ", \"slot\": " << row.semantic.slot
         << ", \"row\": " << row.semantic.row
         << ", \"source\": \""
         << (row.semantic.from_dut_header ? "dut_ingress_first_flit" : "dut_payload_snapshot")
         << "\", \"schema_version\": " << gbp_message_payload_codec::kPhase1SchemaVersion
         << ", \"direction\": \""
         << (gbp_message_payload_codec::direction_for_slot(row.semantic.slot)
                     == gbp_message_payload_codec::Direction::kFactorToVar
                 ? "factor_to_var"
                 : "var_to_factor")
         << "\", \"dim\": 2, \"eta_len\": 2, \"lam_len\": 3"
         << ", \"segment_idx\": 0, \"segment_count\": 1, \"segment_payload_words\": 5"
         << ", \"payload_words\": ["
         << row.semantic.payload_words[0] << ", "
         << row.semantic.payload_words[1] << ", "
         << row.semantic.payload_words[2] << ", "
         << row.semantic.payload_words[3] << ", "
         << row.semantic.payload_words[4]
         << "], \"decoded\": {\"eta\": ["
         << row.semantic.eta[0] << ", "
         << row.semantic.eta[1]
         << "], \"lam\": ["
         << row.semantic.lam[0] << ", "
         << row.semantic.lam[1] << ", "
         << row.semantic.lam[2] << "]}}";
    }
    os << "}";
    os << (i + 1 == evidence.size() ? "\n" : ",\n");
  }
  os << "  ]";
}

inline std::string distributed_trace_path(const std::string& test_name,
                                          const std::string& workload,
                                          uint32_t pe_count,
                                          uint32_t mesh_x,
                                          uint32_t mesh_y) {
  std::ostringstream path;
  path << "build/integration/" << test_name
       << "/" << workload
       << "_distributed_trace_"
       << pe_count << "pe_" << mesh_x << "x" << mesh_y << ".json";
  return path.str();
}

}

#endif
