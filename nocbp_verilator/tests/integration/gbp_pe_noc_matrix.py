#!/usr/bin/env python3

import argparse
import pathlib
import subprocess
import sys
from typing import Final, TypedDict, cast


class MatrixRow(TypedDict):
    name: str
    command: str
    expected_exit: int
    marker: str


ROWS: Final[tuple[MatrixRow, ...]] = (
    {
        "name": "endpoint_baseline",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=endpoint_noc",
        "expected_exit": 0,
        "marker": "BASELINE_PASS endpoint_noc",
    },
    {
        "name": "pe_top_regression",
        "command": 'make -C nocbp_verilator run LEVEL=integration TEST=pe_top_integration VERILATOR="verilator -Wno-fatal -Wno-WIDTHCONCAT -Wno-EOFNEWLINE"',
        "expected_exit": 0,
        "marker": "pe_top integration: PASS",
    },
    {
        "name": "gbp_pe_unit",
        "command": "make -C nocbp_verilator run LEVEL=unit TEST=gbp_pe",
        "expected_exit": 0,
        "marker": "gbp_pe: PASS",
    },
    {
        "name": "single_pe_gbp_line",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_single_pe_gbp WORKLOAD=synthetic_line SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_single_pe_gbp: PASS workload=synthetic_line",
    },
    {
        "name": "single_pe_gbp_lattice",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_single_pe_gbp WORKLOAD=synthetic_lattice SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_single_pe_gbp: PASS workload=synthetic_lattice",
    },
    {
        "name": "partition_export_line_scotch",
        "command": "python3 nocbp_verilator/tests/tools/export_gbp_partition_map.py --workload synthetic_line --seed 12345 --pes 2 --mode scotch --output nocbp_verilator/tests/oracle/generated/synthetic_line_partition_2pe.json",
        "expected_exit": 0,
        "marker": "mode: scotch",
    },
    {
        "name": "partition_cross_edge_check_line",
        "command": "python3 nocbp_verilator/tests/tools/check_gbp_partition_cross_edges.py --mapping nocbp_verilator/tests/oracle/generated/synthetic_line_partition_2pe.json",
        "expected_exit": 0,
        "marker": "cross_pe_edges:",
    },
    {
        "name": "ingress_real_path",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_noc_ingress_spm",
        "expected_exit": 0,
        "marker": "ORDERED_WRITE_MARKER",
    },
    {
        "name": "ingress_order_negative",
        "command": "GBP_PE_NOC_INGRESS_ORDER_NEGATIVE=1 make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_noc_ingress_spm",
        "expected_exit": 2,
        "marker": "ORDERING_ERROR_MARKER",
    },
    {
        "name": "mesh_2pe_positive",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe: PASS src=(0,0) dst=(1,0)",
    },
    {
        "name": "mesh_2pe_order_negative",
        "command": "GBP_PE_MESH_EXPECT_ORDER_ERROR=1 make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe",
        "expected_exit": 2,
        "marker": "ORDERING_ERROR_MARKER",
    },
    {
        "name": "mesh_2pe_gbp_line",
        "command": "PARTITION=tests/oracle/generated/synthetic_line_partition_2pe.json make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_gbp WORKLOAD=synthetic_line SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_gbp: PASS workload=synthetic_line",
    },
    {
        "name": "mesh_2pe_superstep_line",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_superstep WORKLOAD=synthetic_line SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_superstep: PASS",
    },
    {
        "name": "mesh_2x2_positive",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2x2",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2x2: PASS src=(0,0) dst=(1,1)",
    },
    {
        "name": "mesh_2x2_stall_recovery",
        "command": "GBP_PE_MESH_FORCE_STALL=1 make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2x2",
        "expected_exit": 0,
        "marker": "recovered_from_stall=1",
    },
    {
        "name": "mesh_2x2_order_negative",
        "command": "GBP_PE_MESH_EXPECT_ORDER_ERROR=1 make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2x2",
        "expected_exit": 2,
        "marker": "ORDERING_ERROR_MARKER",
    },
    {
        "name": "egress_positive",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_compute_done_egress",
        "expected_exit": 0,
        "marker": "gbp_pe_compute_done_egress: PASS txn_id=",
    },
    {
        "name": "egress_stall_recovery",
        "command": "GBP_PE_EGRESS_FORCE_NOC_STALL=1 make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_compute_done_egress",
        "expected_exit": 0,
        "marker": "recovered_from_stall=1",
    },
    {
        "name": "egress_mismatch_negative",
        "command": "GBP_PE_EGRESS_EXPECT_MISMATCH=1 make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_compute_done_egress",
        "expected_exit": 2,
        "marker": "PACKET_COUNT_MISMATCH_MARKER",
    },
    {
        "name": "mesh_2pe_convergence_line",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_convergence WORKLOAD=synthetic_line SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_convergence: PASS workload=synthetic_line",
    },
    {
        "name": "mesh_2pe_convergence_lattice",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_convergence WORKLOAD=synthetic_lattice SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_convergence: PASS workload=synthetic_lattice",
    },
)


def project_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[3]


def command_exit_matches(actual: int, expected: int) -> bool:
    return actual == expected


def main() -> int:
    parser = argparse.ArgumentParser()
    _ = parser.add_argument("--output", required=True, type=str)
    args = parser.parse_args()
    output_arg = cast(str, args.output)

    repo_root = project_root()
    output_path = pathlib.Path(output_arg)
    if not output_path.is_absolute():
        output_path = repo_root / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)

    blocks: list[str] = []
    all_ok = True

    for row in ROWS:
        proc = subprocess.run(
            ["bash", "-lc", row["command"]],
            cwd=repo_root,
            text=True,
            capture_output=True,
        )
        combined = proc.stdout + proc.stderr
        marker_found = row["marker"] in combined
        exit_ok = command_exit_matches(proc.returncode, row["expected_exit"])
        row_ok = marker_found and exit_ok
        all_ok = all_ok and row_ok

        blocks.append("\n".join((
            f"ROW: {row['name']}",
            f"COMMAND: {row['command']}",
            f"EXPECTED_EXIT: {row['expected_exit']}",
            f"EXIT_CODE: {proc.returncode}",
            f"REQUIRED_MARKER: {row['marker']}",
            f"MARKER_FOUND: {'yes' if marker_found else 'no'}",
            f"ROW_STATUS: {'PASS' if row_ok else 'FAIL'}",
            "OUTPUT_BEGIN",
            combined.rstrip(),
            "OUTPUT_END",
        )))

    _ = output_path.write_text("\n\n".join(blocks) + "\n", encoding="utf-8")

    if all_ok:
        print(f"GBP_PE_NOC_MATRIX_PASS rows={len(ROWS)} output={output_path}")
        return 0

    print(f"GBP_PE_NOC_MATRIX_FAIL rows={len(ROWS)} output={output_path}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    exit_code = main()
    raise SystemExit(exit_code)
