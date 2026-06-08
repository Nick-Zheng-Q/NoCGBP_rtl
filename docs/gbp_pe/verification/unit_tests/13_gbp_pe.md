# gbp_pe Unit Test (Top-Level Whitebox)

> **Status**: 🟡 **Planned** — `gbp_pe.sv` RTL is complete. This document defines the top-level whitebox test plan. RTL testbench (`gbp_pe_top.sv` + `gbp_pe.cc`) not yet implemented.
>
> **Priority**: A (highest among remaining work).

## 1. Test Objective

Verify that `gbp_pe.sv` — the complete GBP PE top-level module — correctly integrates all subsystems, the NoC adapter, and the standalone leaf modules (`pull_server`, `neighbor_state_accumulator`, `writeback_controller`) into a functionally correct unit.

This is the **first test that exercises the full PE as an integrated whole**, not just subsystems in isolation. It validates:

1. **Subsystem wiring**: Control → Compute → Memory → Fetch subsystems connect without port mismatches.
2. **NoC ingress/egress**: Incoming stores (NOTIFICATION, FETCH_REQUEST, FETCH_RESPONSE) decode to correct subsystem inputs; outgoing stores (NOTIFICATION, FETCH_REQUEST, FETCH_RESPONSE) encode correctly.
3. **Foreground compute pipeline end-to-end**: Schedule → Metadata Scan → (Fetch if remote) → Accumulate → Compute → Writeback → Phase Switch.
4. **Internal control flow**: Scoreboard ↔ Fetch Subsystem ↔ Pull Server handshake; Writeback ↔ Scoreboard reset; Phase Controller ↔ Node Scheduler visited-mask management.
5. **GBP_WHITEBOX_TEST interface**: The whitebox command-injection path drives `comp_cmd_*` directly, bypassing `control_subsystem`, allowing deterministic test of the compute/writeback path.

## 2. Preconditions

### 2.1 Global

- DUT: `gbp_pe` with `GBP_WHITEBOX_TEST` defined.
- Clock: 100 MHz (10 ns period).
- Reset: Active-high `reset_i` for 10 cycles (`rst_n` = ~`reset_i`).
- `link_sif_i` tied off to idle (no external NoC traffic unless test case explicitly drives it).
- Barrier pins tied off (`barrier_data_i = 0`, `barrier_src_r_o` / `barrier_dest_r_o` ignored).
- Coordinates: `my_x_i = 2, my_y_i = 1` (arbitrary non-zero sub-cord).

### 2.2 SPM Initialization (via Verilator DPI / direct memory access)

Each test case pre-loads SPM via testbench backdoor before releasing reset. SPM layout follows `02_SPM_AND_METADATA.md`.

**Example: Node N0 (factor, node_id=0x10) with one local consumer**

```
META region (word addresses):
  0x010: NodeHeader
    [9:0]   node_id      = 0x010
    [13:10] dof          = 2
    [17:14] adj_count    = 1
    [35:18] adj_words    = 4        (1 AdjEntry = 4 words)
    [53:36] state_base   = 0x200
    [59:54] state_words  = 6
  0x100..0x103: AdjEntry[0]
    word0: neighbor_id=0x011, neighbor_x=2, neighbor_y=1, is_local=1

STATE region @ 0x200:
  0x200..0x205: {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}
```

> **Whitebox advantage**: Because `GBP_WHITEBOX_TEST` bypasses `control_subsystem`, we do **not** need to pre-populate the scoreboard or wait for `node_scheduler` to select a node. We directly inject the compute command.

---

## 3. Architecture Under Test

```
gbp_pe_top
├── DUT: gbp_pe (GBP_WHITEBOX_TEST defined)
│   ├── noc_adapter ── link_sif_i / link_sif_o
│   ├── gbp_pe_control_subsystem (bypassed in whitebox mode)
│   ├── gbp_pe_compute_subsystem
│   ├── gbp_pe_memory_subsystem
│   ├── gbp_pe_fetch_subsystem
│   ├── pull_server (standalone leaf)
│   ├── neighbor_state_accumulator (standalone leaf)
│   └── writeback_controller (standalone leaf)
└── Testbench (C++):
    ├── SPM backdoor init / read
    ├── link_sif packet injector / monitor
    └── Whitebox command driver
```

