#ifndef SPDLOG_HEADER_ONLY
#define SPDLOG_HEADER_ONLY
#endif

#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <svdpi.h>
#include "verilated.h"
#include "Vgbp_pe_mesh_whitebox_convergence.h"

#include "BAFactorGraph.hpp"

#include "../common/gbp_event_correlation.hpp"
#include "../common/gbp_message_payload_codec.hpp"
#include "../common/gbp_terminal_metrics_adapter.hpp"

using Dut = Vgbp_pe_mesh_whitebox_convergence;

static constexpr uint32_t kRowBytesLg = 5;
static constexpr uint32_t kBankWidth = 3;
static constexpr uint32_t kSpmRowShift = kRowBytesLg + kBankWidth;
static constexpr uint32_t kWordsPerRow = 8;
static constexpr uint32_t kCyclesAfterReset = 4;
static constexpr int kCommandTimeout = 128;
static constexpr int kObserveCycles = 500000;  // 进一步增加以完成 50 次迭代（约需 500k 周期）
static constexpr int kFixedIters = 50;
static constexpr int kMaxWriteDebugPerPe = 24;
static constexpr int kMaxPipelineDebugPerPe = 24;
static constexpr const char* kBaDatasetPath = "data/fr1desk_small.txt";
static constexpr double kAreDropBaseline = 3272.667633;
static constexpr uint32_t kRequiredStateBankLo = 1;
static constexpr uint32_t kRequiredStateBankHi = 3;
static constexpr uint32_t kRequiredMessageBankLo = 4;
static constexpr uint32_t kRequiredMessageBankHi = 7;
static constexpr uint32_t kRequiredRowIndex = 0;

// ============================================
// DUT ARE 计算相关结构体
// ============================================

// 从 DUT 读取的 Variable State
struct DutVariableState {
  int variable_id = -1;
  int pe = -1;
  int dofs = 2;
  
  // Prior (from state bank)
  Eigen::VectorXd prior_eta;
  Eigen::MatrixXd prior_lam;
  
  // Inbound messages
  struct InboundMessage {
    int factor_id = -1;
    int msg_bank = -1;
    int msg_row = -1;
    Eigen::VectorXd eta;
    Eigen::MatrixXd lam;
  };
  std::vector<InboundMessage> inbound_messages;
};

// ============================================

struct RuntimeConfig {
  uint32_t pe_count = 0;
  uint32_t mesh_x = 0;
  uint32_t mesh_y = 0;
  uint32_t seed = 0;
  std::string run_config_path;
  std::string workload;
  std::string dataset_path;
  std::string partition_path;
};

struct BankScope {
  uint32_t pe = 0;
  uint32_t bank = 0;
};

struct Threshold {
  double abs_tol = 0.0;
  double rel_tol = 0.0;
};

struct AreEnergyMetrics {
  double final_are = 0.0;
  double final_energy = 0.0;
};

struct AreEnergyFieldReport {
  const char* field = nullptr;
  double expected = 0.0;
  double observed = 0.0;
  double abs_err = 0.0;
  double rel_err = 0.0;
  bool pass = false;
};

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

struct PeProgressSummary {
  uint32_t starts = 0;
  uint32_t dones = 0;
  uint32_t writes = 0;
  uint32_t epoch_transitions = 0;
  uint32_t doorbell_high_cycles = 0;
  uint32_t final_meta_row = 0;
  uint8_t final_head = 0;
  uint8_t final_tail = 0;
  uint8_t final_credit = 0;
  uint8_t final_epoch = 0;
  bool final_doorbell = false;
  bool final_scan_done = false;
  uint8_t final_ctrl_state = 0;
  bool final_ctrl_phase = false;
  bool final_ctrl_compute_pending = false;
  bool final_ctrl_compute_running = false;
  uint32_t final_ctrl_var_cmd_accept_count = 0;
  uint32_t final_ctrl_fac_cmd_accept_count = 0;
  uint32_t final_ctrl_phase_flip_count = 0;
  uint32_t final_ctrl_epoch_count = 0;
  bool final_wr_desc_pending = false;
  uint8_t final_wrapper_state = 0;
  bool final_wrapper_compute_done = false;
  bool final_wrapper_rsp_done = false;
  bool final_engine_compute_done = false;
  bool final_engine_rsp_done = false;
  uint8_t final_engine_cmd_dofs = 0;
  uint8_t final_engine_cmd_msg_count = 0;
  uint16_t final_engine_stream_in_beats = 0;
  uint16_t final_engine_stream_out_beats = 0;
  uint16_t final_engine_stream_target_beats = 0;
  bool final_engine_stream_active = false;
  bool final_engine_stream_dir_out = false;
  bool final_engine_stream_out_valid = false;
  bool final_engine_stream_out_ready = false;
  uint8_t final_engine_fsm_state = 0;
  uint8_t final_engine_fsm_accum_count = 0;
  uint8_t final_write_fifo_occ = 0;
  bool final_engine_stream_out_nonzero = false;
  uint32_t final_engine_stream_out_word0 = 0;
  uint8_t final_engine_stream_rd_addr = 0;
  bool final_write_fifo_data_nonzero = false;
  uint32_t final_write_fifo_data_word0 = 0;
  bool final_mic_write_data_nonzero = false;
  uint32_t final_mic_write_data_word0 = 0;
  uint32_t wr_req_debug_count = 0;
  uint32_t ingress_wr_req_debug_count = 0;
  uint32_t pipeline_debug_count = 0;
};

struct LocalSemanticMessageRecord {
  int pe = -1;
  int factor_id = -1;
  int variable_id = -1;
  gbp_event_correlation::SemanticPayloadEvidence semantic;
};

struct GraphMessageRecord {
  int factor_id = -1;
  int variable_id = -1;
  int dofs = 0;
  Eigen::VectorXd eta;
  Eigen::MatrixXd lam;
};

struct GraphVariableRecord {
  int pe = -1;
  int variable_id = -1;
  int dofs = 0;
  Eigen::VectorXd prior_eta;
  Eigen::MatrixXd prior_lam;
  std::vector<GraphMessageRecord> inbound_messages;
};

static uint32_t g_mesh_x = 0;
static uint64_t g_cycle_count = 0;
static std::unordered_map<std::string, BankScope> g_scope_cache;
static std::unordered_map<uint64_t, uint32_t> g_pmem_words;
static uint64_t g_pmem_read_count = 0;
static uint64_t g_pmem_write_count = 0;

static uint64_t pmem_key(uint32_t pe, uint32_t bank, uint32_t row, uint32_t word) {
  return (static_cast<uint64_t>(pe) << 32)
      | (static_cast<uint64_t>(bank) << 24)
      | (static_cast<uint64_t>(row) << 8)
      | static_cast<uint64_t>(word);
}

static uint32_t pmem_store_read(uint32_t pe, uint32_t bank, uint32_t row, uint32_t word) {
  const auto it = g_pmem_words.find(pmem_key(pe, bank, row, word));
  return (it == g_pmem_words.end()) ? 0u : it->second;
}

static void pmem_store_write(uint32_t pe, uint32_t bank, uint32_t row, uint32_t word, uint32_t data) {
  g_pmem_words[pmem_key(pe, bank, row, word)] = data;
}

// ============================================
// DUT ARE 计算辅助函数
// ============================================

static float decode_fp32_word(uint32_t word) {
  float value = 0.0f;
  std::memcpy(&value, &word, sizeof(value));
  return value;
}

// 检查 row 是否全为零
static bool is_row_empty(const std::array<uint32_t, kWordsPerRow>& row_words) {
  for (uint32_t word : row_words) {
    if (word != 0) return false;
  }
  return true;
}

static int compact_payload_words(int dofs) {
  static constexpr std::array<int, 7> kCompactPayloadWordsByDofs{{0, 2, 5, 9, 14, 20, 27}};
  if (dofs < 0 || dofs >= static_cast<int>(kCompactPayloadWordsByDofs.size())) {
    return 0;
  }
  return kCompactPayloadWordsByDofs[static_cast<size_t>(dofs)];
}

static int compact_payload_rows(int dofs) {
  return (compact_payload_words(dofs) + 7) >> 3;
}

static uint32_t meta_word_to_row(uint32_t meta_word) {
  return (meta_word >> 12) >> kSpmRowShift;
}

static std::vector<uint32_t> pack_compact_payload_words(const Eigen::VectorXd& eta,
                                                        const Eigen::MatrixXd& lam) {
  const int dofs = static_cast<int>(eta.size());
  std::vector<uint32_t> words;
  words.reserve(compact_payload_words(dofs));

  for (int i = 0; i < dofs; ++i) {
    const float val = static_cast<float>(eta(i));
    uint32_t raw = 0;
    std::memcpy(&raw, &val, sizeof(raw));
    words.push_back(raw);
  }
  for (int i = 0; i < dofs; ++i) {
    for (int j = i; j < dofs; ++j) {
      const float val = static_cast<float>(lam(i, j));
      uint32_t raw = 0;
      std::memcpy(&raw, &val, sizeof(raw));
      words.push_back(raw);
    }
  }
  return words;
}

static void pack_compact_payload_rows(const Eigen::VectorXd& eta,
                                      const Eigen::MatrixXd& lam,
                                      std::vector<std::array<uint32_t, kWordsPerRow>>* out_rows) {
  if (out_rows == nullptr) {
    return;
  }

  const std::vector<uint32_t> words = pack_compact_payload_words(eta, lam);
  const int rows_needed = (static_cast<int>(words.size()) + 7) >> 3;
  out_rows->assign(static_cast<size_t>(rows_needed), std::array<uint32_t, kWordsPerRow>{});
  for (size_t idx = 0; idx < words.size(); ++idx) {
    const size_t row_idx = idx >> 3;
    const size_t word_idx = idx & 7u;
    (*out_rows)[row_idx][word_idx] = words[idx];
  }
}

static int count_nonzero_words(const std::vector<std::array<uint32_t, kWordsPerRow>>& rows) {
  int nonzero = 0;
  for (const auto& row : rows) {
    for (uint32_t word : row) {
      if (word != 0) {
        nonzero++;
      }
    }
  }
  return nonzero;
}

static int repeated_row_span(int count, int rows_per_item) {
  int span = 0;
  for (int idx = 0; idx < count; ++idx) {
    span += rows_per_item;
  }
  return span;
}

static uint32_t row_offset_from_index(size_t index, int rows_per_item) {
  uint32_t offset = 0;
  for (size_t idx = 0; idx < index; ++idx) {
    offset += static_cast<uint32_t>(rows_per_item);
  }
  return offset;
}

// 从 State rows 解码 prior_eta 和 prior_lam (支持多行)
// 格式：连续的 rows，每行8 words，依次存储 eta 和 lam 上三角元素
static bool decode_state_from_rows(const std::vector<std::array<uint32_t, kWordsPerRow>>& row_words_list,
                                    int dofs,
                                    Eigen::VectorXd* out_eta,
                                    Eigen::MatrixXd* out_lam,
                                    std::string* error) {
  if (dofs < 1 || dofs > 6) {
    *error = "dofs must be 1-6";
    return false;
  }
  
  if (row_words_list.empty() || is_row_empty(row_words_list[0])) {
    *error = "row empty";
    return false;
  }
  
  // Flatten all words from all rows
  std::vector<uint32_t> all_words;
  for (const auto& row : row_words_list) {
    for (uint32_t w : row) {
      all_words.push_back(w);
    }
  }
  
  // Decode eta
  *out_eta = Eigen::VectorXd(dofs);
  for (int i = 0; i < dofs; ++i) {
    (*out_eta)(i) = decode_fp32_word(all_words[i]);
  }
  
  // Decode lam (upper triangular)
  *out_lam = Eigen::MatrixXd::Zero(dofs, dofs);
  int word_idx = dofs;
  for (int i = 0; i < dofs; ++i) {
    for (int j = i; j < dofs; ++j) {
      if (word_idx < static_cast<int>(all_words.size())) {
        double lam_val = decode_fp32_word(all_words[word_idx++]);
        (*out_lam)(i, j) = lam_val;
        if (i != j) {
          (*out_lam)(j, i) = lam_val;
        }
      }
    }
  }
  
  return true;
}

// 从 Message rows 解码 (same multi-row format as state)
static bool decode_message_from_rows(const std::vector<std::array<uint32_t, kWordsPerRow>>& row_words_list,
                                      int dofs,
                                      Eigen::VectorXd* out_eta,
                                      Eigen::MatrixXd* out_lam,
                                      std::string* error) {
  return decode_state_from_rows(row_words_list, dofs, out_eta, out_lam, error);
}

// ============================================

static bool parse_scope_bank(const std::string& scope_name, BankScope* out) {
  static const std::regex rx(
      R"(y(?:\[|__BRA__)(\d+)(?:\]|__KET__).*x(?:\[|__BRA__)(\d+)(?:\]|__KET__).*banks(?:\[|__BRA__)(\d+)(?:\]|__KET__))");
  std::smatch match;
  if (!std::regex_search(scope_name, match, rx) || match.size() < 4) {
    return false;
  }
  const uint32_t y = static_cast<uint32_t>(std::stoul(match[1].str()));
  const uint32_t x = static_cast<uint32_t>(std::stoul(match[2].str()));
  const uint32_t bank = static_cast<uint32_t>(std::stoul(match[3].str()));
  out->pe = y * g_mesh_x + x;
  out->bank = bank;
  return true;
}

static bool resolve_current_bank_scope(BankScope* out) {
  const svScope scope = svGetScope();
  if (scope == nullptr) {
    return false;
  }
  const char* scope_name_c = svGetNameFromScope(scope);
  if (scope_name_c == nullptr) {
    return false;
  }
  const std::string scope_name(scope_name_c);
  const auto it = g_scope_cache.find(scope_name);
  if (it != g_scope_cache.end()) {
    *out = it->second;
    return true;
  }
  BankScope parsed{};
  if (!parse_scope_bank(scope_name, &parsed)) {
    return false;
  }
  g_scope_cache.emplace(scope_name, parsed);
  *out = parsed;
  return true;
}

