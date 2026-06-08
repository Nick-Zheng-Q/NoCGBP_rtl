# Legacy Module Migration Plan

## 1. Overview

The `v/gbp_pe/` directory contains modules from two architecture generations:

- **New architecture** (June 2–3): `phase_controller`, `metadata_scanner`, `scoreboard_prefetcher`, `pull_client`, `pull_server`, `response_collector`, `neighbor_state_accumulator`, `writeback_controller`, `noc_adapter`/`_tx`/`_rx`.
- **Old architecture** (April–May): `control_unit_gbp`, `compute_unit`, `read_stream_engine`, `write_stream_engine`, `stream_dispatcher`, `agu`, `mic_read`/`mic_write`, `addr_fifo`/`data_fifo`, `gbp_pe_noc_bridge`, `gbp_pe_endpoint_adapter`, plus the SPM family (`spm_subsystem`, `spm_bank`, `spm_bank_array`, `spm_rd_arbiter`, `spm_wr_arbiter`).

The new architecture is defined in `docs/gbp_pe/04_PE_MICROARCHITECTURE.md` and `05_INTERFACES.md`. This document maps every old module to its disposition.

---

## 2. Disposition Matrix

| Old Module | Action | Replacement / Target | Rationale |
|------------|--------|---------------------|-----------|
| `control_unit_gbp.sv` | **Delete** | `phase_controller` + `node_scheduler` | Old FSM monolith (`S_VAR_READ_STATE`, `S_FAC_READ_FACTOR`, …) is replaced by distributed control: phase scheduling + node selection + metadata scanning + scoreboard tracking. |
| `control_unit.sv` | **Delete** | `phase_controller` + `node_scheduler` | Even older predecessor of `control_unit_gbp`. |
| `compute_unit.sv` | **Rewrite** | New `compute_unit` per `05_INTERFACES.md` §2.10 | Core FP32 SIMD datapath is reusable, but interface is wrong. Old interface: `cmd_kind_i[1:0]`, `data_a_i`/`data_b_i`, `read_stream_if`/`write_stream_if`. New interface: `cmd_valid`/`cmd_ready`, `cmd_node_id`, `cmd_is_factor`, `ns_valid`/`ns_ready`/`ns_data`/`ns_last`, `rd_stream_if`/`wr_stream_if`, `done_valid`, `batch_done`. |
| `compute_unit_wrapper.sv` | **Delete** | Inline into new `compute_unit` | Wrapper only existed to bridge old control_unit → compute_unit signals. No longer needed. |
| `read_stream_engine.sv` | **Modify** | Update interfaces, keep core datapath | Core mechanism (descriptor queue → AGU → addr FIFO → SPM read → data FIFO → stream out) aligns with new "descriptor-driven stream engine" architecture. Needs: (1) replace SystemVerilog `interface` with explicit ports, (2) remove `meta_consume` / sticky META hold (META now read by `metadata_scanner` independently), (3) update descriptor format to match new spec. |
| `write_stream_engine.sv` | **Modify** | Update interfaces, keep core datapath | Same rationale as read_stream_engine. Core datapath (descriptor → AGU → write to SPM) is reusable. |
| `stream_dispatcher.sv` | **Delete** | Functionality absorbed into `compute_unit` | Old dispatcher arbitrated between multiple stream sources. New architecture has a single compute unit issuing descriptors sequentially. |
| `agu.sv` | **Modify** | Keep 1D linear address generator, add explicit ports | Old AGU already does exactly what new spec needs: `base_addr` + `xfer_bytes` → per-beat address sequence with `step_bytes` stride. Needs: (1) replace `desc_t` with new descriptor format, (2) decide whether to implement unused `y_count`/`y_stride_bytes` 2D mode, (3) remove `stream_type` / META special casing. |
| `mic_read.sv` / `mic_write.sv` | **Delete** | No equivalent in new architecture | These were MIC (Memory Interface Controller) ports for external memory. New architecture assumes all state lives in PE-local SPM; DMA/loader is a single client of `spm_arbiter`. |
| `addr_fifo.sv` / `data_fifo.sv` | **Delete** | Use basejump STL FIFOs (`bsg_fifo_1r1w_small`, `bsg_two_fifo`) | Old ad-hoc FIFOs are replaced by proven BaseJump STL primitives already used in `noc_adapter`. |
| `gbp_pe_noc_bridge.sv` | **Delete** | `noc_adapter` + `noc_adapter_tx` + `noc_adapter_rx` | Old bridge was a monolithic NoC wrapper. New design splits TX/RX and uses mailbox store-based protocol (`MBX_NOTIFICATION`, `MBX_FETCH_REQ_*`, `MBX_RESP_*`). |
| `gbp_pe_endpoint_adapter.sv` | **Delete** | `noc_adapter` | Same rationale. Endpoint standard wrapping is now inside `noc_adapter`. |
| `interfaces.sv` | **Delete** | Port-level valid/ready signals per `05_INTERFACES.md` | Old SystemVerilog interfaces (`read_stream_if`, `write_stream_if`, `scratchpad_if`, `endpoint_if`, …) cause Verilator compatibility issues and are replaced by explicit port lists. |
| `spm_arbiter.sv` | **Rewrite** | New 7-client round-robin `spm_arbiter` | Current arbiter has only 2 clients (`rd_if`, `wr_if`). New architecture needs 7: MetadataScanner, CU_state_rd, CU_staging_rd, CU_wb, PullServer, RespCollector, DMA. |
| `spm_rd_arbiter.sv` / `spm_wr_arbiter.sv` | **Delete** | Merged into unified `spm_arbiter` | New architecture uses a single arbiter with separate read/write grant vectors rather than split read/write arbiters. |
| `spm_subsystem.sv` | **Modify** | Keep bank array logic, update interfaces | Bank array / interleaving logic is likely reusable. Must update top-level ports to match new `spm_arbiter` bank interface. |
| `spm_bank.sv` | **Keep** | Minor port renames only | Single bank implementation is architecture-agnostic. |
| `spm_bank_array.sv` | **Keep** | Minor port renames only | Bank array wiring is reusable. |
| `spm_bank_dpi.sv` | **Keep** | DPI bridge for simulation | Only used in simulation / DPI tests. No functional change needed. |
| `gbp_pe.sv` | **Rewrite** | New top-level `gbp_pe` | Old top instantiates `control_unit_gbp`, `compute_unit`, `gbp_pe_noc_bridge`, etc. New top instantiates four subsystem wrappers (`gbp_pe_control_subsystem`, `gbp_pe_compute_subsystem`, `gbp_pe_memory_subsystem`, `gbp_pe_fetch_subsystem`), plus `neighbor_state_accumulator`, `writeback_controller`, and `noc_adapter`. |
| `pe_top.sv` | **Delete** | Replaced by `gbp_pe_*_subsystem` wrappers | `pe_top` was an ad-hoc internal wrapper. The new architecture uses four well-defined subsystem wrappers instead. See `04_PE_MICROARCHITECTURE.md` §1 and `05_INTERFACES.md` §2.16. |

