# PE Control Flow

## 1. Overview

PE operation splits into two concurrent domains:

```
┌─────────────────────────────────────────────────────────┐
│                    Background Domain                     │
│  (runs continuously, independent of node scheduling)     │
│                                                         │
│  • NoC RX: receive NOTIFICATION / FETCH_REQUEST / RESP  │
│  • ScoreboardPrefetcher: track fetch state, node ready  │
│  • Pull Client: send FETCH_REQUESTs                     │
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

**Key design change (v2):**

1. **SPM stores both Forward CSR and Reverse CSR.** Forward CSR (`S1NodeHeader` + `FwdEdgeArray`) is used by Metadata Scanner during compute. Reverse CSR (`RevKeyHash` + `RevHeader` + `RevEntryArray`) is used by `reverse_index_lookup` during NOTIFICATION processing.

2. **`NOTIFICATION` triggers Reverse CSR query.** `reverse_index_lookup` receives `NOTIFICATION(source_node_id)`, queries the Reverse CSR, and streams affected `local_id`s into Node Scheduler's `pending_queue`.

3. **Node Scheduler uses a FIFO (not dirty_mask).** The unscalable per-node dirty bitvector is replaced by a lightweight FIFO fed by `reverse_index_lookup`.

4. **ScoreboardPrefetcher issues FETCH_REQUEST on `adj_valid`.** During SCAN, each remote edge's `adj_valid` immediately marks the edge NOTIFIED and triggers FETCH_REQUEST issuance. No separate NOTIFICATION → edge-table path is needed.

**Two-SCAN model:** Because adjacency is not cached between prefetch and formal compute, each node may be SCANNed twice:
- **First SCAN** (prefetch): scheduler selects a node from pending_queue → SCAN reads Forward CSR → `adj_valid` triggers ScoreboardPrefetcher to issue FETCH_REQUESTs. ACCUMULATE stalls if remote data is not yet ready.
- **Phase switch** clears `visited_mask`, allowing re-selection.
- **Second SCAN** (formal): scheduler re-selects the now-ready node → SCAN reads Forward CSR again → local_reader and accumulator consume the data. ScoreboardPrefetcher deduplicates (ignores `adj_valid` for already-ready nodes).

---

## 2. Background Domain

### 2.1 Concurrent Processes

These processes run every cycle, gated only by their own handshake signals:

```
Process 1: Notification Ingress → Reverse CSR Query
  NoC Adapter RX → reverse_index_lookup
  Step 1: hash(rx_notif_source_node_id) -> RevKeyHash -> rev_id
  Step 2: read RevHeader[rev_id] -> rev_base, rev_len
  Step 3: stream RevEntryArray[rev_base : rev_base+rev_len-1]
  Step 4: enqueue each affected_local_id into Node Scheduler pending_queue
  Latency: 2 + rev_len cycles (pipelined, one notification at a time)

Process 2: Fetch Issue (triggered by SCAN adj_valid)

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
  Node Scheduler scans for first unvisited node where (node_ready || in_pending_queue) is true.
  If a schedulable node exists:
    sched_valid = 1
    sched_node_id = selected node
    sched_is_factor = current phase type
    dequeue selected_node from pending_queue (if present)
    → advance to SCAN
  If no schedulable node:
    no_schedulable_nodes = 1
    Phase Controller switches phase (1 cycle)
    → retry SCHEDULE in new phase (visited_mask cleared)
```

**Scheduling policy**: A node is schedulable if it is **unvisited** AND (**ready** OR **in pending_queue**):
- `ready`: all remote edges have been fetched (ScoreboardPrefetcher.node_ready = 1)
- `in pending_queue`: reverse_index_lookup has found this node as affected by a NOTIFICATION

**Two-SCAN consequence**: If a node from pending_queue is selected but not yet ready, SCAN proceeds and FETCH_REQUESTs are issued. ACCUMULATE will naturally stall until responses arrive. The node remains in `visited_mask`. After phase switch clears `visited_mask`, the now-ready node will be re-selected for the formal compute SCAN.

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
    SPM returns {neighbor_id, neighbor_x, neighbor_y, neighbor_dof, is_local}.
    is_local is read directly from fwd_edge_t.is_local (pre-computed at load time).
    Output adj_valid stream to ScoreboardPrefetcher and local_neighbor_state_reader.
    For remote edges: ScoreboardPrefetcher immediately marks edge NOTIFIED and
      issues FETCH_REQUEST (if scoreboard has space).
    For local edges: ScoreboardPrefetcher immediately marks edge READY.
    Factor node control FSM accumulates msg_offset[i] and msg_words[i] from neighbor_dof.

cycle C+2+adj_count:
  adj_last asserted for final AdjEntry.
  info_valid asserted with node metadata.
  → advance to ACCUMULATE
```

**SPM access pattern**: 1 read for NodeHeader + `adj_count` reads for AdjEntries. Total = `1 + adj_count` cycles (assuming no bank conflicts).