extern "C" int pmem_read(int raddr) {
  g_pmem_read_count++;
  BankScope scope{};;
  if (!resolve_current_bank_scope(&scope) || raddr < 0) {
    std::fprintf(stderr, "FAIL: pmem_read 无法解析当前 scope 或地址非法 raddr=%d\n", raddr);
    std::abort();
  }
  const uint32_t word_addr = static_cast<uint32_t>(raddr);
  const uint32_t row = word_addr / kWordsPerRow;
  const uint32_t word = word_addr % kWordsPerRow;
  
  // Debug: 检查 bank 0 row 0 的读取
  if (scope.bank == 0 && row == 0 && word < 5 && g_pmem_read_count <= 1000) {
    uint32_t data = pmem_store_read(scope.pe, scope.bank, row, word);
    std::printf("GBP_SPM_READ_DEBUG pe=%u bank=%u row=%u word=%u raddr=%d data=%08x\n",
                scope.pe, scope.bank, row, word, raddr, data);
  }
  
  return static_cast<int>(pmem_store_read(scope.pe, scope.bank, row, word));
}

extern "C" void pmem_write(int waddr, int wdata, char byte_num) {
  g_pmem_write_count++;
  // Debug: 打印 message bank 的写入
  BankScope debug_scope{};
  if (resolve_current_bank_scope(&debug_scope) && debug_scope.bank == 4 && g_pmem_write_count <= 100) {
    std::printf("GBP_WHITEBOX_SPM_WRITE_DEBUG pe=%u bank=%u waddr=%d wdata=%08x byte_num=%d\n",
                debug_scope.pe, debug_scope.bank, waddr, wdata, byte_num);
  }
  if (resolve_current_bank_scope(&debug_scope) && debug_scope.bank == 0 && g_pmem_write_count <= 200) {
    std::printf("GBP_WHITEBOX_META_WRITE_RUNTIME pe=%u bank=%u waddr=%d wdata=%08x byte_num=%d cycle=%llu\n",
                debug_scope.pe,
                debug_scope.bank,
                waddr,
                wdata,
                byte_num,
                static_cast<unsigned long long>(g_cycle_count));
  }
  BankScope scope{};
  if (!resolve_current_bank_scope(&scope) || waddr < 0 || byte_num < 0 || byte_num > 3) {
    std::fprintf(stderr,
                 "FAIL: pmem_write scope/地址非法 waddr=%d byte_num=%d\n",
                 waddr,
                 static_cast<int>(byte_num));
    std::abort();
  }
  const uint32_t word_addr = static_cast<uint32_t>(waddr);
  const uint32_t row = word_addr / kWordsPerRow;
  const uint32_t word = word_addr % kWordsPerRow;
  uint32_t current = pmem_store_read(scope.pe, scope.bank, row, word);
  const uint32_t shift = static_cast<uint32_t>(static_cast<unsigned char>(byte_num)) * 8u;
  const uint32_t mask = 0xFFu << shift;
  current = (current & ~mask) | (static_cast<uint32_t>(wdata) & mask);
  pmem_store_write(scope.pe, scope.bank, row, word, current);
}

static bool parse_u32_env(const char* key, uint32_t* out) {
  const char* raw = std::getenv(key);
  if (raw == nullptr || raw[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(raw, &end, 10);
  if (end == raw || *end != '\0') {
    return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
}

static bool parse_string_env(const char* key, std::string* out) {
  const char* raw = std::getenv(key);
  if (raw == nullptr || raw[0] == '\0') {
    return false;
  }
  *out = raw;
  return true;
}

static bool read_text_file(const std::string& path, std::string* out) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    return false;
  }
  *out = std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  return true;
}

static bool load_runtime_config(RuntimeConfig* out) {
  RuntimeConfig cfg{};
  if (!parse_u32_env("PE_COUNT", &cfg.pe_count) || !parse_u32_env("MESH_X", &cfg.mesh_x)
      || !parse_u32_env("MESH_Y", &cfg.mesh_y) || !parse_u32_env("SEED", &cfg.seed)
      || !parse_string_env("RUN_CONFIG_ABS", &cfg.run_config_path)
      || !parse_string_env("WORKLOAD", &cfg.workload)
      || !parse_string_env("DATASET", &cfg.dataset_path)
      || !parse_string_env("PARTITION", &cfg.partition_path)) {
    std::fprintf(stderr, "FAIL: 缺少白盒 mesh 运行期环境变量\n");
    return false;
  }
  if (cfg.mesh_x == 0 || cfg.mesh_y == 0 || cfg.mesh_x * cfg.mesh_y != cfg.pe_count) {
    std::fprintf(stderr,
                 "FAIL: mesh 契约无效 pe_count=%u mesh=%ux%u\n",
                 cfg.pe_count,
                 cfg.mesh_x,
                 cfg.mesh_y);
    return false;
  }
  *out = cfg;
  return true;
}

static bool extract_named_u32(const std::string& text,
                              const std::string& pattern,
                              uint32_t* out) {
  std::regex rx(pattern);
  std::smatch match;
  if (!std::regex_search(text, match, rx) || match.size() < 2) {
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(match[1].str().c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
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

static bool parse_partition_mapping_table(const std::string& text,
                                          const char* key,
                                          std::vector<std::vector<int>>* out) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t pos = text.find(needle);
  if (pos == std::string::npos) {
    return false;
  }
  pos = text.find('[', pos);
  if (pos == std::string::npos) {
    return false;
  }

  std::vector<std::vector<int>> table;
  std::vector<int> current;
  int depth = 0;
  bool in_number = false;
  bool negative = false;
  int value = 0;
  for (size_t i = pos; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch == '[') {
      depth += 1;
      if (depth == 2) {
        current.clear();
      }
      continue;
    }
    if (in_number && !(std::isdigit(static_cast<unsigned char>(ch)) || ch == '-')) {
      current.push_back(negative ? -value : value);
      in_number = false;
      negative = false;
      value = 0;
    }
    if (ch == ']') {
      if (depth == 2) {
        table.push_back(current);
        current.clear();
      }
      depth -= 1;
      if (depth == 0) {
        *out = table;
        return true;
      }
      continue;
    }
    if (depth >= 2) {
      if (!in_number && ch == '-') {
        in_number = true;
        negative = true;
        value = 0;
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(ch))) {
        if (!in_number) {
          in_number = true;
          negative = false;
          value = 0;
        }
        value = value * 10 + (ch - '0');
      }
    }
  }
  return false;
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
  return depth == 0 && out->size() == pe_count;
}

static bool validate_partition_contract(const RuntimeConfig& cfg) {
  std::string partition_text;
  if (!read_text_file(cfg.partition_path, &partition_text)) {
    std::fprintf(stderr, "FAIL: 无法打开 partition 文件: %s\n", cfg.partition_path.c_str());
    return false;
  }

  uint32_t partition_mesh_x = 0;
  uint32_t partition_mesh_y = 0;
  if (!extract_named_u32(partition_text,
                         "\"mesh\"\\s*:\\s*\\{[\\s\\S]*?\"x\"\\s*:\\s*(\\d+)",
                         &partition_mesh_x)
      || !extract_named_u32(partition_text,
                            "\"mesh\"\\s*:\\s*\\{[\\s\\S]*?\"y\"\\s*:\\s*(\\d+)",
                            &partition_mesh_y)) {
    std::fprintf(stderr, "FAIL: partition 缺少 mesh.x / mesh.y\n");
    return false;
  }

  std::vector<std::vector<int>> fac_mapping;
  std::vector<std::vector<int>> var_mapping;
  if (!parse_partition_mapping_table(partition_text, "fac_mapping_table", &fac_mapping)
      || !parse_partition_mapping_table(partition_text, "var_mapping_table", &var_mapping)) {
    std::fprintf(stderr, "FAIL: partition 缺少 fac/var mapping table\n");
    return false;
  }

  if (partition_mesh_x != cfg.mesh_x || partition_mesh_y != cfg.mesh_y) {
    std::fprintf(stderr,
                 "FAIL: partition mesh 与 RUN_CONFIG 不一致 partition=%ux%u run_config=%ux%u\n",
                 partition_mesh_x,
                 partition_mesh_y,
                 cfg.mesh_x,
                 cfg.mesh_y);
    return false;
  }
  if (fac_mapping.size() != cfg.pe_count || var_mapping.size() != cfg.pe_count) {
    std::fprintf(stderr,
                 "FAIL: partition owner 维度与 PE_COUNT 不一致 fac=%zu var=%zu pe_count=%u\n",
                 fac_mapping.size(),
                 var_mapping.size(),
                 cfg.pe_count);
    return false;
  }
  return true;
}

static bool load_partition_info(const char* path, uint32_t pe_count, PartitionInfo* out) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: 无法打开 PARTITION JSON: %s\n", path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  std::string fac_section;
  if (!extract_balanced_region(text, "fac_mapping_table", '[', ']', &fac_section)
      || !parse_pe_table(fac_section, pe_count, &out->fac_mapping)) {
    std::fprintf(stderr, "FAIL: malformed fac_mapping_table in %s\n", path);
    return false;
  }

  std::string var_section;
  if (!extract_balanced_region(text, "var_mapping_table", '[', ']', &var_section)
      || !parse_pe_table(var_section, pe_count, &out->var_mapping)) {
    std::fprintf(stderr, "FAIL: malformed var_mapping_table in %s\n", path);
    return false;
  }

  std::string edge_section;
  if (!extract_balanced_region(text, "factor_var_edges", '[', ']', &edge_section)) {
    std::fprintf(stderr, "FAIL: missing graph.factor_var_edges in %s\n", path);
    return false;
  }
  std::regex edge_re("\\[\\s*([0-9]+)\\s*,\\s*([0-9]+)\\s*\\]");
  for (std::sregex_iterator it(edge_section.begin(), edge_section.end(), edge_re), end; it != end; ++it) {
    out->edges.push_back({std::stoi((*it)[1].str()), std::stoi((*it)[2].str())});
  }
  if (out->edges.empty()) {
    std::fprintf(stderr, "FAIL: no factor_var_edges in %s\n", path);
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
    out.push_back(CrossEdge{edge.first, edge.second, fit->second, vit->second});
  }
  return out;
}

static bool extract_double_field(const std::string& text,
                                 const std::string& key,
                                 double* out) {
  std::regex rx(std::string("\"") + key + "\"\\s*:\\s*([-+0-9.eE]+)");
  std::smatch m;
  if (!std::regex_search(text, m, rx) || m.size() < 2) {
    return false;
  }
  char* end = nullptr;
  *out = std::strtod(m[1].str().c_str(), &end);
  return end && *end == '\0';
}

static bool extract_workload_metric_with_alias(const std::string& section,
                                               const char* primary,
                                               const char* alias,
                                               double* out) {
  return extract_double_field(section, primary, out) || extract_double_field(section, alias, out);
}

static bool extract_are_energy_threshold_from_oracle(const char* oracle_path, Threshold* out) {
  std::ifstream ifs(oracle_path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: 无法打开 oracle JSON: %s\n", oracle_path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  const size_t final_are_key = text.find("\"final_are\"");
  const size_t final_energy_key = text.find("\"final_energy\"");
  if (final_are_key == std::string::npos || final_energy_key == std::string::npos) {
    std::fprintf(stderr, "FAIL: oracle JSON 缺少 thresholds.final_are/final_energy\n");
    return false;
  }

  const auto parse_threshold_block = [&](size_t key,
                                         const char* field,
                                         double* abs_out,
                                         double* rel_out) -> bool {
    const size_t start = text.find('{', key);
    if (start == std::string::npos) {
      std::fprintf(stderr, "FAIL: malformed thresholds.%s\n", field);
      return false;
    }
    int depth = 0;
    size_t end = std::string::npos;
    for (size_t i = start; i < text.size(); ++i) {
      if (text[i] == '{') {
        ++depth;
      } else if (text[i] == '}') {
        --depth;
        if (depth == 0) {
          end = i;
          break;
        }
      }
    }
    if (end == std::string::npos || end <= start) {
      std::fprintf(stderr, "FAIL: unterminated thresholds.%s\n", field);
      return false;
    }
    const std::string section = text.substr(start, end - start + 1);
    if (!extract_double_field(section, "abs_tol", abs_out) ||
        !extract_double_field(section, "rel_tol", rel_out)) {
      std::fprintf(stderr, "FAIL: 无法解析 thresholds.%s abs_tol/rel_tol\n", field);
      return false;
    }
    return true;
  };

  double final_are_abs = 0.0;
  double final_are_rel = 0.0;
  double final_energy_abs = 0.0;
  double final_energy_rel = 0.0;
  if (!parse_threshold_block(final_are_key, "final_are", &final_are_abs, &final_are_rel) ||
      !parse_threshold_block(final_energy_key, "final_energy", &final_energy_abs, &final_energy_rel)) {
    return false;
  }

  out->abs_tol = std::max(final_are_abs, final_energy_abs);
  out->rel_tol = std::max(final_are_rel, final_energy_rel);
  return true;
}

