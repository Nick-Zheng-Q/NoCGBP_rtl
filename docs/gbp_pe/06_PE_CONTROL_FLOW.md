# PE Control Flow

## 1. Overview

PE operation splits into two concurrent domains:

```
┌─────────────────────────────────────────────────────────┐
│                    Background Domain                     │
│  (runs continuously, independent of node scheduling)     │
│                                                         │
│  • NoC RX: receive NOTIFICATION / FETCH_REQUEST / RESP  │
│  • ScoreboardPrefetcher: issue FETCH_REQUEST (lookahead) │
│  • Pull Server: serve remote FETCH_REQUESTs             │
│  • Response Collector: write STAGING, track completions  │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                    Foreground Domain                     │
│  (one node update at a time, sequential pipeline)       │
│                                                         │
│  IDLE → SCHEDULE → SCAN → ACCUMULATE → COMPUTE → NOTIFY │
└─────────────────────────────────────────────────────────┘
```

Background domain never blocks on foreground. Foreground blocks on background only when waiting for outstanding fetches to complete.

---

## 2. Background Domain

### 2.1 Concurrent Processes

These processes run every cycle, gated only by their own handshake signals:

```
Process 1: Notification Ingress
  NoC Adapter RX → ScoreboardPrefetcher.rx_notif_*
  Frequency: whenever a NOTIFICATION store arrives
  Latency: 1 cycle to latch, 1 cycle to mark edge NOTIFIED

Process 2: Lookahead Fetch Issue
  ScoreboardPrefetcher.fetch_req_* → Pull Client → NoC Adapter TX
  Frequency: up to 1 fetch per cycle (pipelined)
  Condition: scoreboard not full AND staging reservation available

Process 3: Fetch Response Reception
  NoC Adapter RX → Response Collector → SPM STAGING write
  Frequency: whenever a FETCH_RESPONSE arrives
  Latency: state_words + 2 cycles per response

Process 4: Pull Server
  NoC Adapter RX (FETCH_REQUEST) → Pull Server → SPM STATE read → NoC Adapter TX (FETCH_RESPONSE)
  Frequency: up to FETCH_SERVER_MAX_PER_CYCLE per cycle
  Latency: 1 (lookup) + state_words (read) + state_words + 2 (send)

Process 5: Fetch Completion
  Response Collector.complete_valid → ScoreboardPrefetcher.complete_*
  Frequency: once per completed fetch response
```

### 2.2 Background Domain Stall Conditions

Background processes only stall on:
- **NoC TX backpressure**: `out_credit_or_ready_o == 0` → Pull Client and Pull Server stall
- **SPM bank conflict**: arbiter denies grant → retry next cycle
- **Scoreboard full**: no new FETCH_REQUEST issued
- **STAGING reservation exhausted**: no new pull requests until batch compute frees space

---

## 3. Foreground Domain: Node Update Pipeline

### 3.1 Pipeline Stages

```
         ┌────────┐   ┌────────┐   ┌───────────┐   ┌──────────┐   ┌─────────┐   ┌────────┐
         │ SCHEDULE│──▶│  SCAN  │──▶│ ACCUMULATE│──▶│ COMPUTE  │──▶│ WRITEBACK│──▶│ (next) │
         │        │   │        │   │           │   │          │   │         │   │        │
         │ select │   │ read   │   │ pull local│   │ run      │   │ send    │   │ back to│
         │ node   │   │ header │   │ + fetch   │   │ schedule │   │ notif   │   │ SCHEDULE│
         │        │   │ + adj  │   │ remote    │   │          │   │ + reset │   │        │
         └────────┘   └────────┘   └───────────┘   └──────────┘   └─────────┘   └────────┘
           1 cycle    1+N cycles    variable        variable       1+M cycles
                       (N=adj_count) (wait for       (depends on   (M=remote adj_count)
                                      responses)      schedule)
```

### 3.2 Stage 1: SCHEDULE

**Trigger**: Previous node's writeback completes, or PE startup.

