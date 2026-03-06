# F2 Code Quality Review (Rerun)

## Scope and Authority
- Plan authority (read-only): `.sisyphus/plans/control-compute-gbp-rtl-verification.md`
- RTL scope: `v/gbp_pe/control_unit.sv`, `v/gbp_pe/compute_unit.sv`
- Unit/integration scope: `nocbp_verilator/tests/unit/control_unit_top.cc`, `nocbp_verilator/tests/unit/compute_unit_top.cc`, `nocbp_verilator/tests/unit/pe_unit.cc`, `nocbp_verilator/tests/unit/gbp_pe.cc`, `nocbp_verilator/tests/integration/pe_top_integration.cc`
- Related harness files checked for coupling context: `nocbp_verilator/tops/unit/control_unit_top.sv`, `nocbp_verilator/tops/unit/compute_unit_top.sv`, `nocbp_verilator/tops/unit/pe_unit_top.sv`, `nocbp_verilator/tops/integration/pe_top_integration.sv`

## Repository State Snapshot
- `git status --short`: repository currently has broad untracked workspace content; no code edits were made in this task outside requested evidence/notepad updates.
- `git diff --stat`: no tracked-diff summary was reported at review start.

## Anti-Pattern Scan
- Pattern used (language-adapted, per instruction): `TODO|FIXME|HACK|XXX|@ts-ignore|as any|console.log`
- Scan paths: `v/gbp_pe/*.sv` and `nocbp_verilator/tests/*.cc`
- Result: **no matches** in scoped RTL/tests.

## Targeted Sanity Runs (Current)
- `make -C nocbp_verilator run LEVEL=unit TEST=compute_unit_top` -> PASS with `OP_MATRIX_PASS_MARKER` and `failures=0`.
- `make -C nocbp_verilator run LEVEL=integration TEST=pe_top_integration VERILATOR="verilator -Wno-fatal -Wno-WIDTHCONCAT -Wno-EOFNEWLINE"` -> PASS with `pe_top integration: PASS`.

## Findings by Severity

### High (blocking)
- **None found.** No high-severity correctness defect observed in current scoped RTL/tests.

### Medium (non-blocking, monitor)
- **M1 - Dispatch pulse depends on sampled ready edge in `S_IDLE`**: `control_unit` emits a message dispatch only on `dispatch_ready_rise_r` while command is pending (`v/gbp_pe/control_unit.sv:31`, `v/gbp_pe/control_unit.sv:130`, `v/gbp_pe/control_unit.sv:174`). This is behaviorally valid for current tests but tightly couples output behavior to historical ready transitions, which is less maintainable than pure combinational handshake intent.
- **M2 - Division launch does not consume per-lane inReady**: `compute_unit` wires `div_ready_lo` from each divider but does not gate `div_active_r` assertion on those ready signals (`v/gbp_pe/compute_unit.sv:73`, `v/gbp_pe/compute_unit.sv:163`, `v/gbp_pe/compute_unit.sv:217`). Current regression is green, so this is not a present blocker; it remains a resilience risk if divider readiness behavior changes.

### Low (non-blocking maintainability)
- **L1 - Unused helper in unit test**: `set_bank` is declared but unused (`nocbp_verilator/tests/unit/control_unit_top.cc:54`), adding review noise only.

## Explicit Re-Assessment: Post-Compute Write-Window (Prior Medium Concern)
- Prior concern was that immediate acceptance could be too permissive. In current code, acceptance is not standalone: post-`compute_done` handling is coupled with additional sequencing and recovery checks.
- Immediate/sustained-path gating is implemented in a bounded state machine (`nocbp_verilator/tests/integration/pe_top_integration.cc:354`, `nocbp_verilator/tests/integration/pe_top_integration.cc:404`, `nocbp_verilator/tests/integration/pe_top_integration.cc:426`).
- The test also enforces transaction/order integrity (`nocbp_verilator/tests/integration/pe_top_integration.cc:518`) and requires either immediate write visibility or sustained-backpressure recovery (`nocbp_verilator/tests/integration/pe_top_integration.cc:528`).
- Given these guards and current integration PASS run, the write-window adjustment is now **acceptable for this plan gate** (no blocking defect).

## Correctness Risk Summary
- Blocking defects: **0**
- Non-blocking medium risks: **2** (`control_unit` ready-edge coupling, `compute_unit` divider-ready robustness)
- Non-blocking low notes: **1** (unused helper in unit test)

## Final Verdict
- **PASS_WITH_NOTES**
- Blocking rationale: no high-severity correctness issue found in scoped control/compute RTL and directly-coupled unit/integration tests; current targeted sanity commands pass.
- Notes rationale: medium maintainability/resilience risks remain and should be tracked, but they do not block final-wave acceptance for this plan scope.
