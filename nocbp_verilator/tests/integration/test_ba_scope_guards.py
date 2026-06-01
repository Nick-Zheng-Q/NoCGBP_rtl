from pathlib import Path
import importlib.util
from typing import Callable, cast

REPO_ROOT = Path(__file__).resolve().parents[3]
TOOLS_PATH = REPO_ROOT / "nocbp_verilator" / "tests" / "tools"
CONTRACT_PATH = TOOLS_PATH / "ba_onboarding_contract.py"
CONTRACT_SPEC = importlib.util.spec_from_file_location("ba_onboarding_contract", CONTRACT_PATH)
assert CONTRACT_SPEC is not None and CONTRACT_SPEC.loader is not None
CONTRACT_MODULE = importlib.util.module_from_spec(CONTRACT_SPEC)
CONTRACT_SPEC.loader.exec_module(CONTRACT_MODULE)

validate_ba_scope = cast(Callable[[int], None], CONTRACT_MODULE.validate_ba_scope)


def test_ba_scope_allows_1pe() -> None:
    validate_ba_scope(1)


def test_ba_scope_allows_2pe() -> None:
    validate_ba_scope(2)


def test_ba_scope_allows_16pe() -> None:
    validate_ba_scope(16)


def test_ba_scope_allows_32pe() -> None:
    validate_ba_scope(32)
