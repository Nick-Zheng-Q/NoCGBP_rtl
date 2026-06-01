# PE Microarchitecture

## 1. PE Internal Modules

```
GBP PE
├── Phase Controller (priority scheduling)
├── Node Scheduler (node selection within phase)
├── Metadata Scanner
├── ScoreboardPrefetcher (outstanding pull tracker)
├── Pull Client (fetch request sender)
├── Pull Server (fetch response sender)
├── Response Collector (fetch response receiver)
├── Neighbor State Accumulator
├── Compute Unit (PEComputeEngine)
├── Writeback Controller
├── SPM Arbiter
└── NoC Adapter (wraps bsg_manycore_endpoint_standard)
```

---

## 2. Module Descriptions

### 2.1 Phase Controller

Manages factor/variable phase alternation.

```
State:
  priority_factor_first: bool  (current phase direction)
  current_priority_had_available_nodes: bool

Flow:
  1. While current phase has schedulable nodes → keep selecting.
  2. When no more schedulable nodes → switch phase immediately.
  3. Reset phase coverage mask on switch.
```

### 2.2 Node Scheduler

Selects which node to compute within the current phase.

Policies (configurable at runtime):

| Policy | Logic |
|--------|-------|
| `"rr"` (default) | Round-robin scan from `next_index`, pick first node where all edges are ready |
| `"dirty_age"` | Scan window, pick node with oldest `latest_dirty_cycle` |
| `"dirty_age_cap"` | Default RR, but promote if dirty time > threshold |
| `"hybrid_rr_da"` | Every N-th pick uses dirty-age, others use RR |
| `"residual"` | Pick node with highest `var_t_values[]` (PE-local RSM) |

**Readiness check**: a node is schedulable when `ScoreboardPrefetcher.is_node_ready()` returns true (all edges ready).

### 2.3 Metadata Scanner

Flow:

1. Read current node's NodeHeader.
2. Extract `dof`, `adj_count`, `adj_base`, `state_base`, `state_words`.
3. Scan `AdjEntry[0 ... adj_count-1]`.
4. For each neighbor, classify local vs remote.

```
is_local = (neighbor_pe_id == self_pe_id)
```

### 2.4 ScoreboardPrefetcher (Outstanding Pull Tracker)

This module serves dual purpose: outstanding fetch tracker and node readiness arbiter.

Coordinates with STAGING Allocator (inside Response Collector) before issuing pull requests. Both must agree: scoreboard has space AND STAGING has reservation.

**Per-edge state fields:**

```
notification: bool   // NOTIFICATION received from producer
in_flight:    bool   // FETCH_REQUEST sent, waiting for response
ready:        bool   // FETCH_RESPONSE received (or local edge)
source_pe:    int    // PE owning the producer node
edge_index:   int    // per-node edge index, used as txn_id
```

**Per-node readiness:**

```
node_ready = AND(all edge_ready bits for this node)
```

**Scoreboard:**

```
entries: vector<ScoreboardEntry>   // in-flight fetches
capacity: SCOREBOARD_DEPTH         // max concurrent fetches
```

**Per-cycle flow:**

```
1. issue_lookahead_fetches():
   for each node with notification edges:
     for each notified edge:
       if scoreboard not full and edge not already in_flight:
         issue FETCH_REQUEST
         edge: notification=false, in_flight=true
         add to scoreboard
2. on_fetch_response(producer_id, consumer_id):
   find matching scoreboard entry
   edge: ready=true, in_flight=false
   remove from scoreboard
   update node readiness
3. reset_edges_after_compute(node_index):
   for each remote edge: ready=false, in_flight=false, notification=false
   for each local edge: ready=true (always)
   remove node's entries from scoreboard
```

### 2.5 Pull Client

Handles sending FETCH_REQUEST packets.

```
Input:  target_node_id, target_pe_id, consumer_node_id, is_factor
Action:
  - Send FETCH_REQUEST to target_pe_id via NoC
  - Track in ScoreboardPrefetcher
```

### 2.6 Pull Server

Responds to FETCH_REQUEST from other PEs. **Reads STATE** from SPM, sends response via NoC Adapter.

