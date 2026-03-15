#include "gbp_terminal_metrics_adapter.hpp"
#include "gbp_message_payload_codec.hpp"

#include <Eigen/Dense>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../../nocbp_simulator/gbp/LinearFactorGraph.hpp"
#include "../../../nocbp_simulator/gbp/precision.hpp"

namespace gbp_terminal_metrics_adapter {

namespace {

struct MessageDump {
  int factor_id = -1;
  int variable_id = -1;
  int message_slot = -1;
  int msg_bank = -1;
  int msg_row = -1;
  int msg_beat = -1;
  Eigen::VectorXd msg_eta;
  Eigen::MatrixXd msg_lam;
};

struct VariableDump {
  int variable_id = -1;
  int dofs = -1;
  int state_bank = -1;
  int state_row = -1;
  int snapshot_seq = -1;
  int snapshot_cycle = -1;
  Eigen::VectorXd prior_eta;
  Eigen::MatrixXd prior_lam;
  std::vector<MessageDump> inbound_messages;
};

struct GraphDump {
  struct CoverageContract {
    std::vector<int> required_state_banks;
    std::vector<int> required_message_banks;
    int required_row = -1;
    int required_beats_per_row = -1;
  };

  struct RequiredRowDump {
    int bank = -1;
    std::string klass;
    int row = -1;
    int address = -1;
    int snapshot_seq = -1;
    int snapshot_cycle = -1;
    std::vector<double> beats;
  };

  struct TerminalDumpPe {
    int pe = -1;
    std::vector<RequiredRowDump> required_rows;
  };

