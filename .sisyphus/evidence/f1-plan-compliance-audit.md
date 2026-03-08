# F1 Plan Compliance Audit - control-compute-factor-variable-validation

Date: 2026-03-06  
Plan audited: `.sisyphus/plans/control-compute-factor-variable-validation.md`

## Binary Task Judgments

| Task | Status | Evidence basis |
|---|---|---|
| 1. Lock simulator function semantics and branch matrix | PASS | `.sisyphus/evidence/task-1-semantic-map.txt` and `.sisyphus/evidence/task-1-semantic-map-error.txt` show required anchors and negative missing-anchor guard with non-zero exit. |
| 2. Define abs-only oracle contract for function-level parity | PASS | `.sisyphus/evidence/task-2-contract-pass.txt` and `.sisyphus/evidence/task-2-contract-fail.txt` show `abs_tol=1e-4`, `abs_only=true`, and structured mismatch fields. |
| 3. Generate deterministic function-level oracle vectors | PASS | `.sisyphus/evidence/task-3-oracle-gen-pass.txt` shows same-seed checksum equality; `.sisyphus/evidence/task-3-oracle-gen-fail.txt` shows seed-mismatch guard. Generated oracle includes `compute_variable_node` and `compute_factor_node` sections in `nocbp_verilator/tests/oracle/generated/gbp_oracle_compute_nodes_seed24680_run1.json`. |
| 4. Add `gbp_compute_nodes` unit target scaffold | PASS | `.sisyphus/evidence/task-4-harness-pass.txt` shows build/run + start marker + exit 0; `.sisyphus/evidence/task-4-harness-fail.txt` shows explicit missing-oracle failure and non-zero exit. Target files exist at `nocbp_verilator/tests/unit/gbp_compute_nodes.cc` and `nocbp_verilator/tests/unit/gbp_compute_nodes.yaml`. |
| 5. Implement variable-node semantic parity checks | PASS | `.sisyphus/evidence/task-5-variable-pass.txt` and `.sisyphus/evidence/task-5-variable-fail.txt` show variable abs-error checks, state markers, pass path, and perturb non-zero fail path. |
| 6. Implement factor-node semantic parity checks | PASS | `.sisyphus/evidence/task-6-factor-pass.txt` and `.sisyphus/evidence/task-6-factor-fail.txt` show branch markers (fallback/non-fallback) and perturb mismatch with non-zero exit. |
| 7. Add deterministic negative-mode signaling and fail markers | PASS | `.sisyphus/evidence/task-7-perturb-pass.txt` shows negative marker absent in normal mode; `.sisyphus/evidence/task-7-perturb-fail.txt` shows `GBP_COMPUTE_NODES_NEGATIVE_MARKER` and non-zero exit in perturb mode. |
| 8. Emit floating semantic report artifact and validate schema | PASS | `.sisyphus/evidence/task-8-report-pass.txt` and `.sisyphus/evidence/task-8-report-fail.txt` show report marker, both function sections, `abs_tol=1e-4`, and fail-mode `abs_err > abs_tol` evidence. Artifact exists at `nocbp_verilator/tests/oracle/generated/gbp_compute_nodes_semantic_report.json`. |
| 9. Run full function-level regression and capture evidence pack | PASS | `.sisyphus/evidence/task-9-function-matrix.txt` shows positive PASS markers with exit 0; `.sisyphus/evidence/task-9-function-matrix-error.txt` shows perturb FAIL markers with non-zero exit. |
| 10. Document stage gate and handoff criterion to full-PE phase | FAIL | Plan expects function-level gate documentation and reproducibility evidence files `task-10-function-doc-pass.txt` / `task-10-function-doc-fail.txt`, but those files are absent in `.sisyphus/evidence/`. Current `nocbp_verilator/ADDING_TESTS.md` documents a broader control+compute runbook (including integration commands) and does not provide explicit two-phase function-level-then-full-PE gate language for `gbp_compute_nodes`. |

## Satisfied Tasks
- PASS: Tasks 1, 2, 3, 4, 5, 6, 7, 8, 9.

## Unsatisfied Tasks
- FAIL: Task 10.

## Blockers
- None observed in repository evidence (no external blocker recorded).

## Task 10 Status (explicit)
- **FAIL based on observable repo state.**
- Missing expected evidence artifacts: `.sisyphus/evidence/task-10-function-doc-pass.txt` and `.sisyphus/evidence/task-10-function-doc-fail.txt`.
- Runbook gap: `nocbp_verilator/ADDING_TESTS.md` lacks explicit two-phase gate criteria anchored to the `gbp_compute_nodes` function-level pass/fail commands.

## Recommended Next Action
1. Update `nocbp_verilator/ADDING_TESTS.md` with an explicit two-phase gate section: (a) function-level gate using `gbp_compute_nodes` pass/perturb commands and markers, then (b) permission to proceed to full-PE phase.
2. Execute the documented positive/negative function-level command pair and capture evidence at `.sisyphus/evidence/task-10-function-doc-pass.txt` and `.sisyphus/evidence/task-10-function-doc-fail.txt` with `COMMAND`, marker lines, and `EXIT_CODE`.
