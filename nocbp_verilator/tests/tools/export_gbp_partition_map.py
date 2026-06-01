#!/usr/bin/env python3

import argparse
import json
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SIM_SRC = ROOT / "nocbp_simulator"
SCOTCH_HELPER_BASENAME = "gbp_partition_scotch_ref_bin"
BA_WORKLOAD_TOKEN = "bal_fr1desk_small"
BA_DATASET_REL_PATH = "data/fr1desk_small.txt"
WORKLOADS = ("synthetic_line", "synthetic_lattice", BA_WORKLOAD_TOKEN)
BA_ALLOWED_TOPOLOGIES = ((2, 2, 1), (16, 4, 4), (32, 8, 4))


@dataclass(frozen=True)
class PartitionContract:
    pe_count: int
    mesh_x: int
    mesh_y: int
    workload: str
    seed: int
    partition_mode: str

    def artifact_stem(self) -> str:
        return f"{self.workload}_partition_{self.pe_count}pe_{self.mesh_x}x{self.mesh_y}"

    def artifact_naming_tokens(self) -> dict[str, str]:
        pe_token = f"{self.pe_count}pe"
        mesh_token = f"{self.mesh_x}x{self.mesh_y}"
        return {
            "workload": self.workload,
            "partition": "partition",
            "pe_token": pe_token,
            "mesh_token": mesh_token,
            "stem": f"{self.workload}_partition_{pe_token}_{mesh_token}",
        }

    def as_dict(self) -> dict[str, object]:
        return {
            "pe_count": self.pe_count,
            "mesh_x": self.mesh_x,
            "mesh_y": self.mesh_y,
            "workload": self.workload,
            "seed": self.seed,
            "partition_mode": self.partition_mode,
            "artifact_naming_tokens": self.artifact_naming_tokens(),
        }


def validate_partition_contract(contract: PartitionContract) -> None:
    if contract.pe_count <= 0:
        raise ValueError("partition contract requires pe_count > 0")
    if contract.mesh_x <= 0 or contract.mesh_y <= 0:
        raise ValueError("partition contract requires mesh_x > 0 and mesh_y > 0")
    if contract.mesh_x * contract.mesh_y != contract.pe_count:
        raise ValueError(
            f"partition contract mismatch: mesh_x * mesh_y must equal pe_count ({contract.mesh_x}*{contract.mesh_y}!={contract.pe_count})"
        )
    if contract.workload == BA_WORKLOAD_TOKEN:
        if contract.partition_mode == "factor_variable_split":
            raise ValueError(
                f"factor_variable_split is unsupported for BA workload '{BA_WORKLOAD_TOKEN}'"
            )
        if contract.partition_mode != "scotch":
            raise ValueError("BA scope requires --mode scotch")
        if (contract.pe_count, contract.mesh_x, contract.mesh_y) not in BA_ALLOWED_TOPOLOGIES:
            raise ValueError(
                "BA scope requires one of: "
                + ", ".join(
                    f"--pes {pe_count} --mesh-x {mesh_x} --mesh-y {mesh_y}"
                    for pe_count, mesh_x, mesh_y in BA_ALLOWED_TOPOLOGIES
                )
            )
        return
    if contract.partition_mode == "factor_variable_split" and contract.pe_count != 2:
        raise ValueError("factor_variable_split requires --pes 2")


def validate_output_name_against_contract(contract: PartitionContract, output_arg: str) -> None:
    output_name = Path(output_arg).name
    expected_stem = contract.artifact_stem()
    if not output_name.startswith(expected_stem):
        raise ValueError(
            f"output artifact name must start with contract stem '{expected_stem}' (got '{output_name}')"
        )
    if contract.pe_count > 2 and "_2pe_" in output_name:
        raise ValueError("large-PE artifact name must not contain '_2pe_'")


