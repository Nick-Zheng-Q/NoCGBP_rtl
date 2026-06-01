#!/usr/bin/env python3

import argparse
import pathlib
import subprocess
import sys
import concurrent.futures
import time
import xml.etree.ElementTree as ET
from datetime import datetime
from typing import Final, NotRequired, Sequence, TypedDict, cast


class MatrixRow(TypedDict):
    name: str
    command: str
    expected_exit: int
    marker: str
    forbidden_markers: NotRequired[tuple[str, ...]]


class ScaleTopology(TypedDict):
    pe_count: int
    mesh_x: int
    mesh_y: int


class ScaleWorkload(TypedDict):
    workload: str
    row_suffix: str
    partition_flags: tuple[str, ...]
    runtime_flags: tuple[str, ...]
    run_shapes: tuple[str, ...]


class ScaleRunShape(TypedDict):
    row_suffix: str
    test: str
    marker_test: str
    forbidden_markers: tuple[str, ...]


SMOKE_ROWS: Final[tuple[MatrixRow, ...]] = (
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
        "name": "single_pe_retire_order_line",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_single_pe_gbp WORKLOAD=synthetic_line SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_single_pe_gbp: PASS workload=synthetic_line",
        "forbidden_markers": ("PREMATURE_RETIRE_MARKER",),
    },
    {
        "name": "single_pe_retire_order_lattice",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_single_pe_gbp WORKLOAD=synthetic_lattice SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_single_pe_gbp: PASS workload=synthetic_lattice",
        "forbidden_markers": ("PREMATURE_RETIRE_MARKER",),
    },
    {
        "name": "ba_single_pe_fr1desk_small",
        "command": "make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_single_pe_gbp WORKLOAD=bal_fr1desk_small DATASET=data/fr1desk_small.txt SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_single_pe_gbp: PASS workload=bal_fr1desk_small",
    },
    {
        "name": "partition_export_line_scotch",
        "command": "python3 nocbp_verilator/tests/tools/export_gbp_partition_map.py --workload synthetic_line --seed 12345 --pes 2 --mesh-x 2 --mesh-y 1 --mode scotch --output nocbp_verilator/tests/oracle/generated/synthetic_line_partition_2pe_2x1.json",
        "expected_exit": 0,
        "marker": "mode: scotch",
    },
    {
        "name": "partition_cross_edge_check_line",
        "command": "python3 nocbp_verilator/tests/tools/check_gbp_partition_cross_edges.py --mapping nocbp_verilator/tests/oracle/generated/synthetic_line_partition_2pe_2x1.json",
        "expected_exit": 0,
        "marker": "cross_pe_edges:",
    },
    {
        "name": "partition_export_line_factor_variable",
        "command": "python3 nocbp_verilator/tests/tools/export_gbp_partition_map.py --workload synthetic_line --seed 12345 --pes 2 --mesh-x 2 --mesh-y 1 --mode factor_variable_split --output nocbp_verilator/tests/oracle/generated/synthetic_line_partition_2pe_2x1_factor_variable.json",
        "expected_exit": 0,
        "marker": "mode: factor_variable_split",
    },
    {
        "name": "partition_cross_edge_check_line_factor_variable",
        "command": "python3 nocbp_verilator/tests/tools/check_gbp_partition_cross_edges.py --mapping nocbp_verilator/tests/oracle/generated/synthetic_line_partition_2pe_2x1_factor_variable.json",
        "expected_exit": 0,
        "marker": "cross_pe_edges:",
    },
    {
        "name": "partition_export_lattice_scotch",
        "command": "python3 nocbp_verilator/tests/tools/export_gbp_partition_map.py --workload synthetic_lattice --seed 12345 --pes 2 --mesh-x 2 --mesh-y 1 --mode scotch --output nocbp_verilator/tests/oracle/generated/synthetic_lattice_partition_2pe_2x1.json",
        "expected_exit": 0,
        "marker": "mode: scotch",
    },
    {
        "name": "partition_cross_edge_check_lattice",
        "command": "python3 nocbp_verilator/tests/tools/check_gbp_partition_cross_edges.py --mapping nocbp_verilator/tests/oracle/generated/synthetic_lattice_partition_2pe_2x1.json",
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
        "command": "PARTITION=tests/oracle/generated/synthetic_line_partition_2pe_2x1.json make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_gbp WORKLOAD=synthetic_line SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_gbp: PASS workload=synthetic_line",
    },
    {
        "name": "mesh_2pe_event_correlation_line",
        "command": "PARTITION=tests/oracle/generated/synthetic_line_partition_2pe_2x1.json make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_gbp WORKLOAD=synthetic_line SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_gbp: PASS workload=synthetic_line",
        "forbidden_markers": ("EVENT_DIVERGENCE_MARKER",),
    },
    {
        "name": "mesh_2pe_event_correlation_lattice",
        "command": "PARTITION=tests/oracle/generated/synthetic_lattice_partition_2pe_2x1.json make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_gbp WORKLOAD=synthetic_lattice SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_gbp: PASS workload=synthetic_lattice",
        "forbidden_markers": ("EVENT_DIVERGENCE_MARKER",),
    },
    {
        "name": "ba_mesh_2pe_distributed_fr1desk_small",
        "command": "PARTITION=tests/oracle/generated/bal_fr1desk_small_partition_2pe_2x1.json make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_gbp WORKLOAD=bal_fr1desk_small DATASET=data/fr1desk_small.txt SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_gbp: PASS workload=bal_fr1desk_small",
        "forbidden_markers": ("EVENT_DIVERGENCE_MARKER",),
    },
    {
        "name": "mesh_2pe_fixed_iters_line",
        "command": "PARTITION=tests/oracle/generated/synthetic_line_partition_2pe_2x1_factor_variable.json make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_convergence WORKLOAD=synthetic_line SEED=12345",
        "expected_exit": 0,
        "marker": "iterations=50 stop_reason=fixed_iters",
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
        "name": "egress_spm_stall_direct_origin",
        "command": "GBP_PE_EGRESS_FORCE_SPM_STALL=1 make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_compute_done_egress",
        "expected_exit": 0,
        "marker": "egress_precedes_persistence=1 persistence_secondary=1",
    },
    {
        "name": "egress_mismatch_negative",
        "command": "GBP_PE_EGRESS_EXPECT_MISMATCH=1 make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_compute_done_egress",
        "expected_exit": 2,
        "marker": "PACKET_COUNT_MISMATCH_MARKER",
    },
    {
        "name": "mesh_2pe_convergence_line",
        "command": "PARTITION=tests/oracle/generated/synthetic_line_partition_2pe_2x1_factor_variable.json make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_convergence WORKLOAD=synthetic_line SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_convergence: PASS pe_count=2 mesh=2x1 workload=synthetic_line",
        "forbidden_markers": ("EVENT_DIVERGENCE_MARKER", "POST_STOP_TRAFFIC_MARKER"),
    },
    {
        "name": "mesh_2pe_convergence_lattice",
        "command": "PARTITION=tests/oracle/generated/synthetic_lattice_partition_2pe_2x1.json make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_convergence WORKLOAD=synthetic_lattice SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_convergence: PASS pe_count=2 mesh=2x1 workload=synthetic_lattice",
        "forbidden_markers": ("EVENT_DIVERGENCE_MARKER", "POST_STOP_TRAFFIC_MARKER"),
    },
    {
        "name": "ba_mesh_2pe_convergence_fr1desk_small",
        "command": "PARTITION=tests/oracle/generated/bal_fr1desk_small_partition_2pe_2x1.json make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe_convergence WORKLOAD=bal_fr1desk_small DATASET=data/fr1desk_small.txt SEED=12345",
        "expected_exit": 0,
        "marker": "gbp_pe_mesh_2pe_convergence: PASS pe_count=2 mesh=2x1 workload=bal_fr1desk_small",
        "forbidden_markers": ("EVENT_DIVERGENCE_MARKER", "POST_STOP_TRAFFIC_MARKER"),
    },
)


