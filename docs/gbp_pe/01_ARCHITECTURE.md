# GBP PE Architecture Overview

## 1. Design Goals

Current PE architecture:

```
pull-based + state-centric + disjoint node ownership + scoreboard-managed outstanding fetches
```

Core rules:

1. Each node is stored on exactly one PE at any time.
2. PE only long-term saves node states it owns.
3. Node state = eta + lambda (compact form), data that can be pulled by other PEs.
4. No persistent remote message copy.
5. When updating a local node:
   - Local neighbor: read STATE directly from this PE's SPM.
   - Remote neighbor: send FETCH_REQUEST to the neighbor's owning PE, wait for FETCH_RESPONSE.
6. After each node update, send NOTIFICATION to all consuming neighbors (lightweight, control-only, no eta/lambda data).
7. A Scoreboard tracks all outstanding fetch requests per-edge. A node is schedulable only when all its edges are ready.
8. Local edges are always ready (direct SPM read). Only remote edges require fetch.

> **PE updates local node by on-demand pulling neighbor state via NoC. The Scoreboard manages outstanding fetches and determines node readiness.**

---

## 2. Overall Data Flow

### 2.1 Producer Side (after node update)

```
PE_A updates node N:

  1. Compute new state for N.
  2. Writeback N's STATE to local SPM.
  3. For each consuming neighbor:
     - Send NOTIFICATION to neighbor's owning PE.
       (lightweight control packet, no eta/lambda payload)
```

### 2.2 Consumer Side (before node update)

```
PE_B wants to update node M (adjacent to N on PE_A):

  1. ScoreboardPrefetcher receives NOTIFICATION from PE_A.
     → Mark edge (M, N) as "notified".
  2. Prefetcher issues FETCH_REQUEST to PE_A (lookahead, before M is scheduled).
     → Mark edge (M, N) as "in_flight".
     → Add entry to Scoreboard.
  3. PE_A receives FETCH_REQUEST, reads N's STATE, sends FETCH_RESPONSE.
  4. PE_B receives FETCH_RESPONSE.
     → Mark edge (M, N) as "ready".
     → Remove entry from Scoreboard.
  5. When ALL edges of M are "ready" → M becomes schedulable.
  6. Scheduler selects M, Compute Unit reads neighbor states, performs update.
  7. After compute: reset remote edges to idle, keep local edges as ready.
```

---

## 3. Explicitly Not Doing (First Version)

1. No persistent remote message copy (no Message region).
2. No Working Buffer.
3. AdjEntry has no flags, no edge_id, no remote state address.
4. Pull response does not persistently write to SPM (feeds directly into compute path).
5. No node replication.
6. No dynamic remapping.
7. No NOTIFY_NODE_UPDATED (deferred to later version).
8. No multiple active node update concurrency per PE.

---

## 4. Key Design Decisions

### 4.1 Notification + Fetch (not raw PullReq/PullResp)

The pull flow uses a two-phase protocol:

| Phase | Packet | Payload | Purpose |
|-------|--------|---------|---------|
| Notify | NOTIFICATION | `{is_factor, source_node_id}` (control-only) | Tell consumer "my state changed". Target edge derived from adjacency info, not in packet. |
| Fetch | FETCH_REQUEST | target_node_id, consumer_node_id | Consumer requests current state |
| Response | FETCH_RESPONSE | eta + lambda data | Producer sends requested state |

Why not raw PullReq/PullResp:
- NOTIFICATION is decoupled from data transfer. Consumer can decide when/whether to fetch.
- ScoreboardPrefetcher can issue fetches **ahead of scheduling** (lookahead), hiding latency.
- Multiple outstanding fetches can be in-flight simultaneously.

### 4.2 Scoreboard = Outstanding Pull Tracker

The ScoreboardPrefetcher serves dual purpose:
1. **Outstanding pull tracker**: tracks in-flight FETCH_REQUESTs, limits concurrency via `scoreboard_cap`.
2. **Node readiness arbiter**: a node is schedulable only when all its edges reach "ready" state.

Per-edge state machine:

```
IDLE ──[notification]──▶ NOTIFIED ──[fetch issued]──▶ IN_FLIGHT ──[response received]──▶ READY
  ↑                                                                                    │
  └──────────────────────[after compute, reset]─────────────────────────────────────────┘
```

Local edges skip directly to READY (no fetch needed).

### 4.3 Single Active Node, Multiple Outstanding Fetches

Each PE updates at most one node at a time. But that node may have multiple outstanding fetch requests (one per remote neighbor). The Scoreboard tracks all of them.

```
PE_B updating node M (3 neighbors: N1 local, N2 remote, N3 remote)

  1. Local read N1 state      → immediate
  2. FETCH_REQ(N2) → PE_C     ──┐
  3. FETCH_REQ(N3) → PE_D     ──┤ parallel, tracked by Scoreboard
                                 │
  4. FETCH_RESP(N2) ◄───────────┤
  5. FETCH_RESP(N3) ◄───────────┘
  6. All edges ready → start compute
```

### 4.4 Phase-Based Scheduling

PE alternates between factor-priority and variable-priority phases:

```
Factor Phase → compute all ready factors → Variable Phase → compute all ready variables → repeat
```

Phase switch: PE switches immediately, no neighbor coordination.

Within each phase, node selection policies:
- **Round-robin** (default): scan from last position, pick first schedulable.
- **Dirty-age**: pick node with oldest dirty cycle.
- **Residual**: pick node with highest residual (information gain).

---

## 5. Related Documents

| Document | Content |
|----------|---------|
| `00_WRITING_GUIDE.md` | How to write architecture documents: structure, granularity, style |
| `02_SPM_AND_METADATA.md` | SPM layout, metadata structures, state block organization, STAGING design |
| `03_NOC_PROTOCOL.md` | NoC adaptation layer, mailbox encoding, manycore store-based messaging |
| `04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters, open items |
| `05_INTERFACES.md` | Port-level interfaces, state machines, timing paths |
| `06_PE_CONTROL_FLOW.md` | PE-level control flow, pipeline stages, module handshakes |
| `verification/README.md` | Verification documentation index and test templates |
