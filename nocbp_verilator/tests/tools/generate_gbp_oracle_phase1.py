#!/usr/bin/env python3
import argparse
import hashlib
import importlib.util
import json
import subprocess
from pathlib import Path
from typing import cast

ROOT = Path(__file__).resolve().parents[3]
ORACLE_DIR = ROOT / "nocbp_verilator" / "tests" / "oracle"
TOOLS_DIR = ROOT / "nocbp_verilator" / "tests" / "tools"
GENERATED_DIR = ORACLE_DIR / "generated"
SIM_SRC = ROOT / "nocbp_simulator"
REF_BIN = TOOLS_DIR / "gbp_oracle_ref_bin"
PE_SRC = ROOT / "nocbp_simulator" / "pe" / "ProcessingElement.cpp"

CONTRACT_PATH = TOOLS_DIR / "ba_onboarding_contract.py"
CONTRACT_SPEC = importlib.util.spec_from_file_location("ba_onboarding_contract", CONTRACT_PATH)
if CONTRACT_SPEC is None or CONTRACT_SPEC.loader is None:
    raise RuntimeError(f"Failed to load contract module from {CONTRACT_PATH}")
CONTRACT_MODULE = importlib.util.module_from_spec(CONTRACT_SPEC)
CONTRACT_SPEC.loader.exec_module(CONTRACT_MODULE)
BA_DATASET_REL_PATH = cast(str, CONTRACT_MODULE.BA_DATASET_REL_PATH)
BA_FIXED_ITERS = cast(int, CONTRACT_MODULE.BA_FIXED_ITERS)
BA_WORKLOAD_TOKEN = cast(str, CONTRACT_MODULE.BA_WORKLOAD_TOKEN)

FUNCTION_ANCHORS = {
    "compute_variable_node": 805,
    "compute_factor_node": 833,
}


def build_reference_binary() -> None:
    cpp_main = TOOLS_DIR / "gbp_oracle_ref_main.cpp"

    gbp_sources = [
        SIM_SRC / "gbp" / "FactorGraph.cpp",
        SIM_SRC / "gbp" / "FactorNode.cpp",
        SIM_SRC / "gbp" / "BAFactorGraph.cpp",
        SIM_SRC / "gbp" / "LinearFactorGraph.cpp",
        SIM_SRC / "gbp" / "VariableNode.cpp",
        SIM_SRC / "gbp" / "factor_utils.cpp",
        SIM_SRC / "gbp" / "precision.cpp",
        SIM_SRC / "gbp" / "reprojection_utils.cpp",
    ]
    utils_sources = [
        SIM_SRC / "utils" / "Logger.cpp",
        SIM_SRC / "utils" / "read_balfile.cpp",
        SIM_SRC / "utils" / "read_g2o.cpp",
    ]

    cmd = [
        "g++",
        "-std=c++17",
        str(cpp_main),
        *[str(p) for p in gbp_sources],
        *[str(p) for p in utils_sources],
        "-I" + str(SIM_SRC),
        "-I/usr/include/eigen3",
        "-O2",
        "-lfmt",
        "-lspdlog",
        "-o",
        str(REF_BIN),
    ]
    subprocess.run(cmd, cwd=ROOT, check=True)


def run_workload(
    workload: str,
    seed: int,
    line_nodes: int,
    rows: int,
    cols: int,
    max_iters: int,
    dataset_path: Path | None,
) -> dict[str, object]:
    cmd = [
        str(REF_BIN),
        workload,
        str(max_iters),
        str(seed),
        str(line_nodes),
        str(rows),
        str(cols),
    ]
    if dataset_path is not None:
        cmd.append(str(dataset_path))

    proc = subprocess.run(
        cmd,
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )

    lines = [ln.strip() for ln in proc.stdout.splitlines() if ln.strip()]
    metrics: dict[str, float | int] = {}
    for ln in lines:
        if "=" not in ln:
            continue
        key, value = ln.split("=", 1)
        if key == "FINAL_ARE":
            metrics["final_are"] = float(value)
        elif key == "FINAL_ENERGY":
            metrics["final_energy"] = float(value)
        elif key == "ITERS":
            metrics["iters"] = int(value)

    if not {"final_are", "final_energy", "iters"}.issubset(metrics):
        raise RuntimeError(f"Failed to parse metrics for {workload}: {proc.stdout}")

    if dataset_path is None:
        raise RuntimeError(f"dataset_path is required for workload '{workload}'")
    config_checksum = hashlib.sha256(dataset_path.read_bytes()).hexdigest()
    return {
        "input_checksum": {
            "algorithm": "sha256",
            "value": config_checksum,
        },
        "metrics": metrics,
    }


def resolve_ba_dataset_path(dataset_arg: str) -> Path:
    expected = (ROOT / BA_DATASET_REL_PATH).resolve()
    candidate = Path(dataset_arg)
    resolved = (ROOT / candidate).resolve() if not candidate.is_absolute() else candidate.resolve()
    if resolved != expected:
        raise RuntimeError(
            f"dataset-scope: only '{BA_DATASET_REL_PATH}' is supported for {BA_WORKLOAD_TOKEN}; got '{dataset_arg}'"
        )
    if not resolved.is_file():
        raise FileNotFoundError(
            f"BA dataset is required at '{BA_DATASET_REL_PATH}' (resolved: {resolved})"
        )
    return resolved


