#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
import subprocess
import sys
from collections.abc import Mapping
from pathlib import Path
from typing import cast


PRIMARY_SIM_BINARY = Path("/home/nick/Workspace/NoCBP/build/GBPSim")
WORKLOADS = ("synthetic_line", "synthetic_lattice")
METRIC_RE = re.compile(r"Cycle\s+(\d+)\s+ARE\s+([-+0-9.eE]+)\s+Energy\s+([-+0-9.eE]+)")
STDOUT_CYCLES_RE = re.compile(r"Cycles:\s+(\d+)")


def parse_flat_yaml(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    current_section = ""
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        key = key.strip()
        value = value.strip()
        if not value:
            current_section = key
            continue
        if (value.startswith('"') and value.endswith('"')) or (
            value.startswith("'") and value.endswith("'")
        ):
            value = value[1:-1]
        full_key = f"{current_section}.{key}" if current_section else key
        values[full_key] = value
    return values


def canonical_json(data: Mapping[str, object]) -> str:
    return json.dumps(data, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def resolve_sim_binary(repo_root: Path) -> Path:
    candidates = [PRIMARY_SIM_BINARY]
    simulator_link = repo_root / "nocbp_simulator"
    if simulator_link.exists():
        try:
            sim_src = simulator_link.resolve()
            candidates.append(sim_src.parent / "build" / "GBPSim")
        except OSError:
            pass
    for candidate in candidates:
        if candidate.exists() and candidate.is_file():
            return candidate
    checked = ", ".join(str(c) for c in candidates)
    raise FileNotFoundError(f"GBPSim binary not found. Checked: {checked}")


def parse_last_metrics(log_text: str, workload: str) -> dict[str, float | int]:
    matches = METRIC_RE.findall(log_text)
    if not matches:
        raise ValueError(f"No metric lines found in log for workload '{workload}'")
    cycle_raw, are_raw, energy_raw = cast(tuple[str, str, str], matches[-1])
    return {
        "terminal_cycle": int(cycle_raw),
        "terminal_are": float(are_raw),
        "terminal_energy": float(energy_raw),
    }


def parse_reported_cycles(stdout_text: str, workload: str) -> int:
    match = STDOUT_CYCLES_RE.search(stdout_text)
    if not match:
        raise ValueError(f"No 'Cycles:' line found in stdout for workload '{workload}'")
    return int(match.group(1))


def run_workload(
    sim_binary: Path, config_path: Path, workload: str, repo_root: Path
) -> Mapping[str, object]:
    config_values = parse_flat_yaml(config_path)
    log_path_raw = config_values.get("logging.path", "")
    if not log_path_raw:
        raise ValueError(f"Missing logging.path in config template: {config_path}")

    log_path = Path(log_path_raw)
    if not log_path.is_absolute():
        log_path = repo_root / log_path
    log_path.parent.mkdir(parents=True, exist_ok=True)

    completed = subprocess.run(
        [str(sim_binary), str(config_path)],
        cwd=str(repo_root),
        check=False,
        text=True,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"Simulator failed for workload '{workload}' with exit code {completed.returncode}\n"
            + f"stdout:\n{completed.stdout}\n"
            + f"stderr:\n{completed.stderr}"
        )

    if not log_path.exists():
        raise FileNotFoundError(f"Expected log file not found: {log_path}")
    log_text = log_path.read_text(encoding="utf-8")

    terminal_metrics = parse_last_metrics(log_text, workload)
    reported_cycles = parse_reported_cycles(completed.stdout, workload)

    return {
        "config_template": str(config_path.relative_to(repo_root)),
        "dataset": {
            "type": config_values.get("dataset.type", ""),
            "line_nodes": int(config_values.get("dataset.line_nodes", "0")),
            "lattice_rows": int(config_values.get("dataset.lattice_rows", "0")),
            "lattice_cols": int(config_values.get("dataset.lattice_cols", "0")),
            "spacing": float(config_values.get("dataset.spacing", "0")),
            "seed": int(config_values.get("dataset.seed", "0")),
        },
        "simulation": {
            "max_cycles": int(config_values.get("simulation.max_cycles", "0")),
            "print_interval": int(config_values.get("simulation.print_interval", "0")),
            "gbp_precision": config_values.get("gbp.precision", ""),
        },
        "metrics": {
            **terminal_metrics,
            "reported_total_cycles": reported_cycles,
        },
        "log_path": str(log_path.relative_to(repo_root)),
    }


def build_artifact(repo_root: Path, sim_binary: Path, output_path: Path) -> Mapping[str, object]:
    contract_path = repo_root / "nocbp_verilator/tests/oracle/gbp_oracle_contract_phase1.yaml"
    contract_values = parse_flat_yaml(contract_path)

    compare_contract = {
        "thresholds": {
            "state_message": {
                "abs_err": float(contract_values["threshold_state_message_abs_err"]),
                "rel_err": float(contract_values["threshold_state_message_rel_err"]),
            },
            "are_energy": {
                "abs_err": float(contract_values["threshold_are_energy_abs_err"]),
                "rel_err": float(contract_values["threshold_are_energy_rel_err"]),
            },
        },
        "floating_point_policy": {
            "nan_mismatch_fail": contract_values["nan_mismatch_fail"] == "true",
            "inf_mismatch_fail": contract_values["inf_mismatch_fail"] == "true",
            "signed_zero_equivalent": contract_values["signed_zero_equivalent"] == "true",
        },
    }

    workload_results = {}
    for workload in WORKLOADS:
        config_path = repo_root / f"nocbp_verilator/tests/oracle/{workload}_phase1.yml"
        workload_results[workload] = run_workload(sim_binary, config_path, workload, repo_root)

    threshold_hash_input = canonical_json(compare_contract)
    artifact_without_checksum = {
        "schema_version": "1.0",
        "artifact_type": "nocbp_gbp_oracle_phase1",
        "metadata": {
            "generator": "nocbp_verilator/tests/tools/generate_gbp_oracle.py",
            "contract_path": "nocbp_verilator/tests/oracle/gbp_oracle_contract_phase1.yaml",
            "simulator_binary": str(sim_binary),
            "seed": 12345,
            "max_iters": 50,
            "max_cycles": 5000,
            "workloads": list(WORKLOADS),
            "thresholds_hash": sha256_text(threshold_hash_input),
        },
        "workloads": workload_results,
        "compare_contract": compare_contract,
    }

    checksum = sha256_text(canonical_json(artifact_without_checksum))
    artifact = dict(artifact_without_checksum)
    artifact["checksum"] = checksum

    output_path.parent.mkdir(parents=True, exist_ok=True)
    _ = output_path.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return artifact


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate deterministic GBP oracle artifact")
    _ = parser.add_argument(
        "--output",
        default="nocbp_verilator/tests/oracle/generated/gbp_oracle_phase1.json",
        help="Output artifact JSON path",
    )
    args = parser.parse_args()
    output_arg_obj = vars(args).get("output")
    if not isinstance(output_arg_obj, str):
        raise ValueError("--output must be a string")
    output_arg = output_arg_obj

    repo_root = Path(__file__).resolve().parents[3]
    output_path = (repo_root / output_arg).resolve()

    sim_binary = resolve_sim_binary(repo_root)
    artifact = build_artifact(repo_root, sim_binary, output_path)

    print(f"oracle artifact generated: {output_path}")
    print(f"checksum: {artifact['checksum']}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
