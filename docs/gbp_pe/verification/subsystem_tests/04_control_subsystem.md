# Control Subsystem Integration Test

## 1. Test Objective

Verify that the Control Subsystem correctly:
- Alternates factor/variable phases via Phase Controller
- Selects ready nodes via Node Scheduler
- Scans node metadata via Metadata Scanner
- Propagates commands through the control pipeline
- Handles phase switching when no schedulable nodes remain

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Control Subsystem                        │
│                                                              │
│   ┌─────────────────┐                                        │
│   │ Phase Controller│──phase_factor_first──┐                 │
│   │                 │──visited_mask────────┤                 │
│   │                 │◀──no_schedulable_nodes│                │
│   │                 │◀──wb_done─────────────┘                │
│   └─────────────────┘                                        │
│            │                                                 │
│            ▼                                                 │
│   ┌─────────────────┐    sched_valid    ┌─────────────────┐ │
│   │  Node Scheduler │ ────────────────▶ │ Metadata Scanner│ │
│   │                 │◀──sched_ready     │                 │ │
│   │                 │◀──node_ready      │                 │ │
│   └─────────────────┘                   │                 │ │
│                                         │──info_valid────┐ │
│                                         │──info_dof      │ │
│                                         │──info_adj_count│ │
│                                         │──info_state_*  │ │
│                                         └─────────────────┘ │
│                                                              │
│   ScoreboardPrefetcher.node_ready ─────────────────────────▶│
│   Writeback Controller.wb_done ────────────────────────────▶│
└─────────────────────────────────────────────────────────────┘
```

**Modules under test:** `phase_controller`, `node_scheduler`, `metadata_scanner`

**Mocked modules:** `scoreboard_prefetcher` (node_ready vector), `writeback_controller` (wb_done), `compute_unit_wrapper` (cmd_ready)

## 3. Preconditions

- Clock: 100MHz
- Reset: Active low
- Phase Controller in FACTOR phase initially
- ScoreboardPrefetcher has some nodes ready
- SPM contains valid NodeHeaders

## 4. Test Stimulus

### ✅ 4.1 Test Case 1: Command Production (One Node) — IMPLEMENTED

**Scenario**: One ready node, full SCHEDULE → SCAN → CMD → ADJ cycle.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | node_ready[0x10] | 1 | Node 16 ready |
| T+2   | sched_valid | 1 | Scheduler selects node 16 |
| T+3   | cmd_valid_o | 1 | Command to compute subsystem |
| T+3   | cmd_node_id_o | 0x10 | Correct node ID |
| T+4   | adj_valid_o | 1 | First adjacency entry |
| T+5   | adj_last_o | 1 | Last adjacency entry |

> **Implementation note:** Current test auto-generates `wb_done` pulses in a loop until all 1024 nodes are visited, triggering phase switch. SPM read and metadata parsing are mocked via fake SPM in the top-level wrapper.

### ✅ 4.2 Test Case 2: Backpressure — IMPLEMENTED

**Scenario**: `cmd_ready_i = 0` stalls the control pipeline.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | node_ready[0x10] | 1 | Node ready |
| T+2   | cmd_ready_i | 0 | Compute not ready |
| T+3   | cmd_valid_o | 1 | Command held valid |
| T+4   | cmd_ready_i | 1 | Ready now |
| T+4   | cmd_valid_o | 0 | Command accepted |

### ✅ 4.3 Test Case 3: Phase Switch — IMPLEMENTED

**Scenario**: All 1024 nodes visited, phase toggles factor ↔ variable.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | node_ready | all 1s | All nodes ready |
| ...   | Auto wb_done loop | | Generate 1024 wb_done pulses |
| T+N   | phase_factor_first_o | toggles | Phase switched |
| T+N+1 | visited_mask_o | all 0 | Mask cleared |

### ❌ 4.4 Test Case 4: Multi-Edge Sequence — NOT YET IMPLEMENTED

**Scenario**: Node with 3 adjacency entries, verify all 3 emitted sequentially.

**Expected**: `adj_valid_o` asserts 3 times with incrementing `adj_edge_idx`.

### ❌ 4.5 Test Case 5: Local State Reader — NOT YET IMPLEMENTED

**Scenario**: Local adjacency triggers SPM read for local state.

**Expected**: `local_valid_o` asserts with local state data from SPM.

> **Blocker:** Local reader shares SPM port with metadata_scanner, causing contention in testbench. Need careful mock SPM timing.

### ❌ 4.6 Test Case 6: Factor/Variable Priority — NOT YET IMPLEMENTED

**Scenario**: Both factor and variable nodes ready simultaneously.

**Expected**: `phase_factor_first_o` determines which type is scheduled first.

### ❌ 4.7 Test Case 7: Empty Queue Idle — NOT YET IMPLEMENTED

**Scenario**: No nodes ready in either phase.

**Expected**: `sched_valid_o` remains 0, no spurious outputs.

## 5. Expected Output

### 5.1 Test Case 1: Full Pipeline

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+2   | sched_valid | 1 | Scheduler selects node |
| T+2   | sched_node_id | 0x10 | Node 16 |
| T+2   | sched_is_factor | 1 | Factor phase |
| T+3   | cmd_valid | 1 | Command to Compute Unit |
| T+3   | info_valid | 1 | Metadata valid |
| T+5   | adj_valid | 1 | First AdjEntry |
| T+6   | adj_valid | 1 | Second AdjEntry |
| T+8   | visited_mask[0x10] | 1 | Node marked visited |

### 5.2 Test Case 2: Phase Switch

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+2   | no_schedulable_nodes | 1 | No nodes in current phase |
| T+3   | phase_switch_pulse | 1 | Single-cycle pulse |
| T+4   | phase_factor_first | 0 | Switched to variable |
| T+4   | visited_mask | all 0 | Mask cleared |

### 5.3 Test Case 3: Multiple Nodes

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+2   | sched_valid | 1 | First node scheduled |
| T+2   | sched_node_id | 0x10 | Round-robin picks lowest |
| T+N+1 | sched_valid | 1 | Second node |
| T+N+1 | sched_node_id | 0x20 | Next ready node |
| T+M+1 | sched_valid | 1 | Third node |
| T+M+1 | sched_node_id | 0x30 | Last ready node |
| T+M+2 | no_schedulable_nodes | 1 | Phase empty, trigger switch |

## 6. Timing Diagram

```
Full Pipeline:
           ___     ___     ___     ___     ___     ___     ___     ___
