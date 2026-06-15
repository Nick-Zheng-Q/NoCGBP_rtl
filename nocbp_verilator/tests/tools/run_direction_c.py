#!/usr/bin/env python3
"""
Direction C: End-to-end blackbox integration test.

Workflow:
  1. Generate golden reference JSON from config
  2. Run Verilator DUT, capture output
  3. Parse DUT output → JSON
  4. Compare DUT JSON vs golden reference JSON
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
VERILATOR_DIR = REPO_ROOT / "nocbp_verilator"
TOOLS_DIR = VERILATOR_DIR / "tests" / "tools"
CONFIG_DIR = VERILATOR_DIR / "tests" / "system" / "configs"
BUILD_DIR = VERILATOR_DIR / "build" / "system" / "mesh_2x2_gbp_interconnect" / "obj_dir"
SIM_BINARY = BUILD_DIR / "Vmesh_2x2_gbp_top"


def generate_golden(config_path: Path, rounds: int, output_path: Path) -> None:
    cmd = [
        sys.executable,
        str(TOOLS_DIR / "gbp_golden_ref.py"),
        "--config", str(config_path),
        "--rounds", str(rounds),
        "--output", str(output_path),
    ]
    subprocess.run(cmd, check=True)


def run_dut() -> str:
    if not SIM_BINARY.exists():
        raise FileNotFoundError(f"DUT binary not found: {SIM_BINARY}")
    result = subprocess.run(
        [str(SIM_BINARY)],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout


def parse_direction_c_block(stdout: str, test_name: str) -> dict | None:
    """Try to find a DIRECTION_C_OUTPUT_BEGIN/END block matching test_name."""
    blocks = list(re.finditer(
        r"DIRECTION_C_OUTPUT_BEGIN\n(.*?)DIRECTION_C_OUTPUT_END",
        stdout, re.DOTALL
    ))
    for m in blocks:
        block = m.group(1).strip().rstrip(",")
        if test_name in block:
            try:
                return json.loads("{" + block + "}")
            except json.JSONDecodeError:
                continue
    return None


def parse_multi_round_block(stdout: str) -> dict | None:
    """Parse the Multi-Round Local Compute section."""
    section_pattern = r"=== TC: Multi-Round Local Compute.*==="
    sections = list(re.finditer(section_pattern, stdout))
    if not sections:
        return None

    start = sections[-1].start()
    # Truncate at the next major section marker to avoid picking up later tests
    end_match = re.search(r"\n=== TC:|\nDIRECTION_C_OUTPUT_BEGIN", stdout[start:])
    section_text = stdout[start:start + end_match.start()] if end_match else stdout[start:]

    # Find last round within this section only
    round_splits = list(re.finditer(r"--\s*Round\s*(\d+)\s*--", section_text))
    if round_splits:
        last_round_start = round_splits[-1].start()
        last_round_text = section_text[last_round_start:]
    else:
        last_round_text = section_text

    pe_pattern = re.compile(r"PE(\d+):\s*eta=([-\d.eE]+),\s*lambda=([-\d.eE]+)")
    nodes = []
    for m in pe_pattern.finditer(last_round_text):
        pe_id = int(m.group(1))
        eta = float(m.group(2))
        lam = float(m.group(3))
        nodes.append({
            "id": pe_id, "pe": pe_id, "dof": 1,
            "state": {"eta": eta, "lambda": lam}
        })

    if nodes:
        return {
            "schema_version": "1.0",
            "artifact_type": "gbp_dut_output",
            "test_name": "4node_local_mesh",
            "final_state": nodes,
        }
    return None


def parse_dut_output(stdout: str, test_name: str) -> dict:
    """Parse DUT stdout and extract final beliefs."""
    result = parse_direction_c_block(stdout, test_name)
    if result is not None:
        return result

    if test_name == "4node_local_mesh":
        result = parse_multi_round_block(stdout)
        if result is not None:
            return result

    raise ValueError(f"Could not parse DUT output for test '{test_name}'")


def compare(dut_json: Path, ref_json: Path, abs_tol: float = 1e-4, rel_tol: float = 1e-3) -> int:
    cmd = [
        sys.executable,
        str(TOOLS_DIR / "compare_gbp_json.py"),
        "--dut", str(dut_json),
        "--ref", str(ref_json),
        "--abs-tol", str(abs_tol),
        "--rel-tol", str(rel_tol),
    ]
    result = subprocess.run(cmd)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Direction C end-to-end test")
    parser.add_argument("--test", default="4node_local_mesh",
                        help="Test config name (without .json)")
    parser.add_argument("--rounds", type=int, default=3,
                        help="Number of GBP rounds")
    parser.add_argument("--output-dir", default=str(VERILATOR_DIR / "tests" / "system" / "output"),
                        help="Directory for output artifacts")
    args = parser.parse_args()

    config_path = CONFIG_DIR / f"{args.test}.json"
    if not config_path.exists():
        print(f"ERROR: Config not found: {config_path}", file=sys.stderr)
        return 1

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    golden_path = output_dir / f"{args.test}_golden.json"
    dut_path = output_dir / f"{args.test}_dut.json"

    print(f"=== Direction C: {args.test} ===")
    print(f"Config: {config_path}")

    print("\n[1/4] Generating golden reference...")
    generate_golden(config_path, args.rounds, golden_path)
    print(f"  Golden reference: {golden_path}")

    print("\n[2/4] Running DUT...")
    dut_stdout = run_dut()

    print("\n[3/4] Parsing DUT output...")
    dut_result = parse_dut_output(dut_stdout, args.test)
    dut_path.write_text(json.dumps(dut_result, indent=2), encoding="utf-8")
    print(f"  DUT output: {dut_path}")

    print("\n[4/4] Comparing DUT vs golden reference...")
    rc = compare(dut_path, golden_path)

    if rc == 0:
        print("\n=== Direction C PASS ===")
    else:
        print("\n=== Direction C FAIL ===")

    return rc


if __name__ == "__main__":
    sys.exit(main())