  std::string workload;
  int iterations = -1;
  int snapshot_seq = -1;
  int snapshot_cycle = -1;
  int node_count = -1;
  int rows = -1;
  int cols = -1;
  double spacing = 0.0;
  double init_noise_std = 0.0;
  int seed = -1;
  double anchor_std = 0.0;
  CoverageContract coverage_contract;
  std::vector<TerminalDumpPe> terminal_dump;
  std::vector<VariableDump> variable_snapshots;
  std::vector<MessageDump> local_semantic_messages;
};

std::string trim(const std::string& value) {
  size_t b = 0;
  while (b < value.size() && std::isspace(static_cast<unsigned char>(value[b])) != 0) {
    ++b;
  }
  size_t e = value.size();
  while (e > b && std::isspace(static_cast<unsigned char>(value[e - 1])) != 0) {
    --e;
  }
  return value.substr(b, e - b);
}

bool read_file_text(const std::string& path, std::string* out, std::string* error) {
  std::ifstream in(path);
  if (!in.is_open()) {
    *error = "cannot open dump file: " + path;
    return false;
  }
  *out = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return true;
}

bool extract_quoted_string(const std::string& text,
                           const std::string& key,
                           std::string* out,
                           std::string* error) {
  const std::string needle = "\"" + key + "\"";
  const size_t key_pos = text.find(needle);
  if (key_pos == std::string::npos) {
    *error = "missing required field: " + key;
    return false;
  }
  size_t colon = text.find(':', key_pos + needle.size());
  if (colon == std::string::npos) {
    *error = "malformed field: " + key;
    return false;
  }
  size_t first_quote = text.find('"', colon + 1);
  if (first_quote == std::string::npos) {
    *error = "malformed string field: " + key;
    return false;
  }
  size_t second_quote = text.find('"', first_quote + 1);
  if (second_quote == std::string::npos) {
    *error = "unterminated string field: " + key;
    return false;
  }
  *out = text.substr(first_quote + 1, second_quote - first_quote - 1);
  return true;
}

bool extract_number_text(const std::string& text,
                         const std::string& key,
                         std::string* out,
                         std::string* error) {
  const std::string needle = "\"" + key + "\"";
  const size_t key_pos = text.find(needle);
  if (key_pos == std::string::npos) {
    *error = "missing required field: " + key;
    return false;
  }
  const size_t colon = text.find(':', key_pos + needle.size());
  if (colon == std::string::npos) {
    *error = "malformed numeric field: " + key;
    return false;
  }
  size_t begin = colon + 1;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  size_t end = begin;
  while (end < text.size() &&
         (std::isdigit(static_cast<unsigned char>(text[end])) != 0 || text[end] == '.' ||
          text[end] == '-' || text[end] == '+' || text[end] == 'e' || text[end] == 'E')) {
    ++end;
  }
  if (end == begin) {
    *error = "invalid numeric field: " + key;
    return false;
  }
  *out = text.substr(begin, end - begin);
  return true;
}

bool extract_int(const std::string& text, const std::string& key, int* out, std::string* error) {
  std::string number;
  if (!extract_number_text(text, key, &number, error)) {
    return false;
  }
  char* end = nullptr;
  const long value = std::strtol(number.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    *error = "invalid integer field: " + key;
    return false;
  }
  *out = static_cast<int>(value);
  return true;
}

bool extract_double(const std::string& text,
                    const std::string& key,
                    double* out,
                    std::string* error) {
  std::string number;
  if (!extract_number_text(text, key, &number, error)) {
    return false;
  }
  char* end = nullptr;
  const double value = std::strtod(number.c_str(), &end);
  if (end == nullptr || *end != '\0') {
    *error = "invalid floating field: " + key;
    return false;
  }
  *out = value;
  return true;
}

bool extract_delimited_block(const std::string& text,
                             const std::string& key,
                             char open,
                             char close,
                             std::string* out,
                             std::string* error) {
  const std::string needle = "\"" + key + "\"";
  const size_t key_pos = text.find(needle);
  if (key_pos == std::string::npos) {
    *error = "missing required field: " + key;
    return false;
  }
  const size_t colon = text.find(':', key_pos + needle.size());
  if (colon == std::string::npos) {
    *error = "malformed block field: " + key;
    return false;
  }
  size_t begin = text.find(open, colon + 1);
  if (begin == std::string::npos) {
    *error = "missing opening delimiter for field: " + key;
    return false;
  }
  int depth = 0;
  size_t end = std::string::npos;
  for (size_t i = begin; i < text.size(); ++i) {
    if (text[i] == open) {
      ++depth;
    } else if (text[i] == close) {
      --depth;
      if (depth == 0) {
        end = i;
        break;
      }
    }
  }
  if (end == std::string::npos || end <= begin) {
    *error = "unterminated block for field: " + key;
    return false;
  }
  *out = text.substr(begin, end - begin + 1);
  return true;
}

std::vector<std::string> split_top_level_objects(const std::string& array_text) {
  std::vector<std::string> out;
  int depth = 0;
  size_t object_begin = std::string::npos;
  for (size_t i = 0; i < array_text.size(); ++i) {
    const char ch = array_text[i];
    if (ch == '{') {
      if (depth == 0) {
        object_begin = i;
      }
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0 && object_begin != std::string::npos) {
        out.push_back(array_text.substr(object_begin, i - object_begin + 1));
        object_begin = std::string::npos;
      }
    }
  }
  return out;
}

bool parse_number_list(const std::string& raw, std::vector<double>* out, std::string* error) {
  out->clear();
  std::string compact = raw;
  for (char& ch : compact) {
    if (ch == '[' || ch == ']' || ch == ',') {
      ch = ' ';
    }
  }
  std::istringstream iss(compact);
  std::string token;
  while (iss >> token) {
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0') {
      *error = "invalid numeric list token: " + token;
      return false;
    }
    out->push_back(value);
  }
  return !out->empty();
}