def build_factor_structure(workload: str, line_nodes: int, rows: int, cols: int) -> tuple[int, int, list[tuple[int, int]]]:
    if workload == "synthetic_line":
        if line_nodes <= 0:
            return 0, 0, []
        edges: list[tuple[int, int]] = []
        factor_id = 0
        for node_id in range(line_nodes - 1):
            edges.append((factor_id, node_id))
            edges.append((factor_id, node_id + 1))
            factor_id += 1
        return factor_id, line_nodes, edges
    if workload == "synthetic_lattice":
        if rows <= 0 or cols <= 0:
            return 0, 0, []
        lattice_edges: list[tuple[int, int]] = []
        factor_id = 0
        for row in range(rows):
            for col in range(cols):
                node_id = row * cols + col
                if col + 1 < cols:
                    lattice_edges.append((factor_id, node_id))
                    lattice_edges.append((factor_id, node_id + 1))
                    factor_id += 1
                if row + 1 < rows:
                    down_id = (row + 1) * cols + col
                    lattice_edges.append((factor_id, node_id))
                    lattice_edges.append((factor_id, down_id))
                    factor_id += 1
        return factor_id, rows * cols, lattice_edges
    raise ValueError(f"Unsupported workload: {workload}")


def resolve_ba_dataset(dataset_arg: str) -> Path:
    if not dataset_arg:
        raise ValueError(f"workload '{BA_WORKLOAD_TOKEN}' requires --dataset {BA_DATASET_REL_PATH}")

    expected_path = (ROOT / BA_DATASET_REL_PATH).resolve()
    candidate = Path(dataset_arg)
    candidate_path = candidate if candidate.is_absolute() else (ROOT / candidate)
    resolved = candidate_path.resolve()

    if resolved != expected_path:
        raise ValueError(
            f"BA scope is locked to dataset '{BA_DATASET_REL_PATH}' (resolved: {expected_path}); got '{dataset_arg}'"
        )
    if not resolved.is_file():
        raise FileNotFoundError(f"BA dataset file not found: {resolved}")
    return resolved


