# F3 Real Manual QA Evidence

Date: 2026-03-06
Scope: `gbp_compute_nodes` function-level manual QA only

## Command Transcript Summary

| case | command | observed_exit | expected_exit_polarity | key_cli_markers |
|---|---|---:|---|---|
| default run | `make -C nocbp_verilator run LEVEL=unit TEST=gbp_compute_nodes` | `0` | pass (`0`) | `GBP_COMPUTE_NODES_UNIT_START_MARKER`; `GBP_COMPUTE_NODES_VARIABLE_PARITY_PASS_MARKER`; `GBP_COMPUTE_NODES_FACTOR_PARITY_PASS_MARKER`; `GBP_COMPUTE_NODES_UNIT_PASS_MARKER`; `GBP_COMPUTE_NODES_SEMANTIC_REPORT_MARKER ... variable_status=PASS factor_status=PASS` |
| perturb run | `GBP_COMPUTE_NODES_PERTURB=1 make -C nocbp_verilator run LEVEL=unit TEST=gbp_compute_nodes` | `2` | fail (non-zero) | `GBP_COMPUTE_NODES_UNIT_START_MARKER`; `GBP_COMPUTE_NODES_NEGATIVE_MARKER: scenario=perturb_mode_detected`; `GBP_COMPUTE_NODES_FACTOR_PERTURB_RESULT_MARKER failures=1`; `GBP_COMPUTE_NODES_NEGATIVE_MARKER: scenario=perturb_result total_failures=2`; `GBP_COMPUTE_NODES_SEMANTIC_REPORT_MARKER ... variable_status=FAIL factor_status=FAIL` |
| JSON validation | `python3 -m json.tool nocbp_verilator/tests/oracle/generated/gbp_compute_nodes_semantic_report.json >/dev/null` | `0` | pass (`0`) | no stdout by design; exit code confirms valid JSON syntax |

## Reproducibility Check

- Re-ran both functional commands without changing sources.
- Re-run exits: default=`0`, perturb=`2`.
- Re-run marker polarity matched first run (`PASS` semantic status in default, `FAIL` semantic status in perturb).

## Verdict

`PASS`: user-visible marker lines are present and stable; pass/fail exit code polarity is reproducible (`default=0`, `perturb=non-zero`, observed `2`).
