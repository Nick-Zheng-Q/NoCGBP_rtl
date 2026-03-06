# F4 Scope Fidelity Check (Deep)

Date: 2026-03-06
Plan authority: `.sisyphus/plans/control-compute-gbp-rtl-verification.md`
Gate target: Tasks 1..10 plus final-wave scope context

## Scope Authority (Plan-Locked)

- Scope is restricted to control+compute behavior in single-PE context (`.sisyphus/plans/control-compute-gbp-rtl-verification.md:19`, `.sisyphus/plans/control-compute-gbp-rtl-verification.md:29`).
- Guardrails forbid multi-PE/full-NoC expansion and forbid over-claiming integration beyond current topology (`.sisyphus/plans/control-compute-gbp-rtl-verification.md:23`, `.sisyphus/plans/control-compute-gbp-rtl-verification.md:53`).
- Acceptance requires explicit positive/negative command evidence for scoped tasks (`.sisyphus/plans/control-compute-gbp-rtl-verification.md:61`, `.sisyphus/plans/control-compute-gbp-rtl-verification.md:62`).

## Scope Fidelity Table (Tasks 1..10)

| Task | Plan intent (scope) | Current evidence references | Scope verdict |
|---|---|---|---|
| 1 | Baseline lock and evidence scaffold only | Baseline PASS markers and withheld-done negative marker are present (`.sisyphus/evidence/task-1-control-compute-baseline.txt:7`, `.sisyphus/evidence/task-1-control-compute-baseline.txt:8`, `.sisyphus/evidence/task-1-control-compute-baseline-error.txt:6`) | PASS |
| 2 | RED extension for `control_unit_top` tests | RED non-zero exit and deterministic red marker are present (`.sisyphus/evidence/task-2-control-unit-red.txt:16`, `.sisyphus/evidence/task-2-control-unit-red-error.txt:1`, `.sisyphus/evidence/task-2-control-unit-red-error.txt:5`) | PASS |
| 3 | GREEN `control_unit.sv` alignment while preserving handshake behavior | GREEN run exits 0 with all-tests marker and no RED marker (`.sisyphus/evidence/task-3-control-unit-green.txt:25`, `.sisyphus/evidence/task-3-control-unit-green.txt:27`, `.sisyphus/evidence/task-3-control-unit-green.txt:29`) | PASS |
| 4 | RED `compute_unit_top` harness/op-matrix before RTL fix | RED run has non-zero exit and planned mismatch markers (`.sisyphus/evidence/task-4-compute-unit-red.txt:39`, `.sisyphus/evidence/task-4-compute-unit-red-error.txt:27`, `.sisyphus/evidence/task-4-compute-unit-red-error.txt:45`, `.sisyphus/evidence/task-4-compute-unit-red-error.txt:47`) | PASS |
| 5 | GREEN `compute_unit` op-matrix and deterministic negative path | PASS marker in positive run and explicit perturb negative marker/non-zero exit in error artifact (`.sisyphus/evidence/task-5-compute-unit-green.txt:11`, `.sisyphus/evidence/task-5-compute-unit-green.txt:15`, `.sisyphus/evidence/task-5-compute-unit-green-error.txt:10`, `.sisyphus/evidence/task-5-compute-unit-green-error.txt:17`) | PASS |
| 6 | Coupled control+compute validation in `pe_unit` with withheld-done negative | Coupled pass markers and withheld marker/non-zero exit are present (`.sisyphus/evidence/task-6-pe-unit-coupling.txt:9`, `.sisyphus/evidence/task-6-pe-unit-coupling.txt:12`, `.sisyphus/evidence/task-6-pe-unit-coupling-error.txt:6`, `.sisyphus/evidence/task-6-pe-unit-coupling-error.txt:9`) | PASS |
| 7 | Oracle-coupled `gbp_pe` verification with perturb mismatch diagnostics | PASS-rule marker and structured perturb mismatch fields are present (`.sisyphus/evidence/task-7-gbp-pe-oracle.txt:10`, `.sisyphus/evidence/task-7-gbp-pe-oracle-error.txt:10`, `.sisyphus/evidence/task-7-gbp-pe-oracle-error.txt:13`) | PASS |
| 8 | Integration re-validation in current topology only | Integration PASS marker and perturb mismatch/non-zero exit are present (`.sisyphus/evidence/task-8-integration-pass.txt:2`, `.sisyphus/evidence/task-8-integration-pass.txt:4`, `.sisyphus/evidence/task-8-integration-error.txt:2`, `.sisyphus/evidence/task-8-integration-error.txt:4`) | PASS |
| 9 | Full regression matrix with required pass and perturb rows | Required pass rows (1..5) and perturb rows (6..8) with expected polarity are present (`.sisyphus/evidence/task-9-regression-matrix.txt:5`, `.sisyphus/evidence/task-9-regression-matrix.txt:25`, `.sisyphus/evidence/task-9-regression-matrix-error.txt:5`, `.sisyphus/evidence/task-9-regression-matrix-error.txt:15`) | PASS |
| 10 | Documentation runbook reproducibility within control+compute scope | 5 pass commands and 3 negative commands are captured with markers and exit polarity (`.sisyphus/evidence/task-10-doc-repro.txt:5`, `.sisyphus/evidence/task-10-doc-repro.txt:25`, `.sisyphus/evidence/task-10-doc-repro-error.txt:5`, `.sisyphus/evidence/task-10-doc-repro-error.txt:15`) | PASS |