---

## 3. Implementation Order for Remaining Work

Based on `00_IMPLEMENTATION_ORDER.md` dependency graph, the remaining modules should be implemented in this order:

```
Phase 1: Missing core
  node_scheduler ──► (unblocks metadata_scanner → compute path)

Phase 2: Compute datapath (can proceed in parallel with Phase 1)
  compute_unit ──► read_stream_engine ──► write_stream_engine
       │                │                      │
       └──► agu (inside stream engines) ◄──────┘

Phase 3: Memory subsystem
  spm_arbiter ──► spm_subsystem (port renames)

Phase 4: Top-level integration
  gbp_pe ──► pe_top (if needed)
```

| # | Module | Complexity | Blockers | Notes |
|---|--------|-----------|----------|-------|
| 10 | `node_scheduler` | ~150 lines | None | RR scan over `node_ready` vector. Interface defined in `05_INTERFACES.md`. |
| 11 | `compute_unit` | ~400 lines | `node_scheduler`, `neighbor_state_accumulator` | Reuse FP32 lanes from old `compute_unit`. Replace control FSM with new command interface. |
| 12 | `read_stream_engine` | ~200 lines | `compute_unit`, `spm_arbiter` | Descriptor queue → AGU → SPM arbiter read port. |
| 13 | `write_stream_engine` | ~200 lines | `compute_unit`, `spm_arbiter` | Descriptor queue → AGU → SPM arbiter write port. |
| 14 | `spm_arbiter` | ~150 lines | None | 7-client RR arbiter. Bank conflict stall logic. |
| 15 | `gbp_pe` | ~300 lines | All above | Top-level wiring. |

---

## 4. File Cleanup Checklist

After new modules are implemented and verified, delete or archive the following old files:

