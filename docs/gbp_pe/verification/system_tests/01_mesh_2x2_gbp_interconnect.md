# 2×2 Mesh GBP PE Interconnect System Test

> **Status**: 🟡 **Planned** — document framework complete; awaiting `mesh_2x2_gbp_top.sv` wrapper and C++ testbench implementation.
>
> **Priority**: B (after Direction A single-PE top-level test passes).

## 1. Test Objective

Verify that GBP PEs correctly communicate through a **single-layer manycore mesh**.
This is the highest-level system test before chip-level integration.

Specifically validates:
1. **NoC routing**: GBP messages (NOTIFICATION / FETCH_REQUEST / FETCH_RESPONSE) correctly traverse the mesh between non-adjacent and adjacent PEs.
2. **End-to-end GBP algorithm step**: A complete factor→variable message propagation round across multiple PEs in the mesh.
3. **Concurrent multi-directional traffic**: Multiple PEs send/receive simultaneously without deadlock or data corruption.
4. **32-bit NoC width vs 64-bit SPM beat integrity**: Data that is split into 32-bit NoC stores at the source is correctly reassembled into 64-bit SPM beats at the destination.

## 2. Architecture

### 2.1 Mesh Topology

Single-layer 2×2 mesh. Each tile contains exactly one GBP PE (no vanilla cores in this test).

```
                    North (tie-off)
                         │
    ┌────────────────────┼────────────────────┐
    │                    │                    │
West├─► PE(0,1) ───────► PE(1,1) ◄─────── East
(tie)│    [tile_01]        [tile_11]        (tie)
    │       ▲                ▲                │
    │       │                │                │
    │       ▼                ▼                │
    │    PE(0,0) ───────► PE(1,0)             │
    │    [tile_00]        [tile_10]             │
    │                    ▲                    │
    └────────────────────┼────────────────────┘
                         │
                    South (tie-off)

Mesh node router: bsg_manycore_mesh_node (or simplified equivalent)
Link: bsg_manycore_link_sif (wormhole-like single-flit store packets)
```

**Coordinates:**
| PE | Global X | Global Y | Local Nodes |
|----|----------|----------|-------------|
| PE(0,0) | 0 | 0 | N0 (factor, dof=2) |
| PE(1,0) | 1 | 0 | N1 (variable, dof=2) |
| PE(0,1) | 0 | 1 | N2 (factor, dof=2) |
| PE(1,1) | 1 | 1 | N3 (variable, dof=2) |

**Edges (graph):**
- N0 (factor) → N1 (variable): remote, N1 on PE(1,0)
- N2 (factor) → N3 (variable): remote, N3 on PE(1,1)
- N0 (factor) → N3 (variable): remote, N3 on PE(1,1)  *(diagonal, 2-hop)*

### 2.2 Test Wrapper Structure

```
module mesh_2x2_gbp_top;
  // 4 GBP PE instances
  gbp_pe pe_00 (... link_sif to routers ...);
  gbp_pe pe_10 (...);
  gbp_pe pe_01 (...);
  gbp_pe pe_11 (...);

  // 4 mesh routers (or bsg_manycore_mesh_node)
  bsg_manycore_mesh_node router_00 (...);
  bsg_manycore_mesh_node router_10 (...);
  bsg_manycore_mesh_node router_01 (...);
  bsg_manycore_mesh_node router_11 (...);

  // North/South/West/East tie-offs for boundary routers
  bsg_manycore_link_sif_tieoff tie_north[2](...);
  bsg_manycore_link_sif_tieoff tie_south[2](...);
  bsg_manycore_link_sif_tieoff tie_west [2](...);
  bsg_manycore_link_sif_tieoff tie_east [2](...);
endmodule
```

> **Note**: If `bsg_manycore_mesh_node` is not available in the simulation environment, a simplified XY-routing router module may be used. The test focuses on GBP PE behavior, not router correctness.

### 2.3 Implementation Roadmap

| Milestone | Deliverable | Status | Dependencies |
|-----------|-------------|--------|--------------|
| M1 | Simplified XY-router (`bsg_mesh_router_xy.sv`) | 🟡 Planned | BaseJump STL crossbar |
| M2 | `mesh_2x2_gbp_top.sv` — 4× `gbp_pe` + 4× router + tie-offs | 🟡 Planned | M1, Direction A passing |
| M3 | C++ testbench with DPI/backdoor SPM init | 🟡 Planned | M2 |
| M4 | Golden-reference integration (Direction C) | 🟡 Planned | M3 |

