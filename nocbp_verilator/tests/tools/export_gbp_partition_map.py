#!/usr/bin/env python3

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SIM_SRC = ROOT / "nocbp_simulator"
SCOTCH_HELPER_BASENAME = "gbp_partition_scotch_ref_bin"
WORKLOADS = ("synthetic_line", "synthetic_lattice")


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


def factor_variable_split_mapping(n_fac: int, n_var: int) -> tuple[list[list[int]], list[list[int]]]:
    return [list(range(n_fac)), []], [[], list(range(n_var))]


def build_scotch_helper(output_bin: Path) -> None:
    compiler = shutil.which("c++")
    if compiler is None:
        raise RuntimeError("c++ compiler not found")

    helper_src_text = """
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <scotch/scotch.h>

#include \"gbp/LinearFactorGraph.hpp\"

int main(int argc, char **argv) {
  if (argc != 6) {
    std::cerr << \"usage: <workload> <seed> <line_nodes> <rows> <cols>\" << std::endl;
    return 2;
  }

  const std::string workload = argv[1];
  const int seed = std::atoi(argv[2]);
  const int line_nodes = std::atoi(argv[3]);
  const int rows = std::atoi(argv[4]);
  const int cols = std::atoi(argv[5]);

  Config config;
  config.eta_damping = 0.4;
  config.beta = 0.01;
  config.num_undamped_iters = 6;
  config.min_linear_iters = 8;
  config.gauss_noise_std = 2.0;
  config.loss = HUBER;
  config.mahalanobis_threshold = 3.0;

  LinearFactorGraph graph(false, config.eta_damping, config.beta,
                          config.num_undamped_iters, config.min_linear_iters);
  if (workload == \"synthetic_line\") {
    graph = create_synthetic_line_graph(line_nodes, 1.0, 1.0, seed, 1e-3, config);
  } else if (workload == \"synthetic_lattice\") {
    graph = create_synthetic_lattice_graph(rows, cols, 1.0, 1.0, seed, 1e-3, config);
  } else {
    std::cerr << \"unsupported workload\" << std::endl;
    return 2;
  }

  const auto fac_nodes = graph.get_fac_nodes();
  const int n_fac = static_cast<int>(fac_nodes.size());
  const int n_var = static_cast<int>(graph.get_var_nodes().size());
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
    std::cerr << \"SCOTCH_graphBuild failed\" << std::endl;
    return 3;
  }
  if (SCOTCH_graphCheck(&scotch_graph) != 0) {
    std::cerr << \"SCOTCH_graphCheck failed\" << std::endl;
    return 3;
  }

  SCOTCH_Arch arch;
  SCOTCH_archInit(&arch);
  SCOTCH_archMesh2(&arch, 2, 1);

  SCOTCH_Strat strat;
  SCOTCH_stratInit(&strat);

  std::vector<SCOTCH_Num> part(static_cast<size_t>(vertnbr), 0);
  if (SCOTCH_graphMap(&scotch_graph, &arch, &strat, part.data()) != 0) {
    std::cerr << \"SCOTCH_graphMap failed\" << std::endl;
    return 4;
  }

  for (int i = 0; i < vertnbr; ++i) {
    if (i != 0) {
      std::cout << \" \";
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
            SIM_SRC / "gbp" / "FactorGraph.cpp",
            SIM_SRC / "gbp" / "FactorNode.cpp",
            SIM_SRC / "gbp" / "LinearFactorGraph.cpp",
            SIM_SRC / "gbp" / "VariableNode.cpp",
            SIM_SRC / "gbp" / "factor_utils.cpp",
            SIM_SRC / "gbp" / "precision.cpp",
            SIM_SRC / "gbp" / "reprojection_utils.cpp",
            SIM_SRC / "utils" / "Logger.cpp",
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
    workload: str,
    seed: int,
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
                workload,
                str(seed),
                str(line_nodes),
                str(lattice_rows),
                str(lattice_cols),
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

    fac_mapping_table: list[list[int]] = [[], []]
    var_mapping_table: list[list[int]] = [[], []]
    for factor_id in range(n_fac):
        pe = parts[factor_id]
        if pe not in (0, 1):
            raise ValueError(f"Invalid PE id for factor {factor_id}: {pe}")
        fac_mapping_table[pe].append(factor_id)
    for variable_id in range(n_var):
        pe = parts[n_fac + variable_id]
        if pe not in (0, 1):
            raise ValueError(f"Invalid PE id for variable {variable_id}: {pe}")
        var_mapping_table[pe].append(variable_id)
    return fac_mapping_table, var_mapping_table


def export_partition(
    workload: str,
    seed: int,
    pes: int,
    mode: str,
    line_nodes: int,
    lattice_rows: int,
    lattice_cols: int,
) -> dict[str, object]:
    if pes != 2:
        raise ValueError("Task 3 export tool supports exactly --pes 2")
    n_fac, n_var, factor_var_edges = build_factor_structure(workload, line_nodes, lattice_rows, lattice_cols)

    fallback_used = False
    mapping_backend = ""
    if mode == "factor_variable_split":
        fac_mapping_table, var_mapping_table = factor_variable_split_mapping(n_fac, n_var)
        fallback_used = True
        mapping_backend = "fixed_fallback"
    elif mode == "scotch":
        fac_mapping_table, var_mapping_table = scotch_mapping_from_simulator(
            workload=workload,
            seed=seed,
            line_nodes=line_nodes,
            lattice_rows=lattice_rows,
            lattice_cols=lattice_cols,
            n_fac=n_fac,
            n_var=n_var,
        )
        mapping_backend = "simulator_scotch"
    else:
        raise ValueError(f"Unsupported mode: {mode}")

    artifact: dict[str, object] = {
        "schema_version": "1.0",
        "workload": workload,
        "seed": seed,
        "pes": pes,
        "mapping_mode": mode,
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
        },
    }
    return artifact


def main() -> int:
    parser = argparse.ArgumentParser(description="Export deterministic 2-PE GBP partition mapping artifact")
    _ = parser.add_argument("--workload", required=True, choices=WORKLOADS)
    _ = parser.add_argument("--seed", required=True, type=int)
    _ = parser.add_argument("--pes", required=True, type=int)
    _ = parser.add_argument("--mode", required=True, choices=("scotch", "factor_variable_split"))
    _ = parser.add_argument("--output", required=True)
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
    if not isinstance(line_nodes_obj, int):
        raise TypeError("--line-nodes must be int")
    if not isinstance(lattice_rows_obj, int):
        raise TypeError("--lattice-rows must be int")
    if not isinstance(lattice_cols_obj, int):
        raise TypeError("--lattice-cols must be int")

    artifact = export_partition(
        workload=workload_obj,
        seed=seed_obj,
        pes=pes_obj,
        mode=mode_obj,
        line_nodes=line_nodes_obj,
        lattice_rows=lattice_rows_obj,
        lattice_cols=lattice_cols_obj,
    )

    output_path = (ROOT / output_obj).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    _ = output_path.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"partition artifact written: {output_path}")
    print(f"mode: {mode_obj}")
    print(f"fallback_used: {mode_obj == 'factor_variable_split'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
