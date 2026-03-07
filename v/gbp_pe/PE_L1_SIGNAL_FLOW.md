# PE L1 Signal Flow (Port/Interface Level)

Scope: only first-level modules below.

- `control_unit`
- `stream_dispatcher`
- `compute_unit`
- `read_stream_engine`
- `write_stream_engine`
- `spm_subsystem`

Notes:

- `clk/reset` are intentionally excluded.
- This document follows current RTL port/interface definitions in `v/gbp_pe/*.sv`.
- "Implemented" means the port/interface exists with clear direction at module boundary. It does not imply `pe_top` has already instantiated/wired all modules.

## 1) `control_dispatch_if` (control_unit <-> stream_dispatcher)

| Signal | From Module | To Module | Role | Status |
|---|---|---|---|---|
| `if_control_dispatch_if_control.valid` | `control_unit` | `stream_dispatcher` | Control/descriptor valid from scheduler side | Implemented |
| `if_control_dispatch_if_control.data` | `control_unit` | `stream_dispatcher` | Control payload (currently `logic` in interface) | Implemented |
| `if_control_dispatch_if_control.ready` | `stream_dispatcher` | `control_unit` | Backpressure/accept from dispatcher side | Implemented |

## 2) `control_compute_if` (control_unit <-> compute_unit)

| Signal | From Module | To Module | Role | Status |
|---|---|---|---|---|
| `if_control_compute_if_control.start` | `control_unit` | `compute_unit` | Launch compute transaction | Interface exists in `control_unit`; `compute_unit` side not exposed yet |
| `if_control_compute_if_control.mode` | `control_unit` | `compute_unit` | Compute mode select (`logic mode` currently) | Interface exists in `control_unit`; `compute_unit` side not exposed yet |
| `if_control_compute_if_control.done` | `compute_unit` | `control_unit` | Completion feedback | Interface exists in `control_unit`; `compute_unit` side not exposed yet |

## 3) `stream_control_if` (read/write engines -> control_unit)

| Signal | From Module | To Module | Role | Status |
|---|---|---|---|---|
| `if_stream_control_if_read.occ` | `read_stream_engine` | `control_unit` | Read-side FIFO occupancy | Implemented at module boundaries |
| `if_stream_control_if_read.afull` | `read_stream_engine` | `control_unit` | Read-side almost-full indication | Implemented at module boundaries |
| `if_stream_control_if_write.occ` | `write_stream_engine` | `control_unit` | Write-side FIFO occupancy | Implemented at module boundaries |
| `if_stream_control_if_write.afull` | `write_stream_engine` | `control_unit` | Write-side almost-full indication | Implemented at module boundaries |

## 4) `stream_dispatcher_if` (stream_dispatcher -> read/write engines)

### Read path (`if_stream_dispatcher_if_read`)

| Signal | From Module | To Module | Role | Status |
|---|---|---|---|---|
| `if_stream_dispatcher_if_read.valid` | `stream_dispatcher` | `read_stream_engine` | Read descriptor valid | Implemented at module boundaries |
| `if_stream_dispatcher_if_read.data` | `stream_dispatcher` | `read_stream_engine` | Read descriptor payload (`gbp_pkg::desc_t`) | Implemented at module boundaries |
| `if_stream_dispatcher_if_read.ready` | `read_stream_engine` | `stream_dispatcher` | Read engine can accept descriptor | Implemented at module boundaries |

### Write path (`if_stream_dispatcher_if_write`)

| Signal | From Module | To Module | Role | Status |
|---|---|---|---|---|
| `if_stream_dispatcher_if_write.valid` | `stream_dispatcher` | `write_stream_engine` | Write descriptor valid | Implemented at module boundaries |
| `if_stream_dispatcher_if_write.data` | `stream_dispatcher` | `write_stream_engine` | Write descriptor payload (`gbp_pkg::desc_t`) | Implemented at module boundaries |
| `if_stream_dispatcher_if_write.ready` | `write_stream_engine` | `stream_dispatcher` | Write engine can accept descriptor | Implemented at module boundaries |