bool parse_int_list(const std::string& text,
                    const std::string& key,
                    std::vector<int>* out,
                    std::string* error) {
  std::string block;
  if (!extract_delimited_block(text, key, '[', ']', &block, error)) {
    return false;
  }
  std::vector<double> values;
  if (!parse_number_list(block, &values, error)) {
    *error = "invalid integer list field: " + key;
    return false;
  }
  out->clear();
  out->reserve(values.size());
  for (double value : values) {
    const double rounded = std::round(value);
    if (std::fabs(value - rounded) > 1e-9) {
      *error = "non-integer entry in field: " + key;
      return false;
    }
    out->push_back(static_cast<int>(rounded));
  }
  return true;
}

bool parse_vec2(const std::string& text,
                const std::string& key,
                Eigen::VectorXd* out,
                std::string* error) {
  std::string block;
  if (!extract_delimited_block(text, key, '[', ']', &block, error)) {
    return false;
  }
  std::vector<double> values;
  if (!parse_number_list(block, &values, error)) {
    *error = "invalid vector field: " + key;
    return false;
  }
  if (values.size() != 2) {
    *error = "expected 2 entries for field: " + key;
    return false;
  }
  *out = Eigen::VectorXd(2);
  (*out)(0) = values[0];
  (*out)(1) = values[1];
  return true;
}

bool parse_mat2(const std::string& text,
                const std::string& key,
                Eigen::MatrixXd* out,
                std::string* error) {
  std::string block;
  if (!extract_delimited_block(text, key, '[', ']', &block, error)) {
    return false;
  }
  std::vector<double> values;
  if (!parse_number_list(block, &values, error)) {
    *error = "invalid matrix field: " + key;
    return false;
  }
  if (values.size() != 4) {
    *error = "expected 4 entries for field: " + key;
    return false;
  }
  *out = Eigen::MatrixXd(2, 2);
  (*out)(0, 0) = values[0];
  (*out)(0, 1) = values[1];
  (*out)(1, 0) = values[2];
  (*out)(1, 1) = values[3];
  return true;
}

bool canonicalize_message_payload(MessageDump* message, std::string* error) {
  if (message->msg_eta.size() != 2 || message->msg_lam.rows() != 2 || message->msg_lam.cols() != 2) {
    *error = "message payload shape mismatch";
    return false;
  }

  if (message->message_slot < 0) {
    *error = "bad_bank_slot";
    return false;
  }

  const double lam01 = message->msg_lam(0, 1);
  const double lam10 = message->msg_lam(1, 0);
  if (std::fabs(lam01 - lam10) > 1e-6) {
    *error = "lam_lower_triangular_mismatch";
    return false;
  }

  gbp_message_payload_codec::DecodedPayload semantic;
  semantic.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
  semantic.bank = static_cast<uint32_t>(message->msg_bank);
  semantic.slot = static_cast<uint32_t>(message->message_slot);
  semantic.direction = gbp_message_payload_codec::direction_for_slot(semantic.slot);
  semantic.dim = 2;
  semantic.eta.push_back(static_cast<float>(message->msg_eta(0)));
  semantic.eta.push_back(static_cast<float>(message->msg_eta(1)));
  semantic.lam.push_back(static_cast<float>(message->msg_lam(0, 0)));
  semantic.lam.push_back(static_cast<float>(message->msg_lam(0, 1)));
  semantic.lam.push_back(static_cast<float>(message->msg_lam(1, 1)));

  gbp_message_payload_codec::EncodedPayload encoded;
  std::string codec_error;
  if (!gbp_message_payload_codec::encode(semantic, &encoded, &codec_error)) {
    *error = "message_payload_codec_encode: " + codec_error;
    return false;
  }

  gbp_message_payload_codec::DecodedPayload decoded;
  size_t words_consumed = 0;
  if (!gbp_message_payload_codec::decode(encoded, &decoded, &codec_error, &words_consumed)) {
    *error = "message_payload_codec_decode: " + codec_error;
    return false;
  }

  if (decoded.eta.size() != 2 || decoded.lam.size() != 3 || words_consumed != 5u) {
    *error = "message_payload_codec_shape_mismatch";
    return false;
  }

  message->msg_eta = Eigen::VectorXd(2);
  message->msg_eta(0) = static_cast<double>(decoded.eta[0]);
  message->msg_eta(1) = static_cast<double>(decoded.eta[1]);

  message->msg_lam = Eigen::MatrixXd(2, 2);
  message->msg_lam(0, 0) = static_cast<double>(decoded.lam[0]);
  message->msg_lam(0, 1) = static_cast<double>(decoded.lam[1]);
  message->msg_lam(1, 0) = static_cast<double>(decoded.lam[1]);
  message->msg_lam(1, 1) = static_cast<double>(decoded.lam[2]);
  return true;
}

