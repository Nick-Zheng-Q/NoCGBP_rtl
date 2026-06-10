# PE Microarchitecture

## 1. PE Internal Modules

The PE is organized as a top-level manycore tile wrapper (`gbp_pe`) that instantiates a NoC adapter plus four subsystem wrappers. Each subsystem wrapper groups related leaf modules and exposes only the signals required for cross-subsystem connectivity.

```
gbp_pe (manycore tile interface)
├── gbp_pe_noc_adapter
│   ├── noc_adapter
│   ├── noc_adapter_tx
│   └── noc_adapter_rx
├── gbp_pe_control_subsystem
│   ├── phase_controller
│   ├── node_scheduler
│   ├── metadata_scanner
│   └── reverse_index_lookup       // NEW: Reverse CSR query for notifications
├── gbp_pe_compute_subsystem
│   ├── compute_unit
│   ├── read_stream_engine
│   │   └── agu
│   └── write_stream_engine
│       └── agu
├── gbp_pe_memory_subsystem
│   ├── spm_arbiter
│   └── spm_bank_array (8 banks)
├── gbp_pe_fetch_subsystem
│   ├── scoreboard_prefetcher
│   ├── pull_client
│   └── response_collector
├── pull_server                    (leaf: serves remote FETCH_REQUESTs)
├── neighbor_state_accumulator     (leaf: straddles compute/control boundary)
└── writeback_controller           (leaf: straddles compute/control boundary)
```

**Subsystem boundaries are fixed:**
- `gbp_pe_control_subsystem` owns the foreground scheduling pipeline.
- `gbp_pe_compute_subsystem` owns descriptor-driven compute datapath.
- `gbp_pe_memory_subsystem` owns all SPM access arbitration and banks.
- `gbp_pe_fetch_subsystem` owns background remote-neighbor fetch lifecycle.
- `neighbor_state_accumulator` and `writeback_controller` remain leaf modules in `gbp_pe` because they straddle subsystem boundaries.

---

## 2. Module Descriptions

### 2.1 Phase Controller

Manages factor/variable phase alternation.

```
State:
  logic priority_factor_first;  // current phase direction
  logic current_priority_had_available_nodes;

Flow:
  1. While current phase has schedulable nodes → keep selecting.
  2. When no more schedulable nodes → switch phase immediately.
  3. Reset phase coverage mask on switch.
```

### 2.2 Node Scheduler

Selects which node to compute within the current phase.

**Inputs** (v2):
- `node_ready_i[NUM_NODES-1:0]`: from ScoreboardPrefetcher
- `visited_mask_i[NUM_NODES-1:0]`: from Phase Controller
- `affected_valid_i`, `affected_local_id_i`: from reverse_index_lookup (one consumer node per cycle)

**State** (v2):
- `pending_queue`: FIFO of local_ids enqueued by reverse_index_lookup. Replaces the unscalable per-node dirty_mask.

**Policies** (hardware-implemented fixed logic):

The scheduler policy is **not runtime-configurable**. It is fixed hardware logic, currently round-robin v1:

| Policy | Logic | Status |
|--------|-------|--------|
| `"rr"` (v1) | Round-robin scan from `next_index`, pick first unvisited node where `node_ready \|\| in_pending_queue` | **Implemented** |
| `"dirty_age"` | Scan window, pick node with oldest `latest_dirty_cycle` | Reserved for future |
| `"dirty_age_cap"` | Default RR, but promote if dirty time > threshold | Reserved for future |
| `"hybrid_rr_da"` | Every N-th pick uses dirty-age, others use RR | Reserved for future |
| `"residual"` | Pick node with highest `var_t_values[]` (PE-local RSM) | Reserved for future |

> **No policy selection port exists**. Changing the scheduling policy requires RTL modification.

**Readiness check**: A node is schedulable when it is **unvisited** AND (**ready** OR **in pending_queue**).

- `ready`: all remote edges fetched (ScoreboardPrefetcher.node_ready = 1)
- `in pending_queue`: reverse_index_lookup has found this node as affected by a NOTIFICATION

When a node is selected, it is dequeued from pending_queue (if present). If the node was not ready, SCAN proceeds and FETCH_REQUESTs are issued; ACCUMULATE will stall until responses arrive. The node remains in `visited_mask` until phase switch.

**No discovery mode**: Removed. Prefetch is triggered by scheduling nodes from the pending_queue.

**No dirty_mask**: The unscalable per-node dirty bitvector is replaced by a lightweight FIFO fed by reverse_index_lookup.

