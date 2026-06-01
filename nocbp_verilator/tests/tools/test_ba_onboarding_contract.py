from pathlib import Path
import importlib.util
from typing import Callable, cast

import pytest

TOOLS_PATH = Path(__file__).resolve().parent
CONTRACT_PATH = TOOLS_PATH / "ba_onboarding_contract.py"
CONTRACT_SPEC = importlib.util.spec_from_file_location("ba_onboarding_contract", CONTRACT_PATH)
assert CONTRACT_SPEC is not None and CONTRACT_SPEC.loader is not None
CONTRACT_MODULE = importlib.util.module_from_spec(CONTRACT_SPEC)
CONTRACT_SPEC.loader.exec_module(CONTRACT_MODULE)

BA_WORKLOAD_TOKEN = cast(str, CONTRACT_MODULE.BA_WORKLOAD_TOKEN)
BA_DATASET_REL_PATH = cast(str, CONTRACT_MODULE.BA_DATASET_REL_PATH)
BA_FIXED_ITERS = cast(int, CONTRACT_MODULE.BA_FIXED_ITERS)
BA_ALLOWED_PE_COUNTS = cast(tuple[int, ...], CONTRACT_MODULE.BA_ALLOWED_PE_COUNTS)
resolve_ba_dataset_path = cast(Callable[[Path], Path], CONTRACT_MODULE.resolve_ba_dataset_path)
validate_ba_scope = cast(Callable[[int], None], CONTRACT_MODULE.validate_ba_scope)
validate_ba_partition_mode = cast(
    Callable[[str], None], CONTRACT_MODULE.validate_ba_partition_mode
)
build_ba_contract = cast(
    Callable[[Path, int, str], dict[str, object]], CONTRACT_MODULE.build_ba_contract
)


REPO_ROOT = Path(__file__).resolve().parents[3]


def test_ba_contract_workload_token_is_locked() -> None:
    assert BA_WORKLOAD_TOKEN == "bal_fr1desk_small"


def test_ba_contract_dataset_path_is_locked() -> None:
    assert BA_DATASET_REL_PATH == "data/fr1desk_small.txt"


def test_ba_contract_fixed_iters_is_locked() -> None:
    assert BA_FIXED_ITERS == 50


def test_ba_allowed_pe_counts_are_explicitly_locked() -> None:
    assert BA_ALLOWED_PE_COUNTS == (1, 2, 4, 16, 32)


def test_missing_dataset_is_rejected(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError, match=r"data/fr1desk_small\.txt"):
        _ = resolve_ba_dataset_path(tmp_path)


@pytest.mark.parametrize("pe_count", [1, 2, 4, 16, 32])
def test_allowed_pe_counts_are_accepted(pe_count: int) -> None:
    validate_ba_scope(pe_count)


def test_unsupported_pe_count_is_rejected() -> None:
    with pytest.raises(ValueError, match="PE counts"):
        validate_ba_scope(8)


def test_unsupported_partition_mode_is_rejected() -> None:
    with pytest.raises(ValueError, match="partition mode"):
        validate_ba_partition_mode("factor_variable_split")


def test_contract_build_uses_frozen_dataset_and_iters() -> None:
    contract = build_ba_contract(REPO_ROOT, 1, "scotch")
    assert contract["workload"] == "bal_fr1desk_small"
    assert isinstance(contract["dataset_path"], str)
    assert contract["dataset_path"].endswith("data/fr1desk_small.txt")
    assert contract["fixed_iters"] == 50
