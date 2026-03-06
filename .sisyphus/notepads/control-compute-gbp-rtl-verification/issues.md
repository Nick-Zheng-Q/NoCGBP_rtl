# Task 1 Issues / Notes

## No Issues Encountered
All three commands executed as expected:
- 2 positive tests passed cleanly
- 1 negative test produced expected failure

## Negative Path Validation
The PE_UNIT_HOLD_DONE=1 test confirms:
- Test properly detects when `rsp_done_i` is withheld
- Timeout mechanism triggers after pending command with no response
- Exit code 2 is consistent failure indicator

## Test Determinism
- Both positive runs exited 0
- Negative run exited 2 with clear "withheld" marker
- Results are reproducible

## Task 4 RED Harness Issues
- `compute_unit_top` op matrix currently reports broad lane mismatches (many lanes observed `0x00000000`) even for add/sub/mul/div vectors; this is expected in RED stage and preserved intentionally.
- Verilator build initially failed on HardFloat/basejump warnings treated as fatal; resolved for this harness by switching YAML to shared `tests/unit/gbp_pe_fpu.f` filelist that carries `-Wno-fatal`.
- Deterministic RED marker path is active: stderr includes `OP_MATRIX_MISMATCH_MARKER` and `COMPUTE_UNIT_RED_PLANNED_GAP`, and command exits non-zero (`EXIT_CODE=2`).

## Task 2 RED Issues / Observations
- RED run is intentionally failing with marker `RED_INVALID_DISPATCH_EDGE_MARKER`
- Command used: `make -C nocbp_verilator run LEVEL=unit TEST=control_unit_top`
- Observed deterministic non-zero result: `EXIT_CODE: 2`
- No RTL files were modified; failure is test-assertion-driven as required
- `lsp_diagnostics` could not run because `clangd` is unavailable in PATH in this environment

## Task 5 GREEN Issues / Observations
- Initial GREEN attempt surfaced two harness-vector mismatches in non-RED cases (`sub` lane2 and `mul` lane2): expected constants did not match IEEE arithmetic for provided operands.
- RED-only `invalid-op-vector` case and `COMPUTE_UNIT_RED_PLANNED_GAP` path blocked GREEN exit behavior; harness was converted to GREEN semantics with deterministic pass marker.
- No new infra/tooling blockers: `make -C nocbp_verilator run LEVEL=unit TEST=compute_unit_top` now exits `0` consistently with no `OP_MATRIX_MISMATCH_MARKER` lines.

## Task 3 GREEN Issues / Observations
- `RED_NO_PROGRESS_BEFORE_RSP_DONE_MARKER` block in `control_unit_top.cc` had contradictory same-cycle expectations (require cmd asserted and deasserted simultaneously) and could never pass once earlier RED failure was fixed.
- Resolved by keeping the red marker tied to the intended invariant (`no META dispatch before rsp_done`) and retaining explicit cmd-handshake clear check.
- No interface or cross-module dependency changes were required; scope remained limited to control-unit RTL/test alignment.

## Task 6 Coupling Issues / Observations
- No functional blockers in `pe_unit` coupling validation; positive and withheld-done paths are deterministic with required markers.
- `lsp_diagnostics` for `nocbp_verilator/tests/unit/pe_unit.cc` could not be executed in this environment because `clangd` is not available in `PATH`.

## Task 7 Evidence Refresh Issues / Observations
- No functional issue in `gbp_pe`; previous failure was missing evidence completeness, not RTL/test behavior.
- Perturbation command exits non-zero (`EXIT_CODE=2`) through `make` when oracle mismatch is intentionally injected.

## Task 8 Integration Issues / Observations
- Initial Task 8 failure was a checker-ordering false negative: one-cycle-delayed `wr_req_valid` after `compute_done` was neither classified immediate nor sustained backpressure.
- `lsp_diagnostics` for `nocbp_verilator/tests/integration/pe_top_integration.cc` remains unavailable in this environment because `clangd` is not present in `PATH`.

## Task 9 Regression Matrix Issues / Observations
- No new RTL/test blockers observed during execution-only task; all required commands ran and produced expected pass/fail polarity.
- Negative-path rows continue to fail through `make` as `EXIT_CODE: 2` with deterministic line-level markers (withheld done or oracle mismatch).
- `lsp_diagnostics` remains unavailable for these markdown/txt evidence updates in this environment because `clangd` is not present in `PATH`.

## F2 Code Quality Review Issues / Observations
- In-scope anti-pattern scan (`TODO|FIXME|placeholder|TBD|XXX|empty catch`) returned no hits across control/compute RTL and unit/integration tests.
- Sensitive-area verdict: post-compute write-window relaxation mostly preserves intent, but `prev_wr_req_valid` acceptance can over-accept a pre-done carryover write and should be tightened.
- Additional maintainability risks noted: `dispatch_ready` edge-coupling in `control_unit.sv` and ungated divide `inReady` usage in `compute_unit.sv`.

## F4 Scope Fidelity Notes
- Core deliverables remain in single-PE control+compute scope; no multi-PE/full-NoC claim found in Task 10 runbook/evidence.
- Audit hygiene issue: `.sisyphus/evidence/` still contains prior-plan final-wave files (`f1*`/older `f4*` lineage), which can mislead reviewers if plan tags are not checked.
- Follow-up recommendation: plan-tag or archive prior-plan `f*.md` files after closure to keep final-wave audits unambiguous.

## F2 Rerun Issues / Observations (Current)
- No blocking correctness defect found in scoped RTL/tests for this rerun.
- Non-blocking medium risks remain: ready-edge-coupled dispatch behavior in `v/gbp_pe/control_unit.sv` and ungated divider readiness usage in `v/gbp_pe/compute_unit.sv`.

## F4 Scope Fidelity Re-Run Issues (2026-03-06)
- `f1-plan-compliance-audit.md` still cites `.sisyphus/plans/control-compute-design-test-plan.md`, so it is out-of-authority for this gate and must be treated as non-gating context.
- `.sisyphus/evidence/` includes legacy matrix artifacts (for example `task-8-full-matrix.txt` with `stream_dispatcher_top`) that are not part of this plan's Task 1..10 command set; explicit exclusion is required during final-wave review.

## F1 Plan Compliance Audit Rerun Issues (2026-03-06)
- Blocking: `.sisyphus/evidence/task-5-compute-unit-green-error.txt` lacks the required negative-path command/exit/fail-marker proof (contains only pass marker line), leaving Task 5 as implemented but unchecked.
- Blocking: Final-wave criterion `ALL must APPROVE` is currently unmet because `.sisyphus/evidence/f2-code-quality-review.md` reports `NEEDS_FIX`.

## Task 5 QA Gap Closure Issues / Observations (2026-03-06)
- Previous blocker resolved: `.sisyphus/evidence/task-5-compute-unit-green-error.txt` now captures perturb command, explicit negative marker, mismatch line, and non-zero `EXIT_CODE`.
- No new functional blockers observed in this closure step; scope remained test-harness/evidence only with no RTL edits.

## F1 Refresh Issue Note (2026-03-06)
- Audit drift risk persists if old F1/F4 snapshots are reused without rereading current verdict lines in `f2`/`f3`/`f4` evidence.
