#!/usr/bin/env python3

from pathlib import Path


BA_WORKLOAD_TOKEN = "bal_fr1desk_small"
BA_DATASET_REL_PATH = "data/fr1desk_small.txt"
BA_FIXED_ITERS = 50
BA_ALLOWED_PE_COUNTS = (1, 2, 4, 16, 32)
BA_ALLOWED_PARTITION_MODES = ("scotch",)


def resolve_ba_dataset_path(repo_root: Path) -> Path:
    dataset_path = (repo_root / BA_DATASET_REL_PATH).resolve()
    if not dataset_path.is_file():
        raise FileNotFoundError(
            f"BA dataset is required at '{BA_DATASET_REL_PATH}' (resolved: {dataset_path})"
        )
    return dataset_path


def validate_ba_scope(pe_count: int) -> None:
    if pe_count not in BA_ALLOWED_PE_COUNTS:
        raise ValueError(
            f"BA scope guard allows PE counts {BA_ALLOWED_PE_COUNTS}; got {pe_count}"
        )


def validate_ba_partition_mode(partition_mode: str) -> None:
    if partition_mode not in BA_ALLOWED_PARTITION_MODES:
        raise ValueError(
            f"BA partition mode must be one of {BA_ALLOWED_PARTITION_MODES}; got '{partition_mode}'"
        )


def build_ba_contract(repo_root: Path, pe_count: int, partition_mode: str) -> dict[str, object]:
    validate_ba_scope(pe_count)
    validate_ba_partition_mode(partition_mode)
    dataset_path = resolve_ba_dataset_path(repo_root)
    return {
        "workload": BA_WORKLOAD_TOKEN,
        "dataset_path": str(dataset_path),
        "fixed_iters": BA_FIXED_ITERS,
        "pe_count": pe_count,
        "partition_mode": partition_mode,
    }