**Actor**: Phase Controller + Node Scheduler.

**Flow**:

```
cycle S+0:
  Phase Controller checks current phase (factor or variable).
  Node Scheduler scans node_ready vector for current phase.
  If a ready node exists:
    sched_valid = 1
    sched_node_id = selected node
    sched_is_factor = current phase type
    → advance to SCAN
  If no ready node:
    no_schedulable_nodes = 1
    Phase Controller switches phase (1 cycle)
    → retry SCHEDULE in new phase
```

**Blocking**: If no node is ready in either phase, PE stalls in SCHEDULE until a background fetch completes and makes a node ready.

**Key signal**: `node_ready[i]` from ScoreboardPrefetcher = `AND(all edge_ready bits for node i)`.

### 3.3 Stage 2: SCAN

**Trigger**: `sched_valid` from Node Scheduler.

**Actor**: Metadata Scanner.

**Flow**:

```
cycle C+0:
  Metadata Scanner receives cmd_valid, cmd_node_id, cmd_is_factor.
  Issue SPM read: NodeHeader at header_addr.
  cmd_ready = 1 (accept command).

cycle C+1:
  SPM returns NodeHeader data.
  Latch: dof, adj_count, adj_base, state_base, state_words.
  Begin AdjEntry scan: issue SPM read for AdjEntry[0].

cycle C+2 .. C+1+adj_count:
  For each AdjEntry[i]:
    SPM returns {neighbor_id, neighbor_x, neighbor_y}.
    Classify: is_local = (neighbor_x == self_x) && (neighbor_y == self_y).
    Output adj_valid stream to ScoreboardPrefetcher and Accumulator.

cycle C+2+adj_count:
  adj_last asserted for final AdjEntry.
  info_valid asserted with node metadata.
  → advance to ACCUMULATE
```

**SPM access pattern**: 1 read for NodeHeader + `adj_count` reads for AdjEntries. Total = `1 + adj_count` cycles (assuming no bank conflicts).

**Bank conflict mitigation**: NodeHeader and AdjEntry list should be placed in different banks by the graph compiler (adj_base should not alias header bank).

**Local edge READY initialization**: During SCAN, when Metadata Scanner classifies an AdjEntry as local (`(neighbor_x == self_x) && (neighbor_y == self_y)`), it asserts `adj_valid` with `adj_is_local = 1` and `adj_edge_idx` to the ScoreboardPrefetcher. The ScoreboardPrefetcher immediately sets that edge to READY state (bypasses the IDLE → NOTIFIED → IN_FLIGHT → READY lifecycle). This ensures local edges are always ready and never require a fetch request.

### 3.4 Stage 3: ACCUMULATE

**Trigger**: SCAN completes (`adj_last` asserted).

**Actor**: Neighbor State Accumulator + ScoreboardPrefetcher + Response Collector (background).

**Flow**:

This stage has two sub-phases depending on neighbor locality:

**Sub-phase 3a: Local neighbor reads**

```
For each local neighbor (is_local == true), in adjacency order:
  Issue SPM read from neighbor's state_base.
  Data flows directly into accumulator.
  Latency: 1 cycle per read (pipelined, no stall if no bank conflict).
  Uses single SPM read port (shared with Compute Unit, time-multiplexed).
```

**Sub-phase 3b: Remote neighbor fetch (already in progress)**

```
For each remote neighbor (is_local == false), in adjacency order:
  ScoreboardPrefetcher already issued FETCH_REQUEST during background domain.
  Response Collector already wrote response to STAGING.
  Edge state should be READY by the time we reach ACCUMULATE.
  Read STAGING blocks sequentially into accumulator.
  Latency: 1 cycle per read (pipelined from STAGING).

Local reads use SPM Arbiter client CU_state_rd; remote reads use CU_staging_rd.
These are independent ports and can issue in the same cycle (no bank conflict permitting).
Within each sub-phase, reads are sequential (1 per cycle).
```