**Bank conflict mitigation**: NodeHeader and AdjEntry list should be placed in different banks by the graph compiler (adj_base should not alias header bank).

**Local edge READY initialization**: During SCAN, Metadata Scanner reads `is_local` directly from `fwd_edge_t.is_local` (pre-computed at load time) and asserts `adj_valid` with `adj_is_local = 1` for local edges. The ScoreboardPrefetcher immediately sets that edge to READY state. Local edges never require a fetch request.

**Remote edge FETCH on SCAN**: During SCAN, each remote AdjEntry causes `adj_valid` to be asserted. The ScoreboardPrefetcher:
1. Registers the edge (unless the node is already ready — dedup guard)
2. Immediately marks it NOTIFIED
3. The fetch-scan loop finds NOTIFIED edges and issues FETCH_REQUESTs

This means FETCH_REQUESTs are issued **during SCAN**, not in a separate background phase. The first SCAN of a node (prefetch) issues all its FETCH_REQUESTs. The second SCAN (formal) sees the node is already ready and skips edge registration.

### 3.4 Stage 3: ACCUMULATE

**Trigger**: SCAN completes (`adj_last` asserted) and local_reader finishes.

**Actor**: Neighbor State Accumulator + local_neighbor_state_reader + Response Collector.

**Flow**:

This stage has two sub-phases depending on neighbor locality:

**Sub-phase 3a: Local neighbor reads**

```
For each local neighbor (is_local == true), in adjacency order:
  Issue SPM read from neighbor's state_base.
  Data flows directly into accumulator.
  Latency: 1 cycle per read (pipelined, no stall if no bank conflict).
```

**Sub-phase 3b: Remote neighbor reads from STAGING**

```
For each remote neighbor (is_local == false), in adjacency order:
  Read STAGING blocks sequentially into accumulator.
  Latency: 1 cycle per read (pipelined from STAGING).

If FETCH_RESPONSE has not yet arrived for a remote edge:
  accumulator.remote_valid = 0
  accumulator.out_valid = 0
  Compute Unit sees ns_valid = 0 and stalls
  → entire pipeline stalls in ACCUMULATE until response arrives
```