---

## 4. Test Stimulus

### 4.1 Test Case 1: Local-Only Node Compute Cycle (Whitebox Command Injection)

**Objective**: Verify the full compute pipeline for a local node with no remote edges.

**Preconditions**:
- SPM initialized with one factor node (node_id=0x10), 1 local consumer, 6 state_words.
- Scoreboard not pre-loaded (whitebox bypasses it).

**Stimulus**:

| Cycle | Action | Signal / Value | Description |
|-------|--------|----------------|-------------|
| T+0   | Reset  | `reset_i=1`    | Assert reset |
| T+10  | Release| `reset_i=0`    | Release reset |
| T+11  | Inject | `wb_cmd_valid_i=1`, `wb_cmd_node_id_i=0x10`, `wb_cmd_is_factor_i=1`, `wb_cmd_dof_i=2`, `wb_cmd_adj_count_i=1`, `wb_cmd_state_words_i=6` | Whitebox compute command |
| T+12  | Clear  | `wb_cmd_valid_i=0` | Command accepted (`wb_cmd_ready_o` should be 1 at T+11) |
| T+13  | —      | —              | Compute Unit reads STATE via RSE |
| T+13..T+18 | — | —              | 6 words = 3 beats read from SPM |
| T+19..T+30 | — | —              | `gbp_compute_engine` computes (MAT_ADD, etc.) |
| T+31  | Observe| `done_valid_o=1` | Compute done |
| T+32  | Observe| `tx_notif_valid=1` | Writeback sends NOTIFICATION to local consumer |
| T+33  | Observe| `reset_valid_o=1` | Writeback triggers scoreboard reset for node 0x10 |

**Expected Output**:
- `wb_cmd_ready_o` = 1 at T+11 (command accepted).
- `spm_rd0_valid_o` asserted for 3 beats (STATE reads).
- `done_valid_o` = 1 at T+31.
- `tx_notif_valid` = 1 at T+32, payload `{is_factor=1, source_node_id=0x10, target_x=2, target_y=1}`.
- `batch_done_o` = 1 at T+31 (from compute subsystem).

**Pass/Fail**:
- [ ] Command accepted in 1 cycle.
- [ ] 3 STATE beats read from correct SPM address (0x200).
- [ ] Compute completes within 20 cycles of command injection.
- [ ] NOTIFICATION sent to correct local coordinates.
- [ ] No `tx_fetch_req_valid` asserted (no remote edges → no fetch).

---

### 4.2 Test Case 2: Remote Edge Full Fetch-Compute-Writeback

**Objective**: Verify the complete cross-PE message loop within a single PE by simulating external NoC traffic via `link_sif_i`.

**Preconditions**:
- SPM initialized with one variable node (node_id=0x11), 1 remote factor edge (neighbor on PE(3,2)).
- STAGING region is empty.

**Stimulus**:

| Cycle | Action | Signal / Value | Description |
|-------|--------|----------------|-------------|
| T+0   | Reset  | `reset_i=1`    | Assert reset |
| T+10  | Release| `reset_i=0`    | Release reset |
| T+11  | NOTIF  | `link_sif_i` = NOTIFICATION store to MBX_NOTIFICATION | Simulate external factor notifying this PE |
| T+11  | Payload| `{is_factor=1, source_node_id=0x20}` | Factor node 0x20 is ready |
| T+12  | Clear  | `link_sif_i` = idle | |
| T+15  | Observe| `tx_fetch_req_valid=1` | Fetch Subsystem issues FETCH_REQUEST to PE(3,2) |
| T+15  | Payload| `{target=0x20, consumer=0x11, is_factor=1, txn_id=0}` | 3-store sequence starts |
| T+16..T+18 | — | `tx_fetch_req_store_idx` = 0,1,2 | 3 stores transmitted |
| T+20  | RESP   | `link_sif_i` = FETCH_RESPONSE meta store to MBX_RESP_META | Simulate response arriving |
| T+20  | Payload| `{is_factor=1, state_words=6}` | |
| T+21  | RESP   | `link_sif_i` = FETCH_RESPONSE data store #1 | Data beat 0 |
| T+22  | RESP   | `link_sif_i` = FETCH_RESPONSE data store #2 | Data beat 1 |
| T+23  | RESP   | `link_sif_i` = FETCH_RESPONSE data store #3 | Data beat 2 |
| T+24  | RESP   | `link_sif_i` = FETCH_RESPONSE done store to MBX_RESP_DONE | `{txn_id=0, node_id=0x20, consumer=0x11}` |
| T+25  | Observe| `spm_wr_valid_o=1` | Response Collector writes STAGING |
| T+30  | Inject | `wb_cmd_valid_i=1`, `wb_cmd_node_id_i=0x11`, ... | Inject compute command for node 0x11 |
| T+31  | Clear  | `wb_cmd_valid_i=0` | |
| T+32..T+50 | — | — | Compute reads STATE + STAGING, computes |
| T+51  | Observe| `done_valid_o=1` | Compute done |
| T+52  | Observe| `tx_notif_valid=1` | Writeback sends NOTIFICATION to consumers |