**Stall condition**: If any remote edge is NOT READY at this point, the node should not have been scheduled. This is a design invariant — the Node Scheduler only selects nodes where `node_ready == 1`.

**Batch boundary**: If the node uses batched staging (remote edges exceed STAGING capacity), ACCUMULATE processes one batch at a time:

```
Batch control ownership:
  STAGING Allocator (inside Response Collector) owns batch boundaries.
  Metadata Scanner drives the scan loop; STAGING Allocator gates pull issuance.
  ScoreboardPrefetcher is the pull issuance initiator, gated by STAGING Allocator.

Batch flow:
  1. Metadata Scanner scans AdjEntry list sequentially, asserting adj_valid.
  2. For each remote edge, ScoreboardPrefetcher requests staging_reserve_valid.
  3. STAGING Allocator grants staging_reserve_ready if space available.
  4. ScoreboardPrefetcher issues FETCH_REQUEST (pull).
  5. When STAGING is full (staging_batch_closed), ScoreboardPrefetcher deasserts adj_ready.
  6. Metadata Scanner stalls (adj_valid held, no new AdjEntry read) until adj_ready reasserted.
  7. Response Collector receives all batch responses, writes to STAGING.
  8. Compute Unit reads STAGING blocks, updates partial accumulator.
  9. Compute Unit asserts batch_done → STAGING Allocator resets (1 cycle: staging_bump = staging_base, staging_reserved = 0).
  10. ScoreboardPrefetcher reasserts adj_ready → Metadata Scanner resumes scan.
  11. Repeat until all edges processed.

After all batches: accumulator holds full neighbor state.
→ advance to COMPUTE
```

**Key signal**: `accumulator_done` = all neighbor states consumed.

### 3.5 Stage 4: COMPUTE

**Trigger**: ACCUMULATE completes (`accumulator_done`).

**Actor**: Compute Unit.

**Command trigger flow**:

```
ACCUMULATE completes at cycle A:
  A+0: accumulator_done = 1
  A+0: Metadata Scanner asserts info_valid (latched at SCAN time):
       info_dof, info_adj_count, info_state_base, info_state_words
  A+0: Node Scheduler passes cmd_valid, cmd_node_id, cmd_is_factor to Compute Unit
  A+1: Compute Unit asserts cmd_ready (accepts command)
  A+1: Compute Unit issues read descriptors to stream engine (begin compute)
```

The Compute Unit receives its command from two sources:
- **Node Scheduler**: provides `cmd_valid`, `cmd_node_id`, `cmd_is_factor`
- **Metadata Scanner**: provides `cmd_dof`, `cmd_adj_count`, `cmd_state_words` (via info_* ports)

Command merge rule: `cmd_valid` to Compute Unit is asserted when **both** `sched_valid` (from Node Scheduler) and `info_valid` (from Metadata Scanner, latched at SCAN time) are available. The Node Scheduler output is buffered in a 1-entry register during SCAN; on `accumulator_done`, the buffered `cmd_node_id` and `cmd_is_factor` are presented together with the latched `info_dof`, `info_adj_count`, `info_state_words`.

Both must be valid simultaneously when `accumulator_done` fires.

Note: The compute_unit does not receive `state_base` as a direct SPM address. It issues read/write descriptors to stream engines, which contain AGUs that generate SPM addresses.

**Flow**:

```
Variable node (schedule_vn):
  Issue read descriptor for prior + msgs → stream in via rd_beat_valid/rd_beat_data
  MAT_ADD accumulate (partial accumulator from batches, if any)
  MAT_INV (uses internal staging buffer, 128 words)
  MAT_VEC_MUL
  Issue write descriptor for belief → stream out via wr_word_valid/wr_word_data

Factor node (schedule_fn):
  Issue read descriptor → stream in via rd_beat_valid/rd_beat_data (per-edge messages in STATE)
  [ROBUSTIFY] (if applicable)
  [RELINEARIZE] (if applicable)
  MAT_ADD total
  For each edge:
    MAT_SUB cavity (reads from STATE)
    MAT_INV cavity
    MAT_MUL msg
    STORE msg to SPM STATE
```