def compute_artifact_checksum(doc: dict[str, object]) -> str:
    shadow = json.loads(json.dumps(doc))
    shadow["checksum"]["value"] = ""
    payload = json.dumps(shadow, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def deterministic_fraction(seed: int, tag: str, low: float, high: float) -> float:
    raw = hashlib.sha256(f"{seed}:{tag}".encode("utf-8")).digest()
    unit = int.from_bytes(raw[:8], "big") / float(2**64 - 1)
    return low + (high - low) * unit


def get_anchor_checksum(src: Path, anchor_line: int, span: int = 8) -> dict[str, object]:
    lines = src.read_text(encoding="utf-8").splitlines()
    if anchor_line < 1 or anchor_line > len(lines):
        raise RuntimeError(f"Anchor line out of range: {anchor_line}")
    end_line = min(anchor_line - 1 + span, len(lines))
    snippet = "\n".join(lines[anchor_line - 1 : end_line])
    return {
        "line": anchor_line,
        "span_lines": end_line - (anchor_line - 1),
        "checksum": {
            "algorithm": "sha256",
            "value": hashlib.sha256(snippet.encode("utf-8")).hexdigest(),
        },
    }


def make_function_placeholder_cases(seed: int, section: str) -> list[dict[str, object]]:
    case0_tag = f"{section}:case0"
    case1_tag = f"{section}:case1"
    return [
        {
            "scenario": f"{section}_staged_case_0",
            "inputs": {
                "vector_id": 0,
                "stale_mask": [0, 1, 0],
                "new_mask": [1, 0, 1],
            },
            "expected": {
                "belief_eta_0": round(deterministic_fraction(seed, f"{case0_tag}:eta", -2.0, 2.0), 8),
                "belief_lam_00": round(deterministic_fraction(seed, f"{case0_tag}:lam", 0.1, 4.0), 8),
                "can_compute": True,
            },
        },
        {
            "scenario": f"{section}_staged_case_1",
            "inputs": {
                "vector_id": 1,
                "stale_mask": [1, 1, 0],
                "new_mask": [1, 1, 0],
            },
            "expected": {
                "belief_eta_0": round(deterministic_fraction(seed, f"{case1_tag}:eta", -2.0, 2.0), 8),
                "belief_lam_00": round(deterministic_fraction(seed, f"{case1_tag}:lam", 0.1, 4.0), 8),
                "can_compute": True,
            },
        },
    ]


def make_function_oracle(seed: int) -> dict[str, object]:
    sections: dict[str, object] = {}
    for section_name, anchor_line in FUNCTION_ANCHORS.items():
        sections[section_name] = {
            "mode": "staged_placeholder_v1",
            "anchor": get_anchor_checksum(PE_SRC, anchor_line),
            "cases": make_function_placeholder_cases(seed, section_name),
        }

    return {
        "source": "nocbp_simulator/pe/ProcessingElement.cpp",
        "runtime_hookup": "pending",
        "deterministic_seed": seed,
        "sections": sections,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate deterministic GBP phase-1 oracle artifact")
    parser.add_argument("--seed", type=int, default=12345, help="Deterministic seed")
    parser.add_argument(
        "--workload",
        required=True,
        choices=(BA_WORKLOAD_TOKEN,),
        help="Workload token to generate",
    )
    parser.add_argument(
        "--dataset",
        required=True,
        help="BA dataset path (must resolve to data/fr1desk_small.txt)",
    )
    parser.add_argument(
        "--metadata-seed",
        type=int,
        default=None,
        help="Override seed written to metadata.determinism.seed (guarded: must match --seed)",
    )
    parser.add_argument(
        "--output",
        default=str(ORACLE_DIR / "gbp_oracle_phase1.json"),
        help="Output oracle artifact JSON path",
    )
    args = parser.parse_args()

    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    dataset_path = resolve_ba_dataset_path(args.dataset)
    build_reference_binary()

    workloads = {
        args.workload: run_workload(
            args.workload,
            args.seed,
            16,
            4,
            4,
            BA_FIXED_ITERS,
            dataset_path,
        )
    }

    metadata_seed = args.seed if args.metadata_seed is None else args.metadata_seed
    if metadata_seed != args.seed:
        raise RuntimeError(
            f"seed-mismatch: runtime --seed={args.seed} does not match metadata seed {metadata_seed}"
        )

    deterministic_generated_at = f"seed-{args.seed:010d}"

    artifact: dict[str, object] = {
        "schema_version": "1.0",
        "artifact_type": "oracle_task4",
        "metadata": {
            "run_id": "phase1-deterministic",
            "generated_at_utc": deterministic_generated_at,
            "git_commit": "not-recorded",
            "tool_version": "generate_gbp_oracle_phase1.py+gbp_oracle_ref_main.cpp",
            "determinism": {
                "seed": metadata_seed,
                "workloads": [args.workload],
                "max_iters": BA_FIXED_ITERS,
                "max_cycles": 5000,
            },
        },
        "workloads": workloads,
        "function_oracle": make_function_oracle(args.seed),
        "compare_contract": {
            "thresholds": {
                "final_are": {"abs_tol": 1e-3, "rel_tol": 1e-2},
                "final_energy": {"abs_tol": 1e-3, "rel_tol": 1e-2},
                "iters": {"abs_tol": 0.0, "rel_tol": 0.0},
            },
            "pass_rule": "all_metrics_within_thresholds",
        },
        "checksum": {
            "algorithm": "sha256",
            "scope": "artifact_without_checksum_field",
            "value": "",
        },
    }

    checksum_block = cast(dict[str, object], artifact["checksum"])
    checksum_block["value"] = compute_artifact_checksum(artifact)

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"oracle artifact written: {out_path}")
    print(f"checksum: {checksum_block['value']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