**Expected Output**:
- NOTIFICATION correctly decoded: `rx_notif_valid_i=1` to Fetch Subsystem at T+12.
- FETCH_REQUEST issued for remote node 0x20 with correct PE coordinates (3,2).
- FETCH_RESPONSE data written to STAGING region (base addr computed by Response Collector).
- Compute Unit reads both STATE (local) and STAGING (remote neighbor) via Accumulator.
- `done_valid_o` and `tx_notif_valid` asserted after compute.

**Pass/Fail**:
- [ ] NOTIFICATION decoded to `rx_notif_valid_i` within 2 cycles of `link_sif_i` arrival.
- [ ] FETCH_REQUEST targets correct remote PE coordinates.
- [ ] FETCH_RESPONSE data beats written to contiguous STAGING addresses.
- [ ] `complete_valid_o` asserted inside Fetch Subsystem after done store received.
- [ ] Compute completes successfully using combined local + remote state.

---

### 4.3 Test Case 3: Multi-Remote Edges Concurrent

**Objective**: Verify scoreboard can manage multiple outstanding remote fetches without overflow.

**Preconditions**:
- SPM initialized with one variable node (node_id=0x12) with **3 remote factor edges** (to 3 different PEs).
- Scoreboard depth = 64 (default).

**Stimulus**:

| Cycle | Action | Description |
|-------|--------|-------------|
| T+10  | Reset release | |
| T+11  | NOTIF #1 | `link_sif_i` = NOTIFICATION from PE-A |
| T+12  | NOTIF #2 | `link_sif_i` = NOTIFICATION from PE-B |
| T+13  | NOTIF #3 | `link_sif_i` = NOTIFICATION from PE-C |
| T+15..T+25 | Observe | 3 FETCH_REQUEST sequences egress on `tx_fetch_req` |
| T+30..T+50 | Inject responses | Inject FETCH_RESPONSEs for all 3 requests (out of order is allowed) |
| T+55  | Inject cmd | `wb_cmd_valid_i=1` for node 0x12 |

**Expected Output**:
- 3 distinct `txn_id` values assigned (0, 1, 2).
- `scoreboard_occupancy` peaks at 3, never exceeds `SCOREBOARD_DEPTH`.
- All 3 responses written to non-overlapping STAGING regions.
- Compute completes after all responses arrive.

**Pass/Fail**:
- [ ] 3 FETCH_REQUESTs issued with unique `txn_id`.
- [ ] Scoreboard occupancy ≤ 3.
- [ ] No `scoreboard_full` asserted (depth 64 >> 3).
- [ ] All responses written before compute starts.
- [ ] Compute produces 3 outgoing NOTIFICATIONs (one per consumer).

---

### 4.4 Test Case 4: Phase Switch Factor → Variable

**Objective**: Verify `phase_controller` toggles `phase_factor_first` when all factor nodes are visited.

**Preconditions**:
- SPM initialized with 2 factor nodes (N0, N1) and 2 variable nodes (N2, N3), all local-only.
- No remote edges (simplifies test — no fetch path).

**Stimulus**:

| Cycle | Action | Description |
|-------|--------|-------------|
| T+10  | Reset release | |
| T+11  | Inject N0 | `wb_cmd_valid_i=1`, node_id=N0, is_factor=1 |
| T+12  | — | Wait for compute + writeback |
| T+35  | Inject N1 | `wb_cmd_valid_i=1`, node_id=N1, is_factor=1 |
| T+36  | — | Wait for compute + writeback |
| T+60  | Observe | Check `phase_factor_first_o` |