SCALE_TOPOLOGIES: Final[tuple[ScaleTopology, ...]] = (
    {"pe_count": 16, "mesh_x": 4, "mesh_y": 4},
    {"pe_count": 32, "mesh_x": 8, "mesh_y": 4},
)

SCALE_WORKLOADS: Final[tuple[ScaleWorkload, ...]] = (
    {
        "workload": "synthetic_line",
        "row_suffix": "line",
        "partition_flags": ("--line-nodes 16",),
        "runtime_flags": (),
        "run_shapes": ("event_correlation", "convergence"),
    },
    {
        "workload": "synthetic_lattice",
        "row_suffix": "lattice",
        "partition_flags": ("--lattice-rows 4", "--lattice-cols 4"),
        "runtime_flags": (),
        "run_shapes": ("event_correlation", "convergence"),
    },
    {
        "workload": "bal_fr1desk_small",
        "row_suffix": "bal_fr1desk_small",
        "partition_flags": ("--dataset data/fr1desk_small.txt",),
        "runtime_flags": ("DATASET=data/fr1desk_small.txt",),
        "run_shapes": ("event_correlation",),
    },
)

SCALE_RUN_SHAPES: Final[tuple[ScaleRunShape, ...]] = (
    {
        "row_suffix": "event_correlation",
        "test": "gbp_pe_mesh_gbp",
        "marker_test": "gbp_pe_mesh_gbp",
        "forbidden_markers": ("EVENT_DIVERGENCE_MARKER",),
    },
    {
        "row_suffix": "convergence",
        "test": "gbp_pe_mesh_convergence",
        "marker_test": "gbp_pe_mesh_2pe_convergence",
        "forbidden_markers": ("EVENT_DIVERGENCE_MARKER", "POST_STOP_TRAFFIC_MARKER"),
    },
)

