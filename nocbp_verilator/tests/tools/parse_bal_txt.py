#!/usr/bin/env python3
"""
parse_bal_txt.py
Parse Bundle Adjustment (BAL) dataset files.

Format:
  # comments
  n_cameras n_landmarks n_observations
  fx fy cx cy
  camera_id landmark_id pixel_u pixel_v   (× n_observations)
  camera_mean[6] × n_cameras              (rotation[3] + translation[3])
  landmark_mean[3] × n_landmarks          (x, y, z)

Output: JSON with cameras, landmarks, observations, intrinsics.
"""

import argparse
import json
import sys
from pathlib import Path


def parse_bal_txt(path: str) -> dict:
    with open(path, 'r') as f:
        lines = f.readlines()

    # Skip comments and blank lines
    data_lines = []
    for line in lines:
        stripped = line.strip()
        if stripped and not stripped.startswith('#'):
            data_lines.append(stripped)

    # Header
    header = data_lines[0].split()
    n_cameras = int(header[0])
    n_landmarks = int(header[1])
    n_observations = int(header[2])

    # Camera intrinsics
    K = list(map(float, data_lines[1].split()))
    fx, fy, cx, cy = K[0], K[1], K[2], K[3]

    # Observations
    observations = []
    idx = 2
    for i in range(n_observations):
        parts = data_lines[idx + i].split()
        observations.append({
            "cam_id": int(parts[0]),
            "lmk_id": int(parts[1]),
            "u": float(parts[2]),
            "v": float(parts[3])
        })
    idx += n_observations

    # Camera means (6 values per camera, one per line: rotation[3] + translation[3])
    cameras = []
    for i in range(n_cameras):
        vals = []
        for j in range(6):
            vals.append(float(data_lines[idx]))
            idx += 1
        cameras.append({
            "rotation": vals[0:3],
            "translation": vals[3:6]
        })

    # Landmark means (3 values per landmark, one per line: x, y, z)
    landmarks = []
    for i in range(n_landmarks):
        vals = []
        for j in range(3):
            vals.append(float(data_lines[idx]))
            idx += 1
        landmarks.append({
            "pos": vals[0:3]
        })

    return {
        "n_cameras": n_cameras,
        "n_landmarks": n_landmarks,
        "n_observations": n_observations,
        "intrinsics": {"fx": fx, "fy": fy, "cx": cx, "cy": cy},
        "cameras": cameras,
        "landmarks": landmarks,
        "observations": observations
    }


def generate_partition(data: dict, n_pes: int, mesh_x: int, mesh_y: int) -> dict:
    """Generate a simple round-robin partition of nodes across PEs."""
    n_cams = data["n_cameras"]
    n_lmk = data["n_landmarks"]
    n_total = n_cams + n_lmk

    # Assign nodes to PEs round-robin
    var_mapping = [[] for _ in range(n_pes)]
    for i in range(n_total):
        pe = i % n_pes
        var_mapping[pe].append(i)

    # Factors: one per observation
    n_factors = data["n_observations"]
    fac_mapping = [[] for _ in range(n_pes)]
    for i in range(n_factors):
        pe = i % n_pes
        fac_mapping[pe].append(i)

    # Factor-variable edges
    factor_var_edges = []
    for i, obs in enumerate(data["observations"]):
        cam_node = obs["cam_id"]
        lmk_node = n_cams + obs["lmk_id"]
        factor_var_edges.append([i, cam_node])
        factor_var_edges.append([i, lmk_node])

    return {
        "schema_version": "1.0",
        "workload": "bal_fr1desk_small",
        "mesh": {"x": mesh_x, "y": mesh_y},
        "pes": n_pes,
        "var_mapping_table": var_mapping,
        "fac_mapping_table": fac_mapping,
        "factor_var_edges": factor_var_edges,
        "graph": {
            "n_fac_nodes": n_factors,
            "n_var_nodes": n_total,
            "factor_var_edges": factor_var_edges
        }
    }


def main():
    parser = argparse.ArgumentParser(description="Parse BAL dataset")
    parser.add_argument("input", help="Input BAL txt file")
    parser.add_argument("--output", "-o", help="Output JSON file")
    parser.add_argument("--partition", type=int, help="Generate partition for N PEs")
    parser.add_argument("--mesh-x", type=int, default=2, help="Mesh X dimension")
    parser.add_argument("--mesh-y", type=int, default=2, help="Mesh Y dimension")
    args = parser.parse_args()

    data = parse_bal_txt(args.input)
    print(f"Parsed: {data['n_cameras']} cameras, {data['n_landmarks']} landmarks, "
          f"{data['n_observations']} observations")

    if args.partition:
        partition = generate_partition(data, args.partition, args.mesh_x, args.mesh_y)
        data["partition"] = partition

    output = json.dumps(data, indent=2, ensure_ascii=False)
    if args.output:
        Path(args.output).write_text(output)
        print(f"Written to {args.output}")
    else:
        print(output)


if __name__ == "__main__":
    main()
