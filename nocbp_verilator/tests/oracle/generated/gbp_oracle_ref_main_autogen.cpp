#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "gbp/BAFactorGraph.hpp"
#include "gbp/LinearFactorGraph.hpp"

int main(int argc, char **argv) {
  if (argc != 7 && argc != 8) {
    std::cerr << "Usage: gbp_oracle_ref_main <workload> <iter> <seed> <line_nodes> <rows> <cols> [dataset_path]" << std::endl;
    return 1;
  }

  const std::string workload = argv[1];
  const int n_iters = std::atoi(argv[2]);
  const int seed = std::atoi(argv[3]);
  const int line_nodes = std::atoi(argv[4]);
  const int rows = std::atoi(argv[5]);
  const int cols = std::atoi(argv[6]);
  const std::string dataset_path = (argc == 8) ? argv[7] : "";

  Config config;
  config.eta_damping = 0.4;
  config.beta = 0.01;
  config.num_undamped_iters = 6;
  config.min_linear_iters = 8;
  config.gauss_noise_std = 2.0;
  config.loss = HUBER;
  config.mahalanobis_threshold = 3.0;

  std::unique_ptr<LinearFactorGraph> linear_graph;
  std::unique_ptr<BAFactorGraph> ba_graph;
  FactorGraph *graph = nullptr;

  if (workload == "synthetic_line") {
    linear_graph = std::make_unique<LinearFactorGraph>(
        create_synthetic_line_graph(line_nodes, 1.0, 1.0, seed, 1e-3, config));
    graph = linear_graph.get();
  } else if (workload == "synthetic_lattice") {
    linear_graph = std::make_unique<LinearFactorGraph>(
        create_synthetic_lattice_graph(rows, cols, 1.0, 1.0, seed, 1e-3, config));
    graph = linear_graph.get();
  } else if (workload == "bal_fr1desk_small") {
    if (dataset_path.empty()) {
      std::cerr << "Missing dataset path for bal_fr1desk_small" << std::endl;
      return 3;
    }
    ba_graph = std::make_unique<BAFactorGraph>(std::move(create_ba_graph(dataset_path, config)));
    graph = ba_graph.get();
  } else {
    std::cerr << "Unsupported workload: " << workload << std::endl;
    return 2;
  }

  graph->generate_priors_var(100);
  graph->update_all_beliefs();

  double final_are = 0.0;
  double final_energy = 0.0;

  for (int i = 0; i < n_iters; ++i) {
    final_are = graph->are();
    final_energy = graph->energy();
    graph->synchronous_iteration(true, true);
  }

  std::cout << "FINAL_ARE=" << final_are << std::endl;
  std::cout << "FINAL_ENERGY=" << final_energy << std::endl;
  std::cout << "ITERS=" << n_iters << std::endl;
  return 0;
}