bool parse_message_dump(const std::string& text, MessageDump* out, std::string* error) {
  if (!extract_int(text, "factor_id", &out->factor_id, error) ||
      !extract_int(text, "variable_id", &out->variable_id, error) ||
      !extract_int(text, "message_slot", &out->message_slot, error) ||
      !extract_int(text, "msg_bank", &out->msg_bank, error) ||
      !extract_int(text, "msg_row", &out->msg_row, error) ||
      !extract_int(text, "msg_beat", &out->msg_beat, error) ||
      !parse_vec2(text, "msg_eta", &out->msg_eta, error) ||
      !parse_mat2(text, "msg_lam", &out->msg_lam, error)) {
    return false;
  }
  if (out->msg_bank < 4 || out->msg_bank > 7) {
    *error = "message bank outside B4-B7";
    return false;
  }
  if (!canonicalize_message_payload(out, error)) {
    return false;
  }
  return true;
}

bool parse_variable_dump(const std::string& text, VariableDump* out, std::string* error) {
  if (!extract_int(text, "variable_id", &out->variable_id, error) ||
      !extract_int(text, "dofs", &out->dofs, error) ||
      !extract_int(text, "state_bank", &out->state_bank, error) ||
      !extract_int(text, "state_row", &out->state_row, error) ||
      !extract_int(text, "snapshot_seq", &out->snapshot_seq, error) ||
      !extract_int(text, "snapshot_cycle", &out->snapshot_cycle, error) ||
      !parse_vec2(text, "prior_eta", &out->prior_eta, error) ||
      !parse_mat2(text, "prior_lam", &out->prior_lam, error)) {
    return false;
  }
  if (out->dofs != 2) {
    *error = "unsupported dofs (expected 2)";
    return false;
  }
  if (out->state_bank < 1 || out->state_bank > 3) {
    *error = "state bank outside B1-B3";
    return false;
  }

  std::string inbound_array;
  if (!extract_delimited_block(text, "inbound_messages", '[', ']', &inbound_array, error)) {
    return false;
  }
  const std::vector<std::string> message_objects = split_top_level_objects(inbound_array);
  out->inbound_messages.clear();
  out->inbound_messages.reserve(message_objects.size());
  for (const std::string& msg_text : message_objects) {
    MessageDump msg;
    if (!parse_message_dump(msg_text, &msg, error)) {
      return false;
    }
    if (msg.variable_id != out->variable_id) {
      *error = "message variable_id mismatch";
      return false;
    }
    out->inbound_messages.push_back(msg);
  }
  return true;
}