> **Prerequisite**: Direction A (`gbp_pe` single-PE top-level whitebox test) must pass before M2 begins. A single PE must work in isolation before it can work in a mesh.

## 3. Preconditions

### 3.1 Global

- Clock: 1 GHz (1 ns cycle time), all PEs and routers share the same clock.
- Reset: 10 cycles active-high `reset_i` (internal `rst_n` = ~`reset_i`).
- NoC link credits: each router input has sufficient credit buffer (default 4).

### 3.2 Per-PE SPM Initialization

Each PE's SPM is pre-loaded with metadata and state via DMA (simulated via direct memory backdoor in testbench).

**PE(0,0) — Node N0 (factor, node_id=0):**
```
META region:
  NodeHeader[0]: dof=2, adj_count=2, adj_base=0x100, state_base=0x200, state_words=6
  AdjEntry[0]: neighbor_id=1, neighbor_x=1, neighbor_y=0  (→ N1 on PE(1,0))
  AdjEntry[1]: neighbor_id=3, neighbor_x=1, neighbor_y=1  (→ N3 on PE(1,1))

STATE region @ 0x200:
  [0..5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}  (6 words = 3 beats)
```

**PE(1,0) — Node N1 (variable, node_id=1):**
```
META region:
  NodeHeader[1]: dof=2, adj_count=1, adj_base=0x110, state_base=0x300, state_words=6
  AdjEntry[0]: neighbor_id=0, neighbor_x=0, neighbor_y=0  (→ N0 on PE(0,0), local? no — remote)

STATE region @ 0x300:
  [0..5] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f}
```

**PE(0,1) — Node N2 (factor, node_id=2):**
```
META region:
  NodeHeader[2]: dof=2, adj_count=1, adj_base=0x120, state_base=0x400, state_words=6
  AdjEntry[0]: neighbor_id=3, neighbor_x=1, neighbor_y=1  (→ N3 on PE(1,1))

STATE region @ 0x400:
  [0..5] = {7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f}
```

**PE(1,1) — Node N3 (variable, node_id=3):**
```
META region:
  NodeHeader[3]: dof=2, adj_count=2, adj_base=0x130, state_base=0x500, state_words=6
  AdjEntry[0]: neighbor_id=0, neighbor_x=0, neighbor_y=0  (→ N0 on PE(0,0))
  AdjEntry[1]: neighbor_id=2, neighbor_x=0, neighbor_y=1  (→ N2 on PE(0,1))

STATE region @ 0x500:
  [0..5] = {0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f}
```

### 3.3 Phase & Scoreboard

- All PEs start in **FACTOR phase** (`phase_factor_first = 1`).
- All edges initially IDLE.
- All scoreboards empty.

### 3.4 Assumptions & Known Limitations

1. **In-order store delivery**: The Response Collector assumes data stores within a single FETCH_RESPONSE arrive in order. The 2×2 mesh with XY routing and single-flit stores preserves this assumption, but larger meshes with adaptive routing may violate it.
2. **No ruche links / pods**: Only single-layer mesh. No heterogeneous tiles (vanilla core + GBP accelerator).
3. **Simplified router latency**: 1 cycle per hop unless actual `bsg_manycore_mesh_node` is used.
4. **No host interface**: PEs are self-scheduling once SPM is initialized. No kernel launch or DMA during the test.
5. **Deterministic scheduling**: `node_scheduler` uses round-robin; phase switches are independent per PE (no global barrier).

## 4. Test Stimulus

### 4.1 Phase 1: Trigger N0 Compute on PE(0,0)

Directly inject a sideband command (or whitebox trigger) to PE(0,0) to start computing node N0. This simulates the host/kernel launching a GBP iteration.

| Cycle | Action | PE | Description |
|-------|--------|-----|-------------|
| T+10  | `cmd_valid=1, cmd_kind=FACTOR, cmd_node_id=0` | PE(0,0) | Trigger N0 compute |
| T+11  | `cmd_valid=0` | PE(0,0) | Clear command |

### 4.2 Phase 2: N0 Compute → Writeback → Notifications

PE(0,0) computes N0 (factor). When done, Writeback Controller sends NOTIFICATIONs to N0's consumers: N1 (PE(1,0)) and N3 (PE(1,1)).

| Cycle | Expected Event | Source | Destination | Description |
|-------|---------------|--------|-------------|-------------|
| T+10..T+50 | Compute | PE(0,0) | internal | N0 factor compute (OP_MSG_F2V: static read → cavity accumulation → LDLT solve → Schur update → damping → writeback) |
| T+51 | NOTIFICATION store | PE(0,0) | PE(1,0) | `{is_factor=1, source=0, target=1}` |
| T+52 | NOTIFICATION store | PE(0,0) | PE(1,1) | `{is_factor=1, source=0, target=3}` (diagonal, 2-hop) |