static bool load_workload_are_energy_metrics(const char* oracle_path,
                                             const char* workload,
                                             AreEnergyMetrics* out) {
  std::ifstream ifs(oracle_path);
  if (!ifs.is_open()) {
    std::fprintf(stderr, "FAIL: 无法打开 oracle JSON: %s\n", oracle_path);
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  const size_t workloads_root = text.find("\"workloads\"");
  if (workloads_root == std::string::npos) {
    std::fprintf(stderr, "FAIL: oracle JSON 缺少 workloads\n");
    return false;
  }
  const std::string workload_key = std::string("\"") + workload + "\"";
  const size_t key = text.find(workload_key, workloads_root);
  if (key == std::string::npos) {
    std::fprintf(stderr, "FAIL: oracle JSON 缺少 workload=%s\n", workload);
    return false;
  }
  const size_t start = text.find('{', key);
  if (start == std::string::npos) {
    std::fprintf(stderr, "FAIL: malformed workload section=%s\n", workload);
    return false;
  }
  int depth = 0;
  size_t end = std::string::npos;
  for (size_t i = start; i < text.size(); ++i) {
    if (text[i] == '{') {
      ++depth;
    } else if (text[i] == '}') {
      --depth;
      if (depth == 0) {
        end = i;
        break;
      }
    }
  }
  if (end == std::string::npos || end <= start) {
    std::fprintf(stderr, "FAIL: unterminated workload section=%s\n", workload);
    return false;
  }
  const std::string section = text.substr(start, end - start + 1);
  if (!extract_workload_metric_with_alias(section, "final_are", "terminal_are", &out->final_are) ||
      !extract_workload_metric_with_alias(section, "final_energy", "terminal_energy", &out->final_energy)) {
    std::fprintf(stderr, "FAIL: oracle JSON 缺少 final_are/final_energy workload=%s\n", workload);
    return false;
  }
  return true;
}

static bool check_are_energy_metric(const char* workload,
                                    const char* field,
                                    double expected,
                                    double observed,
                                    const char* observed_source,
                                    const Threshold& threshold,
                                    AreEnergyFieldReport* report) {
  const double abs_err = std::fabs(observed - expected);
  const double denom = std::fmax(std::fabs(expected), 1e-12);
  const double rel_err = abs_err / denom;
  const bool pass = (abs_err <= threshold.abs_tol) || (rel_err <= threshold.rel_tol);
  if (report != nullptr) {
    report->field = field;
    report->expected = expected;
    report->observed = observed;
    report->abs_err = abs_err;
    report->rel_err = rel_err;
    report->pass = pass;
  }
  std::printf(
      "GBP_WHITEBOX_ARE_ENERGY_MARKER workload=%s field=%s expected=%.9g observed=%.9g observed_source=%s abs_err=%.9g rel_err=%.9g abs_tol=%.9g rel_tol=%.9g status=%s\n",
      workload,
      field,
      expected,
      observed,
      observed_source,
      abs_err,
      rel_err,
      threshold.abs_tol,
      threshold.rel_tol,
      pass ? "PASS" : "FAIL");
  return pass;
}

static void tick(Dut* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
  g_cycle_count += 1;
}

static void reset_dut(Dut* dut) {
  dut->rst_n = 0;
  dut->cmd_valid_i = 0;
  dut->cmd_kind_i = 0;
  dut->cmd_txn_i = 0;
  dut->cmd_pe_i = 0;
  dut->observe_pe_i = 0;
  dut->probe_pe_i = 0;
  dut->probe_bank_i = 0;
  dut->probe_row_i = 0;
  for (uint32_t i = 0; i < kCyclesAfterReset; ++i) {
    tick(dut);
  }
  dut->rst_n = 1;
  tick(dut);
}

static void wb_spm_write_word(uint32_t pe, uint32_t bank, uint32_t row, uint32_t word, uint32_t data) {
  pmem_store_write(pe, bank, row, word, data);
}

static uint32_t wb_spm_read_word(uint32_t pe, uint32_t bank, uint32_t row, uint32_t word) {
  return pmem_store_read(pe, bank, row, word);
}

static bool read_spm_row_words(uint32_t pe,
                               uint32_t bank,
                               uint32_t row,
                               std::array<uint32_t, kWordsPerRow>* beats) {
  if (beats == nullptr) {
    return false;
  }
  for (size_t i = 0; i < beats->size(); ++i) {
    (*beats)[i] = wb_spm_read_word(pe, bank, row, static_cast<uint32_t>(i));
  }
  return true;
}

static bool read_spm_rows(uint32_t pe,
                          uint32_t bank,
                          uint32_t base_row,
                          int rows_needed,
                          std::vector<std::array<uint32_t, kWordsPerRow>>* out_rows) {
  if (rows_needed <= 0 || out_rows == nullptr) {
    return false;
  }
  out_rows->clear();
  out_rows->reserve(static_cast<size_t>(rows_needed));
  for (int r = 0; r < rows_needed; ++r) {
    std::array<uint32_t, kWordsPerRow> row_words{};
    if (!read_spm_row_words(pe, bank, base_row + static_cast<uint32_t>(r), &row_words)) {
      return false;
    }
    out_rows->push_back(row_words);
  }
  return true;
}

static bool launch_cmd(Dut* dut, uint32_t pe, uint32_t txn_id) {
  dut->cmd_pe_i = pe;
  dut->cmd_kind_i = 0;
  dut->cmd_txn_i = txn_id;
  dut->cmd_valid_i = 1;
  for (int cycle = 0; cycle < kCommandTimeout; ++cycle) {
    dut->eval();
    if (dut->cmd_ready_o) {
      tick(dut);
      dut->cmd_valid_i = 0;
      dut->eval();
      return true;
    }
    tick(dut);
  }
  dut->cmd_valid_i = 0;
  dut->eval();
  return false;
}

static uint32_t count_partition_variables(const PartitionInfo& partition) {
  uint32_t total = 0;
  for (const auto& pe_vars : partition.var_mapping) {
    total += static_cast<uint32_t>(pe_vars.size());
  }
  return total;
}

static uint32_t count_partition_factors(const PartitionInfo& partition) {
  uint32_t total = 0;
  for (const auto& pe_factors : partition.fac_mapping) {
    total += static_cast<uint32_t>(pe_factors.size());
  }
  return total;
}

static std::string build_progress_summary(const std::vector<PeProgressSummary>& summaries) {
  std::ostringstream oss;
  for (size_t pe = 0; pe < summaries.size(); ++pe) {
    const PeProgressSummary& s = summaries[pe];
    if (pe != 0) {
      oss << " | ";
    }
    oss << "pe" << pe
        << ":starts=" << s.starts
        << ",dones=" << s.dones
        << ",writes=" << s.writes
        << ",epoch_transitions=" << s.epoch_transitions
        << ",final_epoch=" << static_cast<uint32_t>(s.final_epoch)
        << ",doorbell_high_cycles=" << s.doorbell_high_cycles
        << ",final_doorbell=" << (s.final_doorbell ? 1 : 0)
        << ",head=" << static_cast<uint32_t>(s.final_head)
        << ",tail=" << static_cast<uint32_t>(s.final_tail)
        << ",credit=" << static_cast<uint32_t>(s.final_credit)
        << ",meta_row=" << s.final_meta_row
        << ",scan_done=" << (s.final_scan_done ? 1 : 0)
        << ",ctrl_state=" << static_cast<uint32_t>(s.final_ctrl_state)
        << ",ctrl_phase=" << (s.final_ctrl_phase ? 1 : 0)
        << ",ctrl_pending=" << (s.final_ctrl_compute_pending ? 1 : 0)
        << ",ctrl_running=" << (s.final_ctrl_compute_running ? 1 : 0)
        << ",var_accept=" << s.final_ctrl_var_cmd_accept_count
        << ",fac_accept=" << s.final_ctrl_fac_cmd_accept_count
        << ",phase_flip=" << s.final_ctrl_phase_flip_count
        << ",epoch_count=" << s.final_ctrl_epoch_count
        << ",wr_desc_pending=" << (s.final_wr_desc_pending ? 1 : 0)
        << ",wrapper_state=" << static_cast<uint32_t>(s.final_wrapper_state)
        << ",wrapper_compute_done=" << (s.final_wrapper_compute_done ? 1 : 0)
        << ",wrapper_rsp_done=" << (s.final_wrapper_rsp_done ? 1 : 0)
        << ",engine_compute_done=" << (s.final_engine_compute_done ? 1 : 0)
        << ",engine_rsp_done=" << (s.final_engine_rsp_done ? 1 : 0)
        << ",engine_cmd_dofs=" << static_cast<uint32_t>(s.final_engine_cmd_dofs)
        << ",engine_cmd_msg_count=" << static_cast<uint32_t>(s.final_engine_cmd_msg_count)
        << ",engine_stream_in_beats=" << s.final_engine_stream_in_beats
        << ",engine_stream_out_beats=" << s.final_engine_stream_out_beats
        << ",engine_stream_target_beats=" << s.final_engine_stream_target_beats
        << ",engine_stream_active=" << (s.final_engine_stream_active ? 1 : 0)
        << ",engine_stream_dir_out=" << (s.final_engine_stream_dir_out ? 1 : 0)
        << ",engine_stream_out_valid=" << (s.final_engine_stream_out_valid ? 1 : 0)
        << ",engine_stream_out_ready=" << (s.final_engine_stream_out_ready ? 1 : 0)
        << ",engine_fsm_state=" << static_cast<uint32_t>(s.final_engine_fsm_state)
        << ",engine_fsm_accum_count=" << static_cast<uint32_t>(s.final_engine_fsm_accum_count)
        << ",write_fifo_occ=" << static_cast<uint32_t>(s.final_write_fifo_occ)
        << ",engine_stream_rd_addr=" << static_cast<uint32_t>(s.final_engine_stream_rd_addr)
        << ",engine_stream_out_nonzero=" << (s.final_engine_stream_out_nonzero ? 1 : 0)
        << ",engine_stream_out_word0=0x" << std::hex << s.final_engine_stream_out_word0 << std::dec
        << ",write_fifo_data_nonzero=" << (s.final_write_fifo_data_nonzero ? 1 : 0)
        << ",write_fifo_data_word0=0x" << std::hex << s.final_write_fifo_data_word0 << std::dec
        << ",mic_write_data_nonzero=" << (s.final_mic_write_data_nonzero ? 1 : 0)
        << ",mic_write_data_word0=0x" << std::hex << s.final_mic_write_data_word0 << std::dec;
  }
  return oss.str();
}

static bool build_bootstrap_semantic_evidence(uint32_t bank,
                                              uint32_t slot,
                                              int edge_factor,
                                              int edge_var,
                                              int src_pe,
                                              int dst_pe,
                                              gbp_event_correlation::SemanticPayloadEvidence* out) {
  if (out == nullptr) {
    return false;
  }
  gbp_message_payload_codec::DecodedPayload semantic_seed;
  semantic_seed.schema_version = gbp_message_payload_codec::kPhase1SchemaVersion;
  semantic_seed.bank = bank;
  semantic_seed.slot = slot;
  semantic_seed.direction = gbp_message_payload_codec::direction_for_slot(slot);
  semantic_seed.dim = 2;
  semantic_seed.eta.push_back(static_cast<float>(edge_factor + 1));
  semantic_seed.eta.push_back(static_cast<float>(edge_var + 1));
  semantic_seed.lam.push_back(1.0f + static_cast<float>(src_pe + 1));
  semantic_seed.lam.push_back(0.25f * static_cast<float>((edge_factor % 3) + 1));
  semantic_seed.lam.push_back(1.0f + static_cast<float>(dst_pe + 1));

  gbp_message_payload_codec::EncodedPayload encoded;
  std::string codec_error;
  if (!gbp_message_payload_codec::encode(semantic_seed, &encoded, &codec_error)
      || encoded.segments.empty() || encoded.segments[0].words.size() != 5u) {
    return false;
  }

  std::array<uint32_t, 5> payload_words{{0u, 0u, 0u, 0u, 0u}};
  for (size_t i = 0; i < payload_words.size(); ++i) {
    payload_words[i] = encoded.segments[0].words[i];
  }
  gbp_event_correlation::SemanticPayloadEvidence decoded;
  if (!gbp_event_correlation::decode_semantic_payload(
          bank, kRequiredRowIndex, payload_words, &decoded, &codec_error)) {
    return false;
  }
  *out = decoded;
  return true;
}

static void emit_json_vector(std::ostream& os, const Eigen::VectorXd& vec) {
  os << "[";
  for (Eigen::Index i = 0; i < vec.size(); ++i) {
    os << vec(i);
    if (i + 1 != vec.size()) {
      os << ", ";
    }
  }
  os << "]";
}

static void emit_json_matrix(std::ostream& os, const Eigen::MatrixXd& mat) {
  os << "[";
  for (Eigen::Index r = 0; r < mat.rows(); ++r) {
    os << "[";
    for (Eigen::Index c = 0; c < mat.cols(); ++c) {
      os << mat(r, c);
      if (c + 1 != mat.cols()) {
        os << ", ";
      }
    }
    os << "]";
    if (r + 1 != mat.rows()) {
      os << ", ";
    }
  }
  os << "]";
}

// ============================================
// 核心函数：从 DUT SPM 读取所有 variable states
// ============================================

static bool read_all_dut_variable_states(
    const RuntimeConfig& cfg,
    const PartitionInfo& partition,
    const std::unordered_map<int, VariableNode*>& var_node_map,
    std::vector<DutVariableState>* out_states,
    std::string* error) {
  
  out_states->clear();
  
  // 构建 variable -> factors 邻接关系
  std::unordered_map<int, std::vector<int>> var_to_factors;
  for (const auto& edge : partition.edges) {
    int factor_id = edge.first;
    int var_id = edge.second;
    var_to_factors[var_id].push_back(factor_id);
  }
  
  // 为每个 PE 读取其所有 variables
  int total_vars_checked = 0;
  int vars_with_state = 0;
  int vars_with_messages = 0;
  
  // 首先扫描每个 PE 的所有 SPM rows，建立 row -> variable 映射
  // 基于观察到的数据：state 存储在 Bank 3，messages 存储在 Banks 4-7
  for (uint32_t pe = 0; pe < cfg.pe_count; ++pe) {
	    const auto& pe_vars = partition.var_mapping[pe];
	    const auto& pe_factors = partition.fac_mapping[pe];
    const size_t num_vars = pe_vars.size();
    
    // 为每个 variable 创建状态对象，获取正确的 dofs
    std::unordered_map<int, DutVariableState> var_states;
    var_states.reserve(num_vars);
    
    for (size_t i = 0; i < num_vars; ++i) {
      int var_id = pe_vars[i];
      auto var_it = var_node_map.find(var_id);
      int dofs = (var_it != var_node_map.end()) ? var_it->second->get_dofs() : 2;
      var_states[var_id].variable_id = var_id;
      var_states[var_id].pe = static_cast<int>(pe);
      var_states[var_id].dofs = dofs;
    }
    
    // 扫描所有可能的 rows（需要足够大以容纳所有变量的多行存储）
    const uint32_t kMaxRowsToScan = 4096;

    std::printf("GBP_WHITEBOX_ARE_DEBUG msg_read_config: pe=%u msg_bank=%u num_vars=%zu\n",
                pe, kRequiredMessageBankLo, num_vars);

    for (size_t var_idx = 0; var_idx < num_vars; ++var_idx) {
      int var_id = pe_vars[var_idx];
      auto& state = var_states[var_id];
      std::array<uint32_t, kWordsPerRow> meta_words{};
      if (!read_spm_row_words(pe, 0u, static_cast<uint32_t>(var_idx + 1), &meta_words)) {
        continue;
      }
      if (meta_words[0] == 0u) {
        continue;
      }

      const int meta_dofs = static_cast<int>((meta_words[0] >> 9) & 0x7u);
      if (meta_dofs >= 1 && meta_dofs <= 6) {
        state.dofs = meta_dofs;
      }
      const int fan_in = static_cast<int>((meta_words[0] >> 12) & 0xFu);
      const uint32_t state_base_row = meta_word_to_row(meta_words[1]);
      const uint32_t msg_base_row = meta_word_to_row(meta_words[2]);
      int state_rows_needed = compact_payload_rows(state.dofs);
      int msg_rows_needed = compact_payload_rows(state.dofs);
      if ((meta_words[3] >> 16) != 0u) {
        state_rows_needed = static_cast<int>(((meta_words[3] >> 16) + 31u) >> 5);
      }
      if ((meta_words[3] & 0xFFFFu) != 0u) {
        msg_rows_needed = static_cast<int>(((meta_words[3] & 0xFFFFu) + 31u) >> 5);
      }

      if (var_idx == 0) {
        std::printf("GBP_WHITEBOX_ARE_DEBUG var0: var_id=%d state_row=%u msg_row=%u fan_in=%d msg_rows_needed=%d\n",
                    var_id, state_base_row, msg_base_row, fan_in, msg_rows_needed);
        std::array<uint32_t, kWordsPerRow> debug_beats{};
        read_spm_row_words(pe, kRequiredMessageBankLo, msg_base_row, &debug_beats);
        std::printf("GBP_WHITEBOX_ARE_DEBUG bank4_row%u_words: %08x %08x %08x %08x %08x %08x %08x %08x\n",
                    msg_base_row,
                    debug_beats[0], debug_beats[1], debug_beats[2], debug_beats[3],
                    debug_beats[4], debug_beats[5], debug_beats[6], debug_beats[7]);
      }

      if (state_base_row < kMaxRowsToScan) {
        std::vector<std::array<uint32_t, kWordsPerRow>> state_rows;
        if (read_spm_rows(pe, kRequiredStateBankHi, state_base_row, state_rows_needed, &state_rows)
            && !state_rows.empty() && !is_row_empty(state_rows[0])) {
          std::string decode_error;
          if (decode_state_from_rows(state_rows,
                                     state.dofs,
                                     &state.prior_eta,
                                     &state.prior_lam,
                                     &decode_error)) {
            vars_with_state++;
          }
        }
      }
      
      auto fit = var_to_factors.find(var_id);
      if (fit == var_to_factors.end() || fan_in == 0) continue;
      
      const std::vector<int>& adj_factors = fit->second;
      uint32_t msg_bank = kRequiredMessageBankLo; // Bank 4 for all messages
      
      // 从连续 rows 读取所有 messages
      for (int msg_idx = 0; msg_idx < fan_in; ++msg_idx) {
        if (static_cast<size_t>(msg_idx) >= adj_factors.size()) {
          break;
        }
        int factor_id = adj_factors[static_cast<size_t>(msg_idx)];
        uint32_t msg_row =
            msg_base_row + row_offset_from_index(static_cast<size_t>(msg_idx), msg_rows_needed);
        
        if (msg_row >= kMaxRowsToScan) continue;
        
        std::vector<std::array<uint32_t, kWordsPerRow>> msg_rows;
        if (!read_spm_rows(pe, msg_bank, msg_row, msg_rows_needed, &msg_rows) || msg_rows.empty()) {
          continue;
        }
        bool all_empty = true;
        for (const auto& row_words : msg_rows) {
          if (!is_row_empty(row_words)) {
            all_empty = false;
            break;
          }
        }
        if (all_empty) continue;
        
        DutVariableState::InboundMessage msg;
        msg.factor_id = factor_id;
        
        std::string decode_error;
        if (decode_message_from_rows(msg_rows, state.dofs, &msg.eta, &msg.lam, &decode_error)) {
          msg.msg_bank = static_cast<int>(msg_bank);
          msg.msg_row = static_cast<int>(msg_row);
          state.inbound_messages.push_back(msg);
        }
      }
      
      if (!state.inbound_messages.empty()) {
        vars_with_messages++;
      }
    }
    
    // 收集所有有效的 states
    for (auto& kv : var_states) {
      total_vars_checked++;
      auto& state = kv.second;
      // 只要有 state 或 messages 就添加
      if (state.prior_eta.size() > 0 || !state.inbound_messages.empty()) {
        out_states->push_back(state);
      }
    }
  }
  
  std::printf("GBP_WHITEBOX_ARE_DEBUG checked=%d with_state=%d with_messages=%d total_saved=%zu\n",
              total_vars_checked, vars_with_state, vars_with_messages, out_states->size());
  
  if (out_states->empty()) {
    *error = "no valid variable states read from DUT";
    return false;
  }
  
  return true;
}

// 直接从 DUT states 计算 ARE
static bool compute_are_from_dut_states(
    const std::vector<DutVariableState>& dut_states,
    const PartitionInfo& partition,
    double* out_are,
    double* out_energy,
    std::string* error) {
  
  Config config{};
  config.eta_damping = 0.4;
  config.beta = 0.01;
  config.num_undamped_iters = 6;
  config.min_linear_iters = 8;
  config.gauss_noise_std = 2.0;
  config.loss = HUBER;
  config.mahalanobis_threshold = 3.0;
  
  BAFactorGraph graph(0.0, 0.0, 0, 0);
  try {
    // 尝试从环境变量获取数据集路径
    const char* dataset_env = std::getenv("DATASET");
    std::string dataset_path = dataset_env ? dataset_env : kBaDatasetPath;
    graph = create_ba_graph(dataset_path, config);
  } catch (const std::exception& e) {
    *error = std::string("failed to create BA graph: ") + e.what();
    return false;
  }
  
  // 构建 variable_id -> DutVariableState 映射
  std::unordered_map<int, const DutVariableState*> state_map;
  for (const auto& state : dut_states) {
    state_map[state.variable_id] = &state;
  }
  
  // 应用 DUT 状态到 graph
  int updated_count = 0;
  int vars_missing_prior = 0;
  for (VariableNode* var : graph.get_var_nodes()) {
    int var_id = var->get_variable_ID();
    
    auto it = state_map.find(var_id);
    if (it == state_map.end()) {
      continue;
    }
    
    const DutVariableState* state = it->second;
    
    const Eigen::VectorXd original_eta = var->get_prior().get_eta();
    const Eigen::MatrixXd original_lam = var->get_prior().get_lam();
    const bool has_valid_prior =
        (state->prior_eta.size() == original_eta.size())
        && (state->prior_lam.rows() == original_lam.rows())
        && (state->prior_lam.cols() == original_lam.cols());
    
    // 仅在 DUT 读回的 prior 维度完整时才覆盖；否则保留图中的原始 prior，
    // 让后处理继续评估 messages 路径，而不是被 Eigen 维度断言提前打断。
    if (has_valid_prior) {
      var->set_prior_eta(state->prior_eta);
      var->set_prior_lam(state->prior_lam);
    } else {
      vars_missing_prior++;
      if (vars_missing_prior <= 10) {
        std::printf(
            "GBP_WHITEBOX_ARE_DEBUG missing_prior var=%d dut_eta=%ld dut_lam=%ldx%ld original_eta=%ld original_lam=%ldx%ld\n",
            var_id,
            static_cast<long>(state->prior_eta.size()),
            static_cast<long>(state->prior_lam.rows()),
            static_cast<long>(state->prior_lam.cols()),
            static_cast<long>(original_eta.size()),
            static_cast<long>(original_lam.rows()),
            static_cast<long>(original_lam.cols()));
      }
    }
    
    // 检查 prior 是否有 NaN
    if (!var->get_prior().get_eta().allFinite() || !var->get_prior().get_lam().allFinite()) {
      std::printf("GBP_WHITEBOX_ARE_DEBUG NaN in prior for var=%d\n", var_id);
    }
    
    // 构建 inbound messages
    std::vector<NdimGaussian> inbound;
    for (const auto& msg : state->inbound_messages) {
      NdimGaussian msg_gaussian(state->dofs, msg.eta, msg.lam);
      inbound.push_back(msg_gaussian);
    }
    
    // 更新 belief
    var->update_belief(inbound);
    
    // 检查 belief 是否有 NaN
    if (!var->get_belief().get_eta().allFinite() || !var->get_belief().get_lam().allFinite()) {
      std::printf("GBP_WHITEBOX_ARE_DEBUG NaN in belief after update for var=%d\n", var_id);
    }
    
    // 更新 factor 的 adjacent beliefs
    const std::vector<FactorNode*> adj = var->get_adj_fac_nodes();
    for (FactorNode* factor : adj) {
      const std::vector<int> adj_ids = factor->get_adj_vIDs();
      const auto pos = std::find(adj_ids.begin(), adj_ids.end(), var_id);
      if (pos != adj_ids.end()) {
        const size_t idx = std::distance(adj_ids.begin(), pos);
        factor->get_adj_beliefs()[idx].set_eta(var->get_belief().get_eta());
        factor->get_adj_beliefs()[idx].set_lam(var->get_belief().get_lam());
      }
    }
    
    updated_count++;
  }
  
  std::printf("GBP_WHITEBOX_ARE_DEBUG updated_variables=%d/%zu\n", 
              updated_count, dut_states.size());
  std::printf("GBP_WHITEBOX_ARE_DEBUG vars_missing_prior=%d\n", vars_missing_prior);
  
  // 检查是否有 NaN 在变量和 factor messages 中
  int nan_vars = 0;
  for (VariableNode* var : graph.get_var_nodes()) {
    if (!var->get_belief().get_eta().allFinite() || 
        !var->get_belief().get_lam().allFinite()) {
      nan_vars++;
    }
  }
  int nan_factor_msgs = 0;
  for (auto* f : graph.get_fac_nodes()) {
    for (const auto& msg : f->get_messages()) {
      if (!msg.get_eta().allFinite() || !msg.get_lam().allFinite()) {
        nan_factor_msgs++;
        break;
      }
    }
  }
  std::printf("GBP_WHITEBOX_ARE_DEBUG nan_vars=%d nan_factor_msgs=%d\n", nan_vars, nan_factor_msgs);
  
  if (updated_count == 0) {
    *error = "no variables were updated from DUT states";
    return false;
  }
  
  // 检查 compute 之前 factor 的 adjacent beliefs 是否有 NaN 或奇异矩阵
  int nan_adj_beliefs = 0;
  int singular_lam = 0;
  int total_adj_beliefs = 0;
  for (auto* f : graph.get_fac_nodes()) {
    for (const auto& belief : f->get_adj_beliefs()) {
      total_adj_beliefs++;
      if (!belief.get_eta().allFinite() || !belief.get_lam().allFinite()) {
        nan_adj_beliefs++;
        continue;
      }
      // Check if lam is singular (determinant close to 0)
      double det = belief.get_lam().determinant();
      if (std::abs(det) < 1e-10) {
        singular_lam++;
        std::printf("GBP_WHITEBOX_ARE_DEBUG singular_lam_det=%e dofs=%zu\n", det, belief.get_lam().rows());
        if (singular_lam <= 3) {
          std::printf("GBP_WHITEBOX_ARE_DEBUG singular_lam_matrix=[");
          for (int r = 0; r < belief.get_lam().rows(); ++r) {
            if (r > 0) std::printf("], [");
            for (int c = 0; c < belief.get_lam().cols(); ++c) {
              if (c > 0) std::printf(",");
              std::printf("%.3f", belief.get_lam()(r,c));
            }
          }
          std::printf("]]\n");
        }
      }
    }
  }
  std::printf("GBP_WHITEBOX_ARE_DEBUG total_adj_beliefs=%d nan=%d singular=%d\n", total_adj_beliefs, nan_adj_beliefs, singular_lam);
  
  // 同步 factor 状态
  graph.compute_all_factors();
  
  // 检查 factor 是否有 NaN after compute
  int nan_factors_after_compute = 0;
  for (auto* f : graph.get_fac_nodes()) {
    if (!f->get_factor().get_eta().allFinite() || !f->get_factor().get_lam().allFinite()) {
      nan_factors_after_compute++;
    }
  }
  std::printf("GBP_WHITEBOX_ARE_DEBUG nan_factors_after_compute=%d\n", nan_factors_after_compute);
  
  graph.robustify_all_factors();
  
  // 检查 factor 是否有 NaN after robust
  int nan_factors_after_robust = 0;
  for (auto* f : graph.get_fac_nodes()) {
    if (!f->get_factor().get_eta().allFinite() || !f->get_factor().get_lam().allFinite()) {
      nan_factors_after_robust++;
    }
  }
  std::printf("GBP_WHITEBOX_ARE_DEBUG nan_factors_after_robust=%d\n", nan_factors_after_robust);
  
  // 调试：检查 factor 和 variable 数量
  int total_factors = graph.get_fac_nodes().size();
  int reproj_factors = 0;
  for (auto* f : graph.get_fac_nodes()) {
    if (dynamic_cast<ReprojectionFactor*>(f) != nullptr) {
      reproj_factors++;
    }
  }
  int total_vars = graph.get_var_nodes().size();
  std::printf("GBP_WHITEBOX_ARE_DEBUG vars=%d factors=%d reproj=%d\n", total_vars, total_factors, reproj_factors);
  
  *out_are = graph.are();
  *out_energy = graph.energy();
  
  std::printf("GBP_WHITEBOX_ARE_DEBUG are=%f energy=%f\n", *out_are, *out_energy);
  
  if (!std::isfinite(*out_are) || !std::isfinite(*out_energy)) {
    *error = "computed ARE/energy is not finite";
    return false;
  }
  
  return true;
}

// ============================================

static bool build_reference_ba_snapshot(const RuntimeConfig& cfg,
                                        const std::unordered_map<int, int>& var_owner,
                                        std::vector<GraphVariableRecord>* out,
                                        std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }

  Config config{};
  config.eta_damping = 0.4;
  config.beta = 0.01;
  config.num_undamped_iters = 6;
  config.min_linear_iters = 8;
  config.gauss_noise_std = 2.0;
  config.loss = HUBER;
  config.mahalanobis_threshold = 3.0;

  BAFactorGraph graph(0.0, 0.0, 0, 0);
  try {
    graph = create_ba_graph(cfg.dataset_path, config);
  } catch (const std::exception& e) {
    *error = std::string("构造 BA reference graph 失败: ") + e.what();
    return false;
  }

  graph.generate_priors_var(100);
  graph.update_all_beliefs();
  for (int iter = 0; iter < kFixedIters - 1; ++iter) {
    graph.synchronous_iteration(true, true);
  }

  out->clear();
  out->reserve(graph.get_var_nodes().size());
  for (VariableNode* var : graph.get_var_nodes()) {
    const int var_id = var->get_variable_ID();
    const auto owner_it = var_owner.find(var_id);
    if (owner_it == var_owner.end()) {
      *error = "partition 缺少 variable owner, var_id=" + std::to_string(var_id);
      return false;
    }

    GraphVariableRecord record{};
    record.pe = owner_it->second;
    record.variable_id = var_id;
    record.dofs = var->get_dofs();
    record.prior_eta = var->get_prior().get_eta();
    record.prior_lam = var->get_prior().get_lam();

    const std::vector<FactorNode*> adj = var->get_adj_fac_nodes();
    record.inbound_messages.reserve(adj.size());
    for (FactorNode* factor : adj) {
      const std::vector<int> adj_var_ids = factor->get_adj_vIDs();
      const auto pos = std::find(adj_var_ids.begin(), adj_var_ids.end(), var_id);
      if (pos == adj_var_ids.end()) {
        *error = "reference graph adjacency mismatch, var_id=" + std::to_string(var_id)
            + " factor_id=" + std::to_string(factor->get_factor_id());
        return false;
      }
      const size_t msg_idx = static_cast<size_t>(std::distance(adj_var_ids.begin(), pos));
      const std::vector<NdimGaussian> messages = factor->get_messages();
      if (msg_idx >= messages.size()) {
        *error = "reference graph message index 越界, var_id=" + std::to_string(var_id)
            + " factor_id=" + std::to_string(factor->get_factor_id());
        return false;
      }

      GraphMessageRecord msg{};
      msg.factor_id = factor->get_factor_id();
      msg.variable_id = var_id;
      msg.dofs = record.dofs;
      msg.eta = messages[msg_idx].get_eta();
      msg.lam = messages[msg_idx].get_lam();
      record.inbound_messages.push_back(msg);
    }
    out->push_back(std::move(record));
  }
  std::sort(out->begin(),
            out->end(),
            [](const GraphVariableRecord& lhs, const GraphVariableRecord& rhs) {
              return lhs.variable_id < rhs.variable_id;
            });
  return true;
}

