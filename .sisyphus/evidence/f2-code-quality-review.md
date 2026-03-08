# F2 Code Quality Review - Function-Level Harness/Report Path

## Scope
- Reviewed `nocbp_verilator/tests/unit/gbp_compute_nodes.cc` only (control+compute function-level harness/report path).
- Reviewed semantic report output at `nocbp_verilator/tests/oracle/generated/gbp_compute_nodes_semantic_report.json`.
- Reviewed task evidence from tasks 5-9 under `.sisyphus/evidence/` with emphasis on variable/factor pass/fail and function matrix artifacts.
- Optional anti-pattern sweep in scope (`TODO|FIXME|HACK`) found no matches in `nocbp_verilator/tests/unit/gbp_compute_nodes.cc`.

## Required Checks
- Abs-only logic consistency: confirmed end-to-end use of `abs_err<=abs_tol` with `abs_tol=1e-4` in loader, checks, and report metadata (`nocbp_verilator/tests/unit/gbp_compute_nodes.cc:480`, `nocbp_verilator/tests/unit/gbp_compute_nodes.cc:281`, `nocbp_verilator/tests/unit/gbp_compute_nodes.cc:441`, `nocbp_verilator/tests/unit/gbp_compute_nodes.cc:162`).
- Negative-mode determinism: confirmed deterministic positive/negative split in evidence (`task-7-perturb-pass.txt`, `task-7-perturb-fail.txt`, `task-9-function-matrix-error.txt`):
  - Positive mode: no negative marker, clean pass.
  - Negative mode: negative marker present, stable failure signature (`total_failures=2`, variable=1, factor=1).

## Findings by Severity

### Medium
1) **Semantic report artifact is not mode-isolated (stale/failing report risk)**
- The report path defaults to a single fixed file regardless of positive vs perturb mode (`nocbp_verilator/tests/unit/gbp_compute_nodes.cc:619`, `nocbp_verilator/tests/unit/gbp_compute_nodes.cc:621`).
- The report is always written before returning in both positive and negative flows (`nocbp_verilator/tests/unit/gbp_compute_nodes.cc:870`).
- Current workspace evidence shows a mismatch where PASS logs exist (`.sisyphus/evidence/task-8-report-pass.txt`, `.sisyphus/evidence/task-9-function-matrix.txt`) but the current JSON artifact shows FAIL cases (`nocbp_verilator/tests/oracle/generated/gbp_compute_nodes_semantic_report.json:8`, `nocbp_verilator/tests/oracle/generated/gbp_compute_nodes_semantic_report.json:75`), consistent with a later perturb write.
- Impact: downstream tooling can consume a stale FAIL artifact even after a PASS run unless execution order is controlled.

2) **Perturb toggles use environment presence, not explicit value parsing**
- `GBP_COMPUTE_NODES_VAR_PERTURB` and `GBP_COMPUTE_NODES_FACTOR_PERTURB` are enabled when the variable exists, regardless of value (`nocbp_verilator/tests/unit/gbp_compute_nodes.cc:663`, `nocbp_verilator/tests/unit/gbp_compute_nodes.cc:769`).
- In contrast, `GBP_COMPUTE_NODES_PERTURB` requires `"1"` (`nocbp_verilator/tests/unit/gbp_compute_nodes.cc:604`).
- Impact: operational robustness issue; e.g., setting `GBP_COMPUTE_NODES_VAR_PERTURB=0` still enables perturb behavior.

### Low
1) **Oracle parsing is regex-structure-coupled and brittle for format evolution**
- Oracle loading relies on strict regex ordering over JSON-like text (`nocbp_verilator/tests/unit/gbp_compute_nodes.cc:513`, `nocbp_verilator/tests/unit/gbp_compute_nodes.cc:568`).
- Any harmless field ordering/layout changes can break case extraction even with semantically valid JSON.
- Impact: maintainability risk for future oracle schema/layout changes.

### High
- None found in current function-level scope.

## Verdict
- **PASS_WITH_NOTES** for the requested function-level control+compute harness/report path.
- Correctness baseline is intact for abs-only checks and deterministic negative behavior.
- Two medium robustness concerns should be tracked (mode-isolated report artifact, explicit env value parsing).