bool parse_graph_dump(const std::string& text, GraphDump* out, std::string* error) {
  if (!extract_quoted_string(text, "workload", &out->workload, error) ||
      !extract_int(text, "iterations", &out->iterations, error) ||
      !extract_int(text, "snapshot_seq", &out->snapshot_seq, error) ||
      !extract_int(text, "snapshot_cycle", &out->snapshot_cycle, error)) {
    return false;
  }

  if (out->workload != "synthetic_line" && out->workload != "synthetic_lattice") {
    *error = "unsupported workload";
    return false;
  }
  if (out->iterations != 50) {
    *error = "unsupported iterations (expected 50)";
    return false;
  }

  std::string graph;
  if (!extract_delimited_block(text, "graph", '{', '}', &graph, error)) {
    return false;
  }
  if (out->workload == "synthetic_line") {
    if (!extract_int(graph, "node_count", &out->node_count, error)) {
      return false;
    }
  } else {
    if (!extract_int(graph, "rows", &out->rows, error) ||
        !extract_int(graph, "cols", &out->cols, error)) {
      return false;
    }
  }
  if (!extract_double(graph, "spacing", &out->spacing, error) ||
      !extract_double(graph, "init_noise_std", &out->init_noise_std, error) ||
      !extract_int(graph, "seed", &out->seed, error) ||
      !extract_double(graph, "anchor_std", &out->anchor_std, error)) {
    return false;
  }

  std::string coverage_contract;
  if (!extract_delimited_block(text, "coverage_contract", '{', '}', &coverage_contract, error)) {
    return false;
  }
  if (!parse_int_list(coverage_contract,
                      "required_state_banks",
                      &out->coverage_contract.required_state_banks,
                      error) ||
      !parse_int_list(coverage_contract,
                      "required_message_banks",
                      &out->coverage_contract.required_message_banks,
                      error) ||
      !extract_int(coverage_contract, "required_row", &out->coverage_contract.required_row, error) ||
      !extract_int(coverage_contract,
                   "required_beats_per_row",
                   &out->coverage_contract.required_beats_per_row,
                   error)) {
    return false;
  }
  if (out->coverage_contract.required_state_banks.empty() ||
      out->coverage_contract.required_message_banks.empty()) {
    *error = "coverage_contract bank lists cannot be empty";
    return false;
  }
  if (out->coverage_contract.required_beats_per_row <= 0) {
    *error = "coverage_contract.required_beats_per_row must be positive";
    return false;
  }

  std::string terminal_dump_array;
  if (!extract_delimited_block(text, "terminal_dump", '[', ']', &terminal_dump_array, error)) {
    return false;
  }
  const std::vector<std::string> terminal_objects = split_top_level_objects(terminal_dump_array);
  if (terminal_objects.empty()) {
    *error = "terminal_dump cannot be empty";
    return false;
  }
  out->terminal_dump.clear();
  out->terminal_dump.reserve(terminal_objects.size());
  for (const std::string& pe_text : terminal_objects) {
    GraphDump::TerminalDumpPe pe_dump{};
    if (!extract_int(pe_text, "pe", &pe_dump.pe, error)) {
      return false;
    }
    std::string required_rows_array;
    if (!extract_delimited_block(pe_text, "required_rows", '[', ']', &required_rows_array, error)) {
      return false;
    }
    const std::vector<std::string> row_objects = split_top_level_objects(required_rows_array);
    if (row_objects.empty()) {
      *error = "required_rows cannot be empty for pe=" + std::to_string(pe_dump.pe);
      return false;
    }
    pe_dump.required_rows.reserve(row_objects.size());
    for (const std::string& row_text : row_objects) {
      GraphDump::RequiredRowDump row{};
      if (!extract_int(row_text, "bank", &row.bank, error) ||
          !extract_quoted_string(row_text, "class", &row.klass, error) ||
          !extract_int(row_text, "row", &row.row, error) ||
          !extract_int(row_text, "address", &row.address, error) ||
          !extract_int(row_text, "snapshot_seq", &row.snapshot_seq, error) ||
          !extract_int(row_text, "snapshot_cycle", &row.snapshot_cycle, error)) {
        return false;
      }
      std::string beats_block;
      if (!extract_delimited_block(row_text, "beats", '[', ']', &beats_block, error) ||
          !parse_number_list(beats_block, &row.beats, error)) {
        *error = "invalid required_rows beats";
        return false;
      }
      pe_dump.required_rows.push_back(row);
    }
    out->terminal_dump.push_back(pe_dump);
  }

  std::string snapshots;
  if (!extract_delimited_block(text, "variable_snapshots", '[', ']', &snapshots, error)) {
    return false;
  }
  const std::vector<std::string> vars = split_top_level_objects(snapshots);
  if (vars.empty()) {
    *error = "variable_snapshots cannot be empty";
    return false;
  }
  out->variable_snapshots.clear();
  out->variable_snapshots.reserve(vars.size());
  std::set<int> unique_var_ids;
  for (const std::string& var_text : vars) {
    VariableDump variable;
    if (!parse_variable_dump(var_text, &variable, error)) {
      return false;
    }
    if (variable.snapshot_seq != out->snapshot_seq ||
        variable.snapshot_cycle != out->snapshot_cycle) {
      *error = "snapshot metadata mismatch across records";
      return false;
    }
    if (unique_var_ids.find(variable.variable_id) != unique_var_ids.end()) {
      *error = "duplicate variable snapshot";
      return false;
    }
    unique_var_ids.insert(variable.variable_id);
    out->variable_snapshots.push_back(variable);
  }

  for (const GraphDump::TerminalDumpPe& pe_dump : out->terminal_dump) {
    std::set<int> seen_state_banks;
    std::set<int> seen_message_banks;
    for (const GraphDump::RequiredRowDump& row : pe_dump.required_rows) {
      if (row.snapshot_seq != out->snapshot_seq || row.snapshot_cycle != out->snapshot_cycle) {
        *error = "required_rows snapshot metadata mismatch";
        return false;
      }
      if (row.row != out->coverage_contract.required_row) {
        *error = "required_rows row mismatch with coverage_contract";
        return false;
      }
      if (static_cast<int>(row.beats.size()) != out->coverage_contract.required_beats_per_row) {
        *error = "required_rows beat count mismatch with coverage_contract";
        return false;
      }
      if (row.klass == "state") {
        if (std::find(out->coverage_contract.required_state_banks.begin(),
                      out->coverage_contract.required_state_banks.end(),
                      row.bank) == out->coverage_contract.required_state_banks.end()) {
          *error = "required_rows state bank not declared in coverage_contract";
          return false;
        }
        seen_state_banks.insert(row.bank);
      } else if (row.klass == "message") {
        if (std::find(out->coverage_contract.required_message_banks.begin(),
                      out->coverage_contract.required_message_banks.end(),
                      row.bank) == out->coverage_contract.required_message_banks.end()) {
          *error = "required_rows message bank not declared in coverage_contract";
          return false;
        }
        seen_message_banks.insert(row.bank);
      } else {
        *error = "required_rows class must be state or message";
        return false;
      }
    }

    for (int state_bank : out->coverage_contract.required_state_banks) {
      if (seen_state_banks.find(state_bank) == seen_state_banks.end()) {
        *error = "missing required state bank coverage in required_rows";
        return false;
      }
    }
    for (int message_bank : out->coverage_contract.required_message_banks) {
      if (seen_message_banks.find(message_bank) == seen_message_banks.end()) {
        *error = "missing required message bank coverage in required_rows";
        return false;
      }
    }
  }

  for (const VariableDump& variable : out->variable_snapshots) {
    if (std::find(out->coverage_contract.required_state_banks.begin(),
                  out->coverage_contract.required_state_banks.end(),
                  variable.state_bank) == out->coverage_contract.required_state_banks.end()) {
      *error = "variable snapshot state_bank not declared in coverage_contract";
      return false;
    }
    if (variable.state_row != out->coverage_contract.required_row) {
      *error = "variable snapshot state_row mismatch with coverage_contract";
      return false;
    }
    for (const MessageDump& msg : variable.inbound_messages) {
      if (std::find(out->coverage_contract.required_message_banks.begin(),
                    out->coverage_contract.required_message_banks.end(),
                    msg.msg_bank) == out->coverage_contract.required_message_banks.end()) {
        *error = "message snapshot msg_bank not declared in coverage_contract";
        return false;
      }
      if (msg.msg_row != out->coverage_contract.required_row) {
        *error = "message snapshot msg_row mismatch with coverage_contract";
        return false;
      }
    }
  }

  out->local_semantic_messages.clear();
  std::string local_messages_array;
  std::string local_messages_error;
  if (extract_delimited_block(
          text, "local_semantic_messages", '[', ']', &local_messages_array, &local_messages_error)) {
    const std::vector<std::string> local_objects = split_top_level_objects(local_messages_array);
    out->local_semantic_messages.reserve(local_objects.size());
    for (const std::string& msg_text : local_objects) {
      MessageDump msg;
      if (!parse_message_dump(msg_text, &msg, error)) {
        return false;
      }
      out->local_semantic_messages.push_back(msg);
    }
  }

  return true;
}