### 4.3 Phase 3: Consumers Receive Notifications → Issue Fetches

PE(1,0) and PE(1,1) receive NOTIFICATIONs, mark edges NOTIFIED, and issue FETCH_REQUESTs back to PE(0,0).

| Cycle | Expected Event | Source | Destination | Description |
|-------|---------------|--------|-------------|-------------|
| T+55 | NOTIFICATION rx | PE(1,0) | from PE(0,0) | Edge (N1←N0) marked NOTIFIED |
| T+56 | FETCH_REQUEST (3 stores) | PE(1,0) | PE(0,0) | `{is_factor=1, consumer=1}, {target=0}, {txn_id=0}` |
| T+57 | NOTIFICATION rx | PE(1,1) | from PE(0,0) | Edge (N3←N0) marked NOTIFIED |
| T+58 | FETCH_REQUEST (3 stores) | PE(1,1) | PE(0,0) | `{is_factor=1, consumer=3}, {target=0}, {txn_id=1}` |

### 4.4 Phase 4: Pull Server Responds → Response Collector Writes STAGING

PE(0,0)'s Pull Server receives FETCH_REQUESTs, reads N0's STATE from SPM, and sends FETCH_RESPONSEs.

| Cycle | Expected Event | Source | Destination | Description |
|-------|---------------|--------|-------------|-------------|
| T+60 | FETCH_RESPONSE meta | PE(0,0) | PE(1,0) | `{is_factor=1, state_words=6}` |
| T+61..T+63 | FETCH_RESPONSE data ×3 beats | PE(0,0) | PE(1,0) | 6 words = 3 beats of 64-bit |
| T+64 | FETCH_RESPONSE done | PE(0,0) | PE(1,0) | `{txn_id=0, node_id=0, consumer=1}` |
| T+65 | FETCH_RESPONSE meta | PE(0,0) | PE(1,1) | `{is_factor=1, state_words=6}` |
| T+66..T+68 | FETCH_RESPONSE data ×3 beats | PE(0,0) | PE(1,1) | 6 words = 3 beats |
| T+69 | FETCH_RESPONSE done | PE(0,0) | PE(1,1) | `{txn_id=1, node_id=0, consumer=3}` |

### 4.5 Phase 5: Concurrent N2 Compute on PE(0,1)

While PE(0,0) is still responding to fetches, trigger N2 compute on PE(0,1). This creates concurrent multi-directional traffic.

| Cycle | Action | PE | Description |
|-------|--------|-----|-------------|
| T+40  | `cmd_valid=1, cmd_kind=FACTOR, cmd_node_id=2` | PE(0,1) | Trigger N2 compute |
| T+41  | `cmd_valid=0` | PE(0,1) | Clear command |

**Expected concurrent traffic:**
- PE(0,1) → PE(1,1): NOTIFICATION for N3
- PE(1,1) → PE(0,1): FETCH_REQUEST for N2
- PE(0,1) → PE(1,1): FETCH_RESPONSE for N2

This overlaps with PE(0,0) ↔ PE(1,0) / PE(1,1) traffic, stressing router arbitration and NoC adapter TX/RX.

### 4.6 Phase 6: Consumers Complete → Schedule Variables

After responses arrive, PE(1,0) and PE(1,1) mark edges READY, N1 and N3 become schedulable. Phase switches to VARIABLE phase.

| Cycle | Expected Event | PE | Description |
|-------|---------------|-----|-------------|
| T+70 | `node_ready[1]=1` | PE(1,0) | N1 ready (all edges ready) |
| T+71 | `node_ready[3]=1` | PE(1,1) | N3 ready (all edges ready) |
| T+72 | Phase switch pulse | All PEs | FACTOR → VARIABLE (each PE switches independently) |
| T+73 | `sched_valid=1, node_id=1` | PE(1,0) | N1 scheduled |
| T+74 | `sched_valid=1, node_id=3` | PE(1,1) | N3 scheduled |

## 5. Expected Output

### 5.1 NoC Traffic Summary