static bool write_terminal_dump_from_store(const std::string& path,
                                           const RuntimeConfig& cfg,
                                           const PartitionInfo& partition,
                                           const std::vector<DutVariableState>& dut_states) {
  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    std::fprintf(stderr, "FAIL: 无法写 terminal dump: %s\n", path.c_str());
    return false;
  }

  // 构建 variable_id -> local_index 映射
  std::unordered_map<int, int> var_to_local_index;
  for (size_t pe = 0; pe < partition.var_mapping.size(); ++pe) {
    for (size_t idx = 0; idx < partition.var_mapping[pe].size(); ++idx) {
      int var_id = partition.var_mapping[pe][idx];
      var_to_local_index[var_id] = static_cast<int>(idx);
    }
  }

  ofs << "{\n"
      << "  \"test\": \"gbp_pe_mesh_whitebox_convergence\",\n"
      << "  \"workload\": \"" << cfg.workload << "\",\n"
      << "  \"iterations\": " << kFixedIters << ",\n"
      << "  \"snapshot\": {\"seq\": 1, \"cycle\": 0},\n"
      << "  \"snapshot_seq\": 1,\n"
      << "  \"snapshot_cycle\": 0,\n"
      << "  \"graph\": {\n"
      << "    \"problem\": \"bundle_adjustment\",\n"
      << "    \"dataset\": \"" << kBaDatasetPath << "\",\n"
      << "    \"spacing\": 1.0,\n"
      << "    \"init_noise_std\": 1.0,\n"
      << "    \"seed\": " << cfg.seed << ",\n"
      << "    \"anchor_std\": 0.001\n"
      << "  },\n"
      << "  \"coverage_contract\": {\"required_state_banks\": [1, 2, 3], "
      << "\"required_message_banks\": [4, 5, 6, 7], "
      << "\"required_row\": " << kRequiredRowIndex << ", "
      << "\"required_beats_per_row\": " << kWordsPerRow << "},\n"
      << "  \"terminal_dump\": [\n";

  for (uint32_t pe = 0; pe < cfg.pe_count; ++pe) {
    ofs << "    {\"pe\": " << pe << ",\n";
    ofs << "      \"required_rows\": [\n";
    bool first_row = true;
    for (uint32_t bank = kRequiredStateBankLo; bank <= kRequiredMessageBankHi; ++bank) {
      std::array<uint32_t, kWordsPerRow> beats{};
      if (!read_spm_row_words(pe, bank, kRequiredRowIndex, &beats)) {
        std::fprintf(stderr, "FAIL: 无法读取 terminal row pe=%u bank=%u\n", pe, bank);
        return false;
      }
      if (!first_row) {
        ofs << ",\n";
      }
      first_row = false;
      ofs << "        {\"bank\": " << bank
          << ", \"class\": \"" << ((bank <= kRequiredStateBankHi) ? "state" : "message") << "\""
          << ", \"row\": " << kRequiredRowIndex
          << ", \"address\": " << ((kRequiredRowIndex << (kRowBytesLg + kBankWidth)) | (bank << kRowBytesLg))
          << ", \"snapshot_seq\": 1"
          << ", \"snapshot_cycle\": 0"
          << ", \"beats\": [";
      for (size_t w = 0; w < beats.size(); ++w) {
        ofs << beats[w];
        if (w + 1 != beats.size()) {
          ofs << ", ";
        }
      }
      ofs << "]}";
    }
    ofs << "\n      ]\n";
    ofs << "    }" << (pe + 1u == cfg.pe_count ? "\n" : ",\n");
  }

  ofs << "  ],\n"
      << "  \"variable_snapshots\": [\n";

  for (size_t i = 0; i < dut_states.size(); ++i) {
    const DutVariableState& state = dut_states[i];
    int local_idx = var_to_local_index.count(state.variable_id) ? 
                    var_to_local_index[state.variable_id] : 0;
    
    ofs << "    {\n"
        << "      \"pe\": " << state.pe << ",\n"
        << "      \"variable_id\": " << state.variable_id << ",\n"
        << "      \"dofs\": " << state.dofs << ",\n"
        << "      \"state_bank\": " << kRequiredStateBankHi << ",\n"
        << "      \"state_row\": " << local_idx << ",\n"
        << "      \"snapshot_seq\": 1,\n"
        << "      \"snapshot_cycle\": 0,\n"
        << "      \"prior_eta\": ";
    emit_json_vector(ofs, state.prior_eta);
    ofs << ",\n"
        << "      \"prior_lam\": ";
    emit_json_matrix(ofs, state.prior_lam);
    ofs << ",\n"
        << "      \"inbound_messages\": [\n";

    for (size_t j = 0; j < state.inbound_messages.size(); ++j) {
      const auto& msg = state.inbound_messages[j];
      ofs << "        {\n"
          << "          \"factor_id\": " << msg.factor_id << ",\n"
          << "          \"variable_id\": " << state.variable_id << ",\n"
          << "          \"message_slot\": 0,\n"
          << "          \"msg_bank\": " << msg.msg_bank << ",\n"
          << "          \"msg_row\": " << msg.msg_row << ",\n"
          << "          \"msg_beat\": 0,\n"
          << "          \"schema_version\": 0,\n"
          << "          \"direction\": \"factor_to_var\",\n"
          << "          \"dim\": " << state.dofs << ",\n"
          << "          \"eta_len\": " << state.dofs << ",\n"
          << "          \"lam_len\": " << (state.dofs * (state.dofs + 1) / 2) << ",\n"
          << "          \"segment_idx\": 0,\n"
          << "          \"segment_count\": 1,\n"
          << "          \"segment_payload_words\": 0,\n"
          << "          \"payload_words\": [],\n"
          << "          \"msg_eta\": ";
      emit_json_vector(ofs, msg.eta);
      ofs << ",\n"
          << "          \"msg_lam\": ";
      emit_json_matrix(ofs, msg.lam);
      ofs << "\n"
          << "        }" << (j + 1 == state.inbound_messages.size() ? "\n" : ",\n");
    }
    ofs << "      ]\n"
        << "    }" << (i + 1 == dut_states.size() ? "\n" : ",\n");
  }

  ofs << "  ],\n"
      << "  \"local_semantic_messages\": []\n"
      << "}\n";
  return true;
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  RuntimeConfig cfg{};
  if (!load_runtime_config(&cfg)) {
    return 1;
  }

  std::string dataset_text;
  if (!read_text_file(cfg.dataset_path, &dataset_text)) {
    std::fprintf(stderr, "FAIL: 无法打开 dataset 文件: %s\n", cfg.dataset_path.c_str());
    return 1;
  }
  if (dataset_text.empty()) {
    std::fprintf(stderr, "FAIL: dataset 文件为空: %s\n", cfg.dataset_path.c_str());
    return 1;
  }
  if (!validate_partition_contract(cfg)) {
    return 1;
  }
  if (cfg.workload != "bal_fr1desk_small") {
    std::fprintf(stderr, "FAIL: 当前白盒收敛测试仅支持 WORKLOAD=bal_fr1desk_small\n");
    return 1;
  }
  if (cfg.dataset_path != kBaDatasetPath && cfg.dataset_path.find(kBaDatasetPath) == std::string::npos) {
    std::fprintf(stderr, "FAIL: 当前白盒收敛测试仅支持 DATASET=%s\n", kBaDatasetPath);
    return 1;
  }

  PartitionInfo partition{};
  if (!load_partition_info(cfg.partition_path.c_str(), cfg.pe_count, &partition)) {
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
  const std::vector<CrossEdge> cross_edges = collect_cross_edges(partition, factor_owner, var_owner);
  if (cross_edges.empty()) {
    std::fprintf(stderr, "FAIL: partition 中没有跨 PE factor_var_edges\n");
    return 1;
  }

  g_mesh_x = cfg.mesh_x;
  g_cycle_count = 0;
  g_scope_cache.clear();
  g_pmem_words.clear();

  auto* dut = new Dut;
  reset_dut(dut);

  // ============================================
  // SPM 自检 + 预加载所有 variable 的 State 和 Messages
  // ============================================
  
  // 构建 factor -> variable 邻接关系，用于预加载 messages
  std::unordered_map<int, std::vector<int>> var_to_factors;
  for (const auto& edge : partition.edges) {
    int factor_id = edge.first;
    int var_id = edge.second;
    var_to_factors[var_id].push_back(factor_id);
  }
  
  // 构建参考 graph 获取初始值
  Config init_config{};
  init_config.eta_damping = 0.4;
  init_config.beta = 0.01;
  init_config.num_undamped_iters = 6;
  init_config.min_linear_iters = 8;
  init_config.gauss_noise_std = 2.0;
  init_config.loss = HUBER;
  init_config.mahalanobis_threshold = 3.0;
  
  BAFactorGraph init_graph(0.0, 0.0, 0, 0);
  try {
    const char* dataset_env = std::getenv("DATASET");
    std::string dataset_path = dataset_env ? dataset_env : kBaDatasetPath;
    init_graph = create_ba_graph(dataset_path, init_config);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: 无法创建初始化 graph: %s\n", e.what());
    delete dut;
    return 1;
  }
  
  // 初始化 graph
  init_graph.generate_priors_var(100);
  init_graph.update_all_beliefs();

  // 构建 variable_id -> variable_node 映射
  std::unordered_map<int, VariableNode*> var_node_map;
  for (VariableNode* var : init_graph.get_var_nodes()) {
    var_node_map[var->get_variable_ID()] = var;
  }

  std::vector<GraphVariableRecord> reference_snapshot;
  std::string reference_error;
  if (!build_reference_ba_snapshot(cfg, var_owner, &reference_snapshot, &reference_error)) {
    std::fprintf(stderr, "FAIL: 无法构造 reference snapshot: %s\n", reference_error.c_str());
    delete dut;
    return 1;
  }

  std::unordered_map<int, const GraphVariableRecord*> reference_var_map;
  reference_var_map.reserve(reference_snapshot.size());
  int reference_msg_nonzero_words = 0;
  int reference_msg_total_words = 0;
  int reference_msg_debug_count = 0;
  for (const auto& record : reference_snapshot) {
    reference_var_map.emplace(record.variable_id, &record);
    for (const auto& msg : record.inbound_messages) {
      std::vector<std::array<uint32_t, kWordsPerRow>> msg_rows;
      pack_compact_payload_rows(msg.eta, msg.lam, &msg_rows);
      reference_msg_total_words += static_cast<int>(msg_rows.size()) << 3;
      reference_msg_nonzero_words += count_nonzero_words(msg_rows);
      if (reference_msg_debug_count < 6) {
        std::printf(
            "GBP_WHITEBOX_ARE_DEBUG reference_msg_sample var=%d factor=%d eta_norm=%e lam_norm=%e nonzero=%d total_words=%d\n",
            record.variable_id,
            msg.factor_id,
            msg.eta.norm(),
            msg.lam.norm(),
            count_nonzero_words(msg_rows),
            static_cast<int>(msg_rows.size()) << 3);
        reference_msg_debug_count++;
      }
    }
  }
  std::printf("GBP_WHITEBOX_ARE_DEBUG reference_snapshot vars=%zu msg_nonzero=%d total_words=%d\n",
              reference_snapshot.size(),
              reference_msg_nonzero_words,
              reference_msg_total_words);
  
  for (uint32_t pe = 0; pe < cfg.pe_count; ++pe) {
    // SPM 自检
    const uint32_t meta_marker = 0xABCD0000u | pe;
    const uint32_t state_marker = 0x12340000u | ((cfg.seed + pe) & 0xFFFFu);
    wb_spm_write_word(pe, 0u, 0u, 0u, meta_marker);
    wb_spm_write_word(pe, 1u, 0u, 0u, state_marker);
    const uint32_t meta_observed = wb_spm_read_word(pe, 0u, 0u, 0u);
    const uint32_t state_observed = wb_spm_read_word(pe, 1u, 0u, 0u);
    if (meta_observed != meta_marker || state_observed != state_marker) {
      std::fprintf(stderr,
                   "FAIL: 白盒 SPM 自检失败 pe=%u meta=0x%08x/0x%08x state=0x%08x/0x%08x\n",
                   pe,
                   meta_marker,
                   meta_observed,
                   state_marker,
                   state_observed);
      delete dut;
      return 1;
    }
    
    // 清除自检 marker，避免被 DUT 误认为是 meta 数据
    wb_spm_write_word(pe, 0u, 0u, 0u, 0u);
    wb_spm_write_word(pe, 1u, 0u, 0u, 0u);
    
    // 预加载此 PE 的所有 variables 和 factors
    const auto& pe_vars = partition.var_mapping[pe];
    const auto& pe_factors = partition.fac_mapping[pe];
    
    // state/message 在 whitebox SPM 中独立紧凑排布，避免 message 区与 state 区或相邻变量重叠。
    std::vector<int> state_rows_needed;
    std::vector<int> state_start_row;
    std::vector<int> message_rows_needed;
    std::vector<int> message_start_row;
    int state_cumulative = 0;
    int message_cumulative = 0;
    for (size_t var_idx = 0; var_idx < pe_vars.size(); ++var_idx) {
      int var_id = pe_vars[var_idx];
      const auto ref_it = reference_var_map.find(var_id);
      auto var_it = var_node_map.find(var_id);
      int dofs = (var_it != var_node_map.end()) ? var_it->second->get_dofs() : 2;
      if (ref_it != reference_var_map.end() && ref_it->second != nullptr) {
        dofs = ref_it->second->dofs;
      }
      int rows_needed = compact_payload_rows(dofs);
      int fan_in = 0;
      if (ref_it != reference_var_map.end() && ref_it->second != nullptr) {
        fan_in = static_cast<int>(ref_it->second->inbound_messages.size());
      } else {
        const auto factor_it = var_to_factors.find(var_id);
        if (factor_it != var_to_factors.end()) {
          fan_in = static_cast<int>(factor_it->second.size());
        }
      }
      state_rows_needed.push_back(rows_needed);
      state_start_row.push_back(state_cumulative);
      message_rows_needed.push_back(rows_needed);
      message_start_row.push_back(message_cumulative);
      state_cumulative += rows_needed;
      message_cumulative += repeated_row_span(fan_in, rows_needed);
    }

	    std::vector<int> factor_payload_start_row;
	    std::vector<int> factor_message_start_row;
	    int factor_payload_cumulative = 0;
	    int factor_message_cumulative = message_cumulative;
	    for (size_t fac_idx = 0; fac_idx < pe_factors.size(); ++fac_idx) {
	      factor_payload_start_row.push_back(factor_payload_cumulative);
	      factor_message_start_row.push_back(factor_message_cumulative);
	      factor_payload_cumulative += 1;
	      factor_message_cumulative += 1;
	    }

	    int preload_msg_nonzero_words = 0;
	    int preload_msg_total_words = 0;
	    int preload_msg_debug_count = 0;
    
    for (size_t var_idx = 0; var_idx < pe_vars.size(); ++var_idx) {
      int var_id = pe_vars[var_idx];
      uint32_t state_row = static_cast<uint32_t>(state_start_row[var_idx]);
      uint32_t message_row_base = static_cast<uint32_t>(message_start_row[var_idx]);
      const auto ref_it = reference_var_map.find(var_id);
      if (ref_it == reference_var_map.end() || ref_it->second == nullptr) {
        std::fprintf(stderr, "FAIL: reference snapshot 缺少 variable=%d\n", var_id);
        delete dut;
        return 1;
      }

      auto var_it = var_node_map.find(var_id);
      if (var_it == var_node_map.end()) continue;

      const GraphVariableRecord* ref_var = ref_it->second;
      int dofs = ref_var->dofs;
      int rows_needed = state_rows_needed[var_idx];
      
      // 写入 State (Bank 3)
      // prior_eta 后接紧凑上三角 prior_lam
      const Eigen::VectorXd& prior_eta = ref_var->prior_eta;
      const Eigen::MatrixXd& prior_lam = ref_var->prior_lam;

      std::vector<std::array<uint32_t, kWordsPerRow>> state_rows;
      pack_compact_payload_rows(prior_eta, prior_lam, &state_rows);
      for (size_t r = 0; r < state_rows.size(); ++r) {
        for (int w = 0; w < static_cast<int>(kWordsPerRow); ++w) {
          wb_spm_write_word(pe,
                            kRequiredStateBankHi,
                            state_row + static_cast<uint32_t>(r),
                            static_cast<uint32_t>(w),
                            state_rows[r][static_cast<size_t>(w)]);
        }
      }
      
      // 验证：立即读取并检查编码误差
      if (var_idx < 3) {
        std::vector<std::array<uint32_t, kWordsPerRow>> read_rows;
        read_spm_rows(pe, kRequiredStateBankHi, state_row, rows_needed, &read_rows);
        Eigen::VectorXd read_eta;
        Eigen::MatrixXd read_lam;
        std::string decode_error;
        decode_state_from_rows(read_rows, dofs, &read_eta, &read_lam, &decode_error);
        
        double eta_diff = (read_eta - prior_eta).norm();
        double lam_diff = (read_lam - prior_lam).norm();
        std::printf("GBP_WHITEBOX_ARE_DEBUG preload_verify var=%d eta_diff=%e lam_diff=%e\n", 
                    var_id, eta_diff, lam_diff);
      }
      
      // 写入 Messages (Bank 4，连续存储)
      const auto& inbound_messages = ref_var->inbound_messages;

      // 计算 message 需要的行数
      int msg_rows_needed = message_rows_needed[var_idx];
      
      for (size_t msg_idx = 0; msg_idx < inbound_messages.size(); ++msg_idx) {
        const auto& msg = inbound_messages[msg_idx];
        const Eigen::VectorXd& msg_eta = msg.eta;
        const Eigen::MatrixXd& msg_lam = msg.lam;
        
        // 所有 messages 存储在 bank 4，连续行
        uint32_t msg_bank = kRequiredMessageBankLo; // Bank 4
        uint32_t msg_row = message_row_base + row_offset_from_index(msg_idx, msg_rows_needed);

        std::vector<std::array<uint32_t, kWordsPerRow>> msg_rows;
        pack_compact_payload_rows(msg_eta, msg_lam, &msg_rows);
        preload_msg_total_words += static_cast<int>(msg_rows.size()) << 3;
        preload_msg_nonzero_words += count_nonzero_words(msg_rows);

        for (size_t r = 0; r < msg_rows.size(); ++r) {
          for (int w = 0; w < static_cast<int>(kWordsPerRow); ++w) {
            wb_spm_write_word(pe,
                              msg_bank,
                              msg_row + static_cast<uint32_t>(r),
                              static_cast<uint32_t>(w),
                              msg_rows[r][static_cast<size_t>(w)]);
          }
        }

        if (preload_msg_debug_count < 6) {
          std::vector<std::array<uint32_t, kWordsPerRow>> msg_rows_readback;
          read_spm_rows(pe, msg_bank, msg_row, msg_rows_needed, &msg_rows_readback);
          Eigen::VectorXd read_eta;
          Eigen::MatrixXd read_lam;
          std::string decode_error;
          if (decode_message_from_rows(msg_rows_readback, dofs, &read_eta, &read_lam, &decode_error)) {
            std::printf(
                "GBP_WHITEBOX_ARE_DEBUG preload_msg_verify pe=%u var=%d factor=%d row=%u eta_norm=%e lam_norm=%e eta_diff=%e lam_diff=%e nonzero=%d total_words=%d\n",
                pe,
                var_id,
                msg.factor_id,
                msg_row,
                msg_eta.norm(),
                msg_lam.norm(),
                (read_eta - msg_eta).norm(),
                (read_lam - msg_lam).norm(),
                count_nonzero_words(msg_rows),
                static_cast<int>(msg_rows.size()) << 3);
          } else {
            std::printf(
                "GBP_WHITEBOX_ARE_DEBUG preload_msg_verify_fail pe=%u var=%d factor=%d row=%u err=%s\n",
                pe,
                var_id,
                msg.factor_id,
                msg_row,
                decode_error.c_str());
          }
          preload_msg_debug_count++;
        }
      }
    }

	    std::printf(
	        "GBP_WHITEBOX_ARE_DEBUG preload_msg_nonzero pe=%u nonzero=%d/%d state_rows=%d message_rows=%d\n",
	        pe,
	        preload_msg_nonzero_words,
	        preload_msg_total_words,
	        state_cumulative,
	        message_cumulative);

	    std::array<uint32_t, kWordsPerRow> dummy_factor_row{};
	    for (size_t fac_idx = 0; fac_idx < pe_factors.size(); ++fac_idx) {
	      uint32_t payload_row = static_cast<uint32_t>(factor_payload_start_row[fac_idx]);
	      uint32_t dummy_msg_row = static_cast<uint32_t>(factor_message_start_row[fac_idx]);
	      for (uint32_t w = 0; w < kWordsPerRow; ++w) {
	        wb_spm_write_word(pe,
	                          kRequiredStateBankLo,
	                          payload_row,
	                          w,
	                          dummy_factor_row[static_cast<size_t>(w)]);
	        wb_spm_write_word(pe,
	                          kRequiredMessageBankLo,
	                          dummy_msg_row,
	                          w,
	                          dummy_factor_row[static_cast<size_t>(w)]);
	      }
	    }
	    
	    // ============================================
	    // 写入 META 数据到 Bank 0
	    // ============================================
	    uint32_t meta_rows_written = 0;
	    uint32_t meta_rows_missing = 0;
	    const uint32_t header_w0 =
	        (static_cast<uint32_t>(pe_factors.size()) << 16)
	        | static_cast<uint32_t>(pe_vars.size());
	    wb_spm_write_word(pe, 0u, 0u, 0u, header_w0);
	    wb_spm_write_word(pe, 0u, 0u, 1u, 0u);
	    wb_spm_write_word(pe, 0u, 0u, 2u, 0u);
	    wb_spm_write_word(pe, 0u, 0u, 3u, 0u);
	    wb_spm_write_word(pe, 0u, 0u, 4u, 0u);
	    meta_rows_written += 1;
	    for (size_t var_idx = 0; var_idx < pe_vars.size(); ++var_idx) {
	      int var_id = pe_vars[var_idx];
      const auto ref_it = reference_var_map.find(var_id);
      if (ref_it == reference_var_map.end() || ref_it->second == nullptr) {
        meta_rows_missing += 1;
        continue;
      }
      auto var_meta_it = var_node_map.find(var_id);
      if (var_meta_it == var_node_map.end() || var_meta_it->second == nullptr) {
        meta_rows_missing += 1;
        continue;
      }

      const GraphVariableRecord* ref_var = ref_it->second;
      int dofs = ref_var->dofs;
      int adj_count = static_cast<int>(ref_var->inbound_messages.size());
      
      // META word 0: [7:0]=txn_id, [8]=is_factor(0=var), [11:9]=dofs, [15:12]=adj_count
      uint32_t meta_w0 = (static_cast<uint32_t>(var_id) & 0xFF) |  // txn_id
                         (0u << 8) |                               // is_factor=0 (variable)
                         ((static_cast<uint32_t>(dofs) & 0x7) << 9) |
                         ((static_cast<uint32_t>(adj_count) & 0xF) << 12);
      
      // META 地址字段沿用 RTL 约定：
      // word1/word2 的 [31:12] 存 20b base_addr，真正的 row 位在 base_addr[19:8]。
      uint32_t state_row = static_cast<uint32_t>(state_start_row[var_idx]);
      uint32_t state_base_addr = (state_row << kSpmRowShift);
	      uint32_t meta_w1 = (state_base_addr << 12) | (kRequiredStateBankHi << 9);
	      
	      uint32_t msg_row = static_cast<uint32_t>(message_start_row[var_idx]);
	      uint32_t msg_base_addr = (msg_row << kSpmRowShift);
	      uint32_t meta_w2 = (msg_base_addr << 12) | (kRequiredMessageBankLo << 9);
      
      // Calculate transfer bytes
      int state_words = compact_payload_words(dofs);
      int msg_words = compact_payload_words(dofs);
      int state_xfer = state_words << 2;  // 4 bytes per word
      // Note: msg_xfer is for a SINGLE message read, not all messages
      // control_unit reads messages one by one in followup loop
      int msg_xfer = msg_words << 2;
      
      // META word 3: [31:16]=state_xfer_bytes, [15:0]=message_xfer_bytes
      uint32_t meta_w3 = ((static_cast<uint32_t>(state_xfer) & 0xFFFF) << 16) |
                         (static_cast<uint32_t>(msg_xfer) & 0xFFFF);
      
      // META word 4: [31:24]=state_step_bytes, [23:16]=message_step_bytes
      uint32_t meta_w4 = (static_cast<uint32_t>(state_xfer) << 24) |
                         (static_cast<uint32_t>(msg_xfer) << 16);
      
	      const uint32_t meta_row = static_cast<uint32_t>(var_idx + 1);
	      wb_spm_write_word(pe, 0u, meta_row, 0u, meta_w0);
	      wb_spm_write_word(pe, 0u, meta_row, 1u, meta_w1);
	      wb_spm_write_word(pe, 0u, meta_row, 2u, meta_w2);
	      wb_spm_write_word(pe, 0u, meta_row, 3u, meta_w3);
	      wb_spm_write_word(pe, 0u, meta_row, 4u, meta_w4);
	      meta_rows_written += 1;
      
      // Debug: 打印第一个 variable 的 META
      if (var_idx == 0) {
        std::printf("GBP_META_WRITE_DEBUG pe=%u var=%d meta=[%08x %08x %08x %08x %08x]\n",
                    pe, var_id, meta_w0, meta_w1, meta_w2, meta_w3, meta_w4);
	      }
	    }

	    for (size_t fac_idx = 0; fac_idx < pe_factors.size(); ++fac_idx) {
	      const int factor_id = pe_factors[fac_idx];
	      const uint32_t payload_row = static_cast<uint32_t>(factor_payload_start_row[fac_idx]);
	      const uint32_t dummy_msg_row = static_cast<uint32_t>(factor_message_start_row[fac_idx]);
	      const uint32_t payload_base_addr = (payload_row << kSpmRowShift);
	      const uint32_t dummy_msg_base_addr = (dummy_msg_row << kSpmRowShift);
	      const uint32_t meta_row =
	          1u + static_cast<uint32_t>(pe_vars.size()) + static_cast<uint32_t>(fac_idx);
	      const uint32_t factor_meta_w0 =
	          (static_cast<uint32_t>(factor_id) & 0xFFu)
	          | (1u << 8)
	          | (2u << 9)
	          | (0u << 12);
	      const uint32_t factor_meta_w1 =
	          (payload_base_addr << 12) | (kRequiredStateBankLo << 9);
	      const uint32_t factor_meta_w2 =
	          (dummy_msg_base_addr << 12) | (kRequiredMessageBankLo << 9);
	      const uint32_t factor_meta_w3 = (20u << 16) | 20u;
	      const uint32_t factor_meta_w4 = (20u << 24) | (20u << 16);

	      wb_spm_write_word(pe, 0u, meta_row, 0u, factor_meta_w0);
	      wb_spm_write_word(pe, 0u, meta_row, 1u, factor_meta_w1);
	      wb_spm_write_word(pe, 0u, meta_row, 2u, factor_meta_w2);
	      wb_spm_write_word(pe, 0u, meta_row, 3u, factor_meta_w3);
	      wb_spm_write_word(pe, 0u, meta_row, 4u, factor_meta_w4);
	      meta_rows_written += 1;
	    }
	    
	    std::printf(
	        "GBP_WHITEBOX_ARE_DEBUG preloaded pe=%u variables=%zu factors=%zu meta_rows=%u missing_vars=%u factor_payload_rows=%d factor_message_rows=%d\n",
	        pe,
	        pe_vars.size(),
	        pe_factors.size(),
	        meta_rows_written,
	        meta_rows_missing,
	        factor_payload_cumulative,
	        factor_message_cumulative);
	    const uint32_t meta_rows_total =
	        1u + static_cast<uint32_t>(pe_vars.size()) + static_cast<uint32_t>(pe_factors.size());
	    for (uint32_t row_dbg = 0; row_dbg < 12 && row_dbg < meta_rows_total; ++row_dbg) {
	      std::printf("GBP_META_PRELOAD_ROW pe=%u row=%u words=[%08x %08x %08x %08x %08x]\n",
	                  pe,
                  row_dbg,
                  pmem_store_read(pe, 0u, row_dbg, 0u),
                  pmem_store_read(pe, 0u, row_dbg, 1u),
                  pmem_store_read(pe, 0u, row_dbg, 2u),
                  pmem_store_read(pe, 0u, row_dbg, 3u),
                  pmem_store_read(pe, 0u, row_dbg, 4u));
    }
  }

  // 当前 whitebox 依赖 control_unit_gbp 自主扫描本地 META rows。
  // 外部 sideband launch_cmd 会抢占 compute_cmd 通路，反而干扰真实进度观测，这里直接关闭。

  const uint32_t total_variables = count_partition_variables(partition);
  const uint32_t total_factors = count_partition_factors(partition);
  const uint64_t convergence_progress_floor =
      static_cast<uint64_t>(total_variables) * static_cast<uint64_t>(kFixedIters);

  std::vector<PeProgressSummary> progress(cfg.pe_count);
  std::vector<bool> prev_start(cfg.pe_count, false);
  std::vector<bool> prev_done(cfg.pe_count, false);
  std::vector<bool> prev_wr(cfg.pe_count, false);
  std::vector<uint8_t> prev_epoch(cfg.pe_count, 0u);
  std::vector<bool> epoch_seen(cfg.pe_count, false);

  for (int cycle = 0; cycle < kObserveCycles; ++cycle) {
    tick(dut);
    
    // Debug: 每10000周期打印一次进度
    if (cycle % 10000 == 0 && cycle > 0) {
      uint32_t dbg_starts = 0, dbg_dones = 0, dbg_writes = 0;
      for (uint32_t p = 0; p < cfg.pe_count; ++p) {
        dbg_starts += progress[p].starts;
        dbg_dones += progress[p].dones;
        dbg_writes += progress[p].writes;
      }
      std::printf("GBP_DEBUG_CYCLE cycle=%d starts=%u dones=%u writes=%u\n",
                  cycle, dbg_starts, dbg_dones, dbg_writes);
    }
    
    for (uint32_t pe = 0; pe < cfg.pe_count; ++pe) {
      dut->observe_pe_i = pe;
      dut->eval();
      const bool start_now = dut->observe_compute_start_o;
      const bool done_now = dut->observe_compute_done_o;
      const bool wr_now = dut->observe_wr_req_valid_o;
      const uint32_t wr_addr_now = dut->observe_wr_req_addr_o;
      const uint32_t wr_data_now = dut->observe_wr_req_data_o;
      const bool ingress_wr_now = dut->observe_ingress_wr_req_valid_o;
      const uint32_t ingress_wr_addr_now = dut->observe_ingress_wr_req_addr_o;
      const uint32_t ingress_wr_data_now = dut->observe_ingress_wr_req_data_o;
      const bool bank0_src0_now = dut->observe_bank0_wr_src0_valid_o;
      const uint32_t bank0_src0_row_now = dut->observe_bank0_wr_src0_row_o;
      const bool bank0_src1_now = dut->observe_bank0_wr_src1_valid_o;
      const uint32_t bank0_src1_row_now = dut->observe_bank0_wr_src1_row_o;
      const uint8_t epoch_now = dut->observe_epoch_o;
      const bool doorbell_now = dut->observe_doorbell_o;
      const uint8_t q_head_now = dut->observe_q_head_o;
      const uint8_t q_tail_now = dut->observe_q_tail_o;
      const uint8_t q_credit_now = dut->observe_q_credit_o;
	      const uint32_t meta_row_now = dut->observe_meta_row_o;
	      const bool scan_done_now = dut->observe_scan_done_o;
	      const uint8_t ctrl_state_now = dut->observe_ctrl_state_o;
	      const bool ctrl_phase_now = dut->observe_ctrl_phase_o;
	      const bool ctrl_compute_pending_now = dut->observe_ctrl_compute_pending_o;
	      const bool ctrl_compute_running_now = dut->observe_ctrl_compute_running_o;
	      const uint32_t ctrl_var_cmd_accept_count_now = dut->observe_ctrl_var_cmd_accept_count_o;
	      const uint32_t ctrl_fac_cmd_accept_count_now = dut->observe_ctrl_fac_cmd_accept_count_o;
	      const uint32_t ctrl_phase_flip_count_now = dut->observe_ctrl_phase_flip_count_o;
	      const uint32_t ctrl_epoch_count_now = dut->observe_ctrl_epoch_count_o;
	      const bool wr_desc_pending_now = dut->observe_wr_desc_pending_o;
      const uint8_t wrapper_state_now = dut->observe_wrapper_state_o;
      const bool wrapper_compute_done_now = dut->observe_wrapper_compute_done_o;
      const bool wrapper_rsp_done_now = dut->observe_wrapper_rsp_done_o;
      const bool engine_compute_done_now = dut->observe_engine_compute_done_o;
      const bool engine_rsp_done_now = dut->observe_engine_rsp_done_o;
      const uint8_t engine_cmd_dofs_now = dut->observe_engine_cmd_dofs_o;
      const uint8_t engine_cmd_msg_count_now = dut->observe_engine_cmd_msg_count_o;
      const uint16_t engine_stream_in_beats_now = dut->observe_engine_stream_in_beats_o;
      const uint16_t engine_stream_out_beats_now = dut->observe_engine_stream_out_beats_o;
      const uint16_t engine_stream_target_beats_now = dut->observe_engine_stream_target_beats_o;
      const bool engine_stream_active_now = dut->observe_engine_stream_active_o;
      const bool engine_stream_dir_out_now = dut->observe_engine_stream_dir_out_o;
      const bool engine_stream_out_valid_now = dut->observe_engine_stream_out_valid_o;
      const bool engine_stream_out_ready_now = dut->observe_engine_stream_out_ready_o;
      const uint8_t engine_fsm_state_now = dut->observe_engine_fsm_state_o;
      const uint8_t engine_fsm_accum_count_now = dut->observe_engine_fsm_accum_count_o;
      const uint8_t write_fifo_occ_now = dut->observe_write_fifo_occ_o;
      const bool engine_stream_out_nonzero_now = dut->observe_engine_stream_out_nonzero_o;
      const uint32_t engine_stream_out_word0_now = dut->observe_engine_stream_out_word0_o;
      const uint8_t engine_stream_rd_addr_now = dut->observe_engine_stream_rd_addr_o;
      const bool write_fifo_data_nonzero_now = dut->observe_write_fifo_data_nonzero_o;
      const uint32_t write_fifo_data_word0_now = dut->observe_write_fifo_data_word0_o;
      const bool mic_write_data_nonzero_now = dut->observe_mic_write_data_nonzero_o;
      const uint32_t mic_write_data_word0_now = dut->observe_mic_write_data_word0_o;
      if (start_now && !prev_start[pe]) {
        progress[pe].starts += 1;
      }
      if (done_now && !prev_done[pe]) {
        progress[pe].dones += 1;
      }
      if (wr_now && !prev_wr[pe]) {
        progress[pe].writes += 1;
        if (progress[pe].wr_req_debug_count < kMaxWriteDebugPerPe) {
          std::printf(
              "GBP_WR_REQ_DEBUG pe=%u cycle=%d addr=%05x bank=%u row=%u data=%08x meta_row=%u scan_done=%u\n",
              pe,
              cycle,
              wr_addr_now,
              (wr_addr_now >> kRowBytesLg) & ((1u << kBankWidth) - 1u),
              (wr_addr_now >> kSpmRowShift),
              wr_data_now,
              meta_row_now,
              scan_done_now ? 1u : 0u);
          progress[pe].wr_req_debug_count += 1;
        }
      }
      if (ingress_wr_now && progress[pe].ingress_wr_req_debug_count < kMaxWriteDebugPerPe) {
        std::printf(
            "GBP_INGRESS_WR_REQ_DEBUG pe=%u cycle=%d addr=%05x bank=%u row=%u data=%08x meta_row=%u scan_done=%u\n",
            pe,
            cycle,
            ingress_wr_addr_now,
            (ingress_wr_addr_now >> kRowBytesLg) & ((1u << kBankWidth) - 1u),
            (ingress_wr_addr_now >> kSpmRowShift),
            ingress_wr_data_now,
            meta_row_now,
            scan_done_now ? 1u : 0u);
        progress[pe].ingress_wr_req_debug_count += 1;
      }
      if ((bank0_src0_now || bank0_src1_now)
          && (progress[pe].wr_req_debug_count + progress[pe].ingress_wr_req_debug_count) < (2u * kMaxWriteDebugPerPe)) {
        std::printf(
            "GBP_BANK0_SRC_DEBUG pe=%u cycle=%d src0_valid=%u src0_row=%u src1_valid=%u src1_row=%u meta_row=%u scan_done=%u\n",
            pe,
            cycle,
            bank0_src0_now ? 1u : 0u,
            bank0_src0_row_now,
            bank0_src1_now ? 1u : 0u,
            bank0_src1_row_now,
            meta_row_now,
            scan_done_now ? 1u : 0u);
      }
      if ((engine_stream_out_valid_now || write_fifo_data_nonzero_now || mic_write_data_nonzero_now)
          && progress[pe].pipeline_debug_count < kMaxPipelineDebugPerPe) {
        std::printf(
            "GBP_PIPELINE_DEBUG pe=%u cycle=%d rd_addr=%u out_valid=%u out_ready=%u out_nonzero=%u out_word0=%08x fifo_occ=%u fifo_nonzero=%u fifo_word0=%08x mic_nonzero=%u mic_word0=%08x fsm=%u accum=%u wr_desc=%u\n",
            pe,
            cycle,
            static_cast<unsigned>(engine_stream_rd_addr_now),
            engine_stream_out_valid_now ? 1u : 0u,
            engine_stream_out_ready_now ? 1u : 0u,
            engine_stream_out_nonzero_now ? 1u : 0u,
            engine_stream_out_word0_now,
            static_cast<unsigned>(write_fifo_occ_now),
            write_fifo_data_nonzero_now ? 1u : 0u,
            write_fifo_data_word0_now,
            mic_write_data_nonzero_now ? 1u : 0u,
            mic_write_data_word0_now,
            static_cast<unsigned>(engine_fsm_state_now),
            static_cast<unsigned>(engine_fsm_accum_count_now),
            wr_desc_pending_now ? 1u : 0u);
        progress[pe].pipeline_debug_count += 1;
      }
      if (doorbell_now) {
        progress[pe].doorbell_high_cycles += 1;
      }
      if (epoch_seen[pe] && epoch_now != prev_epoch[pe]) {
        progress[pe].epoch_transitions += 1;
      }
      prev_start[pe] = start_now;
      prev_done[pe] = done_now;
      prev_wr[pe] = wr_now;
      prev_epoch[pe] = epoch_now;
      epoch_seen[pe] = true;
      progress[pe].final_epoch = epoch_now;
      progress[pe].final_doorbell = doorbell_now;
      progress[pe].final_head = q_head_now;
      progress[pe].final_tail = q_tail_now;
      progress[pe].final_credit = q_credit_now;
	      progress[pe].final_meta_row = meta_row_now;
	      progress[pe].final_scan_done = scan_done_now;
	      progress[pe].final_ctrl_state = ctrl_state_now;
	      progress[pe].final_ctrl_phase = ctrl_phase_now;
	      progress[pe].final_ctrl_compute_pending = ctrl_compute_pending_now;
	      progress[pe].final_ctrl_compute_running = ctrl_compute_running_now;
	      progress[pe].final_ctrl_var_cmd_accept_count = ctrl_var_cmd_accept_count_now;
	      progress[pe].final_ctrl_fac_cmd_accept_count = ctrl_fac_cmd_accept_count_now;
	      progress[pe].final_ctrl_phase_flip_count = ctrl_phase_flip_count_now;
	      progress[pe].final_ctrl_epoch_count = ctrl_epoch_count_now;
	      progress[pe].final_wr_desc_pending = wr_desc_pending_now;
      progress[pe].final_wrapper_state = wrapper_state_now;
      progress[pe].final_wrapper_compute_done = wrapper_compute_done_now;
      progress[pe].final_wrapper_rsp_done = wrapper_rsp_done_now;
      progress[pe].final_engine_compute_done = engine_compute_done_now;
      progress[pe].final_engine_rsp_done = engine_rsp_done_now;
      progress[pe].final_engine_cmd_dofs = engine_cmd_dofs_now;
      progress[pe].final_engine_cmd_msg_count = engine_cmd_msg_count_now;
      progress[pe].final_engine_stream_in_beats = engine_stream_in_beats_now;
      progress[pe].final_engine_stream_out_beats = engine_stream_out_beats_now;
      progress[pe].final_engine_stream_target_beats = engine_stream_target_beats_now;
      progress[pe].final_engine_stream_active = engine_stream_active_now;
      progress[pe].final_engine_stream_dir_out = engine_stream_dir_out_now;
      progress[pe].final_engine_stream_out_valid = engine_stream_out_valid_now;
      progress[pe].final_engine_stream_out_ready = engine_stream_out_ready_now;
      progress[pe].final_engine_fsm_state = engine_fsm_state_now;
      progress[pe].final_engine_fsm_accum_count = engine_fsm_accum_count_now;
      progress[pe].final_write_fifo_occ = write_fifo_occ_now;
      progress[pe].final_engine_stream_out_nonzero = engine_stream_out_nonzero_now;
      progress[pe].final_engine_stream_out_word0 = engine_stream_out_word0_now;
      progress[pe].final_engine_stream_rd_addr = engine_stream_rd_addr_now;
      progress[pe].final_write_fifo_data_nonzero = write_fifo_data_nonzero_now;
      progress[pe].final_write_fifo_data_word0 = write_fifo_data_word0_now;
      progress[pe].final_mic_write_data_nonzero = mic_write_data_nonzero_now;
      progress[pe].final_mic_write_data_word0 = mic_write_data_word0_now;
    }
  }

  uint32_t total_starts = 0;
  uint32_t total_dones = 0;
  uint32_t total_writes = 0;
  uint32_t total_var_cmd_accepts = 0;
  uint32_t total_fac_cmd_accepts = 0;
  uint32_t total_phase_flips = 0;
  uint32_t total_epoch_count = 0;
  for (uint32_t pe = 0; pe < cfg.pe_count; ++pe) {
    total_starts += progress[pe].starts;
    total_dones += progress[pe].dones;
    total_writes += progress[pe].writes;
    total_var_cmd_accepts += progress[pe].final_ctrl_var_cmd_accept_count;
    total_fac_cmd_accepts += progress[pe].final_ctrl_fac_cmd_accept_count;
    total_phase_flips += progress[pe].final_ctrl_phase_flip_count;
    total_epoch_count += progress[pe].final_ctrl_epoch_count;
  }
  const uint32_t total_compute_events = std::max(total_starts, total_dones);
  const std::string progress_summary = build_progress_summary(progress);
  if (total_starts == 0 && total_dones == 0 && total_writes == 0) {
    std::fprintf(stderr,
                 "FAIL: 白盒观测窗口内未捕获任何 compute_start/compute_done/wr_req 事件 progress=%s\n",
                 progress_summary.c_str());
    delete dut;
    return 1;
  }
  if (total_compute_events < total_variables) {
    for (uint32_t pe = 0; pe < cfg.pe_count; ++pe) {
      for (uint32_t delta = 0; delta < 2; ++delta) {
        const uint32_t row = progress[pe].final_meta_row + delta;
        std::array<uint32_t, kWordsPerRow> dut_meta_words{};
        (void)read_spm_row_words(pe, 0u, row, &dut_meta_words);
        std::printf(
            "GBP_META_ROW_DEBUG pe=%u row=%u store=[%08x %08x %08x %08x %08x] dut=[%08x %08x %08x %08x %08x]\n",
            pe,
            row,
            pmem_store_read(pe, 0u, row, 0u),
            pmem_store_read(pe, 0u, row, 1u),
            pmem_store_read(pe, 0u, row, 2u),
            pmem_store_read(pe, 0u, row, 3u),
            pmem_store_read(pe, 0u, row, 4u),
            dut_meta_words[0],
            dut_meta_words[1],
            dut_meta_words[2],
            dut_meta_words[3],
            dut_meta_words[4]);
      }
    }
    std::fprintf(stderr,
                 "FAIL: progress_insufficient total_cycles=%llu total_variables=%u required_for_one_round>=%u required_for_%d_iters>=%llu observed_starts=%u observed_dones=%u observed_writes=%u progress=%s\n",
                 static_cast<unsigned long long>(g_cycle_count),
                 total_variables,
                 total_variables,
                 kFixedIters,
                 static_cast<unsigned long long>(convergence_progress_floor),
                 total_starts,
                 total_dones,
                 total_writes,
                 progress_summary.c_str());
    delete dut;
    return 1;
  }
  if (total_var_cmd_accepts == 0 || total_fac_cmd_accepts == 0
      || total_phase_flips == 0 || total_epoch_count == 0) {
    std::fprintf(stderr,
                 "FAIL: priority_switch_incomplete total_variables=%u total_factors=%u var_accept=%u fac_accept=%u phase_flip=%u epoch_count=%u progress=%s\n",
                 total_variables,
                 total_factors,
                 total_var_cmd_accepts,
                 total_fac_cmd_accepts,
                 total_phase_flips,
                 total_epoch_count,
                 progress_summary.c_str());
    delete dut;
    return 1;
  }

  // ============================================
  // 新增：从 DUT 直接读取所有 variable states
  // ============================================
  
  // 调试：打印 SPM 存储统计
  std::printf("GBP_WHITEBOX_ARE_DEBUG pmem_words_size=%zu\n", g_pmem_words.size());
  
  // 统计每个 PE 的写入情况，并检查 message banks 的数据值
  std::map<uint32_t, std::map<uint32_t, int>> pe_bank_writes;
  std::map<uint32_t, std::map<uint32_t, std::map<uint32_t, int>>> pe_bank_row_writes;
  // 统计非零值
  int nonzero_data_count = 0;
  int total_data_count = 0;
  for (const auto& kv : g_pmem_words) {
    uint64_t key = kv.first;
    uint32_t pe = (key >> 32) & 0xFFFFFFFF;
    uint32_t bank = (key >> 24) & 0xFF;
    uint32_t row = (key >> 8) & 0xFFFF;
    uint32_t word = key & 0xFF;
    uint32_t data = kv.second;
    pe_bank_writes[pe][bank]++;
    pe_bank_row_writes[pe][bank][row]++;
    // Check if data is non-zero (for message banks)
    if (bank >= kRequiredMessageBankLo && bank <= kRequiredMessageBankHi) {
      total_data_count++;
      if (data != 0) {
        nonzero_data_count++;
        if (nonzero_data_count <= 10) {
          float fval = *reinterpret_cast<const float*>(&data);
          std::printf("GBP_WHITEBOX_ARE_DEBUG msg_data pe=%u bank=%u row=%u word=%u data=%08x float=%f\n",
                      pe, bank, row, word, data, fval);
        }
      }
    }
  }
  std::printf("GBP_WHITEBOX_ARE_DEBUG msg_nonzero=%d/%d\n", nonzero_data_count, total_data_count);
  
  for (const auto& pe_kv : pe_bank_writes) {
    for (const auto& bank_kv : pe_kv.second) {
      auto& rows = pe_bank_row_writes[pe_kv.first][bank_kv.first];
      uint32_t min_row = rows.begin()->first;
      uint32_t max_row = rows.rbegin()->first;
      // Count rows with multiple writes
      int multi_write_rows = 0;
      for (const auto& row_kv : rows) {
        if (row_kv.second > 1) multi_write_rows++;
      }
      std::printf("GBP_WHITEBOX_ARE_DEBUG pe=%u bank=%u writes=%d min_row=%u max_row=%u multi_write_rows=%d\n", 
                  pe_kv.first, bank_kv.first, bank_kv.second, min_row, max_row, multi_write_rows);
    }
  }
  
  std::vector<DutVariableState> dut_states;
  std::string dut_read_error;
  if (!read_all_dut_variable_states(cfg, partition, var_node_map, &dut_states, &dut_read_error)) {
    std::fprintf(stderr, "FAIL: 无法从 DUT 读取 states: %s\n", dut_read_error.c_str());
    delete dut;
    return 1;
  }
  std::printf("GBP_WHITEBOX_ARE_DEBUG read_variables=%zu\n", dut_states.size());
  
  // 统计 dofs 分布
  std::unordered_map<int, int> dofs_count;
  for (const auto& s : dut_states) {
    dofs_count[s.dofs]++;
  }
  for (const auto& kv : dofs_count) {
    std::printf("GBP_WHITEBOX_ARE_DEBUG dofs=%d count=%d\n", kv.first, kv.second);
  }
  
  // 调试：比较 preload 和读取的值（检查 DUT 是否修改了数据）
  int unchanged_vars = 0;
  int changed_vars = 0;
  int vars_missing_state_compare = 0;
  for (const auto& state : dut_states) {
    auto it = var_node_map.find(state.variable_id);
    if (it != var_node_map.end()) {
      VariableNode* var = it->second;
      Eigen::VectorXd original_eta = var->get_prior().get_eta();
      Eigen::MatrixXd original_lam = var->get_prior().get_lam();
      const bool has_comparable_state =
          (state.prior_eta.size() == original_eta.size())
          && (state.prior_lam.rows() == original_lam.rows())
          && (state.prior_lam.cols() == original_lam.cols());
      if (!has_comparable_state) {
        vars_missing_state_compare++;
        if (vars_missing_state_compare <= 10) {
          std::printf(
              "GBP_WHITEBOX_ARE_DEBUG compare_skip var=%d dut_eta=%ld dut_lam=%ldx%ld original_eta=%ld original_lam=%ldx%ld msgs=%zu\n",
              state.variable_id,
              static_cast<long>(state.prior_eta.size()),
              static_cast<long>(state.prior_lam.rows()),
              static_cast<long>(state.prior_lam.cols()),
              static_cast<long>(original_eta.size()),
              static_cast<long>(original_lam.rows()),
              static_cast<long>(original_lam.cols()),
              state.inbound_messages.size());
        }
        continue;
      }
      
      double eta_diff = (state.prior_eta - original_eta).norm();
      double lam_diff = (state.prior_lam - original_lam).norm();
      
      if (eta_diff < 1e-6 && lam_diff < 1e-6) {
        unchanged_vars++;
      } else {
        changed_vars++;
        if (changed_vars <= 10) {
          std::printf("GBP_WHITEBOX_ARE_DEBUG changed_var id=%d dofs=%d eta_diff=%e lam_diff=%e\n", 
                      state.variable_id, state.dofs, eta_diff, lam_diff);
          std::printf("GBP_WHITEBOX_ARE_DEBUG   original_eta=[%.3f,%.3f] read_eta=[%.3f,%.3f]\n",
                      original_eta(0), original_eta(1), state.prior_eta(0), state.prior_eta(1));
        }
      }
    }
  }
  std::printf("GBP_WHITEBOX_ARE_DEBUG unchanged_vars=%d/%zu\n", unchanged_vars, dut_states.size());
  std::printf("GBP_WHITEBOX_ARE_DEBUG vars_missing_state_compare=%d\n", vars_missing_state_compare);
  
  // 调试：检查是否有 NaN 在读取的数据中
  int nan_in_data = 0;
  for (const auto& s : dut_states) {
    if (!s.prior_eta.allFinite() || !s.prior_lam.allFinite()) {
      nan_in_data++;
    }
  }
  std::printf("GBP_WHITEBOX_ARE_DEBUG nan_in_dut_states=%d\n", nan_in_data);
  
  // 直接从 DUT states 计算 ARE
  double dut_are = 0.0;
  double dut_energy = 0.0;
  if (!compute_are_from_dut_states(dut_states, partition, &dut_are, &dut_energy, &dut_read_error)) {
    std::fprintf(stderr, "FAIL: 无法从 DUT states 计算 ARE: %s\n", dut_read_error.c_str());
    delete dut;
    return 1;
  }
  std::printf("GBP_WHITEBOX_ARE_DEBUG computed_are=%.9g energy=%.9g\n", dut_are, dut_energy);

  // 写入 terminal dump (用于调试，现在使用真实 DUT 数据)
  const std::string dut_terminal_dump_path =
      std::string("build/integration/gbp_pe_mesh_whitebox_convergence/")
      + cfg.workload + "_dut_terminal_dump_" + std::to_string(cfg.pe_count)
      + "pe_" + std::to_string(cfg.mesh_x) + "x" + std::to_string(cfg.mesh_y) + ".json";
  if (!write_terminal_dump_from_store(dut_terminal_dump_path, cfg, partition, dut_states)) {
    delete dut;
    return 1;
  }

  const bool final_are_ok = std::isfinite(dut_are) && (dut_are < kAreDropBaseline);
  const bool final_energy_ok = std::isfinite(dut_energy);
  const char* final_status = (final_are_ok && final_energy_ok) ? "PASS" : "FAIL";

  std::printf("GBP_WHITEBOX_SPM_DEBUG pmem_read_count=%llu pmem_write_count=%llu\n",
              static_cast<unsigned long long>(g_pmem_read_count),
              static_cast<unsigned long long>(g_pmem_write_count));
  
  std::printf(
      "gbp_pe_mesh_whitebox_convergence: %s pe_count=%u mesh=%ux%u workload=%s iterations=%d seed=%u dataset=%s partition=%s total_cycles=%llu total_variables=%u total_factors=%u required_for_%d_iters>=%llu total_starts=%u total_dones=%u total_writes=%u var_accept=%u fac_accept=%u phase_flip=%u epoch_count=%u progress=%s baseline_are=%.9g final_are_observed=%.9g final_are_compare=%s final_energy_observed=%.9g final_energy_finite=%s terminal_dump=%s\n",
      final_status,
      cfg.pe_count,
      cfg.mesh_x,
      cfg.mesh_y,
      cfg.workload.c_str(),
      kFixedIters,
      cfg.seed,
      cfg.dataset_path.c_str(),
      cfg.partition_path.c_str(),
      static_cast<unsigned long long>(g_cycle_count),
      total_variables,
      total_factors,
      kFixedIters,
      static_cast<unsigned long long>(convergence_progress_floor),
      total_starts,
      total_dones,
      total_writes,
      total_var_cmd_accepts,
      total_fac_cmd_accepts,
      total_phase_flips,
      total_epoch_count,
      progress_summary.c_str(),
      kAreDropBaseline,
      dut_are,
      final_are_ok ? "PASS" : "FAIL",
      dut_energy,
      final_energy_ok ? "PASS" : "FAIL",
      dut_terminal_dump_path.c_str());

  delete dut;
  return (final_are_ok && final_energy_ok) ? 0 : 1;
}

#include "../common/gbp_terminal_metrics_adapter.cpp"
