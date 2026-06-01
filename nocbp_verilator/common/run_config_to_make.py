#!/usr/bin/env python3
"""将白盒 mesh 运行配置 YAML 展开为 make 变量。"""

from __future__ import annotations

import sys
from pathlib import Path

import yaml


def fail(message: str) -> int:
    print(f"$(error {message})")
    return 1


def quote_make(value: str) -> str:
    return value.replace("\\", "\\\\").replace(" ", "\\ ")


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("run_config_to_make.py expects exactly one RUN_CONFIG path")

    config_path = Path(argv[1]).expanduser()
    if not config_path.is_absolute():
        config_path = Path.cwd() / config_path
    config_path = config_path.resolve()
    if not config_path.is_file():
        return fail(f"RUN_CONFIG not found: {config_path}")

    try:
        data = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    except Exception as exc:  # pragma: no cover
        return fail(f"failed to parse RUN_CONFIG {config_path}: {exc}")

    if not isinstance(data, dict):
        return fail("RUN_CONFIG root must be a YAML mapping")

    mesh = data.get("mesh")
    if not isinstance(mesh, dict):
        return fail("RUN_CONFIG.mesh must be a mapping with x/y")

    level = data.get("level")
    test = data.get("test")
    mesh_x = mesh.get("x")
    mesh_y = mesh.get("y")
    workload = data.get("workload")
    dataset = data.get("dataset")
    seed = data.get("seed")
    partition = data.get("partition")

    required_scalars = {
        "level": level,
        "test": test,
        "workload": workload,
        "dataset": dataset,
        "seed": seed,
        "partition": partition,
    }
    for key, value in required_scalars.items():
        if value in (None, ""):
            return fail(f"RUN_CONFIG missing required field: {key}")

    if not isinstance(mesh_x, int) or not isinstance(mesh_y, int):
        return fail("RUN_CONFIG.mesh.x and RUN_CONFIG.mesh.y must be integers")
    if mesh_x <= 0 or mesh_y <= 0:
        return fail("RUN_CONFIG mesh dimensions must be positive")

    dataset_path = Path(dataset).expanduser()
    if not dataset_path.is_absolute():
        dataset_path = (config_path.parent / dataset_path).resolve()
    partition_path = Path(partition).expanduser()
    if not partition_path.is_absolute():
        partition_path = (config_path.parent / partition_path).resolve()

    print(f"LEVEL := {level}")
    print(f"TEST := {test}")
    print(f"PE_COUNT := {mesh_x * mesh_y}")
    print(f"MESH_X := {mesh_x}")
    print(f"MESH_Y := {mesh_y}")
    print(f"WORKLOAD := {quote_make(str(workload))}")
    print(f"DATASET := {quote_make(str(dataset_path))}")
    print(f"SEED := {seed}")
    print(f"PARTITION := {quote_make(str(partition_path))}")
    print(f"RUN_CONFIG_ABS := {quote_make(str(config_path))}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