## 5) `mic_spm_arbiter_if` (read_stream_engine <-> spm_subsystem)

| Signal | From Module | To Module | Role | Status |
|---|---|---|---|---|
| `mic_to_spm_arbiter.spm_rd_req_valid` | `read_stream_engine` | `spm_subsystem` | Read request valid | Implemented |
| `mic_to_spm_arbiter.spm_rd_req_addr` | `read_stream_engine` | `spm_subsystem` | Read request address | Implemented |
| `mic_to_spm_arbiter.spm_rd_req_bytes` | `read_stream_engine` | `spm_subsystem` | Read request byte count/beat size field | Implemented |
| `mic_to_spm_arbiter.spm_rd_rsp_valid` | `spm_subsystem` | `read_stream_engine` | Read response valid | Implemented |
| `mic_to_spm_arbiter.spm_rd_rsp_data` | `spm_subsystem` | `read_stream_engine` | Read response data | Implemented |

## 6) `mic_spm_arbiter_wr_if` (write_stream_engine <-> spm_subsystem)

| Signal | From Module | To Module | Role | Status |
|---|---|---|---|---|
| `mic_to_spm_arbiter.spm_wr_req_valid` | `write_stream_engine` | `spm_subsystem` | Write request valid | Implemented |
| `mic_to_spm_arbiter.spm_wr_req_addr` | `write_stream_engine` | `spm_subsystem` | Write request address | Implemented |
| `mic_to_spm_arbiter.spm_wr_req_data` | `write_stream_engine` | `spm_subsystem` | Write request data | Implemented |
| `mic_to_spm_arbiter.spm_wr_req_wstrb` | `write_stream_engine` | `spm_subsystem` | Write strobe/mask | Implemented |
| `mic_to_spm_arbiter.spm_wr_req_ready` | `spm_subsystem` | `write_stream_engine` | Write-side backpressure/accept | Implemented |

## 7) `stream_if` on first-level modules

This interface exists on three first-level modules with current directions below.

| Interface Instance | From Module | To Module | Role | Status |
|---|---|---|---|---|
| `read_stream_engine.if_stream_if_stream` (`stream_if.slave`) | `read_stream_engine` (`valid/data`) | external downstream consumer | Streams read data out of RSE | Implemented as boundary port |
| `compute_unit.if_stream_if_stream` (`stream_if.slave`) | `compute_unit` (`valid/data`) | external downstream consumer | Streams compute results out | Implemented as boundary port |
| `write_stream_engine.if_stream_if_stream` (`stream_if.master`) | external upstream producer | `write_stream_engine` (`ready` feedback) | Accepts write payload stream into WSE | Implemented as boundary port |

Current consequence at first-level boundary:

- `read_stream_engine` and `compute_unit` both expose producer-like `stream_if.slave` direction.
- `write_stream_engine` exposes consumer-like `stream_if.master` direction.
- Exact one-to-one producer/consumer pairing among these three modules is not fully locked by current top-level wiring yet.

## 8) `compute_unit` discrete data/control ports (non-interface)

| Signal | From Module | To Module | Role | Status |
|---|---|---|---|---|
| `data_a_i` | upstream provider (not one of listed six by current ports) | `compute_unit` | Operand A vector | Implemented on `compute_unit` boundary |
| `data_b_i` | upstream provider (not one of listed six by current ports) | `compute_unit` | Operand B vector | Implemented on `compute_unit` boundary |
| `op_i` | upstream provider | `compute_unit` | Operation select | Implemented on `compute_unit` boundary |
| `valid_i` | upstream provider | `compute_unit` | Input transaction valid | Implemented on `compute_unit` boundary |
| `data_o` | `compute_unit` | downstream consumer (not one of listed six by current ports) | Result vector output | Implemented on `compute_unit` boundary |
| `valid_o` | `compute_unit` | downstream consumer | Result valid | Implemented on `compute_unit` boundary |

---

If needed, this can be extended into a strict wiring checklist in `A.port -> B.port` form once `pe_top` final instantiation/wiring is fixed.