def build_ba_structure_from_simulator(dataset_path: Path) -> tuple[int, int, list[tuple[int, int]]]:
    compiler = shutil.which("c++")
    if compiler is None:
        raise RuntimeError("c++ compiler not found")

    helper_src_text = """
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "gbp/BAFactorGraph.hpp"

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: <dataset>" << std::endl;
    return 2;
  }

  const std::string dataset = argv[1];
  if (!std::filesystem::is_regular_file(dataset)) {
    std::cerr << "dataset file not found" << std::endl;
    return 2;
  }

  Config config;
  config.eta_damping = 0.4;
  config.beta = 0.01;
  config.num_undamped_iters = 6;
  config.min_linear_iters = 8;
  config.gauss_noise_std = 2.0;
  config.loss = HUBER;
  config.mahalanobis_threshold = 3.0;

  BAFactorGraph graph = create_ba_graph(dataset, config);
  const auto fac_nodes = graph.get_fac_nodes();
  const int n_fac = static_cast<int>(fac_nodes.size());
  const int n_var = static_cast<int>(graph.get_var_nodes().size());

  std::vector<std::pair<int, int>> edges;
  for (int f = 0; f < n_fac; ++f) {
    const FactorNode *fn = fac_nodes[static_cast<size_t>(f)];
    for (auto var : fn->get_adj_var_nodes()) {
      edges.push_back({f, var->get_id()});
    }
  }

  std::cout << n_fac << " " << n_var << " " << edges.size() << std::endl;
  for (const auto &edge : edges) {
    std::cout << edge.first << " " << edge.second << std::endl;
  }
  return 0;
}
"""

    with tempfile.TemporaryDirectory(prefix="gbp_partition_ba_graph_build_") as td_raw:
        td = Path(td_raw)
        helper_src = td / "gbp_partition_ba_graph_ref_main.cpp"
        helper_bin = td / "gbp_partition_ba_graph_ref_bin"
        _ = helper_src.write_text(helper_src_text, encoding="utf-8")

        gbp_sources = [
            SIM_SRC / "gbp" / "BAFactorGraph.cpp",
            SIM_SRC / "gbp" / "FactorGraph.cpp",
            SIM_SRC / "gbp" / "FactorNode.cpp",
            SIM_SRC / "gbp" / "VariableNode.cpp",
            SIM_SRC / "gbp" / "factor_utils.cpp",
            SIM_SRC / "gbp" / "precision.cpp",
            SIM_SRC / "gbp" / "reprojection_utils.cpp",
            SIM_SRC / "utils" / "Logger.cpp",
            SIM_SRC / "utils" / "read_balfile.cpp",
        ]

        compile_cmd = [
            compiler,
            "-std=c++17",
            "-O2",
            str(helper_src),
            *[str(src) for src in gbp_sources],
            "-I" + str(SIM_SRC),
            "-I/usr/include/eigen3",
            "-lfmt",
            "-lspdlog",
            "-o",
            str(helper_bin),
        ]

        completed = subprocess.run(compile_cmd, cwd=ROOT, check=False, text=True, capture_output=True)
        if completed.returncode != 0:
            raise RuntimeError(
                "Failed to compile simulator-backed BA graph helper\n"
                + f"stdout:\n{completed.stdout}\n"
                + f"stderr:\n{completed.stderr}"
            )

        run_result = subprocess.run(
            [str(helper_bin), str(dataset_path)],
            cwd=ROOT,
            check=False,
            text=True,
            capture_output=True,
        )

    if run_result.returncode != 0:
        raise RuntimeError(
            "Simulator-backed BA graph helper failed\n"
            + f"stdout:\n{run_result.stdout}\n"
            + f"stderr:\n{run_result.stderr}"
        )

    lines = [line.strip() for line in run_result.stdout.splitlines() if line.strip()]
    if not lines:
        raise ValueError("BA graph helper returned empty output")

    header_tokens = lines[0].split()
    if len(header_tokens) != 3:
        raise ValueError("BA graph helper header must be: <n_fac> <n_var> <n_edges>")
    n_fac = int(header_tokens[0])
    n_var = int(header_tokens[1])
    n_edges = int(header_tokens[2])

    if len(lines) != n_edges + 1:
        raise ValueError(f"BA graph helper edge line count mismatch: {len(lines) - 1} expected {n_edges}")

    factor_var_edges: list[tuple[int, int]] = []
    for edge_line in lines[1:]:
        parts = edge_line.split()
        if len(parts) != 2:
            raise ValueError(f"Invalid BA graph edge line: '{edge_line}'")
        factor_var_edges.append((int(parts[0]), int(parts[1])))
    return n_fac, n_var, factor_var_edges


def factor_variable_split_mapping(n_fac: int, n_var: int, pes: int) -> tuple[list[list[int]], list[list[int]]]:
    if pes != 2:
        raise ValueError("factor_variable_split requires --pes 2")
    fac_mapping_table: list[list[int]] = [[] for _ in range(pes)]
    var_mapping_table: list[list[int]] = [[] for _ in range(pes)]
    fac_mapping_table[0] = list(range(n_fac))
    var_mapping_table[1] = list(range(n_var))
    return fac_mapping_table, var_mapping_table