SCALE_RUN_SHAPE_BY_SUFFIX: Final[dict[str, ScaleRunShape]] = {
    run_shape["row_suffix"]: run_shape for run_shape in SCALE_RUN_SHAPES
}


def scale_partition_artifact(topology: ScaleTopology, workload: ScaleWorkload) -> str:
    pe_count = topology["pe_count"]
    mesh_x = topology["mesh_x"]
    mesh_y = topology["mesh_y"]
    workload_name = workload["workload"]
    return (
        "nocbp_verilator/tests/oracle/generated/"
        f"{workload_name}_partition_{pe_count}pe_{mesh_x}x{mesh_y}.json"
    )


def build_scale_rows() -> tuple[MatrixRow, ...]:
    rows: list[MatrixRow] = []
    for topology in SCALE_TOPOLOGIES:
        pe_count = topology["pe_count"]
        mesh_x = topology["mesh_x"]
        mesh_y = topology["mesh_y"]
        mesh_token = f"{mesh_x}x{mesh_y}"

        for workload in SCALE_WORKLOADS:
            workload_name = workload["workload"]
            workload_suffix = workload["row_suffix"]
            partition_artifact = scale_partition_artifact(topology, workload)

            partition_export_parts = [
                "python3 nocbp_verilator/tests/tools/export_gbp_partition_map.py",
                f"--workload {workload_name}",
                "--seed 12345",
                f"--pes {pe_count}",
                f"--mesh-x {mesh_x}",
                f"--mesh-y {mesh_y}",
                *workload["partition_flags"],
                "--mode scotch",
                f"--output {partition_artifact}",
            ]
            rows.append({
                "name": f"partition_export_{pe_count}pe_{workload_suffix}",
                "command": " ".join(partition_export_parts),
                "expected_exit": 0,
                "marker": "mode: scotch",
            })

            rows.append({
                "name": f"partition_cross_edge_check_{pe_count}pe_{workload_suffix}",
                "command": (
                    "python3 nocbp_verilator/tests/tools/check_gbp_partition_cross_edges.py "
                    f"--mapping {partition_artifact}"
                ),
                "expected_exit": 0,
                "marker": "cross_pe_edges:",
            })

            for run_shape_suffix in workload["run_shapes"]:
                run_shape = SCALE_RUN_SHAPE_BY_SUFFIX[run_shape_suffix]
                run_command_parts = [
                    f"PARTITION=tests/oracle/generated/{workload_name}_partition_{pe_count}pe_{mesh_token}.json",
                    *workload["runtime_flags"],
                    "make -C nocbp_verilator run LEVEL=integration",
                    f"TEST={run_shape['test']}",
                    f"PE_COUNT={pe_count}",
                    f"MESH_X={mesh_x}",
                    f"MESH_Y={mesh_y}",
                    f"WORKLOAD={workload_name}",
                    "SEED=12345",
                ]
                rows.append({
                    "name": f"mesh_{pe_count}pe_{run_shape['row_suffix']}_{workload_suffix}",
                    "command": " ".join(run_command_parts),
                    "expected_exit": 0,
                    "marker": (
                        f"{run_shape['marker_test']}: PASS "
                        f"pe_count={pe_count} mesh={mesh_token} workload={workload_name}"
                    ),
                    "forbidden_markers": run_shape["forbidden_markers"],
                })

    return tuple(rows)


