# gbp_pe Unit Test (Top-Level)

> **Status**: 🟡 **Partially implemented** — `gbp_pe.sv` RTL is complete (noc_adapter + 4 subsystems + pull_server + accumulator + writeback_controller). Top-level functional test is not yet written.

## 1. Test Objective

Verify that the `gbp_pe` top-level module correctly:
- Instantiates `noc_adapter` and connects to manycore `link_sif` interface
- Integrates 4 subsystems (`control`, `compute`, `memory`, `fetch`) with `pull_server`
- Handles incoming NoC stores (NOTIFICATION, FETCH_REQUEST, FETCH_RESPONSE)
- Sends outgoing NoC stores (FETCH_REQUEST, FETCH_RESPONSE, NOTIFICATION)
- Manages the foreground compute pipeline (schedule → scan → compute → writeback)

## 2. Preconditions

- Module: `gbp_pe`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: Reset, all outputs deasserted

## 3. Architecture Under Test

```
gbp_pe
├── noc_adapter (link_sif_i/o)
├── gbp_pe_control_subsystem
│   ├── phase_controller
│   ├── node_scheduler
│   └── metadata_scanner
├── gbp_pe_compute_subsystem
│   ├── compute_unit
│   ├── read_stream_engine (×2)
│   └── write_stream_engine
├── gbp_pe_memory_subsystem
│   ├── spm_arbiter
│   └── spm_bank_array (8 banks)
├── gbp_pe_fetch_subsystem
│   ├── scoreboard_prefetcher
│   ├── pull_client
│   └── response_collector
├── pull_server
├── neighbor_state_accumulator
└── writeback_controller
```

## 4. Test Stimulus (Planned)

### 4.1 Test Case 1: NoC Store Ingress

**Scenario**: External PE sends NOTIFICATION store to this PE.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | link_sif_i | NOTIF_STORE | NoC forward packet |
| T+1   | store_addr | MBX_NOTIFICATION | Mailbox address |
| T+1   | store_data | {is_factor, src_node} | Payload |
| T+2   | link_sif_i.fwd.v | 0 | Clear |

**Expected**: `rx_notif_valid_o` asserted to `gbp_pe_fetch_subsystem`.

### 4.2 Test Case 2: Compute Pipeline End-to-End

**Scenario**: One full node update cycle.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | node_ready[0x10] | 1 | Node ready (from scoreboard) |
| T+N   | cmd_valid | 1 | Control → Compute command |
| T+N+M | done_valid | 1 | Compute done |
| T+N+M+1 | wb_done | 1 | Writeback complete |

### 4.3 Test Case 3: Fetch Request Egress

**Scenario**: Remote adjacency triggers fetch request to NoC.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | adj_valid (remote) | 1 | Register remote edge |
| T+2   | rx_notif_valid | 1 | Notification arrives |
| T+5   | tx_fetch_req_valid | 1 | Fetch request to NoC |

## 5. Pass/Fail Criteria

- [ ] NoC stores correctly decoded to subsystem inputs
- [ ] Compute pipeline completes without deadlock
- [ ] Fetch request issued for remote edges
- [ ] Writeback sends NOTIFICATION to consuming neighbors
- [ ] Credit-based flow control respected
- [ ] Phase switching occurs when all nodes visited

## 6. Corner Cases

1. **Reset during active compute**: Clean abort, no SPM corruption
2. **NoC TX backpressure**: All 3 TX sources (notif, fetch_req, fetch_resp) stall correctly
3. **Bank conflict under full load**: All 7 clients access SPM simultaneously
4. **Scoreboard full**: New remote edges blocked until completions free slots
5. **Empty queue**: No schedulable nodes, idle state

## 7. Related Documents

| Document | Content |
|----------|---------|
| `../../04_PE_MICROARCHITECTURE.md` | Module hierarchy |
| `../../05_INTERFACES.md` | Port definitions |
| `../../06_PE_CONTROL_FLOW.md` | Control flow |
| `../subsystem_tests/` | Subsystem integration tests (prerequisite) |
