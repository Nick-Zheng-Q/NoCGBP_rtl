#ifndef NOCBP_VERILATOR_TESTS_COMMON_ORACLE_CONTRACT_HPP_
#define NOCBP_VERILATOR_TESTS_COMMON_ORACLE_CONTRACT_HPP_

#include <string>

namespace oracle_contract {

struct WorkloadConfig {
  std::string name;
  int nodes = 0;
  int rows = 0;
  int cols = 0;
  double spacing = 0.0;
  int seed = 0;
};

struct ThresholdTuple {
  double abs_err = 0.0;
  double rel_err = 0.0;
};

struct Contract {
  int contract_version = 0;
  std::string contract_type;  // "phase1" or "function_level_parity"
  std::string simulator_config_path;
  WorkloadConfig workload_a;
  WorkloadConfig workload_b;
  int max_iters = 0;
  int max_cycles = 0;
  ThresholdTuple state_message_threshold;
  ThresholdTuple are_energy_threshold;
  bool nan_mismatch_fail = true;
  bool inf_mismatch_fail = true;
  bool signed_zero_equivalent = true;
  // Function-level contract fields
  double abs_tol = 0.0;
  bool abs_only = false;
  int generator_seed = 0;
  int function_count = 0;
};

bool load_contract(const std::string& path, Contract* out, std::string* error);
bool validate_frozen_contract(const Contract& contract, std::string* error);
bool compare_abs_only(const char* scenario, double observed, double expected, double abs_tol, std::string* mismatch_output);

}

#endif