**Stall condition**: Unlike the old design (which required `node_ready == 1` before scheduling), the new design allows scheduling dirty nodes before all fetches complete. ACCUMULATE naturally stalls if remote data is not yet in STAGING. This stall is harmless — the Metadata Scanner is already idle (SCAN completed), so the Node Scheduler can proceed to select the next node for prefetch SCAN while the current node waits for its fetches.

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
  // Step 1: Read factor's own STATE (per-edge old messages + measurement + Jacobian)
  Issue read descriptor → stream in via rd_beat_valid/rd_beat_data
    - First adj_count segments: per-edge old messages (each sized by msg_words[i])
    - Next segment: measurement
    - Final segment: Jacobian

  // Step 2: Accumulate incoming neighbor beliefs (via accumulator)
  [ROBUSTIFY] (if applicable)
  [RELINEARIZE] (if applicable)
  MAT_ADD total (build joint belief from measurement + Jacobian + incoming messages)

  // Step 3: Per-edge message computation
  For each edge i (0 .. adj_count-1):
    // Extract cavity = joint - variable_i's contribution
    MAT_SUB cavity
    MAT_INV cavity
    MAT_MUL msg (Schur complement to extract message for variable i)
    DAMPING: msg_new = (1-damping)*msg_new + damping*msg_old[i]
    // Writeback
    STORE msg_new to SPM STATE at msg_addr[i] (factor's own per-edge message slot)
    // Notification sent by Writeback Controller after all edges complete
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

**First SCAN (prefetch — node M is dirty but not ready):**

```
T+0:   NOTIFICATION from PE_C (source=node M) → reverse_index_lookup → affected_local_id=M → enqueue M into pending_queue
T+1:   NOTIFICATION from PE_D (source=node M) → reverse_index_lookup → M already in queue
T+2:   SCHEDULE: Node Scheduler selects M (in pending_queue && unvisited)
T+3:   SCAN: read NodeHeader for M
T+4:   SCAN: read AdjEntry[0] (N1, local) → ScoreboardPrefetcher marks READY
T+5:   SCAN: read AdjEntry[1] (N2, remote, PE_C) → ScoreboardPrefetcher marks NOTIFIED,
       issues FETCH_REQ(N2) → PE_C → IN_FLIGHT
T+6:   SCAN: read AdjEntry[2] (N3, remote, PE_D) → ScoreboardPrefetcher marks NOTIFIED,
       issues FETCH_REQ(N3) → PE_D → IN_FLIGHT
T+7:   adj_last → local_reader reads N1 state → cmd_valid → ACCUMULATE starts
T+8:   ACCUMULATE: read N1 state (local) → accumulator_done for local
T+9:   ACCUMULATE: read N2 from STAGING — NOT READY yet (FETCH_RESPONSE not arrived)
       → pipeline stalls

(While stalled, Node Scheduler can prefetch other nodes:
  T+8:   SCHEDULE: select next node from pending_queue
  T+9:   SCAN: node K adjacency → issue FETCH_REQs for K)

T+15:  FETCH_RESPONSE(N2) arrives → STAGING write → edge(N2) READY
T+16:  FETCH_RESPONSE(N3) arrives → STAGING write → edge(N3) READY
T+16:  node_ready[M] = 1 (all edges ready)
T+16:  ACCUMULATE resumes: read N2 state from STAGING
T+17:  ACCUMULATE: read N3 state from STAGING → accumulator_done
T+18:  COMPUTE: LOAD prior
...    (compute continues)
T+23:  WRITEBACK: send NOTIFICATION to consumers of M
T+24:  WRITEBACK: reset edges for M → wb_done
```

**Second SCAN (formal — after phase switch clears visited_mask):**

```
T+50:  Phase switch → visited_mask cleared
T+51:  SCHEDULE: Node Scheduler selects M again (now ready)
T+52:   SCAN: read NodeHeader for M
T+53:   SCAN: read AdjEntry[0] (N1, local)
T+54:   SCAN: read AdjEntry[1] (N2, remote) → ScoreboardPrefetcher sees M already ready,
        deduplicates (ignores adj_valid)
T+55:   SCAN: read AdjEntry[2] (N3, remote) → deduplicated
T+56:   ACCUMULATE: read N1 state (local)
T+57:   ACCUMULATE: read N2 from STAGING (ready)
T+58:   ACCUMULATE: read N3 from STAGING (ready) → accumulator_done
T+59:   COMPUTE → WRITEBACK → done
```

Note: In practice, if the first prefetch SCAN completes before all fetches return, the node will stall in ACCUMULATE. When phase switch eventually clears `visited_mask`, the now-ready node is re-selected. The second SCAN re-reads adjacency from SPM (no cache) but ScoreboardPrefetcher ignores duplicate edge registration.

### 4.2 Factor Node (2 variable neighbors, both remote, batched)

```
T+0:  NOTIFICATION from PE_A (source=node F) → reverse_index_lookup → affected_local_id=F → enqueue F into pending_queue
T+1:  NOTIFICATION from PE_B (source=node F) → reverse_index_lookup → F already in queue
T+2:  SCHEDULE: select F (in pending_queue && unvisited)
T+3:  SCAN: read NodeHeader
T+4:  SCAN: read AdjEntry[0] (V1, remote) → ScoreboardPrefetcher marks NOTIFIED,
      issues FETCH_REQ(V1)
T+5:  SCAN: read AdjEntry[1] (V2, remote) → ScoreboardPrefetcher marks NOTIFIED,
      issues FETCH_REQ(V2)
T+6:  adj_last → cmd_valid → ACCUMULATE starts
T+7:  ACCUMULATE batch 1: read V1 from STAGING — may stall if response not ready
      (While stalled, prefetch other nodes)
T+20: FETCH_RESPONSE(V1) arrives → ACCUMULATE resumes
T+21: ACCUMULATE batch 1: partial accumulator update
T+22: ACCUMULATE batch 2: read V2 from STAGING — may stall
T+35: FETCH_RESPONSE(V2) arrives → ACCUMULATE resumes
T+36: ACCUMULATE batch 2: final accumulator → accumulator_done
T+37: COMPUTE → WRITEBACK → NOTIFICATION to V1, V2 → wb_done

Phase switch clears visited_mask. If F was ready by then, second SCAN proceeds
with deduplication (no redundant FETCH_REQUESTs).
```

---

## 5. Module Handshake Summary

### 5.1 Foreground Pipeline Handshakes

```
NoC Adapter RX ──[rx_notif_valid]──▶ reverse_index_lookup
NoC Adapter RX ──[affected_valid, affected_local_id]──▶ Node Scheduler (pending_queue enqueue)
Phase Controller ──[phase_factor_first, visited_mask]──▶ Node Scheduler
Node Scheduler ──[sched_valid, sched_node_id, sched_is_factor]──▶ Metadata Scanner (cmd_* inputs)
Metadata Scanner ──[cmd_ready]──▶ Node Scheduler (sched_ready input)
Metadata Scanner ──[adj_valid, adj_neighbor_*, adj_neighbor_dof, adj_is_local, adj_last, adj_edge_idx, adj_current_node_id]──▶ ScoreboardPrefetcher / Fetch Subsystem
ScoreboardPrefetcher ──[adj_ready]──▶ Metadata Scanner (backpressure)
Metadata Scanner ──[info_valid, info_dof, info_adj_count, info_state_words, info_state_base]──▶ Compute Unit / PE controller (cmd_* inputs)
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
  Metadata Scanner.adj_valid → ScoreboardPrefetcher (edge registration + FETCH trigger)

Key change: adj_valid from Metadata Scanner (during SCAN) now directly triggers
FETCH_REQUEST issuance in ScoreboardPrefetcher. No separate NOTIFICATION →
ScoreboardPrefetcher path is required for fetch initiation.

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