### 2.3 Metadata Scanner

Flow (v2 — uses Forward CSR from SPM):

1. Read current node's `S1NodeHeader` from SPM.
2. Extract `dof`, `adj_count`, `adj_base`, `state_base`, `state_words`.
3. Scan `FwdEdgeArray[adj_base ... adj_base + adj_count - 1]`.
4. For each neighbor, `is_local` is read directly from `fwd_edge_t.is_local` (pre-computed at load time).

```
// No runtime local/remote classification needed
is_local = fwd_edge[i].is_local
```

**Output**: `adj_valid` stream with `neighbor_id`, `neighbor_x`, `neighbor_y`, `is_local`, `edge_slot`.

---

### 2.3b Reverse Index Lookup (NEW)

Handles NOTIFICATION ingress by querying the Reverse CSR to find all affected local nodes.

**Trigger**: `rx_notif_valid` from NoC Adapter RX.

**Flow**:

```
Step 1: RevKeyHash lookup (1-2 cycles)
  hash(rx_notif_source_node_id) -> bucket
  compare key -> rev_id (or miss)

Step 2: RevHeader read (1 cycle)
  Read RevHeader[rev_id] -> rev_base, rev_len

Step 3: Stream RevEntryArray (rev_len cycles)
  For i = 0 .. rev_len-1:
    Read RevEntryArray[rev_base + i]
    Output: affected_valid=1, affected_local_id=entry.local_id
  affected_last=1 on final entry
```

**Output**: Stream of `affected_local_id` values enqueued into Node Scheduler's pending_queue.

**Miss handling**: If RevKeyHash misses, the NOTIFICATION's source node does not affect any local node. No action needed.

**SPM access**: Uses a dedicated SPM read port (client for reverse CSR banks), independent of Forward CSR and Compute Unit ports.

### 2.4 ScoreboardPrefetcher (Outstanding Pull Tracker)

This module serves dual purpose: outstanding fetch tracker and node readiness arbiter.

Coordinates with STAGING Allocator (inside Response Collector) before issuing pull requests. Both must agree: scoreboard has space AND STAGING has reservation.

**Per-edge state fields:**

```
typedef struct packed {
    logic [1:0]            state;         // IDLE(00) → NOTIFIED(01) → IN_FLIGHT(10) → READY(11)
    logic [NODE_ID_W-1:0]  consumer_node; // node that owns this edge
    logic [NODE_ID_W-1:0]  source_node;   // neighbor node ID (target of fetch)
    logic [X_CORD_W-1:0]   source_x;      // NoC x coordinate of producer's PE
    logic [Y_CORD_W-1:0]   source_y;      // NoC y coordinate of producer's PE
    logic                  is_factor;     // phase type from NOTIFICATION
} edge_entry_t;
```

**Per-node readiness:**

```
node_ready = node_has_edge && (node_pending == 0)
```

- `node_has_edge`: set when first adj_valid for this node is registered
- `node_pending`: counts remote edges that are NOTIFIED or IN_FLIGHT; decremented on completion

**Scoreboard:**

```
entries: vector<edge_entry_t>      // edge table, indexed by free_ptr / scan_ptr
capacity: SCOREBOARD_DEPTH         // max concurrent edges tracked
```

**Per-cycle flow:**

```
1. on_adj_valid():
   if node is NOT already ready (dedup guard):
     register edge in edge table
     if local: state = READY
     if remote: state = NOTIFIED
     increment node_pending
2. issue_fetches():
   scan edge table for NOTIFIED edges
   if scoreboard not full and staging available:
     issue FETCH_REQUEST
     edge: NOTIFIED → IN_FLIGHT
     increment scoreboard occupancy
3. on_fetch_complete(txn_id):
   edge: IN_FLIGHT → READY
   decrement scoreboard occupancy
   decrement node_pending
   if node_pending == 0: node_ready = 1
4. reset_edges_after_compute(node_index):
   clear all edges for this node from table
   clear node_has_edge, node_pending
```

**Dedup guard**: When `adj_valid` arrives for a node that is already ready (`node_ready == 1`), the edge registration is skipped. This prevents duplicate entries during the second SCAN (formal compute).

### 2.5 Pull Client

Handles sending FETCH_REQUEST packets.