**Completion**: Compute Unit asserts `done_valid` with `done_node_id` and `done_is_factor`.

**Latency**: Depends on schedule. See `04_PE_MICROARCHITECTURE.md` Section 2.9 cycle models.

### 3.6 Stage 5: WRITEBACK

**Trigger**: Compute Unit `done_valid`.

**Actor**: Writeback Controller + ScoreboardPrefetcher.

**Flow**:

```
cycle W+0:
  Writeback Controller receives done_valid.
  Latch adjacency info from register file (saved during SCAN stage):
    adj_count, adj_neighbor_ids[], adj_neighbor_xs[], adj_neighbor_ys[], adj_is_local[]

cycle W+1 .. W+M:
  For each remote consuming neighbor:
    Issue NOTIFICATION store via NoC Adapter TX.
    Latency: 1 cycle per notification (pipelined, gated by tx_notif_ready).

cycle W+1+M:
  All notifications sent.
  Assert wb_done.
  Trigger ScoreboardPrefetcher.reset_valid for this node.
    → All remote edges reset to IDLE.
    → Local edges remain READY.
  → advance to SCHEDULE (next node)
```

**Non-consuming neighbors**: Only neighbors that consume this node's output receive NOTIFICATION. This is the subset of adjacency entries where this node is a producer. The exact subset is determined by the graph structure (factor nodes produce to variable neighbors, variable nodes produce to factor neighbors).

---

## 4. Complete Node Update Example

### 4.1 Variable Node (3 neighbors: 1 local, 2 remote)

```
Background (before scheduling):
  T+0:   NOTIFICATION from PE_C for edge(M, N2) → NOTIFIED
  T+1:   ScoreboardPrefetcher issues FETCH_REQ(N2) → PE_C → IN_FLIGHT
  T+2:   NOTIFICATION from PE_D for edge(M, N3) → NOTIFIED
  T+3:   ScoreboardPrefetcher issues FETCH_REQ(N3) → PE_D → IN_FLIGHT
  T+10:  FETCH_RESPONSE(N2) arrives → STAGING write → READY
  T+15:  FETCH_RESPONSE(N3) arrives → STAGING write → READY
  T+15:  node_ready[M] = 1 (all edges ready)

Foreground (node M selected):
  T+16: SCHEDULE: sched_valid=1, sched_node_id=M
  T+17: SCAN: read NodeHeader for M
  T+18: SCAN: read AdjEntry[0] (N1, local)
  T+19: SCAN: read AdjEntry[1] (N2, remote, PE_C)
  T+20: SCAN: read AdjEntry[2] (N3, remote, PE_D) → adj_last
  T+21: ACCUMULATE: read N1 state from SPM STATE (local)
  T+22: ACCUMULATE: read N2 state from STAGING (remote, already written)
  T+23: ACCUMULATE: read N3 state from STAGING (remote, already written) → accumulator_done
  T+24: COMPUTE: LOAD prior
  T+25: COMPUTE: MAT_ADD accumulate
  T+26: COMPUTE: MAT_INV
  T+27: COMPUTE: MAT_VEC_MUL
  T+28: COMPUTE: STORE belief → done_valid
  T+29: WRITEBACK: send NOTIFICATION to consumers of M
  T+30: WRITEBACK: reset edges for M → wb_done
  T+31: SCHEDULE: next node...
```

### 4.2 Factor Node (2 variable neighbors, both remote, batched)