SCALE_ROWS: Final[tuple[MatrixRow, ...]] = build_scale_rows()


def rows_for_suite(suite: str) -> tuple[MatrixRow, ...]:
    if suite == "smoke":
        return SMOKE_ROWS
    if suite == "scale":
        return SCALE_ROWS
    return SMOKE_ROWS + SCALE_ROWS


def project_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[3]


def command_exit_matches(actual: int, expected: int) -> bool:
    return actual == expected


class MatrixResult(TypedDict):
    row: MatrixRow
    exit_code: int
    stdout: str
    stderr: str
    marker_found: bool
    forbidden_found: tuple[str, ...]
    row_ok: bool
    duration: float


def run_single_row(repo_root: pathlib.Path, row: MatrixRow, timeout: int = 1800) -> MatrixResult:
    """Execute a single test row and return results."""
    start_time = time.time()
    try:
        proc = subprocess.run(
            ["bash", "-lc", row["command"]],
            cwd=repo_root,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
        duration = time.time() - start_time
        combined = proc.stdout + proc.stderr
        marker_found = row["marker"] in combined
        forbidden_markers = row.get("forbidden_markers", ())
        forbidden_found = tuple(marker for marker in forbidden_markers if marker in combined)
        exit_ok = command_exit_matches(proc.returncode, row["expected_exit"])
        row_ok = marker_found and exit_ok and not forbidden_found
        
        return {
            "row": row,
            "exit_code": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "marker_found": marker_found,
            "forbidden_found": forbidden_found,
            "row_ok": row_ok,
            "duration": duration,
        }
    except subprocess.TimeoutExpired:
        duration = time.time() - start_time
        return {
            "row": row,
            "exit_code": -1,
            "stdout": "",
            "stderr": f"TIMEOUT: test exceeded {timeout} seconds",
            "marker_found": False,
            "forbidden_found": (),
            "row_ok": False,
            "duration": duration,
        }
    except Exception as e:
        duration = time.time() - start_time
        return {
            "row": row,
            "exit_code": -1,
            "stdout": "",
            "stderr": f"ERROR: {str(e)}",
            "marker_found": False,
            "forbidden_found": (),
            "row_ok": False,
            "duration": duration,
        }


def run_matrix_parallel(rows: tuple[MatrixRow, ...], max_workers: int = 4, timeout: int = 1800) -> tuple[MatrixResult, ...]:
    """Execute test rows in parallel using ThreadPoolExecutor."""
    repo_root = project_root()
    results = []
    
    print(f"Running {len(rows)} tests with {max_workers} parallel workers (timeout={timeout}s per test)")
    
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        # 提交所有任务
        future_to_row = {
            executor.submit(run_single_row, repo_root, row, timeout): row 
            for row in rows
        }
        
        # 收集结果
        completed = 0
        for future in concurrent.futures.as_completed(future_to_row):
            try:
                result = future.result()
                results.append(result)
                completed += 1
                
                # 打印进度
                row = result["row"]
                status = "PASS" if result["row_ok"] else "FAIL"
                print(f"[{completed}/{len(rows)}] {row['name']}: {status} ({result['duration']:.1f}s)")
                
            except Exception as e:
                row = future_to_row[future]
                results.append({
                    "row": row,
                    "exit_code": -1,
                    "stdout": "",
                    "stderr": str(e),
                    "marker_found": False,
                    "forbidden_found": (),
                    "row_ok": False,
                    "duration": 0.0,
                })
    
    # 按原始顺序排序结果
    row_order = {id(row): i for i, row in enumerate(rows)}
    results.sort(key=lambda r: row_order.get(id(r["row"]), 0))
    
    return tuple(results)


def generate_junit_xml(results: Sequence[MatrixResult], output_path: str, suite_name: str = "verilator_tests"):
    """Generate JUnit XML report from test results."""
    
    # 创建根元素
    testsuites = ET.Element("testsuites")
    testsuite = ET.SubElement(testsuites, "testsuite")
    testsuite.set("name", suite_name)
    testsuite.set("tests", str(len(results)))
    
    # 计数器
    failures = sum(1 for r in results if not r["row_ok"])
    testsuite.set("failures", str(failures))
    testsuite.set("errors", "0")
    testsuite.set("timestamp", datetime.now().isoformat())
    
    # 计算总时间
    total_time = sum(r["duration"] for r in results)
    testsuite.set("time", f"{total_time:.3f}")
    
    # 添加测试用例
    for result in results:
        row = result["row"]
        testcase = ET.SubElement(testsuite, "testcase")
        testcase.set("name", row["name"])
        testcase.set("classname", f"verilator.{row.get('level', 'tests')}")
        testcase.set("time", f"{result['duration']:.3f}")
        
        if not result["row_ok"]:
            failure = ET.SubElement(testcase, "failure")
            failure.set("message", f"Test failed: {row['name']}")
            failure.set("type", "AssertionError")
            
            # 添加详细信息
            details = []
            if not result["marker_found"]:
                details.append(f"Required marker not found: {row['marker']}")
            if result["exit_code"] != row["expected_exit"]:
                details.append(f"Exit code mismatch: expected {row['expected_exit']}, got {result['exit_code']}")
            if result["forbidden_found"]:
                details.append(f"Forbidden markers found: {', '.join(result['forbidden_found'])}")
            
            failure.text = "\n".join(details)
        
        # 添加标准输出
        combined_output = result["stdout"] + result["stderr"]
        if combined_output.strip():
            system_out = ET.SubElement(testcase, "system-out")
            system_out.text = combined_output
    
    # 写入文件
    tree = ET.ElementTree(testsuites)
    ET.indent(tree, space="  ")
    tree.write(output_path, encoding="utf-8", xml_declaration=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Matrix test runner for verilator tests")
    _ = parser.add_argument("--output", required=True, type=str, help="Output file path")
    _ = parser.add_argument("--suite", choices=("all", "smoke", "scale"), default="all", type=str, help="Test suite to run")
    _ = parser.add_argument("--parallel", type=int, default=1, help="Number of parallel workers (default: 1, sequential)")
    _ = parser.add_argument("--timeout", type=int, default=1800, help="Timeout per test in seconds (default: 1800)")
    _ = parser.add_argument("--verbose", action="store_true", help="Verbose output")
    _ = parser.add_argument("--junit-xml", type=str, help="Generate JUnit XML report to specified file")
    args = parser.parse_args()
    
    output_arg = cast(str, args.output)
    suite = cast(str, args.suite)
    parallel = cast(int, args.parallel)
    timeout = cast(int, args.timeout)
    verbose = cast(bool, args.verbose)
    junit_xml_path = cast(str, args.junit_xml) if args.junit_xml else None
    rows = rows_for_suite(suite)

    repo_root = project_root()
    output_path = pathlib.Path(output_arg)
    if not output_path.is_absolute():
        output_path = repo_root / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)

    start_time = time.time()
    
    # 执行测试
    if parallel > 1:
        results = run_matrix_parallel(rows, max_workers=parallel, timeout=timeout)
    else:
        # 顺序执行
        results = []
        for i, row in enumerate(rows, 1):
            if verbose:
                print(f"[{i}/{len(rows)}] Running {row['name']}...")
            result = run_single_row(repo_root, row, timeout=timeout)
            results.append(result)
            if verbose:
                status = "PASS" if result["row_ok"] else "FAIL"
                print(f"[{i}/{len(rows)}] {row['name']}: {status} ({result['duration']:.1f}s)")
        results = tuple(results)
    
    total_duration = time.time() - start_time
    
    # 生成报告
    blocks: list[str] = []
    all_ok = True
    pass_count = 0
    fail_count = 0
    
    for result in results:
        row = result["row"]
        row_ok = result["row_ok"]
        all_ok = all_ok and row_ok
        
        if row_ok:
            pass_count += 1
        else:
            fail_count += 1
        
        blocks.append("\n".join((
            f"ROW: {row['name']}",
            f"COMMAND: {row['command']}",
            f"EXPECTED_EXIT: {row['expected_exit']}",
            f"EXIT_CODE: {result['exit_code']}",
            f"REQUIRED_MARKER: {row['marker']}",
            f"MARKER_FOUND: {'yes' if result['marker_found'] else 'no'}",
            f"FORBIDDEN_MARKERS: {','.join(row.get('forbidden_markers', ()))}",
            f"FORBIDDEN_FOUND: {','.join(result['forbidden_found'])}",
            f"ROW_STATUS: {'PASS' if row_ok else 'FAIL'}",
            f"DURATION: {result['duration']:.1f}s",
            "OUTPUT_BEGIN",
            (result['stdout'] + result['stderr']).rstrip(),
            "OUTPUT_END",
        )))

    # 添加摘要信息
    summary_block = "\n".join((
        "SUMMARY:",
        f"  TOTAL: {len(rows)}",
        f"  PASSED: {pass_count}",
        f"  FAILED: {fail_count}",
        f"  DURATION: {total_duration:.1f}s",
        f"  PARALLEL_WORKERS: {parallel}",
        f"  TIMEOUT: {timeout}s",
    ))
    
    blocks.insert(0, summary_block)
    
    _ = output_path.write_text("\n\n".join(blocks) + "\n", encoding="utf-8")

    # 生成JUnit XML报告
    if junit_xml_path:
        junit_path = pathlib.Path(junit_xml_path)
        if not junit_path.is_absolute():
            junit_path = repo_root / junit_path
        junit_path.parent.mkdir(parents=True, exist_ok=True)
        generate_junit_xml(results, str(junit_path), f"verilator_{suite}")
        print(f"JUnit XML report generated: {junit_path}")

    if all_ok:
        print(f"GBP_PE_NOC_MATRIX_PASS suite={suite} rows={len(rows)} passed={pass_count} duration={total_duration:.1f}s output={output_path}")
        return 0

    print(f"GBP_PE_NOC_MATRIX_FAIL suite={suite} rows={len(rows)} passed={pass_count} failed={fail_count} duration={total_duration:.1f}s output={output_path}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    exit_code = main()
    raise SystemExit(exit_code)
