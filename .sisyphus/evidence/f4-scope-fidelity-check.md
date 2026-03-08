# F4 Scope Fidelity Check (Deep)

Date: 2026-03-06
Plan authority: `.sisyphus/plans/control-compute-factor-variable-validation.md`
Assessment target: implemented work and evidence for Tasks 4-9, plus stage-gate wording

## Guardrail Baseline (Plan-Explicit)

- Scope is function-level control+compute parity only (`.sisyphus/plans/control-compute-factor-variable-validation.md:18`, `.sisyphus/plans/control-compute-factor-variable-validation.md:31`).
- No full-PE/NoC/end-to-end claims allowed in this phase (`.sisyphus/plans/control-compute-factor-variable-validation.md:26`, `.sisyphus/plans/control-compute-factor-variable-validation.md:56`).
- Task 9 must avoid integration/full-PE regression commands (`.sisyphus/plans/control-compute-factor-variable-validation.md:400`).
- Task 10 may describe gate/handoff only; it must not claim full-PE coverage (`.sisyphus/plans/control-compute-factor-variable-validation.md:438`).

## Findings (PASS/FAIL)

1) **PASS — Task 4 harness remains function-level**
- Evidence uses only `LEVEL=unit TEST=gbp_compute_nodes` with deterministic start/pass and explicit negative missing-oracle behavior (`.sisyphus/evidence/task-4-harness-pass.txt:1`, `.sisyphus/evidence/task-4-harness-fail.txt:1`).

2) **PASS — Tasks 5-8 stay within variable/factor function semantics**
- Variable/factor evidence shows abs-only semantic checks, branch/state markers, perturb-path failure markers, and semantic report status; no integration command/claim appears (`.sisyphus/evidence/task-5-variable-pass.txt:1`, `.sisyphus/evidence/task-6-factor-pass.txt:1`, `.sisyphus/evidence/task-7-perturb-fail.txt:1`, `.sisyphus/evidence/task-8-report-pass.txt:1`).

3) **PASS — In-scope Task 9 artifact is function-level**
- `task-9-function-matrix*` files use only the `gbp_compute_nodes` unit target and expected positive/negative polarity (`.sisyphus/evidence/task-9-function-matrix.txt:1`, `.sisyphus/evidence/task-9-function-matrix-error.txt:1`).

4) **FAIL — Scope drift present in parallel Task 9 lineage artifacts**
- `task-9-regression-matrix*` includes `LEVEL=integration TEST=pe_top_integration` and broader unit matrix outside function-level compute-node scope (`.sisyphus/evidence/task-9-regression-matrix.txt:25`, `.sisyphus/evidence/task-9-regression-matrix-error.txt:15`).
- This conflicts with Task 9 guardrail forbidding integration/full-PE regression commands for this phase (`.sisyphus/plans/control-compute-factor-variable-validation.md:400`).

5) **FAIL — Stage-gate wording currently over-claims phase scope**
- `nocbp_verilator/ADDING_TESTS.md` "Control+Compute GBP Verification Runbook" section includes `pe_top_integration` pass/negative commands and markers, which are beyond this phase's function-level gate (`nocbp_verilator/ADDING_TESTS.md:308`, `nocbp_verilator/ADDING_TESTS.md:322`).
- This is incompatible with "full-PE comes later" and "no full-PE claims" guardrails (`.sisyphus/plans/control-compute-factor-variable-validation.md:15`, `.sisyphus/plans/control-compute-factor-variable-validation.md:438`).

6) **AMBIGUOUS/AT-RISK — Task 10 completion is blocked/timeouts**
- Existing `task-10-doc-repro*` evidence reflects broad matrix + integration behavior, not a function-level-only handoff note (`.sisyphus/evidence/task-10-doc-repro.txt:24`, `.sisyphus/evidence/task-10-doc-repro-error.txt:14`).
- Given the reported timeout/blocker, current documentation state should not be used as phase-authorization evidence.

## Unsupported Claims/Features Check

- Unsupported for this phase: any claim that integration/full-PE validation is already part of this gate.
- Found unsupported wording/artifacts in `task-9-regression-matrix*`, `task-9-doc-repro*`, `task-10-doc-repro*`, and `nocbp_verilator/ADDING_TESTS.md` runbook section.

## Recommended Remediation

1. Treat `task-9-function-matrix*` as the only valid Task 9 evidence set for this plan; quarantine `task-9-regression-matrix*` and `task-9-doc-repro*` from this gate.
2. Rewrite/replace stage-gate docs to a strict two-phase statement: (a) function-level `gbp_compute_nodes` pass/fail matrix, then (b) separate future full-PE gate.
3. Add an explicit "out-of-scope in this phase" note for `pe_top_integration` in the runbook to prevent recurrence.
4. Re-run Task 10 doc evidence after timeout issue is resolved, using only function-level commands in this phase document.

## Gate Decision

Decision: `FAIL` (scope fidelity not clean due documented drift)

Confidence: High

Rationale: Core implementation artifacts for Tasks 4-8 and in-scope Task 9 function matrix are compliant, but concurrent evidence/docs introduce integration/full-PE commands and claims that violate explicit phase guardrails.