```
Input:  target_node_id, target_x, target_y, consumer_node_id, is_factor
Action:
  - Send FETCH_REQUEST to (target_x, target_y) via NoC
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
6. NoC Adapter TX sends response automatically:
   - Response FSM inserts metadata store: addr=MBX_RESP_META, payload={is_factor, state_words}
   - Pull Server streams data stores: addr=MBX_RESP_DATA × state_words, payload=word[i]
   - Response FSM inserts done store: addr=MBX_RESP_DONE, payload={txn_id, node_id, consumer_node_id}
```

Pull Server does not understand eta/lambda. Does not compute. Only does state readback from STATE region (not STAGING).

Max fetch requests processed per cycle: 1. Pull Server FSM: IDLE → LOOKUP → SEND_DATA → SEND_DONE. Metadata store is inserted by NoC Adapter TX, not by Pull Server.

### 2.7 Response Collector

Receives FETCH_RESPONSE from NoC Adapter and **writes to STAGING region** of SPM. Uses batched staging with conservative reservation.

```
1. Receive metadata store (MBX_RESP_META): {is_factor, state_words}.
2. Allocate STAGING block: txn_table[txn_id].base = staging_bump; staging_bump += align2(state_words).
3. Receive data stores (MBX_RESP_DATA × state_words): write each word to SPM[base + word_idx].
4. Receive done store (MBX_RESP_DONE): {txn_id, node_id, consumer_node_id}.
5. Notify ScoreboardPrefetcher: assert complete_valid with complete_txn_id, complete_node_id, complete_consumer_node_id.
6. When all responses in batch complete → assert batch_done to trigger STAGING reset.
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

**Architecture**: Stream-based, descriptor-driven SPM access. The compute_unit does not have direct SPM read/write ports. Instead, it uses `read_stream_if` and `write_stream_if` interfaces. Descriptors are sent to stream engines (`read_stream_engine`, `write_stream_engine`), which contain AGUs that translate descriptors into per-beat SPM addresses and drive the SPM Arbiter.

**Data flow**:
```
Accumulator ──[ns_valid/data/last]──▶ Compute Unit (neighbor state stream in)
                                          │
Descriptor ──[read_stream_if]──▶ Read Stream Engine ──[AGU]──▶ SPM Arbiter ──▶ SPM Bank Array
                                          │                              │
                                          └── stream data in ◀───────────┘
                                          │
Compute Unit ──[write_stream_if]──▶ Write Stream Engine ──[AGU]──▶ SPM Arbiter ──▶ SPM Bank Array
```

**Supported operations:**

| Op | Description | Cycle Model |
|---|---|---|
| `MAT_ADD` | matrix/vector addition | `base(2) + ops * (3 + ceil(words/16))` |
| `MAT_SUB` | matrix/vector subtraction | same as MAT_ADD |
| `MAT_MUL` | matrix multiplication | `base(2) + 3 + ceil(m*n*k / 16)` |
| `MAT_VEC_MUL` | matrix-vector multiply | `base(2) + 3 + ceil(dofs² / 16)` |
| `MAT_INV` | matrix inversion (Gauss-Jordan) | `base(2) + (dofs + dofs*(dofs-1)) * (2 + ceil(2*dofs/16))` |
| `FACTOR_MSG_SOLVE` | LDLT + multi-RHS solve | PartialPivLU pipeline |
| `ROBUSTIFY` | robust loss | 2 cycles |
| `RELINEARIZE` | Jacobian update | `base(2) + 3 + ceil(obs_dim*dofs / 16)` |

Note: `LOAD` and `STORE` are not compute operations — they are handled by the stream engines via descriptors. The compute_unit receives data through `read_stream_if` and outputs results through `write_stream_if`.

**Staging buffer**: 128 words (512B) for MAT_INV. Constraint: `2 * dofs² ≤ 128` (dofs ≤ 8).

**Accumulator depth**: 128 words (512B). Supports dof=8 compact form (44 words).

**Batch completion**: Compute Unit asserts `batch_done` when the current batch of neighbor state accumulation and compute completes. This triggers the STAGING Allocator to reset (`staging_bump = staging_base`, `staging_reserved = 0`). `batch_done` is distinct from `done_valid`, which indicates the entire node's compute is complete. In batched staging mode, one node update may produce multiple `batch_done` pulses before the final `done_valid`.

**Variable Node schedule (`schedule_vn`):**

```
Issue read descriptor for prior + msgs → stream in → MAT_ADD accumulate → MAT_INV → MAT_VEC_MUL → stream out belief via write descriptor
```

**Factor Node schedule (`schedule_fn`):**

```
Issue read descriptor → stream in → [ROBUSTIFY] → [RELINEARIZE] → MAT_ADD total →
  per-edge { MAT_SUB cavity → MAT_INV cavity → MAT_MUL msg → stream out msg via write descriptor }
