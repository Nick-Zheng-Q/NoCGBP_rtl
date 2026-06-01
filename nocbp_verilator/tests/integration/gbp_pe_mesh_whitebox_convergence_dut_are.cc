// 这是 gbp_pe_mesh_whitebox_convergence.cc 的修改版本
// 主要修改：真正实现从 DUT 读取 ARE
// 只包含修改部分的代码，需要合并到原文件中

// ============================================
// 新增/修改的常量和结构体
// ============================================

// SPM Layout 配置
// 根据分析，每个 variable 占用一个 row
// State banks (1-3): 存储 prior_eta (2 floats) 和 prior_lam (3 floats, symmetric)
// Message banks (4-7): 存储 inbound messages，每个 message 5 words (eta[2] + lam[3])

static constexpr uint32_t kMaxRowsPerPe = 1024;  // 假设最大 1024 rows

// Variable 在 SPM 中的布局信息
struct VariableSpmLayout {
  int variable_id = -1;
  int pe = -1;
  uint32_t state_bank = 3;  // 使用 Bank 3 存储 state
  uint32_t state_row = 0;   // row = variable 在 PE 中的索引
  int dofs = 2;
};

// 从 DUT 读取的 Variable State
struct DutVariableState {
  int variable_id = -1;
  int pe = -1;
  int dofs = 2;
  
  // Prior (from state bank)
  Eigen::VectorXd prior_eta;
  Eigen::MatrixXd prior_lam;
  
