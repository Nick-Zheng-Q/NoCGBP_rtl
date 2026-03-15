#ifndef GBP_TERMINAL_METRICS_ADAPTER_HPP_
#define GBP_TERMINAL_METRICS_ADAPTER_HPP_

#include <string>
#include <unordered_map>
#include <vector>

namespace gbp_terminal_metrics_adapter {

struct Metrics {
  double are = 0.0;
  double energy = 0.0;
};

bool reconstruct_metrics_from_dump(const std::string& dump_path,
                                   Metrics* out,
                                   std::string* error);

bool collect_static_inbound_factors(const std::string& workload,
                                    int seed,
                                    std::unordered_map<int, std::vector<int>>* out,
                                    std::string* error);

}

#endif