| Message Type | Path | Count | Data Integrity Check |
|--------------|------|-------|---------------------|
| NOTIFICATION | PE(0,0) → PE(1,0) | 1 | `{is_factor=1, src=0, tgt=1}` |
| NOTIFICATION | PE(0,0) → PE(1,1) | 1 | `{is_factor=1, src=0, tgt=3}` |
| NOTIFICATION | PE(0,1) → PE(1,1) | 1 | `{is_factor=1, src=2, tgt=3}` |
| FETCH_REQUEST | PE(1,0) → PE(0,0) | 1 (3 stores) | txn_id=0 |
| FETCH_REQUEST | PE(1,1) → PE(0,0) | 1 (3 stores) | txn_id=1 |
| FETCH_REQUEST | PE(1,1) → PE(0,1) | 1 (3 stores) | txn_id=0 (on PE(0,1)) |
| FETCH_RESPONSE | PE(0,0) → PE(1,0) | 1 meta + 3 data + 1 done | 6 words match SPM[0x200] |
| FETCH_RESPONSE | PE(0,0) → PE(1,1) | 1 meta + 3 data + 1 done | 6 words match SPM[0x200] |
| FETCH_RESPONSE | PE(0,1) → PE(1,1) | 1 meta + 3 data + 1 done | 6 words match SPM[0x400] |

### 5.2 SPM Verification (Backdoor Read)

After all traffic completes, read STAGING regions via testbench backdoor:

| PE | STAGING Address | Expected Data | Source |
|----|----------------|---------------|--------|
| PE(1,0) | staging_base + 0..5 | {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f} | N0 STATE via FETCH_RESPONSE |
| PE(1,1) | staging_base + 0..5 | {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f} | N0 STATE via FETCH_RESPONSE |
| PE(1,1) | staging_base + 6..11 | {7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f} | N2 STATE via FETCH_RESPONSE |

### 5.3 Signal-Level Checks

| Cycle | Signal | Expected | PE |
|-------|--------|----------|-----|
| T+55 | `rx_notif_valid_o` | 1 | PE(1,0) |
| T+55 | `rx_notif_source_node_id_o` | 0 | PE(1,0) |
| T+64 | `complete_valid_o` | 1 | PE(1,0) |
| T+64 | `complete_txn_id_o` | 0 | PE(1,0) |
| T+70 | `node_ready[1]` | 1 | PE(1,0) |
| T+71 | `node_ready[3]` | 1 | PE(1,1) |
| T+72 | `phase_factor_first` | 0 (was 1) | All PEs |

## 6. Timing Diagram

```
Cycle:   T+10      T+40      T+50      T+55      T+60      T+64      T+70      T+72      T+74
         │         │         │         │         │         │         │         │         │
PE(0,0)  ├─N0 compute─────────────┤    │NOTIF───┤    │RESP to PE(1,0)───────┤    │         │
         │         │         │    │    │    │NOTIF───┤    │RESP to PE(1,1)───────────┤    │
         │         │         │    │    │    │    │    │    │    │    │    │    │    │    │
PE(1,0)  │         │         │    │    │rxNOTIF  │    │FETCH_REQ      │    │RESP rx  │    │
         │         │         │    │    │    │    │    │    │    │    │    │complete │    │
         │         │         │    │    │    │    │    │    │    │    │    │N1 ready │    │
         │         │         │    │    │    │    │    │    │    │    │    │    │    │    │
PE(0,1)  │    ├─N2 compute─────────────┤    │    │    │NOTIF to PE(1,1)   │    │    │    │
         │         │    │    │    │    │    │    │    │    │    │    │    │    │    │    │
PE(1,1)  │         │    │    │    │    │    │rxNOTIF(0)│    │    │rxRESP(0)│    │    │    │
         │         │    │    │    │    │    │    │    │FETCH_REQ(0)│    │    │    │    │    │
         │         │    │    │    │    │    │    │    │    │    │    │rxNOTIF(2)│    │    │
         │         │    │    │    │    │    │    │    │    │    │    │    │FETCH_REQ(2)│    │
         │         │    │    │    │    │    │    │    │    │    │    │    │    │    │N3 ready│
         │         │    │    │    │    │    │    │    │    │    │    │    │    │    │    │
Phase    │FACTOR   │    │FACTOR │    │    │    │    │    │    │    │    │    │SWITCH │VARIABLE│
```

## 7. Pass/Fail Criteria

- [ ] All 3 NOTIFICATIONs arrive at correct destination PEs with correct payload.
- [ ] All 3 FETCH_REQUESTs arrive at correct source PEs with correct txn_id.
- [ ] All FETCH_RESPONSE data beats match original SPM STATE data (bit-exact FP32).
- [ ] STAGING regions in PE(1,0) and PE(1,1) contain correct data after responses complete.
- [ ] `complete_valid_o` asserted on all consumer PEs with matching `txn_id`.
- [ ] `node_ready` asserted for N1 and N3 after all their edges are READY.
- [ ] Phase switches from FACTOR to VARIABLE on all PEs.
- [ ] No deadlock during concurrent multi-directional traffic.
- [ ] No credit starvation (TX not blocked for >100 cycles).
- [ ] No message corruption (all flits route to correct destination).