```
1. Receive FETCH_REQUEST from NoC Adapter (decoded from MBX_FETCH_REQ_0/1/2 stores).
2. Latch txn_id from FETCH_REQUEST (store 3).
3. Lookup local NodeHeader by target_node_id.
4. Read state_base and state_words.
5. Read state words from STATE region.
6. Send response via NoC Adapter:
   - Metadata store: addr=MBX_RESP_META, payload={is_factor, state_words}
   - Data stores: addr=MBX_RESP_DATA × state_words, payload=word[i]
   - Done store: addr=MBX_RESP_DONE, payload={txn_id, node_id, consumer_node_id}
```

Pull Server does not understand eta/lambda. Does not compute. Only does state readback from STATE region (not STAGING).

Max fetch requests processed per cycle: 4.

### 2.7 Response Collector

Receives FETCH_RESPONSE from NoC Adapter and **writes to STAGING region** of SPM. Uses batched staging with conservative reservation.

```
1. Receive metadata store (MBX_RESP_META): {is_factor, state_words}.
2. Allocate STAGING block: txn_table[txn_id].base = staging_bump; staging_bump += align2(state_words).
3. Receive data stores (MBX_RESP_DATA × state_words): write each word to SPM[base + word_idx].
4. Receive done store (MBX_RESP_DONE): {node_id, consumer_node_id}.
5. Notify ScoreboardPrefetcher: complete_valid(txn_id).
6. When all responses in batch complete → trigger compute.
7. After batch compute: reset staging_bump to staging_base.
```

STAGING allocation happens when PULL_RESP_HDR arrives (not when pull request is issued). Space is reserved conservatively at request time.

### 2.8 Neighbor State Accumulator

All neighbor states enter the same input path:

```
local STATE read ----\
                     -> neighbor_state_stream -> accumulator -> compute
remote response -----/
```

Compute Unit does not care whether data comes from local or remote.

### 2.9 Compute Unit (PEComputeEngine)

Handles current node update. Coarse-grain operation scheduler with 16-lane FP32 SIMD model.

**Supported operations:**

| Op | Description | Cycle Model |
|---|---|---|
| `LOAD` | SPM → compute | 0 (overlapped) |
| `STORE` | compute → SPM | 0 (overlapped) |
| `MAT_ADD` | matrix/vector addition | `base(2) + ops * (3 + ceil(words/16))` |
| `MAT_SUB` | matrix/vector subtraction | same as MAT_ADD |
| `MAT_MUL` | matrix multiplication | `base(2) + 3 + ceil(m*n*k / 16)` |
| `MAT_VEC_MUL` | matrix-vector multiply | `base(2) + 3 + ceil(dofs² / 16)` |
| `MAT_INV` | matrix inversion (Gauss-Jordan) | `base(2) + (dofs + dofs*(dofs-1)) * (2 + ceil(2*dofs/16))` |
| `FACTOR_MSG_SOLVE` | LDLT + multi-RHS solve | PartialPivLU pipeline |
| `ROBUSTIFY` | robust loss | 2 cycles |
| `RELINEARIZE` | Jacobian update | `base(2) + 3 + ceil(obs_dim*dofs / 16)` |

**Staging buffer**: 128 words (512B) for MAT_INV. Constraint: `2 * dofs² ≤ 128` (dofs ≤ 8).

**Variable Node schedule (`schedule_vn`):**

```
LOAD prior + msgs → MAT_ADD accumulate → MAT_INV → MAT_VEC_MUL → STORE belief
```

**Factor Node schedule (`schedule_fn`):**

```
LOAD → [ROBUSTIFY] → [RELINEARIZE] → MAT_ADD total →
  per-edge { MAT_SUB cavity → MAT_INV cavity → MAT_MUL msg → STORE msg }
```

### 2.10 Writeback Controller

After compute, sends NOTIFICATION to all consuming neighbors via NoC Adapter.

```
for each consuming neighbor (remote):
  send NOTIFICATION store: addr=MBX_NOTIFICATION, payload={is_factor, source_node_id}
  (lightweight, control-only, no eta/lambda data)
```

### 2.11 SPM Arbiter

SPM clients (7):