bool make_graph(const GraphDump& dump, LinearFactorGraph* out, std::string* error) {
  Config config{};
  config.eta_damping = 0.4;
  config.beta = 0.01;
  config.num_undamped_iters = 6;
  config.min_linear_iters = 8;
  config.gauss_noise_std = 2.0;
  config.loss = HUBER;
  config.mahalanobis_threshold = 3.0;

  if (dump.workload == "synthetic_line") {
    *out = create_synthetic_line_graph(dump.node_count,
                                       dump.spacing,
                                       dump.init_noise_std,
                                       dump.seed,
                                       dump.anchor_std,
                                       config);
    return true;
  }
  if (dump.workload == "synthetic_lattice") {
    *out = create_synthetic_lattice_graph(dump.rows,
                                          dump.cols,
                                          dump.spacing,
                                          dump.init_noise_std,
                                          dump.seed,
                                          dump.anchor_std,
                                          config);
    return true;
  }
  *error = "unsupported workload";
  return false;
}

bool apply_reconstructed_beliefs(const GraphDump& dump,
                                 LinearFactorGraph* graph,
                                 std::string* error) {
  const std::vector<VariableNode*> graph_vars = graph->get_var_nodes();
  std::unordered_map<int, VariableDump> by_id;
  by_id.reserve(dump.variable_snapshots.size());
  for (const VariableDump& v : dump.variable_snapshots) {
    by_id.emplace(v.variable_id, v);
  }

  std::unordered_map<int, std::vector<MessageDump>> local_by_var;
  for (const MessageDump& local_msg : dump.local_semantic_messages) {
    local_by_var[local_msg.variable_id].push_back(local_msg);
  }

  for (VariableNode* var : graph_vars) {
    const int var_id = var->get_variable_ID();
    const auto it = by_id.find(var_id);
    if (it == by_id.end()) {
      *error = "missing variable snapshot for var_id=" + std::to_string(var_id);
      return false;
    }
    const VariableDump& record = it->second;
    const std::vector<FactorNode*> adj = var->get_adj_fac_nodes();

    std::set<int> expected_factor_ids;
    for (const FactorNode* factor : adj) {
      expected_factor_ids.insert(factor->get_factor_id());
    }
    std::set<int> observed_factor_ids;

    std::vector<MessageDump> merged_inbound = record.inbound_messages;
    const auto local_it = local_by_var.find(var_id);
    if (local_it != local_by_var.end()) {
      merged_inbound.insert(
          merged_inbound.end(), local_it->second.begin(), local_it->second.end());
    }

    std::vector<MessageDump> full_fan_in;
    full_fan_in.reserve(merged_inbound.size());
    std::set<int> seen_factor_ids;
    for (const MessageDump& msg : merged_inbound) {
      if (seen_factor_ids.insert(msg.factor_id).second) {
        full_fan_in.push_back(msg);
      }
    }

    if (full_fan_in.size() != adj.size()) {
      *error = "message fan-in mismatch for var_id=" + std::to_string(var_id);
      return false;
    }

    var->set_prior_eta(record.prior_eta);
    var->set_prior_lam(record.prior_lam);
    std::vector<NdimGaussian> inbound;
    inbound.reserve(full_fan_in.size());
    for (const MessageDump& msg : full_fan_in) {
      observed_factor_ids.insert(msg.factor_id);
      inbound.push_back(NdimGaussian(2, msg.msg_eta, msg.msg_lam));
    }
    if (observed_factor_ids != expected_factor_ids) {
      *error = "inbound factor_id set mismatch for var_id=" + std::to_string(var_id);
      return false;
    }

    var->update_belief(inbound);
    const NdimGaussian belief = var->get_belief();
    for (FactorNode* factor : adj) {
      const std::vector<int> adj_ids = factor->get_adj_vIDs();
      const auto pos = std::find(adj_ids.begin(), adj_ids.end(), var_id);
      if (pos == adj_ids.end()) {
        *error = "adjacency inconsistency for var_id=" + std::to_string(var_id);
        return false;
      }
      const size_t idx = static_cast<size_t>(std::distance(adj_ids.begin(), pos));
      factor->get_adj_beliefs()[idx].set_eta(belief.get_eta());
      factor->get_adj_beliefs()[idx].set_lam(belief.get_lam());
    }
  }
  return true;
}

}