node_rdy  ___|        |________________________________________________
              ________
sched     ___|        |________________________________________________
                      ________    ________    ________
adj       ___________|   A0   |__|   A1   |__|   A2   |________________
                                                              ________
wb_done   ___________________________________________________|        |
                                                                      ________
visited   ___________________________________________________________|  0x10  |
```

## 7. Pass/Fail Criteria

- [ ] `sched_valid` asserted only when `node_ready && !visited`
- [ ] `sched_node_id` increments in round-robin within phase
- [ ] Metadata Scanner starts within 1 cycle of `sched_valid`
- [ ] `adj_valid` produced for each AdjEntry
- [ ] `adj_last` on final AdjEntry
- [ ] `phase_switch_pulse` when no schedulable nodes
- [ ] `visited_mask` cleared on phase switch
- [ ] `phase_factor_first` toggles on each switch
- [ ] Pipeline stalls correctly when downstream not ready

## 8. Corner Cases

1. **No nodes ready in any phase**: Idle, continuous phase toggling
2. **All nodes visited**: Phase switch even if nodes ready (prevented by visited_mask)
3. **Node becomes ready during compute**: Not scheduled until next phase
4. **Reset during scan**: Clean abort, FSM returns to IDLE
5. **Metadata Scanner stalls**: Node Scheduler holds `sched_valid` until `sched_ready`
6. **Zero adjacency**: Node with `adj_count = 0`, immediate `adj_last`

## 9. Related Documents

| Document | Content |
|----------|---------|
| `../../04_PE_MICROARCHITECTURE.md` §2.1-2.3 | Phase Controller, Node Scheduler, Metadata Scanner |
| `../../05_INTERFACES.md` §2.1-2.3 | Port definitions |
| `../../06_PE_CONTROL_FLOW.md` §3 | Foreground pipeline stages |
| `../unit_tests/01_phase_controller.md` | Phase Controller unit test |
| `../unit_tests/02_node_scheduler.md` | Node Scheduler unit test |
| `../unit_tests/03_metadata_scanner.md` | Metadata Scanner unit test |