def build_scotch_helper(output_bin: Path) -> None:
    compiler = shutil.which("c++")
    if compiler is None:
        raise RuntimeError("c++ compiler not found")

    helper_src_text = """
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <scotch/scotch.h>

#include "gbp/BAFactorGraph.hpp"
#include "gbp/LinearFactorGraph.hpp"

int main(int argc, char **argv) {
  if (argc != 10) {
    std::cerr << "usage: <workload> <dataset> <seed> <line_nodes> <rows> <cols> <pe_count> <mesh_x> <mesh_y>" << std::endl;
    return 2;
  }

  const std::string workload = argv[1];
  const std::string dataset = argv[2];
  const int seed = std::atoi(argv[3]);
  const int line_nodes = std::atoi(argv[4]);
  const int rows = std::atoi(argv[5]);
  const int cols = std::atoi(argv[6]);
  const int pe_count = std::atoi(argv[7]);
  const int mesh_x = std::atoi(argv[8]);
  const int mesh_y = std::atoi(argv[9]);

  if (mesh_x <= 0 || mesh_y <= 0 || pe_count <= 0 || mesh_x * mesh_y != pe_count) {
    std::cerr << "invalid mesh/pe contract" << std::endl;
    return 2;
  }

  Config config;
  config.eta_damping = 0.4;
  config.beta = 0.01;
  config.num_undamped_iters = 6;
  config.min_linear_iters = 8;
  config.gauss_noise_std = 2.0;
  config.loss = HUBER;
  config.mahalanobis_threshold = 3.0;

  std::vector<FactorNode *> fac_nodes;
  int n_var = 0;

  if (workload == "synthetic_line") {
    LinearFactorGraph graph(false, config.eta_damping, config.beta,
                            config.num_undamped_iters, config.min_linear_iters);
    graph = create_synthetic_line_graph(line_nodes, 1.0, 1.0, seed, 1e-3, config);
    fac_nodes = graph.get_fac_nodes();
    n_var = static_cast<int>(graph.get_var_nodes().size());
  } else if (workload == "synthetic_lattice") {
    LinearFactorGraph graph(false, config.eta_damping, config.beta,
                            config.num_undamped_iters, config.min_linear_iters);
    graph = create_synthetic_lattice_graph(rows, cols, 1.0, 1.0, seed, 1e-3, config);
    fac_nodes = graph.get_fac_nodes();
    n_var = static_cast<int>(graph.get_var_nodes().size());
  } else if (workload == "bal_fr1desk_small") {
    if (dataset.empty() || !std::filesystem::is_regular_file(dataset)) {
      std::cerr << "dataset file not found for bal_fr1desk_small" << std::endl;
      return 2;
    }
    BAFactorGraph graph = create_ba_graph(dataset, config);
    fac_nodes = graph.get_fac_nodes();
    n_var = static_cast<int>(graph.get_var_nodes().size());
  } else {
    std::cerr << "unsupported workload" << std::endl;
    return 2;
  }

  const int n_fac = static_cast<int>(fac_nodes.size());
  const int vertnbr = n_fac + n_var;

  std::vector<std::vector<int>> neighbors(static_cast<size_t>(vertnbr));
  for (int f = 0; f < n_fac; ++f) {
    const FactorNode *fn = fac_nodes[static_cast<size_t>(f)];
    for (auto var : fn->get_adj_var_nodes()) {
      const int varid = var->get_id();
      const int v_global = n_fac + varid;
      neighbors[static_cast<size_t>(f)].push_back(v_global);
      neighbors[static_cast<size_t>(v_global)].push_back(f);
    }
  }

  size_t total_edges = 0;
  for (const auto &nbrs : neighbors) {
    total_edges += nbrs.size();
  }

  std::vector<SCOTCH_Num> verttab(static_cast<size_t>(vertnbr + 1), 0);
  std::vector<SCOTCH_Num> edgetab;
  edgetab.reserve(total_edges);

  int edge_count = 0;
  for (int i = 0; i < vertnbr; ++i) {
    verttab[static_cast<size_t>(i)] = edge_count;
    for (int nb : neighbors[static_cast<size_t>(i)]) {
      edgetab.push_back(static_cast<SCOTCH_Num>(nb));
      edge_count++;
    }
  }
  verttab[static_cast<size_t>(vertnbr)] = edge_count;

  SCOTCH_Graph scotch_graph;
  SCOTCH_graphInit(&scotch_graph);
  if (SCOTCH_graphBuild(&scotch_graph, 0, vertnbr, verttab.data(), nullptr,
                        nullptr, nullptr, edge_count, edgetab.data(), nullptr) != 0) {
    std::cerr << "SCOTCH_graphBuild failed" << std::endl;
    return 3;
  }
  if (SCOTCH_graphCheck(&scotch_graph) != 0) {
    std::cerr << "SCOTCH_graphCheck failed" << std::endl;
    return 3;
  }

  SCOTCH_Arch arch;
  SCOTCH_archInit(&arch);
  if (SCOTCH_archMesh2(&arch, mesh_x, mesh_y) != 0) {
    std::cerr << "SCOTCH_archMesh2 failed" << std::endl;
    return 3;
  }

  SCOTCH_Strat strat;
  SCOTCH_stratInit(&strat);

  std::vector<SCOTCH_Num> part(static_cast<size_t>(vertnbr), 0);
  if (SCOTCH_graphMap(&scotch_graph, &arch, &strat, part.data()) != 0) {
    std::cerr << "SCOTCH_graphMap failed" << std::endl;
    return 4;
  }

  for (int i = 0; i < vertnbr; ++i) {
    if (i != 0) {
      std::cout << " ";
    }
    std::cout << static_cast<long long>(part[static_cast<size_t>(i)]);
  }
  std::cout << std::endl;

  SCOTCH_stratExit(&strat);
  SCOTCH_archExit(&arch);
  SCOTCH_graphExit(&scotch_graph);
  return 0;
}
"""

    with tempfile.TemporaryDirectory(prefix="gbp_partition_scotch_build_") as td_raw:
        td = Path(td_raw)
        helper_src = td / "gbp_partition_scotch_ref_main.cpp"
        _ = helper_src.write_text(helper_src_text, encoding="utf-8")

        gbp_sources = [
            SIM_SRC / "gbp" / "BAFactorGraph.cpp",
            SIM_SRC / "gbp" / "FactorGraph.cpp",
            SIM_SRC / "gbp" / "FactorNode.cpp",
            SIM_SRC / "gbp" / "LinearFactorGraph.cpp",
            SIM_SRC / "gbp" / "VariableNode.cpp",
            SIM_SRC / "gbp" / "factor_utils.cpp",
            SIM_SRC / "gbp" / "precision.cpp",
            SIM_SRC / "gbp" / "reprojection_utils.cpp",
            SIM_SRC / "utils" / "Logger.cpp",
            SIM_SRC / "utils" / "read_balfile.cpp",
            SIM_SRC / "utils" / "read_g2o.cpp",
        ]

        compile_cmd = [
            compiler,
            "-std=c++17",
            "-O2",
            str(helper_src),
            *[str(src) for src in gbp_sources],
            "-I" + str(SIM_SRC),
            "-I/usr/include/eigen3",
            "-lfmt",
            "-lspdlog",
            "-lscotch",
            "-lscotcherr",
            "-lscotcherrexit",
            "-o",
            str(output_bin),
        ]

        completed = subprocess.run(compile_cmd, cwd=ROOT, check=False, text=True, capture_output=True)
        if completed.returncode != 0:
            raise RuntimeError(
                "Failed to compile simulator-backed SCOTCH helper\n"
                + f"stdout:\n{completed.stdout}\n"
                + f"stderr:\n{completed.stderr}"
            )