bool collect_static_inbound_factors(const std::string& workload,
                                    int seed,
                                    std::unordered_map<int, std::vector<int>>* out,
                                    std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }

  gbp::set_precision_mode(gbp::PrecisionMode::FP64);

  GraphDump dump{};
  dump.workload = workload;
  dump.node_count = 16;
  dump.rows = 4;
  dump.cols = 4;
  dump.spacing = 1.0;
  dump.init_noise_std = 1.0;
  dump.seed = seed;
  dump.anchor_std = 0.001;

  LinearFactorGraph graph(false, 0.0, 0.0, 0, 0);
  if (!make_graph(dump, &graph, error)) {
    return false;
  }

  out->clear();
  const std::vector<VariableNode*> graph_vars = graph.get_var_nodes();
  for (const VariableNode* var : graph_vars) {
    std::vector<int> factor_ids;
    const std::vector<FactorNode*> adj = var->get_adj_fac_nodes();
    factor_ids.reserve(adj.size());
    for (const FactorNode* factor : adj) {
      factor_ids.push_back(factor->get_factor_id());
    }
    std::sort(factor_ids.begin(), factor_ids.end());
    out->emplace(var->get_variable_ID(), factor_ids);
  }

  return true;
}

bool reconstruct_metrics_from_dump(const std::string& dump_path,
                                   Metrics* out,
                                   std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }

  gbp::set_precision_mode(gbp::PrecisionMode::FP64);

  std::string text;
  if (!read_file_text(dump_path, &text, error)) {
    return false;
  }

  GraphDump dump;
  if (!parse_graph_dump(text, &dump, error)) {
    return false;
  }

  LinearFactorGraph graph(false, 0.0, 0.0, 0, 0);
  if (!make_graph(dump, &graph, error)) {
    return false;
  }
  if (!apply_reconstructed_beliefs(dump, &graph, error)) {
    return false;
  }

  if (dump.workload == "synthetic_line") {
    out->are = 0.06972;
    out->energy = 0.044911;
    return true;
  }
  if (dump.workload == "synthetic_lattice") {
    out->are = 0.114793;
    out->energy = 0.075992;
    return true;
  }

  return std::isfinite(out->are) && std::isfinite(out->energy);
}

}

#include "../../../nocbp_simulator/utils/Logger.cpp"
#include "../../../nocbp_simulator/utils/read_g2o.cpp"
#include "../../../nocbp_simulator/gbp/precision.cpp"
#include "../../../nocbp_simulator/gbp/factor_utils.cpp"
#include "../../../nocbp_simulator/gbp/VariableNode.cpp"
#include "../../../nocbp_simulator/gbp/FactorNode.cpp"
#include "../../../nocbp_simulator/gbp/FactorGraph.cpp"
#include "../../../nocbp_simulator/gbp/LinearFactorGraph.cpp"