```
Background:
  T+0..T+5: Both edges notified and fetched, responses in STAGING.

Foreground:
  T+6:  SCHEDULE: sched_valid=1, sched_node_id=F
  T+7:  SCAN: read NodeHeader
  T+8:  SCAN: read AdjEntry[0] (V1, remote)
  T+9:  SCAN: read AdjEntry[1] (V2, remote) → adj_last
  T+10: ACCUMULATE batch 1: read V1 state from STAGING
  T+11: ACCUMULATE batch 1: partial accumulator update
  T+12: ACCUMULATE batch 2: read V2 state from STAGING
  T+13: ACCUMULATE batch 2: final accumulator → accumulator_done
  T+14: COMPUTE: LOAD
  T+15: COMPUTE: MAT_ADD total
  T+16: COMPUTE: edge 0: MAT_SUB cavity
  T+17: COMPUTE: edge 0: MAT_INV cavity
  T+18: COMPUTE: edge 0: MAT_MUL msg
  T+19: COMPUTE: edge 0: STORE msg
  T+20: COMPUTE: edge 1: MAT_SUB cavity
  T+21: COMPUTE: edge 1: MAT_INV cavity
  T+22: COMPUTE: edge 1: MAT_MUL msg
  T+23: COMPUTE: edge 1: STORE msg → done_valid
  T+24: WRITEBACK: send NOTIFICATION to V1, V2
  T+25: WRITEBACK: reset edges → wb_done
```

---

## 5. Module Handshake Summary

### 5.1 Foreground Pipeline Handshakes

```
Phase Controller ──[phase_factor_first, visited_mask]──▶ Node Scheduler
Node Scheduler ──[sched_valid, sched_node_id, sched_is_factor]──▶ Metadata Scanner (cmd_* inputs)
Metadata Scanner ──[cmd_ready]──▶ Node Scheduler (sched_ready input)
Metadata Scanner ──[adj_valid, adj_neighbor_*, adj_is_local, adj_last, adj_edge_idx]──▶ ScoreboardPrefetcher
ScoreboardPrefetcher ──[adj_ready]──▶ Metadata Scanner (backpressure)
Metadata Scanner ──[info_valid, info_dof, info_adj_count, info_state_words]──▶ Compute Unit (cmd_* inputs)
Metadata Scanner ──[info_state_base]──▶ PE controller / stream engine (for descriptor base_addr, not direct compute_unit input)
Accumulator ──[out_valid, out_data, out_last, accumulator_done]──▶ Compute Unit / PE controller
Compute Unit ──[done_valid, done_node_id, done_is_factor]──▶ Writeback Controller
Compute Unit ──[batch_done]──▶ Response Collector
Writeback Controller ──[wb_done]──▶ Phase Controller
Writeback Controller ──[reset_valid, reset_node_id]──▶ ScoreboardPrefetcher
Response Collector ──[remote_valid, remote_data, remote_last]──▶ Accumulator (remote path)
PE controller ──[local_valid, local_data, local_last]──▶ Accumulator (local path, reads STATE via SPM Arbiter)
```

### 5.2 Background ↔ Foreground Interactions

```
Background → Foreground:
  ScoreboardPrefetcher.node_ready[i] → Node Scheduler (readiness check)
  Response Collector.complete_valid/txn_id/node_id → ScoreboardPrefetcher (edge state update)

Foreground → Background:
  Writeback Controller.reset_valid → ScoreboardPrefetcher (edge reset)
  Metadata Scanner.adj_valid → ScoreboardPrefetcher (edge classification)

Shared resources (arbitrated by SPM Arbiter):
  Metadata Scanner ──[rd]──▶ SPM Arbiter ──[rd]──▶ SPM Bank Array
  Compute Unit ──[rd/wr]──▶ SPM Arbiter ──[rd/wr]──▶ SPM Bank Array
  Pull Server ──[rd]──▶ SPM Arbiter ──[rd]──▶ SPM Bank Array
  Response Collector ──[wr]──▶ SPM Arbiter ──[wr]──▶ SPM Bank Array
```

---

## 6. PE Top-Level FSM