## 8. Corner Cases

1. **Simultaneous TX from all 4 PEs**: All PEs attempt to send NOTIFICATION/FETCH_REQUEST at the same cycle. Router arbitration must not drop messages.
2. **Response-to-self**: A FETCH_REQUEST is misrouted back to the requesting PE (should not happen with correct coordinates, but verify Pull Server ignores self-requests).
3. **Credit exhaustion**: Rapid-fire messages from PE(0,0) exhaust router credits. Verify NoC Adapter TX stalls gracefully and resumes when credits return.
4. **Diagonal 2-hop latency**: PE(0,0) → PE(1,1) requires XY routing through (1,0) or (0,1). Verify latency is approximately 2× single-hop.
5. **In-order response assumption**: The Response Collector computes STAGING write addresses by counting received data stores. If the mesh reorders stores, addresses will be corrupted. In the 2×2 mesh with XY routing this does not happen; this corner case documents the **design limitation** rather than a test failure mode. Future work: add sequence numbers or explicit address fields to FETCH_RESPONSE data stores.
6. **Barrier not used**: GBP PEs do not use manycore barrier in this architecture. Verify barrier pins are tied off correctly.

## 9. Simulation Environment

### 9.1 Makefile Target

```makefile
# In nocbp_verilator/Makefile
SYSTEM_TESTS += mesh_2x2_gbp_interconnect

mesh_2x2_gbp_interconnect:
	$(MAKE) LEVEL=system TEST=mesh_2x2_gbp_interconnect
```

### 9.2 File List

```
nocbp_verilator/tests/system/mesh_2x2_gbp_interconnect.f:
  ${BSG_MANYCORE_DIR}/v/bsg_manycore_pkg.sv
  ${BSG_MANYCORE_DIR}/v/bsg_manycore_defines.svh
  ${BSG_MANYCORE_DIR}/v/bsg_manycore_mesh_node.sv
  ${BSG_MANYCORE_DIR}/v/bsg_manycore_link_sif_tieoff.sv
  ${BSG_MANYCORE_DIR}/basejump_stl/bsg_noc/bsg_router_crossbar_o_by_i.sv
  ... (other manycore base modules)
  ${BSG_MANYCORE_DIR}/v/gbp_pe/gbp_pkg.sv
  ${BSG_MANYCORE_DIR}/v/gbp_pe/gbp_pe.sv
  nocbp_verilator/tops/system/mesh_2x2_gbp_top.sv
```

### 9.3 Testbench Notes

- Use DPI or direct memory access to initialize SPM contents before releasing reset.
- Use whitebox probes (`GBP_WHITEBOX_TEST` define) to observe internal signals (`node_ready`, `phase_factor_first`, `scoreboard_occupancy`).
- Router latency: model as 1-cycle per hop for simplified router, or use actual manycore router latency.
- Simulation timeout: 10,000 cycles (sufficient for 2×2 mesh with 6-word state transfers).

## 10. Relationship to Direction C (Golden Reference)

This mesh test validates **functional correctness** and **NoC routing**:
- Messages arrive at the correct destination.
- Data is not corrupted during transfer.
- Deadlock does not occur under concurrent traffic.
- Phase switching and scheduling work across PE boundaries.

It does **not** validate that the **numerical values** computed by the Compute Unit are mathematically correct. That is the scope of Direction C (`02_gbp_algorithm_golden_reference.md`), which runs the same 4-node graph through a floating-point Python reference and compares beliefs against the RTL output.

**Execution order**:
1. Direction A: single-PE top-level test passes.
2. Direction B (this document): mesh functional test passes.
3. Direction C: mesh numerical correctness verified against golden reference.

## 11. Related Documents

| Document | Content |
|----------|---------|
| `../../01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `../../03_NOC_PROTOCOL.md` | NoC adaptation layer, mailbox encoding |
| `../../04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters |
| `../../05_INTERFACES.md` | Port-level interfaces |
| `../../06_PE_CONTROL_FLOW.md` | PE-level control flow |
| `../integration_tests/04_full_pull_cycle.md` | Single-PE pull cycle |
| `../integration_tests/06_multi_node_concurrent.md` | Multi-node on single PE |
| `../subsystem_tests/05_noc_subsystem.md` | NoC subsystem standalone test |
