#!/usr/bin/env python3
"""
Compare GBP DUT output against golden reference JSON.

Supports:
  - State-by-state comparison (eta, lambda)
  - Absolute and relative error thresholds
  - Pass/fail reporting with detailed mismatch info
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def compare_float(a: float, b: float, abs_tol: float, rel_tol: float) -> tuple[bool, str]:
    if a == b:
        return True, ""
    diff = abs(a - b)
    rel = diff / max(abs(a), abs(b), 1e-12)
    if diff <= abs_tol or rel <= rel_tol:
        return True, ""
    return False, f"abs_err={diff:.6e} rel_err={rel:.6e}"


def compare_state(dut_state: dict, ref_state: dict, node_id: int,
                  abs_tol: float, rel_tol: float) -> list[str]:
    errors = []
    for key in ("eta", "lambda"):
        a = dut_state.get(key)
        b = ref_state.get(key)
        if a is None:
            errors.append(f"Node {node_id}: missing '{key}' in DUT")
            continue
        if b is None:
            errors.append(f"Node {node_id}: missing '{key}' in reference")
            continue
        ok, info = compare_float(float(a), float(b), abs_tol, rel_tol)
        if not ok:
            errors.append(
                f"Node {node_id}: {key} mismatch "
                f"DUT={a:.8f} REF={b:.8f} ({info})"
            )
    return errors


def compare(dut_path: Path, ref_path: Path, abs_tol: float = 1e-4, rel_tol: float = 1e-3) -> int:
    dut = load_json(dut_path)
    ref = load_json(ref_path)

    dut_nodes = {n["id"]: n for n in dut.get("final_state", [])}
    ref_nodes = {n["id"]: n for n in ref.get("final_state", [])}

    all_errors = []
    for nid in sorted(ref_nodes):
        if nid not in dut_nodes:
            all_errors.append(f"Node {nid}: missing in DUT output")
            continue
        dut_state = dut_nodes[nid].get("state", {})
        ref_state = ref_nodes[nid].get("state", {})
        all_errors.extend(compare_state(dut_state, ref_state, nid, abs_tol, rel_tol))

    for nid in sorted(dut_nodes):
        if nid not in ref_nodes:
            all_errors.append(f"Node {nid}: unexpected extra node in DUT output")

    if all_errors:
        print("GBP_COMPARE_FAIL")
        for e in all_errors:
            print(f"  {e}")
        return 1

    print(f"GBP_COMPARE_PASS nodes={len(ref_nodes)} abs_tol={abs_tol} rel_tol={rel_tol}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare GBP DUT vs golden reference")
    parser.add_argument("--dut", required=True, help="DUT output JSON")
    parser.add_argument("--ref", required=True, help="Golden reference JSON")
    parser.add_argument("--abs-tol", type=float, default=1e-4)
    parser.add_argument("--rel-tol", type=float, default=1e-3)
    args = parser.parse_args()
    return compare(Path(args.dut), Path(args.ref), args.abs_tol, args.rel_tol)


if __name__ == "__main__":
    sys.exit(main())