  // Inbound messages (from message banks)
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
// 辅助函数：从 SPM raw words 解码
// ============================================

static float decode_fp32_word(uint32_t word) {
  float value = 0.0f;
  std::memcpy(&value, &word, sizeof(value));
  return value;
}

// 检查一个 row 是否全为零（表示没有数据）
static bool is_row_empty(const std::array<uint32_t, kWordsPerRow>& row_words) {
  for (uint32_t word : row_words) {
    if (word != 0) return false;
  }
  return true;
}

// 从 State row 解码 prior_eta 和 prior_lam
// 假设格式：words[0-1] = eta[0-1], words[2-4] = lam[0,0], lam[0,1], lam[1,1]
static bool decode_state_from_row(const std::array<uint32_t, kWordsPerRow>& row_words,
                                   int dofs,
                                   Eigen::VectorXd* out_eta,
                                   Eigen::MatrixXd* out_lam,
                                   std::string* error) {
  if (dofs != 2) {
    *error = "only dim=2 is supported";
    return false;
  }
  
  // 检查是否为空
  if (is_row_empty(row_words)) {
    *error = "row is empty";
    return false;
  }
  
  *out_eta = Eigen::VectorXd(2);
  (*out_eta)(0) = decode_fp32_word(row_words[0]);
  (*out_eta)(1) = decode_fp32_word(row_words[1]);
  
  *out_lam = Eigen::MatrixXd(2, 2);
  double lam00 = decode_fp32_word(row_words[2]);
  double lam01 = decode_fp32_word(row_words[3]);
  double lam11 = decode_fp32_word(row_words[4]);
  (*out_lam)(0, 0) = lam00;
  (*out_lam)(0, 1) = lam01;
  (*out_lam)(1, 0) = lam01;  // symmetric
  (*out_lam)(1, 1) = lam11;
  
  return true;
}

// 从 Message row 解码 eta 和 lam
// 使用与 message payload codec 相同的格式
static bool decode_message_from_row(const std::array<uint32_t, kWordsPerRow>& row_words,
                                     int dofs,
                                     Eigen::VectorXd* out_eta,
                                     Eigen::MatrixXd* out_lam,
                                     std::string* error) {
  if (dofs != 2) {
    *error = "only dim=2 is supported";
    return false;
  }
  
  if (is_row_empty(row_words)) {
    *error = "row is empty";
    return false;
  }
  
  *out_eta = Eigen::VectorXd(2);
  (*out_eta)(0) = decode_fp32_word(row_words[0]);
  (*out_eta)(1) = decode_fp32_word(row_words[1]);
  
  *out_lam = Eigen::MatrixXd(2, 2);
  double lam00 = decode_fp32_word(row_words[2]);
  double lam01 = decode_fp32_word(row_words[3]);
  double lam11 = decode_fp32_word(row_words[4]);
  (*out_lam)(0, 0) = lam00;
  (*out_lam)(0, 1) = lam01;
  (*out_lam)(1, 0) = lam01;
  (*out_lam)(1, 1) = lam11;
  
  return true;
}

// ============================================
// 核心函数：从 DUT 读取所有 variable states
// ============================================

static bool read_all_dut_variable_states(
    const RuntimeConfig& cfg,
    const PartitionInfo& partition,
    std::vector<DutVariableState>* out_states,
    std::string* error) {
  
  out_states->clear();
  
  // 构建 variable -> PE 映射
  std::unordered_map<int, int> var_to_pe;
  std::unordered_map<int, int> var_to_local_index;
  
  for (size_t pe = 0; pe < partition.var_mapping.size(); ++pe) {
    for (size_t idx = 0; idx < partition.var_mapping[pe].size(); ++idx) {
      int var_id = partition.var_mapping[pe][idx];
      var_to_pe[var_id] = static_cast<int>(pe);
      var_to_local_index[var_id] = static_cast<int>(idx);
    }
  }
  
  // 构建 factor -> variables 邻接关系
  std::unordered_map<int, std::vector<int>> var_to_factors;
  for (const auto& edge : partition.edges) {
    int factor_id = edge.first;
    int var_id = edge.second;
    var_to_factors[var_id].push_back(factor_id);
  }
  
  // 为每个 PE 读取其所有 variables
  for (uint32_t pe = 0; pe < cfg.pe_count; ++pe) {
    const auto& pe_vars = partition.var_mapping[pe];
    
    for (size_t local_idx = 0; local_idx < pe_vars.size(); ++local_idx) {
      int var_id = pe_vars[local_idx];
      uint32_t row = static_cast<uint32_t>(local_idx);  // row = local index
      
      DutVariableState state;
      state.variable_id = var_id;
      state.pe = static_cast<int>(pe);
      state.dofs = 2;  // BA 问题维度为 2
      
      // 从 State Bank 3 读取 prior
      std::array<uint32_t, kWordsPerRow> state_beats{};
      if (!read_spm_row_words(pe, kRequiredStateBankHi, row, &state_beats)) {
        // 读取失败，跳过此 variable
        continue;
      }
      
      std::string decode_error;
      if (!decode_state_from_row(state_beats, state.dofs, 
                                  &state.prior_eta, &state.prior_lam, 
                                  &decode_error)) {
        // State 为空或解码失败，说明此 variable 可能未初始化
        // 继续尝试下一个
        continue;
      }
      
      // 从 Message Banks 读取 inbound messages
      auto factor_it = var_to_factors.find(var_id);
      if (factor_it != var_to_factors.end()) {
        const std::vector<int>& adj_factors = factor_it->second;
        
        // 每个 factor 对应一个 message slot
        // Message 存储在 banks 4-7，每个 bank 存储一部分 messages
        for (size_t msg_idx = 0; msg_idx < adj_factors.size(); ++msg_idx) {
          int factor_id = adj_factors[msg_idx];
          
          // 计算 message 存储位置
          // 策略：每个 variable 的 messages 从 row = local_idx 开始
          // 多个 messages 可以存储在同一 row 的不同 banks
          uint32_t msg_bank = kRequiredMessageBankLo + (msg_idx % 4);
          uint32_t msg_row = row;  // 简化：假设所有 messages 在同一 row
          
          std::array<uint32_t, kWordsPerRow> msg_beats{};
          if (!read_spm_row_words(pe, msg_bank, msg_row, &msg_beats)) {
            continue;
          }
          
          DutVariableState::InboundMessage msg;
          msg.factor_id = factor_id;
          msg.msg_bank = static_cast<int>(msg_bank);
          msg.msg_row = static_cast<int>(msg_row);
          
          if (decode_message_from_row(msg_beats, state.dofs, 
                                       &msg.eta, &msg.lam, &decode_error)) {
            state.inbound_messages.push_back(msg);
          }
        }
      }
      
      out_states->push_back(state);
    }
  }
  
  if (out_states->empty()) {
    *error = "no valid variable states read from DUT";
    return false;
  }
  
  return true;
}

// ============================================
// 核心函数：直接从 DUT states 计算 ARE
// ============================================

static bool compute_are_from_dut_states(
    const std::vector<DutVariableState>& dut_states,
    const PartitionInfo& partition,
    double* out_are,
    double* out_energy,
    std::string* error) {
  
  // 构建 BA graph
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
    graph = create_ba_graph(kBaDatasetPath, config);
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
  for (VariableNode* var : graph.get_var_nodes()) {
    int var_id = var->get_variable_ID();
    
    auto it = state_map.find(var_id);
    if (it == state_map.end()) {
      continue;  // 没有从 DUT 读取到此 variable 的状态
    }
    
    const DutVariableState* state = it->second;
    
    // 设置 prior
    var->set_prior_eta(state->prior_eta);
    var->set_prior_lam(state->prior_lam);
    
    // 构建 inbound messages
    std::vector<NdimGaussian> inbound;
    for (const auto& msg : state->inbound_messages) {
      NdimGaussian msg_gaussian(state->dofs, msg.eta, msg.lam);
      inbound.push_back(msg_gaussian);
    }
    
    // 更新 belief
    var->update_belief(inbound);
    
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
  
  if (updated_count == 0) {
    *error = "no variables were updated from DUT states";
    return false;
  }
  
  std::printf("GBP_WHITEBOX_ARE_DEBUG updated_variables=%d/%zu\n", 
              updated_count, dut_states.size());
  
  // 同步 factor 状态
  graph.compute_all_factors();
  graph.robustify_all_factors();
  
  *out_are = graph.are();
  *out_energy = graph.energy();
  
  if (!std::isfinite(*out_are) || !std::isfinite(*out_energy)) {
    *error = "computed ARE/energy is not finite";
    return false;
  }
  
  return true;
}

// ============================================
// 修改后的 write_terminal_dump_from_store
// ============================================

static bool write_terminal_dump_from_store(
    const std::string& path,
    const RuntimeConfig& cfg,
    const PartitionInfo& partition,
    const std::vector<DutVariableState>& dut_states) {
  
  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    std::fprintf(stderr, "FAIL: cannot write terminal dump: %s\n", path.c_str());
    return false;
  }
  
  // 构建 var_id -> state 映射
  std::unordered_map<int, const DutVariableState*> state_map;
  for (const auto& state : dut_states) {
    state_map[state.variable_id] = &state;
  }

  // 写入 header
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

  // 写入 terminal_dump (保持原有格式)
  for (uint32_t pe = 0; pe < cfg.pe_count; ++pe) {
    ofs << "    {\"pe\": " << pe << ",\n";
    ofs << "      \"required_rows\": [\n";
    bool first_row = true;
    for (uint32_t bank = kRequiredStateBankLo; bank <= kRequiredMessageBankHi; ++bank) {
      std::array<uint32_t, kWordsPerRow> beats{};
      read_spm_row_words(pe, bank, kRequiredRowIndex, &beats);
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

  // 写入 variable_snapshots (使用从 DUT 读取的数据)
  ofs << "  ],\n"
      << "  \"variable_snapshots\": [\n";

  bool first_var = true;
  for (const auto& state : dut_states) {
    if (!first_var) {
      ofs << ",\n";
    }
    first_var = false;
    
    ofs << "    {\n"
        << "      \"pe\": " << state.pe << ",\n"
        << "      \"variable_id\": " << state.variable_id << ",\n"
        << "      \"dofs\": " << state.dofs << ",\n"
        << "      \"state_bank\": " << kRequiredStateBankHi << ",\n"
        << "      \"state_row\": " << var_to_local_index[state.variable_id] << ",\n"
        << "      \"snapshot_seq\": 1,\n"
        << "      \"snapshot_cycle\": 0,\n"
        << "      \"prior_eta\": ";
    emit_json_vector(ofs, state.prior_eta);
    ofs << ",\n"
        << "      \"prior_lam\": ";
    emit_json_matrix(ofs, state.prior_lam);
    ofs << ",\n"
        << "      \"inbound_messages\": [\n";
    
    bool first_msg = true;
    for (const auto& msg : state.inbound_messages) {
      if (!first_msg) {
        ofs << ",\n";
      }
      first_msg = false;
      
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
          << "        }";
    }
    
    ofs << "\n      ]\n"
        << "    }";
  }

  ofs << "\n  ],\n"
      << "  \"local_semantic_messages\": []\n"
      << "}\n";
  return true;
}

// ============================================
// 修改后的 run_test 主逻辑
// ============================================

// 在 run_test 函数中，替换原来的：
// 1. 调用 build_reference_ba_snapshot 的部分
// 2. 使用 write_terminal_dump_from_store 的部分
// 3. ARE 计算逻辑

/*
int run_test(int argc, char** argv) {
  // ... 原有初始化代码 ...

  // 新增：从 DUT 读取所有 variable states
  std::vector<DutVariableState> dut_states;
  std::string read_error;
  if (!read_all_dut_variable_states(cfg, partition, &dut_states, &read_error)) {
    std::fprintf(stderr, "FAIL: failed to read DUT states: %s\n", read_error.c_str());
    delete dut;
    return 1;
  }
  
  std::printf("GBP_WHITEBOX_ARE_DEBUG read_variables=%zu\n", dut_states.size());

  // 直接计算 ARE (不通过 JSON dump)
  double dut_are = 0.0;
  double dut_energy = 0.0;
  if (!compute_are_from_dut_states(dut_states, partition, &dut_are, &dut_energy, &read_error)) {
    std::fprintf(stderr, "FAIL: failed to compute ARE from DUT: %s\n", read_error.c_str());
    delete dut;
    return 1;
  }

  // 写入 terminal dump (用于调试)
  const std::string dut_terminal_dump_path =
      std::string("build/integration/gbp_pe_mesh_whitebox_convergence/")
      + cfg.workload + "_dut_terminal_dump_" + std::to_string(cfg.pe_count)
      + "pe_" + std::to_string(cfg.mesh_x) + "x" + std::to_string(cfg.mesh_y) + ".json";
  write_terminal_dump_from_store(dut_terminal_dump_path, cfg, partition, dut_states);

  // 比较 ARE
  const char* observed_source = "dut_spm_direct_read";
  AreEnergyFieldReport final_are_report{"final_are", 0.0, 0.0, 0.0, 0.0, false};
  AreEnergyFieldReport final_energy_report{"final_energy", 0.0, 0.0, 0.0, 0.0, false};
  
  const bool final_are_ok = check_are_energy_metric(cfg.workload.c_str(),
                                                    final_are_report.field,
                                                    are_energy_expected.final_are,
                                                    dut_are,  // 使用从 DUT 计算的 ARE
                                                    observed_source,
                                                    are_energy_threshold,
                                                    &final_are_report);
  const bool final_energy_ok = check_are_energy_metric(cfg.workload.c_str(),
                                                       final_energy_report.field,
                                                       are_energy_expected.final_energy,
                                                       dut_energy,  // 使用从 DUT 计算的 energy
                                                       observed_source,
                                                       are_energy_threshold,
                                                       &final_energy_report);

  // ... 后续代码保持不变 ...
}
*/
