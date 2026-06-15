#!/usr/bin/env python3
"""
run_e2e_convergence.py
Automated end-to-end GBP convergence test runner.

Flow:
  1. Parse graph data (BAL txt or synthetic txt)
  2. Generate partition
  3. Generate SPM init data
  4. Build Verilator binary
  5. Run simulation
  6. Parse output (beliefs, ARE)
  7. Report PASS/FAIL

Usage:
  python3 run_e2e_convergence.py --dataset fr1desk_small.txt --mesh 2x2 --iters 50
  python3 run_e2e_convergence.py --graph synthetic_line --mesh 2x1 --iters 10
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
VERILATOR_DIR = REPO_ROOT / "nocbp_verilator"
TOOLS_DIR = VERILATOR_DIR / "tests" / "tools"
SYSTEM_DIR = VERILATOR_DIR / "tests" / "system"
ORACLE_DIR = VERILATOR_DIR / "tests" / "oracle"


def parse_dataset(dataset_path: str) -> dict:
    """Parse dataset file (BAL txt or JSON)."""
    path = Path(dataset_path)
    if path.suffix == '.json':
        return json.loads(path.read_text())
    elif path.suffix == '.txt':
        # BAL format
        result = subprocess.run(
            [sys.executable, str(TOOLS_DIR / "parse_bal_txt.py"), str(path)],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            print(f"Error parsing dataset: {result.stderr}")
            sys.exit(1)
        # Parse the JSON output (skip the first line "Parsed: ...")
        json_start = result.stdout.find('{')
        if json_start < 0:
            print(f"No JSON in parser output: {result.stdout}")
            sys.exit(1)
        return json.loads(result.stdout[json_start:])
    else:
        print(f"Unknown dataset format: {path.suffix}")
        sys.exit(1)


def generate_partition(dataset: dict, n_pes: int, mesh_x: int, mesh_y: int) -> dict:
    """Generate partition using parse_bal_txt.py."""
    n_cams = dataset.get('n_cameras', 0)
    n_lmk = dataset.get('n_landmarks', 0)
    n_total = n_cams + n_lmk

    var_mapping = [[] for _ in range(n_pes)]
    for i in range(n_total):
        var_mapping[i % n_pes].append(i)

    n_factors = dataset.get('n_observations', 0)
    fac_mapping = [[] for _ in range(n_pes)]
    for i in range(n_factors):
        fac_mapping[i % n_pes].append(i)

    edges = []
    for obs in dataset.get('observations', []):
        cam_id = obs['cam_id']
        lmk_id = n_cams + obs['lmk_id']
        edges.append([len(edges) // 2, cam_id])
        edges.append([len(edges) // 2 - 1, lmk_id])

    return {
        "schema_version": "1.0",
        "workload": "bal_fr1desk_small",
        "mesh": {"x": mesh_x, "y": mesh_y},
        "pes": n_pes,
        "var_mapping_table": var_mapping,
        "fac_mapping_table": fac_mapping,
        "factor_var_edges": edges,
        "graph": {
            "n_fac_nodes": n_factors,
            "n_var_nodes": n_total,
            "factor_var_edges": edges
        }
    }


def generate_spm_init(partition: dict, dataset: dict = None) -> dict:
    """Generate SPM init data using gen_spm_init.py."""
    import tempfile
    with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
        json.dump(partition, f)
        partition_path = f.name

    cmd = [sys.executable, str(TOOLS_DIR / "gen_spm_init.py"), partition_path]
    if dataset:
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            json.dump(dataset, f)
            dataset_path = f.name
        cmd.extend(["--dataset", dataset_path])

    result = subprocess.run(cmd, capture_output=True, text=True)
    os.unlink(partition_path)

    if result.returncode != 0:
        print(f"Error generating SPM init: {result.stderr}")
        return {}

    # Parse output
    spm_data = {}
    for line in result.stdout.strip().split('\n'):
        if line.startswith('Generated'):
            print(f"  {line}")
        elif 'PE' in line and ':' in line:
            print(f"  {line.strip()}")
    return spm_data


def build_test(test_name: str = "mesh_e2e_convergence") -> bool:
    """Build the Verilator test binary."""
    print(f"\nBuilding {test_name}...")
    result = subprocess.run(
        ["make", "build", f"LEVEL=system", f"TEST={test_name}",
         "VERILATOR_WARNINGS=minimal"],
        cwd=str(VERILATOR_DIR),
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"Build failed:\n{result.stderr[-500:]}")
        return False
    print("Build succeeded.")
    return True


def run_test(test_name: str = "mesh_e2e_convergence", timeout: int = 300) -> tuple:
    """Run the test and return (returncode, stdout, stderr)."""
    print(f"\nRunning {test_name}...")
    result = subprocess.run(
        ["make", "run", f"LEVEL=system", f"TEST={test_name}",
         "VERILATOR_WARNINGS=minimal"],
        cwd=str(VERILATOR_DIR),
        capture_output=True, text=True,
        timeout=timeout
    )
    return result.returncode, result.stdout, result.stderr


def parse_results(stdout: str) -> dict:
    """Parse test output to extract beliefs and ARE."""
    results = {
        'are': None,
        'beliefs': {},
        'pass': False,
        'cycles': 0,
        'phase_switches': 0
    }

    for line in stdout.split('\n'):
        line = line.strip()
        if 'ARE =' in line:
            try:
                are_str = line.split('ARE =')[1].split('(')[0].strip()
                results['are'] = float(are_str)
            except (ValueError, IndexError):
                pass
        elif 'Node' in line and 'eta=[' in line:
            try:
                parts = line.split(':')
                node_id = int(parts[0].replace('Node', '').strip())
                results['beliefs'][node_id] = line
            except (ValueError, IndexError):
                pass
        elif 'Simulation complete after' in line:
            try:
                results['cycles'] = int(line.split('after')[1].split('cycles')[0].strip())
            except (ValueError, IndexError):
                pass
        elif 'Phase switches:' in line:
            try:
                results['phase_switches'] = int(line.split(':')[1].strip())
            except (ValueError, IndexError):
                pass
        elif '=== PASS ===' in line:
            results['pass'] = True

    return results


def main():
    parser = argparse.ArgumentParser(description="Run GBP end-to-end convergence test")
    parser.add_argument("--dataset", help="Dataset file (BAL txt or JSON)")
    parser.add_argument("--graph", help="Graph type (synthetic_line, synthetic_lattice)")
    parser.add_argument("--mesh", default="2x2", help="Mesh dimensions (e.g., 2x2)")
    parser.add_argument("--iters", type=int, default=10, help="Max iterations")
    parser.add_argument("--cycles", type=int, default=10000, help="Max simulation cycles")
    parser.add_argument("--threshold", type=float, default=0.1, help="ARE threshold")
    parser.add_argument("--build-only", action="store_true", help="Only build, don't run")
    args = parser.parse_args()

    mesh_parts = args.mesh.split('x')
    mesh_x = int(mesh_parts[0])
    mesh_y = int(mesh_parts[1]) if len(mesh_parts) > 1 else mesh_x
    n_pes = mesh_x * mesh_y

    print("=" * 60)
    print("GBP End-to-End Convergence Test")
    print("=" * 60)
    print(f"Mesh: {mesh_x}x{mesh_y} ({n_pes} PEs)")
    print(f"Max iterations: {args.iters}")
    print(f"ARE threshold: {args.threshold}")

    # Parse dataset
    dataset = None
    if args.dataset:
        print(f"\nParsing dataset: {args.dataset}")
        dataset = parse_dataset(args.dataset)
        print(f"  {dataset.get('n_cameras', 0)} cameras, "
              f"{dataset.get('n_landmarks', 0)} landmarks, "
              f"{dataset.get('n_observations', 0)} observations")

    # Generate partition
    if dataset:
        print(f"\nGenerating partition for {n_pes} PEs...")
        partition = generate_partition(dataset, n_pes, mesh_x, mesh_y)
    else:
        print("\nUsing default 4-node chain graph")
        partition = None

    # Generate SPM init
    if partition:
        generate_spm_init(partition, dataset)

    # Build
    if not build_test():
        sys.exit(1)

    if args.build_only:
        print("\nBuild-only mode. Exiting.")
        return

    # Run
    returncode, stdout, stderr = run_test(timeout=600)

    # Parse results
    results = parse_results(stdout)

    print(f"\nSimulation: {results['cycles']} cycles, "
          f"{results['phase_switches']} phase switches")

    if results['are'] is not None:
        print(f"ARE = {results['are']:.6f} (threshold = {args.threshold})")

    for node_id, info in sorted(results['beliefs'].items()):
        print(f"  {info}")

    # Report
    if results['pass'] or (results['are'] is not None and results['are'] < args.threshold):
        print(f"\n{'='*60}")
        print(f"PASS: ARE {results['are']:.6f} < threshold {args.threshold}")
        print(f"{'='*60}")
        sys.exit(0)
    else:
        print(f"\n{'='*60}")
        are_val = results['are'] if results['are'] is not None else 'N/A'
        print(f"FAIL: ARE {are_val} >= threshold {args.threshold}")
        print(f"{'='*60}")
        sys.exit(1)


if __name__ == "__main__":
    main()