1. Metadata Scanner read (META)
2. Compute Unit STATE read (STATE — current node + local neighbor)
3. Compute Unit staging read (STAGING — remote neighbor state)
4. Compute Unit writeback (STATE)
5. Pull Server read (STATE — respond to remote pulls)
6. Response Collector write (STAGING — write pull response data)
7. DMA / loader (META + STATE — initialization)

First version: round-robin, no priority.

Bank conflict: if multiple requests hit same bank in same cycle, un-granted requests stall / retry.

---

## 3. Parameter Classification

### 3.1 RTL Microarchitecture Parameters

```systemverilog
parameter int NUM_PE_X;
parameter int NUM_PE_Y;
parameter int NUM_BANKS;
parameter int SPM_BYTES_PER_PE;
parameter int WORD_BYTES;
parameter int NOC_DATA_W;
parameter int SCOREBOARD_DEPTH;    // max outstanding fetches per PE
parameter int FETCH_SERVER_MAX_PER_CYCLE; // max fetch requests served per cycle
parameter int NUM_NODES_PER_PE;   // max nodes assigned to one PE
parameter int MAX_ADJ_COUNT;      // max adjacency entries per node
```

Derived:

```systemverilog
localparam int NUM_PE          = NUM_PE_X * NUM_PE_Y;
localparam int PE_ID_W         = $clog2(NUM_PE);
localparam int BANK_ID_W       = $clog2(NUM_BANKS);
localparam int SPM_WORD_ADDR_W = $clog2(SPM_BYTES_PER_PE / WORD_BYTES);
localparam int NUM_NODES       = NUM_NODES_PER_PE;
localparam int NUM_ADJ         = MAX_ADJ_COUNT;
localparam int BEAT_BITS       = WORD_BYTES * 8;    // SPM data width in bits
localparam int SPM_ADDR_W      = SPM_WORD_ADDR_W;   // alias for interface clarity
localparam int FP32_W          = 32;                 // FP32 data width
localparam int ROW_ADDR_W      = $clog2(SPM_BYTES_PER_PE / NUM_BANKS / WORD_BYTES); // bank row address
```

### 3.2 Graph Format / Metadata ABI Parameters

Not decided by RTL alone. Format bitwidths jointly agreed by software graph compiler / loader and RTL:

```systemverilog
parameter int NODE_ID_W;
parameter int DOF_W;
parameter int DIM_W;           // max dof for NoC payload sizing (e.g., 6)
parameter int ADJ_COUNT_W;
parameter int STATE_WORDS_W;
```

### 3.3 Runtime Metadata Values

Stored per-node at runtime, not RTL parameters:

```
dof
adj_count
adj_base
state_base
state_words
neighbor_id
neighbor_pe_id
```

---

## 4. Open Items

| # | Item | Status | Notes |
|---|------|--------|-------|
| 1 | SCOREBOARD_DEPTH sizing | Open | Should equal max_adj_count? Or larger for multi-node lookahead? |
| 2 | Fetch response store count | Open | state_words + 2 stores per response; latency vs bandwidth tradeoff |
| 3 | Accumulator depth | Open | Must be >= max_state_words (dof=6: 42 words for compact) |
| 4 | SPM arbiter port count | Open | 2R+2W sufficient? May need 3R+2W |
| 5 | Node lookup method | Open | Contiguous id range vs lookup table |
| 6 | Bank interleaving | Open | Unified address space, verify access distribution |
| 7 | Fetch server throughput | Open | 4 requests/cycle from GBPSim; may need tuning |
| 8 | SPM layout | Deferred | Separate design effort |

---

## 5. Related Documents

| Document | Content |
|----------|---------|
| `00_WRITING_GUIDE.md` | How to write architecture documents: structure, granularity, style |
| `01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `02_SPM_AND_METADATA.md` | SPM layout, metadata structures, state block organization, STAGING design |
| `03_NOC_PROTOCOL.md` | NoC adaptation layer, mailbox encoding, manycore store-based messaging |
| `05_INTERFACES.md` | Port-level interfaces, state machines, timing paths |
| `06_PE_CONTROL_FLOW.md` | PE-level control flow, pipeline stages, module handshakes |
| `verification/README.md` | Verification documentation index and test templates |
