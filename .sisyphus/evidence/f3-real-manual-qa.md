# F3 Real Manual QA Evidence

Date: 2026-03-06
Scope: `F3. Real Manual QA` verification runbook execution (manual CLI run)

## Manual Command Observations

| Type | Command | Exit code | Operator-actionable output | Evidence lines |
|---|---|---:|---|---|
| Representative PASS | `make -C nocbp_verilator run LEVEL=unit TEST=control_unit_top` | `0` | Yes | `control_unit unit test`; `PASS: Test 1` ... `PASS: Test 8d`; `All tests passed` |
| Representative NEGATIVE | `PE_UNIT_HOLD_DONE=1 make -C nocbp_verilator run LEVEL=unit TEST=pe_unit` | `2` | Yes | `PE_UNIT_WITHHELD_DONE_MARKER`; `pe_unit: FAIL: timeout: rsp_done_i withheld while command pending`; `make: *** [Makefile:50：run] 错误 1` |
| Integration PASS | `make -C nocbp_verilator run LEVEL=integration TEST=pe_top_integration VERILATOR="verilator -Wno-fatal -Wno-WIDTHCONCAT -Wno-EOFNEWLINE"` | `0` | Yes | `build/integration/pe_top_integration/obj_dir/Vpe_top_integration`; `pe_top integration: PASS` |

## UX Quality Notes

- Output clarity: PASS paths are easy to confirm via explicit terminal markers (`All tests passed`, `pe_top integration: PASS`).
- Failure readability: NEGATIVE path is readable and specific; the timeout reason directly names the blocked signal (`rsp_done_i`) and context (pending command).
- Marker usability: Withheld marker (`PE_UNIT_WITHHELD_DONE_MARKER`) and coupled markers are easy to grep and suitable for operator triage.

## Final Verdict

`PASS`

All required commands were executed manually; observed exit codes match expected polarity for this run (`pass=0`, `negative=2`).