**Expected Output**:
- After N0 writeback: `visited_mask_o[N0] = 1`, `phase_factor_first_o = 1`.
- After N1 writeback: `visited_mask_o[N1] = 1`, all factor nodes visited.
- Within 1 cycle of N1 writeback completion: `phase_switch_pulse_o = 1`, `phase_factor_first_o = 0`.
- `visited_mask_o` clears to all-zeros.

**Pass/Fail**:
- [ ] `phase_factor_first_o` transitions from 1 → 0 after all factor nodes visited.
- [ ] `phase_switch_pulse_o` is a single-cycle pulse.
- [ ] `visited_mask_o` clears on phase switch.
- [ ] Variable nodes can now be scheduled (inject N2, N3 commands and verify compute).

---

### 4.5 Test Case 5: NoC TX Arbitration (3 Channels Simultaneous)

**Objective**: Verify `noc_adapter` correctly arbitrates between NOTIFICATION, FETCH_REQUEST, and FETCH_RESPONSE TX channels when all 3 assert simultaneously.

**Preconditions**:
- SPM initialized with 2 nodes: one local factor (sends NOTIFICATION) and one remote variable (sends FETCH_REQUEST).
- `pull_server` has a pending FETCH_REQUEST to respond to (simulated via internal state or whitebox preload).

**Stimulus**:

| Cycle | Action | Description |
|-------|--------|-------------|
| T+10  | Reset release | |
| T+11  | Trigger NOTIF | Inject compute command for local factor node → writeback sends NOTIFICATION |
| T+12  | Trigger FETCH_REQ | Inject NOTIFICATION for remote edge → fetch subsystem sends FETCH_REQUEST |
| T+13  | Trigger FETCH_RESP | Preload pull_server with a response → pull_server sends FETCH_RESPONSE |
| T+15..T+25 | Observe | Monitor `link_sif_o` for all 3 message types |

**Expected Output**:
- All 3 message types appear on `link_sif_o` within 20 cycles.
- No message dropped or corrupted.
- Arbitration is round-robin (or priority-based per `noc_adapter` design).

**Pass/Fail**:
- [ ] All 3 NOTIFICATION stores sent (correct payload).
- [ ] All 3 FETCH_REQUEST stores sent (correct target coordinates).
- [ ] All FETCH_RESPONSE stores sent (correct data).
- [ ] No TX channel starved for >10 cycles.
- [ ] `tx_busy` asserted while any channel is active.

---

### 4.6 Test Case 6: Reset During Active Fetch

**Objective**: Verify clean abort and recovery when reset is asserted mid-transaction.

**Preconditions**:
- Same as TC2 (remote edge setup).
- FETCH_RESPONSE in progress (2 of 3 data beats received).

**Stimulus**:

| Cycle | Action | Description |
|-------|--------|-------------|
| T+10  | Reset release | |
| T+11  | NOTIF | Inject NOTIFICATION |
| T+15  | FETCH_REQ | Observe FETCH_REQUEST egress |
| T+20  | RESP start | Inject FETCH_RESPONSE meta + data beat 1 |
| T+21  | RESP cont | Inject data beat 2 |
| T+22  | **Reset** | Assert `reset_i=1` mid-response |
| T+25  | Release | `reset_i=0` |
| T+26  | Observe | Check all FSM states |

**Expected Output**:
- At T+22: all TX/RX activity stops immediately.
- At T+25: `tx_fetch_req_valid=0`, `spm_wr_valid_o=0`, all FSMs in IDLE.
- Scoreboard occupancy = 0.
- STAGING partial write is incomplete (acceptable — reset does not guarantee atomic response).
- After T+25: PE can accept new commands (inject fresh command and verify normal operation).

**Pass/Fail**:
- [ ] All FSMs return to IDLE within 1 cycle of reset assertion.
- [ ] Scoreboard cleared.
- [ ] No SPM corruption outside STAGING region.
- [ ] PE recovers and accepts new commands after reset release.

---

## 5. Corner Cases