```
v/gbp_pe/control_unit.sv              → DELETE
v/gbp_pe/control_unit_gbp.sv          → DELETE
v/gbp_pe/compute_unit_wrapper.sv      → DELETE
v/gbp_pe/stream_dispatcher.sv         → DELETE
v/gbp_pe/mic_read.sv                  → DELETE
v/gbp_pe/mic_write.sv                 → DELETE
v/gbp_pe/addr_fifo.sv                 → DELETE
v/gbp_pe/data_fifo.sv                 → DELETE
v/gbp_pe/gbp_pe_noc_bridge.sv         → DELETE
v/gbp_pe/gbp_pe_endpoint_adapter.sv   → DELETE
v/gbp_pe/spm_rd_arbiter.sv            → DELETE
v/gbp_pe/spm_wr_arbiter.sv            → DELETE
v/gbp_pe/interfaces.sv                → DELETE

v/gbp_pe/read_stream_engine.sv        → MODIFY (keep core, update interfaces)
v/gbp_pe/write_stream_engine.sv       → MODIFY (keep core, update interfaces)
v/gbp_pe/agu.sv                       → MODIFY (keep core, update descriptor format)
v/gbp_pe/spm_arbiter.sv               → DELETE (rewrite)
v/gbp_pe/compute_unit.sv              → DELETE (rewrite)
v/gbp_pe/gbp_pe.sv                    → DELETE (rewrite)
v/gbp_pe/pe_top.sv                    → DELETE (replaced by subsystem wrappers)
```

**New files to create:**
```
v/gbp_pe/gbp_pe_control_subsystem.sv  → NEW
v/gbp_pe/gbp_pe_compute_subsystem.sv  → NEW
v/gbp_pe/gbp_pe_memory_subsystem.sv   → NEW
v/gbp_pe/gbp_pe_fetch_subsystem.sv    → NEW
```

**Files to keep with minor edits:**
```
v/gbp_pe/spm_bank.sv                  → Keep (port renames only)
v/gbp_pe/spm_bank_array.sv            → Keep (port renames only)
v/gbp_pe/spm_bank_dpi.sv              → Keep (no change)
v/gbp_pe/spm_subsystem.sv             → Keep (top-level port update) — or absorbed into gbp_pe_memory_subsystem
```

---

## 5. Test Impact

Old unit tests that will become invalid and should be removed:

| Old Test | Reason |
|----------|--------|
| `tests/unit/control_unit_top*` | `control_unit_gbp` deleted |
| `tests/unit/compute_unit_top*` | `compute_unit` interface changed |
| `tests/unit/gbp_compute_engine_top*` | Old compute path deleted |
| `tests/unit/gbp_pe_top*` | Old top-level deleted |
| `tests/unit/pe_unit_top*` | Old top-level deleted |
| `tests/integration/pe_top_integration*` | `pe_top` deleted; replace with subsystem tests and new `gbp_pe` system test |
| `tests/unit/mic_read*` / `mic_write*` | Modules deleted |
| `tests/unit/read_stream_engine*` / `write_stream_engine*` | Modules rewritten |
| `tests/unit/stream_dispatcher*` | Module deleted |
| `tests/unit/endpoint_rx_top*` | Old NoC adapter deleted |
| `tests/unit/agu_top*` | Module rewritten |

New tests to add:

| New Test | Module Under Test |
|----------|-------------------|
| `node_scheduler` | `node_scheduler` |
| `compute_unit` | `compute_unit` (new interface) |
| `read_stream_engine` | `read_stream_engine` |
| `write_stream_engine` | `write_stream_engine` |
| `spm_arbiter` | `spm_arbiter` (7-client) |
| `gbp_pe_control_subsystem` | `gbp_pe_control_subsystem` |
| `gbp_pe_compute_subsystem` | `gbp_pe_compute_subsystem` |
| `gbp_pe_memory_subsystem` | `gbp_pe_memory_subsystem` |
| `gbp_pe_fetch_subsystem` | `gbp_pe_fetch_subsystem` |
| `gbp_pe` | `gbp_pe` (full PE integration) |

---

## 6. Open Questions

1. **Compute Unit reuse depth**: How much of the old `compute_unit.sv` FP32 datapath (HardFloat adders/multipliers/dividers) can be salvaged? The old unit has 16 lanes with `OP_ADD`/`OP_SUB`/`OP_MUL`/`OP_DIV`. The new spec calls for `MAT_ADD`, `MAT_SUB`, `MAT_MUL`, `MAT_VEC_MUL`, `MAT_INV`, `FACTOR_MSG_SOLVE`, `ROBUSTIFY`, `RELINEARIZE`. These are higher-level ops that likely schedule the same FP32 lanes, but the control sequencer is completely different.

2. **Stream engine granularity**: Does the new `compute_unit` issue one descriptor per matrix/vector operation, or one per word/beat? This affects AGU complexity.

3. **SPM bank count**: Old code uses parameterized `NUM_BANKS` (default 8). New spec says "First version uses interleave-by-beat (word_addr[3:1])". Confirm bank count matches interleave scheme.

4. **`pe_top` necessity**: Is `pe_top` still the mesh-level wrapper, or has the mesh instantiation moved to `v/chip/`? If `pe_top` is redundant, delete it rather than rewrite.