def scotch_mapping_from_simulator(
    contract: PartitionContract,
    dataset_path: str,
    line_nodes: int,
    lattice_rows: int,
    lattice_cols: int,
    n_fac: int,
    n_var: int,
) -> tuple[list[list[int]], list[list[int]]]:
    with tempfile.TemporaryDirectory(prefix="gbp_partition_scotch_runtime_") as td_raw:
        helper_bin = Path(td_raw) / SCOTCH_HELPER_BASENAME
        build_scotch_helper(helper_bin)

        completed = subprocess.run(
            [
                str(helper_bin),
                contract.workload,
                dataset_path,
                str(contract.seed),
                str(line_nodes),
                str(lattice_rows),
                str(lattice_cols),
                str(contract.pe_count),
                str(contract.mesh_x),
                str(contract.mesh_y),
            ],
            cwd=ROOT,
            check=False,
            text=True,
            capture_output=True,
        )
    if completed.returncode != 0:
        raise RuntimeError(
            "Simulator-backed SCOTCH helper failed\n"
            + f"stdout:\n{completed.stdout}\n"
            + f"stderr:\n{completed.stderr}"
        )

    parts = [int(token) for token in completed.stdout.strip().split()]
    if len(parts) != n_fac + n_var:
        raise ValueError(f"Unexpected SCOTCH output length: {len(parts)} expected {n_fac + n_var}")

    fac_mapping_table: list[list[int]] = [[] for _ in range(contract.pe_count)]
    var_mapping_table: list[list[int]] = [[] for _ in range(contract.pe_count)]
    for factor_id in range(n_fac):
        pe = parts[factor_id]
        if pe < 0 or pe >= contract.pe_count:
            raise ValueError(f"Invalid PE id for factor {factor_id}: {pe}")
        fac_mapping_table[pe].append(factor_id)
    for variable_id in range(n_var):
        pe = parts[n_fac + variable_id]
        if pe < 0 or pe >= contract.pe_count:
            raise ValueError(f"Invalid PE id for variable {variable_id}: {pe}")
        var_mapping_table[pe].append(variable_id)
    return fac_mapping_table, var_mapping_table