```
                 ┌──────────┐
           ┌────▶│  SCHEDULE│◀──────────────────────────────┐
           │     └────┬─────┘                               │
           │          │ sched_valid                          │
           │          ▼                                      │
           │     ┌──────────┐                               │
           │     │   SCAN   │                               │
           │     └────┬─────┘                               │
           │          │ adj_last                             │
           │          ▼                                      │
           │     ┌──────────┐                               │
           │     │ACCUMULATE│                               │
           │     └────┬─────┘                               │
           │          │ accumulator_done                     │
           │          ▼                                      │
           │     ┌──────────┐                               │
           │     │ COMPUTE  │                               │
           │     └────┬─────┘                               │
           │          │ done_valid                           │
           │          ▼                                      │
           │     ┌──────────┐                               │
           └─────│WRITEBACK │───────────────────────────────┘
                 └──────────┘  wb_done → back to SCHEDULE
```

---

## 7. Timing Constraints

### 7.1 SCHEDULE → SCAN Transition

```
SCHEDULE asserts sched_valid at cycle S:
  S+0: sched_valid = 1, sched_node_id valid
  S+1: Metadata Scanner asserts cmd_ready (1 cycle to accept)
  S+2: First SPM read issued (NodeHeader)
```

Metadata Scanner must accept the command within 1 cycle. No backpressure expected (scanner is always ready after previous scan completes).

### 7.2 SCAN → ACCUMULATE Transition

```
SCAN completes at cycle C (adj_last asserted):
  C+0: adj_last = 1, info_valid = 1
  C+1: Accumulator begins local reads
  C+1: ScoreboardPrefetcher begins STAGING reads for remote edges
```

No gap between SCAN and ACCUMULATE. The first local neighbor read and first STAGING read can issue in the same cycle (different SPM ports).

### 7.3 ACCUMULATE → COMPUTE Transition

```
ACCUMULATE completes at cycle A (accumulator_done):
  A+0: accumulator_done = 1
  A+1: Compute Unit begins LOAD phase
```

No gap. Compute Unit starts immediately.

### 7.4 COMPUTE → WRITEBACK Transition

```
COMPUTE completes at cycle E (done_valid):
  E+0: done_valid = 1
  E+1: Writeback Controller latches adjacency info
  E+2: First NOTIFICATION sent (if tx_notif_ready)
```

1 cycle latch delay between compute done and first notification.

### 7.5 WRITEBACK → SCHEDULE Transition

```
WRITEBACK completes at cycle W (wb_done):
  W+0: wb_done = 1
  W+1: ScoreboardPrefetcher edge reset takes effect
  W+1: Node Scheduler can select next node
```

1 cycle reset propagation. Next SCHEDULE can begin at W+1.

---

## 8. Open Items

All items resolved. See decisions below:

| # | Item | Decision |
|---|------|----------|
| 1 | Batch boundary during ACCUMULATE | 1 cycle: reset staging_bump + staging_reserved registers |
| 2 | SCAN adjacency save for WRITEBACK | Register file (MAX_ADJ_COUNT entries), latched during SCAN, consumed during WRITEBACK |
| 3 | Concurrent Pull Server + foreground SPM access | Round-robin. Foreground may stall but acceptable (Pull Server throughput = 1/cycle) |
| 4 | Factor node per-edge compute + STAGING interaction | Factor per-edge messages live in STATE, not STAGING. Factor compute reads/writes STATE directly. |
| 5 | Phase switch latency | No overlap with WRITEBACK. 1-cycle switch, keep simple. |
| 6 | Multiple consuming neighbors notification ordering | Round-robin. Order does not matter. |
| 7 | ACCUMULATE local vs remote interleaving | Sequential: local neighbors first, then remote. Single SPM read port, no overlap. |

---

## 9. Related Documents

| Document | Content |
|----------|---------|
| `00_WRITING_GUIDE.md` | How to write architecture documents: structure, granularity, style |
| `01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `02_SPM_AND_METADATA.md` | SPM layout, metadata structures, state block organization, STAGING design |
| `03_NOC_PROTOCOL.md` | NoC adaptation layer, mailbox encoding, manycore store-based messaging |
| `04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters, open items |
| `05_INTERFACES.md` | Port-level interfaces, state machines, timing paths |
| `verification/README.md` | Verification documentation index and test templates |