## Final-Wave Context (Current Artifacts)

- F1 now points to the correct plan authority (`.sisyphus/evidence/f1-plan-compliance-audit.md:4`) and no longer contains the prior wrong-plan assertion.
- F1 now reports Task 5 as implemented and checked, with explicit negative-path closure proof (`.sisyphus/evidence/f1-plan-compliance-audit.md:23`, `.sisyphus/evidence/task-5-compute-unit-green-error.txt:10`, `.sisyphus/evidence/task-5-compute-unit-green-error.txt:17`), and F1 final gate is `PASS` (`.sisyphus/evidence/f1-plan-compliance-audit.md:44`).
- F2 verdict is `PASS_WITH_NOTES` and remains in plan scope (`.sisyphus/evidence/f2-code-quality-review.md:46`, `.sisyphus/evidence/f2-code-quality-review.md:4`).
- F3 verdict is `PASS` with pass/negative command polarity preserved (`.sisyphus/evidence/f3-real-manual-qa.md:22`, `.sisyphus/evidence/f3-real-manual-qa.md:10`, `.sisyphus/evidence/f3-real-manual-qa.md:11`).

## Multi-PE / Full-NoC Exclusion Check

- Runbook scope remains explicit: single-PE only; multi-PE out of scope (`nocbp_verilator/ADDING_TESTS.md:291`).
- No required Task 1..10 artifact introduces a multi-PE/full-NoC correctness claim; all markers/commands map to single-PE control+compute lineage.

## Explicit Out-of-Scope Artifacts (Non-Blocking)

Artifacts below are intentionally excluded from this gate because they are legacy lineage and not part of this plan's Task 1..10 evidence set:

- `.sisyphus/evidence/task-1-oracle-contract.txt`
- `.sisyphus/evidence/task-2-pe-unit-harness.txt`
- `.sisyphus/evidence/task-3-gbp-pe-wrapper-control.txt`
- `.sisyphus/evidence/task-4-oracle-generation.txt`
- `.sisyphus/evidence/task-5-gbp-pe-oracle.txt`
- `.sisyphus/evidence/task-7-single-pe-e2e.txt`
- `.sisyphus/evidence/task-8-full-matrix.txt`
- `.sisyphus/evidence/task-9-doc-repro.txt`

Non-blocking rationale:

- These files are outside the control-compute task lineage defined by this plan's command/evidence contract (`.sisyphus/plans/control-compute-gbp-rtl-verification.md:62`).
- Required in-scope artifacts for Tasks 1..10 are present and internally consistent, so legacy files do not invalidate this scope gate.

## Gate Decision

Decision: `PASS`

Confidence: High.

Reasoning:

- Task-by-task scope mapping (1..10) is complete and anchored to current evidence with line-level references.
- Final-wave context contains no stale wrong-plan contradiction and reflects current F1/F2/F3 artifact state.
- Scope boundary remains explicit: control+compute, single-PE/current-topology, with multi-PE/full-NoC excluded.