def export_partition(
    contract: PartitionContract,
    dataset_path: str,
    line_nodes: int,
    lattice_rows: int,
    lattice_cols: int,
) -> dict[str, object]:
    validate_partition_contract(contract)
    if contract.workload == BA_WORKLOAD_TOKEN:
        if contract.partition_mode == "factor_variable_split":
            raise ValueError(f"factor_variable_split is unsupported for BA workload '{BA_WORKLOAD_TOKEN}'")
        n_fac, n_var, factor_var_edges = build_ba_structure_from_simulator(Path(dataset_path))
    else:
        n_fac, n_var, factor_var_edges = build_factor_structure(
            contract.workload,
            line_nodes,
            lattice_rows,
            lattice_cols,
        )

    fallback_used = False
    mapping_backend = ""
    if contract.partition_mode == "factor_variable_split":
        fac_mapping_table, var_mapping_table = factor_variable_split_mapping(n_fac, n_var, contract.pe_count)
        fallback_used = True
        mapping_backend = "fixed_fallback"
    elif contract.partition_mode == "scotch":
        fac_mapping_table, var_mapping_table = scotch_mapping_from_simulator(
            contract=contract,
            dataset_path=dataset_path,
            line_nodes=line_nodes,
            lattice_rows=lattice_rows,
            lattice_cols=lattice_cols,
            n_fac=n_fac,
            n_var=n_var,
        )
        mapping_backend = "simulator_scotch"
    else:
        raise ValueError(f"Unsupported mode: {contract.partition_mode}")

    artifact: dict[str, object] = {
        "schema_version": "1.0",
        "partition_contract": contract.as_dict(),
        "workload": contract.workload,
        "seed": contract.seed,
        "pes": contract.pe_count,
        "mesh": {"x": contract.mesh_x, "y": contract.mesh_y},
        "mapping_mode": contract.partition_mode,
        "fac_mapping_table": fac_mapping_table,
        "var_mapping_table": var_mapping_table,
        "graph": {
            "n_fac_nodes": n_fac,
            "n_var_nodes": n_var,
            "factor_var_edges": [[factor_id, variable_id] for factor_id, variable_id in factor_var_edges],
        },
        "mapping_info": {
            "fallback_used": fallback_used,
            "fallback_rule": "pe0_factors_pe1_variables",
            "source_of_truth": "nocbp_simulator/pe/FactorGraphManager.cpp:scotchMapping",
            "mapping_backend": mapping_backend,
            "scotch_helper_binary": f"ephemeral_tmp_build/{SCOTCH_HELPER_BASENAME}",
            "mesh_x": contract.mesh_x,
            "mesh_y": contract.mesh_y,
        },
    }
    return artifact


