#include <cstdio>
#include <string>

#include "../common/oracle_contract.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <contract-file>\n", argv[0]);
    return 2;
  }

  oracle_contract::Contract contract;
  std::string error;
  if (!oracle_contract::load_contract(argv[1], &contract, &error)) {
    std::fprintf(stderr, "ERROR: %s\n", error.c_str());
    return 1;
  }

  if (!oracle_contract::validate_frozen_contract(contract, &error)) {
    std::fprintf(stderr, "ERROR: %s\n", error.c_str());
    return 1;
  }

  std::printf("oracle contract parse: PASS\n");
  std::printf("seed=%d max_iters=%d max_cycles=%d config=%s\n",
              contract.workload_a.seed,
              contract.max_iters,
              contract.max_cycles,
              contract.simulator_config_path.c_str());
  std::printf("state/message abs_err=%g rel_err=%g\n",
              contract.state_message_threshold.abs_err,
              contract.state_message_threshold.rel_err);
  std::printf("are/energy abs_err=%g rel_err=%g\n",
              contract.are_energy_threshold.abs_err,
              contract.are_energy_threshold.rel_err);
  return 0;
}