1. **Scoreboard full**: Pre-load scoreboard to `SCOREBOARD_DEPTH-1` entries, then inject one more NOTIFICATION. Verify new edge is blocked (`rx_notif_ready_o=0`) until a completion frees a slot.
2. **Empty queue (no schedulable nodes)**: SPM with zero nodes. Whitebox command injection is the only way to start compute. Verify PE idles correctly (no spurious TX activity).
3. **Backpressure on all 3 TX channels**: Tie off `link_sif_o` ready to 0. Verify all 3 TX sources stall without deadlock, and resume when ready returns.
4. **Zero state_words node**: Node with `state_words=0`. Verify Pull Server sends immediate done response; Compute Unit skips read and goes straight to compute (if DOF > 0) or done.
5. **Maximum adjacency count (8 edges)**: Node with `MAX_ADJ_COUNT=8` edges, mix of local and remote. Verify Writeback Controller sends 8 NOTIFICATIONs sequentially.
6. **Self-notification (bug check)**: Inject NOTIFICATION targeting this PE's own coordinates. Verify Fetch Subsystem does **not** issue FETCH_REQUEST to self (or Pull Server drops self-requests).

---

## 6. Implementation Roadmap

### Step 1: Top-Level Wrapper (`nocbp_verilator/tops/unit/gbp_pe_top.sv`)

```systemverilog
module gbp_pe_top;
  import gbp_pkg::*;

  logic clk, rst_n;

  // link_sif interface (flattened for Verilator)
  logic [link_sif_width_lp-1:0] link_sif_i, link_sif_o;

  // Whitebox command
  logic wb_cmd_valid;
  logic [NODE_ID_W-1:0] wb_cmd_node_id;
  logic wb_cmd_is_factor;
  // ... etc

  gbp_pe #(
    .x_cord_width_p(6),
    .y_cord_width_p(5),
    // ... other params
  ) dut (
    .clk_i(clk),
    .reset_i(~rst_n),
    .link_sif_i(link_sif_i),
    .link_sif_o(link_sif_o),
    .my_x_i(6'd2),
    .my_y_i(5'd1),
    // ... other ports
    `ifdef GBP_WHITEBOX_TEST
    .wb_cmd_valid_i(wb_cmd_valid),
    .wb_cmd_node_id_i(wb_cmd_node_id),
    // ... etc
    `endif
  );
endmodule
```

### Step 2: C++ Testbench (`nocbp_verilator/tests/unit/gbp_pe.cc`)

Functions to implement:
- `init_spm_backdoor(node_id, node_header, adj_entries, state_words)` — writes SPM via Verilator DPI or direct `spm_bank_array` access.
- `inject_notification(source_node, is_factor, target_x, target_y)` — forms `bsg_manycore_packet_s` and drives `link_sif_i`.
- `inject_fetch_response(txn_id, node_id, consumer, state_words, data_beats[])` — forms MBX_RESP_* stores.
- `read_staging_backdoor(addr, num_words)` — reads STAGING region for verification.
- `whitebox_inject_cmd(node_id, is_factor, dof, adj_count, state_words)` — drives whitebox ports.

### Step 3: Makefile Integration

Add to `nocbp_verilator/Makefile`:
```makefile
UNIT_TESTS += gbp_pe
```

### Step 4: Dependencies

This test depends on:
- All subsystem tests passing (prerequisite).
- `noc_adapter` unit test passing (NoC packet encode/decode).
- `pull_client`, `pull_server`, `response_collector`, `writeback_controller` unit tests passing.

---

## 7. Related Documents

| Document | Content |
|----------|---------|
| `../../01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `../../02_SPM_AND_METADATA.md` | SPM layout, NodeHeader/AdjEntry formats |
| `../../03_NOC_PROTOCOL.md` | NoC packet formats, mailbox addresses, store sequences |
| `../../04_PE_MICROARCHITECTURE.md` | Module hierarchy and internal datapath |
| `../../05_INTERFACES.md` | Port definitions for all subsystems and leaf modules |
| `../../06_PE_CONTROL_FLOW.md` | PE-level control flow and pipeline stages |
| `../subsystem_tests/` | Subsystem integration tests (prerequisite) |
| `../integration_tests/` | End-to-end flow tests (prerequisite) |