```

### 2.10 Writeback Controller

After compute, sends NOTIFICATION to all consuming neighbors via NoC Adapter.

```
for each consuming neighbor (remote):
  send NOTIFICATION store: addr=MBX_NOTIFICATION, payload={is_factor, source_node_id, target_node_id}
  (lightweight, control-only, no eta/lambda data)
```

### 2.11 SPM Arbiter

SPM clients (8), accessed through `gbp_pe_memory_subsystem`:

1. Metadata Scanner read (META — NodeHeader, AdjEntry) — from `gbp_pe_control_subsystem`
2. reverse_index_lookup read (META — RevKeyHash, RevHeader, RevEntryArray) — from `gbp_pe_control_subsystem`
3. Read Stream Engine read (STATE — current node + local neighbor, via descriptor + AGU) — from `gbp_pe_compute_subsystem`
4. Read Stream Engine read (STAGING — remote neighbor state, via descriptor + AGU) — from `gbp_pe_compute_subsystem`
5. Write Stream Engine write (STATE — compute result writeback, via descriptor + AGU) — from `gbp_pe_compute_subsystem`
6. Pull Server read (STATE — respond to remote pulls) — leaf module in `gbp_pe`
7. Response Collector write (STAGING — write pull response data) — from `gbp_pe_fetch_subsystem`
8. DMA / loader (META + STATE — initialization) — external

Note: Clients 3-5 are driven by stream engines inside `gbp_pe_compute_subsystem`. The compute_unit itself does not directly drive the SPM Arbiter.

First version: round-robin, no priority.

Bank conflict: if multiple requests hit same bank in same cycle, un-granted requests stall / retry.

### 2.12 Subsystem Wrappers

The four subsystem wrappers do not add new algorithms; they only encapsulate leaf-module instantiation and local handshakes.

#### 2.12.1 `gbp_pe_control_subsystem`
Encapsulates: `phase_controller`, `node_scheduler`, `metadata_scanner`, `reverse_index_lookup`.

External interfaces:
- To `gbp_pe_fetch_subsystem`: `node_ready` output, `reset_valid` input.
- To `gbp_pe_compute_subsystem`: `cmd_valid/node_id/is_factor/dof/adj_count/state_words/state_base` output, `cmd_ready` input.
- To `gbp_pe_memory_subsystem`: **Two SPM read ports** — port 0 for Forward CSR (Metadata Scanner), port 1 for Reverse CSR (reverse_index_lookup).
- To `gbp_pe`: `sched_valid`, `no_schedulable_nodes`, `wb_done`.

#### 2.12.2 `gbp_pe_compute_subsystem`
Encapsulates: `compute_unit`, `read_stream_engine`, `write_stream_engine`, and internal `agu` instances.

External interfaces:
- From `gbp_pe_control_subsystem`: compute command (`cmd_valid`, `cmd_node_id`, etc.).
- From `neighbor_state_accumulator`: `ns_valid`, `ns_data`, `ns_last`.
- To `gbp_pe_memory_subsystem`: SPM read/write ports.
- To `gbp_pe`: `done_valid`, `done_node_id`, `done_is_factor`, `batch_done`.

#### 2.12.3 `gbp_pe_memory_subsystem`
Encapsulates: `spm_arbiter`, `spm_bank_array` (8 banks).

External interfaces:
- 7 client SPM read/write ports exposed as vectors.
- Connected to `gbp_pe_control_subsystem`, `gbp_pe_compute_subsystem`, `gbp_pe_fetch_subsystem`, and DMA.

#### 2.12.4 `gbp_pe_fetch_subsystem`
Encapsulates: `scoreboard_prefetcher`, `pull_client`, `response_collector`.

External interfaces:
- From `gbp_pe_control_subsystem`: `adj_valid` stream and `reset_valid`.
- To `gbp_pe_control_subsystem`: `node_ready`.
- To `gbp_pe_noc_adapter`: FETCH_REQUEST TX, FETCH_RESPONSE RX.
- To `gbp_pe_memory_subsystem`: Pull Server read, Response Collector write.
- To `neighbor_state_accumulator`: remote data stream.
- To `gbp_pe_compute_subsystem`: `batch_done` input.

---

## 3. Parameter Classification

### 3.1 RTL Microarchitecture Parameters

```systemverilog
parameter int NUM_PE_X;
parameter int NUM_PE_Y;
parameter int NUM_BANKS         = 8;
parameter int SPM_BYTES_PER_PE  = 1048576;   // 1MB default, override at top level
parameter int WORD_BYTES        = 4;          // 32-bit word. Derived from beat size (8B) / 2 words per beat.
parameter int BEAT_BYTES        = 8;          // 64-bit beat. Bank interleave granularity.
parameter int NOC_DATA_W        = 32;         // NoC store data width
parameter int SCOREBOARD_DEPTH  = 64;         // max outstanding fetches per PE
parameter int FETCH_SERVER_MAX_PER_CYCLE = 1; // max fetch requests served per cycle (sequential FSM: LOOKUP→SEND_DATA→SEND_DONE)
parameter int NUM_NODES_PER_PE  = 1024;       // max nodes assigned to one PE
parameter int MAX_ADJ_COUNT     = 8;          // max adjacency entries per node
```

Derived:

```systemverilog
localparam int NUM_PE          = NUM_PE_X * NUM_PE_Y;
localparam int NODE_ID_W       = $clog2(NUM_NODES_PER_PE); // 10 for 1024 nodes
localparam int X_CORD_W        = $clog2(NUM_PE_X);   // NoC x coordinate width
localparam int Y_CORD_W        = $clog2(NUM_PE_Y);   // NoC y coordinate width
localparam int BANK_ID_W       = $clog2(NUM_BANKS);
localparam int SPM_WORD_ADDR_W = $clog2(SPM_BYTES_PER_PE / WORD_BYTES);
localparam int NUM_NODES       = NUM_NODES_PER_PE;
localparam int NUM_ADJ         = MAX_ADJ_COUNT;
localparam int BEAT_BITS       = BEAT_BYTES * 8;    // SPM data width in bits = 64
localparam int SPM_ADDR_W      = SPM_WORD_ADDR_W;   // word address width
localparam int FP32_W          = 32;                 // FP32 data width
localparam int ROW_ADDR_W      = $clog2(SPM_BYTES_PER_PE / NUM_BANKS / BEAT_BYTES); // bank row address
```

**Parameter consistency check** (for `SPM_BYTES_PER_PE = 1MB`):
- `SPM_WORD_ADDR_W = $clog2(1MB / 4) = 18`
- `ROW_ADDR_W = $clog2(1MB / 8 / 8) = 14`
- Bank mapping: `bank_id = word_addr[3:1]` (3 bits), `row_addr = word_addr[17:4]` (14 bits)
- Total: `1 (ignored) + 3 (bank) + 14 (row) = 18 = SPM_WORD_ADDR_W` ✅

### 3.2 Graph Format / Metadata ABI Parameters

Not decided by RTL alone. Format bitwidths jointly agreed by software graph compiler / loader and RTL:

```systemverilog
parameter int DOF_W;
parameter int DIM_W;           // max dof for NoC payload sizing (e.g., 8)
parameter int ADJ_COUNT_W;
parameter int STATE_WORDS_W = 9;  // ≥ $clog2(max_state_words+1), see 02_SPM_AND_METADATA.md §6.1
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
neighbor_x, neighbor_y
```

---

## 4. Open Items

| # | Item | Status | Notes |
|---|------|--------|-------|
| 1 | SCOREBOARD_DEPTH sizing | Resolved = 64 | Max outstanding fetches per PE |
| 2 | Fetch response store count | Resolved | state_words + 2 stores per response |
| 3 | Accumulator depth | Resolved = 128 words | Supports dof=8 compact form (44 words) |
| 4 | SPM arbiter port count | Resolved = 7 clients, round-robin | MetadataScanner, CU_state_rd, CU_staging_rd, CU_wb, PullServer, RespCollector, DMA |
| 5 | Node lookup method | Resolved | Contiguous id range: local_index = node_id - local_node_base |
| 6 | Bank interleaving | Resolved | First version uses interleave-by-beat (word_addr[3:1]). No hash optimization. |
| 7 | Fetch server throughput | Resolved = 1 request/cycle | Pull Server FSM is sequential (LOOKUP→SEND_DATA→SEND_DONE). Metadata store is inserted by NoC Adapter TX. NoC Adapter mailbox can buffer up to 4 requests, but processing throughput is 1/cycle. |
| 8 | SPM layout | Resolved | META/STATE/STAGING regions. base/limit are loader-configured registers. See 02_SPM_AND_METADATA.md. |

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