def main() -> int:
    parser = argparse.ArgumentParser(description="Export deterministic N-PE GBP partition mapping artifact")
    _ = parser.add_argument("--workload", required=True, choices=WORKLOADS)
    _ = parser.add_argument("--seed", required=True, type=int)
    _ = parser.add_argument("--pes", required=True, type=int)
    _ = parser.add_argument("--mode", required=True, choices=("scotch", "factor_variable_split"))
    _ = parser.add_argument("--output", required=True)
    _ = parser.add_argument("--mesh-x", required=True, type=int)
    _ = parser.add_argument("--mesh-y", required=True, type=int)
    _ = parser.add_argument("--dataset", default="")
    _ = parser.add_argument("--line-nodes", type=int, default=16)
    _ = parser.add_argument("--lattice-rows", type=int, default=4)
    _ = parser.add_argument("--lattice-cols", type=int, default=4)
    args = parser.parse_args()
    args_dict = vars(args)

    workload_obj = args_dict.get("workload")
    seed_obj = args_dict.get("seed")
    pes_obj = args_dict.get("pes")
    mode_obj = args_dict.get("mode")
    output_obj = args_dict.get("output")
    mesh_x_obj = args_dict.get("mesh_x")
    mesh_y_obj = args_dict.get("mesh_y")
    dataset_obj = args_dict.get("dataset")
    line_nodes_obj = args_dict.get("line_nodes")
    lattice_rows_obj = args_dict.get("lattice_rows")
    lattice_cols_obj = args_dict.get("lattice_cols")

    if not isinstance(workload_obj, str):
        raise TypeError("--workload must be string")
    if not isinstance(seed_obj, int):
        raise TypeError("--seed must be int")
    if not isinstance(pes_obj, int):
        raise TypeError("--pes must be int")
    if not isinstance(mode_obj, str):
        raise TypeError("--mode must be string")
    if not isinstance(output_obj, str):
        raise TypeError("--output must be string")
    if not isinstance(mesh_x_obj, int):
        raise TypeError("--mesh-x must be int")
    if not isinstance(mesh_y_obj, int):
        raise TypeError("--mesh-y must be int")
    if not isinstance(dataset_obj, str):
        raise TypeError("--dataset must be string")
    if not isinstance(line_nodes_obj, int):
        raise TypeError("--line-nodes must be int")
    if not isinstance(lattice_rows_obj, int):
        raise TypeError("--lattice-rows must be int")
    if not isinstance(lattice_cols_obj, int):
        raise TypeError("--lattice-cols must be int")

    contract = PartitionContract(
        pe_count=pes_obj,
        mesh_x=mesh_x_obj,
        mesh_y=mesh_y_obj,
        workload=workload_obj,
        seed=seed_obj,
        partition_mode=mode_obj,
    )
    validate_partition_contract(contract)
    validate_output_name_against_contract(contract, output_obj)

    dataset_path = ""
    if workload_obj == BA_WORKLOAD_TOKEN:
        dataset_path = str(resolve_ba_dataset(dataset_obj))

    artifact = export_partition(
        contract=contract,
        dataset_path=dataset_path,
        line_nodes=line_nodes_obj,
        lattice_rows=lattice_rows_obj,
        lattice_cols=lattice_cols_obj,
    )

    output_path = (ROOT / output_obj).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    _ = output_path.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"partition artifact written: {output_path}")
    print(f"mode: {mode_obj}")
    print(f"pe_count: {pes_obj} mesh: {mesh_x_obj}x{mesh_y_obj}")
    print(f"fallback_used: {mode_obj == 'factor_variable_split'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
