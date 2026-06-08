# SPM and Metadata Design

## 1. SPM Logical Layout

SPM is physically banked memory (8 banks). Logically it has three regions:

```
SPM
├── META region
│   ├── NodeHeader array
│   └── AdjEntry array
│
├── STATE region
│   └── owner-node variable-length state blocks
│
└── STAGING region
    └── pull response temporary blocks
        lifetime = one fetch/compute batch
        not a cache (no tag/hit/miss/replacement/coherence)
```

Region base/limit are loader-configured registers, exclusive upper bound:

```
meta_base,    meta_limit      // [meta_base, meta_limit)
state_base,   state_limit     // [state_base, state_limit)
staging_base, staging_limit   // [staging_base, staging_limit)
```

All base/limit values use **word address** and must be 8B-aligned (`word_addr[0] = 0`).

---

## 2. Bank Mapping

SPM uses interleave-by-beat. Beat size = 8 bytes (64 bits).

Since metadata uses word address and 8B-aligned:

```
beat_addr = word_addr >> 1       // 8B = 2 words
bank_id   = beat_addr[2:0]       // = word_addr[3:1]
row_addr  = beat_addr >> 3
```

Or equivalently from byte address:

```
byte_addr = word_addr << 2
beat_addr = byte_addr >> 3
bank_id   = beat_addr[2:0]       // = byte_addr[5:3]
row_addr  = beat_addr >> 3
```

RTL (direct from word address):

```systemverilog
bank_id = word_addr[3:1];
row_addr = word_addr[WORD_ADDR_W-1:4];
```

Every 8 consecutive beats (64 bytes) cycle through all 8 banks. No combinational hash logic needed.

---

## 3. Metadata Structure

### 3.1 NodeHeader

Each local node has one header:

```systemverilog
typedef struct packed {
    logic [NODE_ID_W-1:0]        node_id;
    logic [DOF_W-1:0]            dof;
    logic [ADJ_COUNT_W-1:0]      adj_count;
    logic [SPM_WORD_ADDR_W-1:0]  adj_base;       // start of AdjEntry list (word addr)
    logic [SPM_WORD_ADDR_W-1:0]  state_base;     // start of STATE block (word addr)
    logic [STATE_WORDS_W-1:0]    state_words;    // word count of STATE block
} node_header_t;
```

| Field | Meaning |
|-------|---------|
| `node_id` | Unique node ID (factor_id or variable_id) |
| `dof` | Mathematical dimension (e.g., 3 for SE2 landmark, 6 for camera) |
| `adj_count` | Number of adjacent nodes (also used as degree for compute) |
| `adj_base` | Start word address of adjacency list in META region |
| `state_base` | Start word address of this node's STATE block |
| `state_words` | Total word count of STATE block |

`state_words` is explicit. Pull Server does not need to derive state length from `dof`.

### 3.2 AdjEntry

Each adjacency entry identifies a neighbor and its owning PE:

```systemverilog
typedef struct packed {
    logic [NODE_ID_W-1:0]      neighbor_id;
    logic [X_CORD_W-1:0]       neighbor_x;    // NoC x coordinate of neighbor's PE
    logic [Y_CORD_W-1:0]       neighbor_y;    // NoC y coordinate of neighbor's PE
} adj_entry_t;
```

| Field | Meaning |
|-------|---------|
| `neighbor_id` | ID of adjacent node (factor or variable) |
| `neighbor_x` | NoC x coordinate of the PE that owns this neighbor |
| `neighbor_y` | NoC y coordinate of the PE that owns this neighbor |

`neighbor_id + (neighbor_x, neighbor_y)` is sufficient for pull routing. Whether neighbor is local is determined by `(neighbor_x, neighbor_y) == (my_x, my_y)`. Remote state address is looked up by the remote PE itself; consumer PE does not need to store it.

Not included: flags, edge_id, remote address, state_words, message slot.

---

## 4. STATE Block Organization

STATE is a variable-length block per node. Contents differ between variable and factor nodes.

### 4.1 Variable Node STATE

```
state_base
  ↓
[eta words]      dof × FP32
[Lambda words]   dof × dof × FP32 (full matrix, or upper-triangular)
  ↓
state_base + state_words
```

GBPSim stores: `prior` (eta + lam) + `belief` (eta + lam). But for pull purposes, only the **current belief** is transmitted to consumers.

Compact form (for NoC transfer):

```
compact_words(dof) = dof + dof*(dof+1)/2
```

(eta vector + upper-triangular Lambda matrix)

> **Bit-width constraint**: `STATE_WORDS_W` must be ≥ `$clog2(compact_words(MAX_DOF) + 1)`. For MAX_DOF = 8, `compact_words(8) = 44`, so `STATE_WORDS_W` ≥ 6.

### 4.2 Factor Node STATE

```
state_base
  ↓
[per-edge message words]   adj_count × (dof_i + dof_i²) × FP32
[factor eta/lam words]     total_dofs + total_dofs² × FP32
[linpoint words]           total_dofs × FP32
[measurement words]        meas_dim × FP32
[config words]             (noise var, loss enum, damping, etc.)
  ↓
state_base + state_words
```

For pull purposes, factor node returns its **outgoing messages** (one per adjacent variable) or its **adj_beliefs** depending on consumer request.

### 4.3 Pull Server Semantics

```
Pull Server  = state transport engine (does not understand eta/lambda)
Compute Unit = state semantic consumer (does not care where data comes from)
```

Pull Server reads `state_words` words from STATE region sequentially and streams them as FETCH_RESPONSE. It does not interpret the data.

---

## 5. STAGING Region Design

### 5.1 Semantics

STAGING is **not a cache**. It has no tag, hit/miss, replacement, or coherence. It is a **temporary landing buffer** for pull responses.

Key properties:
- Lifetime = one fetch/compute batch (not one full node update)
- One node update may contain multiple batches
- After each batch compute completes, STAGING is reset
- Partial accumulator across batches is preserved until node update finishes

> **STAGING capacity only limits the batch size, not the maximum remote degree of a node.**

### 5.2 Batched Staging + Conservative Reservation

Three staging modes exist. First version uses Mode B.

| Mode | Description | STAGING Capacity | Status |
|------|-------------|-----------------|--------|
| A: Full-node staging | Pull all remote state at once | Largest | Not recommended |
| **B: Batched staging** | Batch pull → STAGING → compute → release → next batch | Medium | **First version** |
| C: Pure streaming | Response feeds directly into accumulator | Smallest | Future optimization |

### 5.3 STAGING Allocator

Two separate counters:
- `staging_reserved`: conservative budget reserved before issuing pull requests
- `staging_bump`: actual write pointer after response arrives

Hardware lifecycle (one node update, potentially multiple batches):

```
begin_node_update:                      // Metadata Scanner starts scanning adjacency list
    staging_bump = staging_base
    staging_reserved = 0
    accumulator_clear()
    scan AdjEntry[0], AdjEntry[1], ...

    // For each remote edge, ScoreboardPrefetcher issues FETCH_REQUEST
    // (gated by OUTSTANDING_DEPTH and STAGING capacity)
    // When either limit is hit, or all edges scanned → batch is "closed"

begin_batch:                            // STAGING resources reset for next batch
    staging_bump = staging_base         // (same reset as begin_node_update;
    staging_reserved = 0                //  this repeats for each batch within one node update)
    batch_outstanding_count = 0

before_issue_pull:
    reserve = align2(2^STATE_WORDS_W - 1)   // reserve_words_per_pull
    if batch_outstanding_count < OUTSTANDING_DEPTH
       and staging_reserved + reserve <= staging_limit - staging_base:
           issue PULL_REQ
           staging_reserved += reserve
           batch_outstanding_count++
    else:
           close batch (stop issuing new pulls)

on_pull_resp_hdr(txn_id, state_words):
    alloc_words = align2(state_words)
    assert alloc_words <= reserved_words_for_this_txn  // graph compiler guarantee
    txn_table[txn_id].valid = true
    txn_table[txn_id].adj_index = ...      // latched from request context
    txn_table[txn_id].base = staging_bump
    txn_table[txn_id].words = state_words
    txn_table[txn_id].received_words = 0
    staging_bump += alloc_words

on_pull_resp_data(txn_id, word_idx, data):
    SPM[txn_table[txn_id].base + word_idx] = data
    txn_table[txn_id].received_words++

after_all_batch_responses_arrive:       // batch_outstanding_count == 0
    compute reads STAGING blocks
    accumulator_update()

after_batch_compute_done:               // Compute Unit signals batch_done
    staging_bump = staging_base         // reset for next batch
    staging_reserved = 0
    → resume scanning remaining AdjEntry (begin next batch)

after_all_neighbors_processed:
    finalize update
    write back STATE
```

No free list, no fragmentation management.

### 5.4 Transaction Table

```
txn_table[txn_id]:
    valid           : bool
    adj_index       : int   // which adjacency entry this response corresponds to
    base            : word_addr   // STAGING base for this response
    words           : int         // state_words from header
    received_words  : int         // data words received so far
```

### 5.5 STAGING Capacity Constraints

```
Minimum constraint:
  STAGING_REGION_WORDS ≥ max_single_response_words

where:
  max_single_response_words = max(align2(state_words(M)))
  for every node M that may be pulled remotely.
  Checked by graph compiler at load time.

If batch allows B outstanding pulls:
  STAGING_REGION_WORDS ≥ B × reserve_words_per_pull

where:
  B ≤ OUTSTANDING_DEPTH
  reserve_words_per_pull = align2(2^STATE_WORDS_W - 1)
  Later: graph compiler can provide tighter per-kernel bound.

Runtime guarantee:
  Pull Client only issues PULL_REQ when both outstanding slot
  and STAGING reservation are available.
  Therefore, any issued pull response is guaranteed to have
  STAGING space.
```

### 5.6 Batch Close Conditions

A batch is closed when **any** of:
1. `batch_outstanding_count >= OUTSTANDING_DEPTH`
2. `staging_reserved + reserve_words_per_pull > staging_limit - staging_base`
3. All adjacency entries scanned

After batch close:
1. No new PULL_REQ issued
2. Wait for all outstanding responses in this batch (`batch_outstanding_count == 0`)
3. Compute reads STAGING blocks, updates partial accumulator
4. Reset `staging_bump` and `staging_reserved`
5. Continue scanning remaining AdjEntry for next batch

---

## 6. STAGING Allocator ↔ ScoreboardPrefetcher Coordination

### 6.1 Overview

Two independent limiters gate pull request issuance:

```
ScoreboardPrefetcher:  limits outstanding fetch count (SCOREBOARD_DEPTH)
STAGING Allocator:     limits STAGING space    (STAGING_REGION_WORDS)
```

Both must agree before a pull request is issued. ScoreboardPrefetcher is the **initiator**; STAGING Allocator is the **gate**.

### 6.2 Coordination Interface

```systemverilog
// ScoreboardPrefetcher → STAGING Allocator (inside Response Collector)
output logic                 staging_reserve_valid,      // requesting reservation
output logic [STATE_WORDS_W-1:0] staging_reserve_words,  // estimated words
input  logic                 staging_reserve_ready,      // reservation granted

// STAGING Allocator → ScoreboardPrefetcher
output logic                 staging_batch_closed,       // batch is full, stop issuing

// Compute Unit → STAGING Allocator
input  logic                 batch_done,                 // batch compute complete, reset
```

### 6.3 Pull Request Issuance Flow

```
ScoreboardPrefetcher has edge(N) in NOTIFIED state:
  1. Check: scoreboard not full?
  2. Check: staging_reserve_ready? (STAGING Allocator has space)
  3. If both: issue FETCH_REQUEST, edge → IN_FLIGHT, scoreboard++, staging_reserved += reserve
  4. If either fails: hold edge in NOTIFIED, retry next cycle
```

### 6.4 Batch Control Handshake

```
STAGING Allocator closes batch when:
  - batch_outstanding_count >= OUTSTANDING_DEPTH, OR
  - staging_reserved + reserve > staging_limit - staging_base

STAGING Allocator asserts staging_batch_closed.
ScoreboardPrefetcher stops issuing new pulls (holds NOTIFIED edges).

After batch compute completes:
  Foreground asserts batch_done.
  STAGING Allocator resets: staging_bump = staging_base, staging_reserved = 0.
  ScoreboardPrefetcher resumes issuing pulls for remaining NOTIFIED edges.
```

### 6.5 STAGING Allocator Ownership

The STAGING Allocator is a **sub-component of the Response Collector** (not a separate module). It owns:
- `staging_bump` register (write pointer)
- `staging_reserved` register (reservation budget)
- `batch_outstanding_count` counter
- `txn_table[]` (transaction tracking)

The ScoreboardPrefetcher queries the STAGING Allocator via the coordination interface. It does not own or modify STAGING state.

---

## 7. Per-Edge State Tracking (Scoreboard)

The ScoreboardPrefetcher maintains per-edge state for all remote edges. This is **not stored in SPM** — it is held in registers / small SRAM inside the Scoreboard module.

### 7.1 Per-Edge State Fields

```systemverilog
typedef struct packed {
    logic        notification;    // NOTIFICATION received from producer
    logic        in_flight;       // FETCH_REQUEST sent, waiting for response
    logic        ready;           // FETCH_RESPONSE received, data available
    logic [X_CORD_W-1:0] source_x; // NoC x coordinate of producer's PE
    logic [Y_CORD_W-1:0] source_y; // NoC y coordinate of producer's PE
} edge_scoreboard_t;
```

### 7.2 Per-Node Readiness

A node is schedulable when ALL its remote edges have `ready == true`. Local edges are always `ready`.

```systemverilog
logic node_ready;  // AND of all edge_ready bits
```

### 7.3 Scoreboard Capacity

The Scoreboard has a fixed capacity (`SCOREBOARD_DEPTH`). When full, no new fetch requests are issued until existing ones complete.

---

## 8. Node Lookup

Pull Server must locate the local NodeHeader from `neighbor_id` upon receiving a FETCH_REQUEST.

Graph compiler / mapper ensures each PE's local node id range is contiguous.

```
local_index  = node_id - local_node_base
header_addr  = header_base + local_index * HEADER_WORDS
```

One subtract + one multiply. No lookup table needed.

---

## 9. NoC ↔ SPM Width Conversion and Byte Order

NoC data width (`DATA_WIDTH`) is 32 bits. SPM beat width (`BEAT_BITS`) is 64 bits (8 bytes). All modules that cross this boundary must perform width conversion.

### 9.1 Byte Order within a 64-bit Beat

Little-endian word order:

```
64-bit SPM beat:
  [63:32]  word 1  (fp32_1)
  [31: 0]  word 0  (fp32_0)   ← lower address
```

`fp32_0` at `beat[31:0]` is the lower-address word. `fp32_1` at `beat[63:32]` is the higher-address word.

### 9.2 Pull Server: SPM 64-bit → NoC 32-bit

Pull Server reads 64-bit beats from SPM STATE and sends 32-bit stores to NoC Adapter.

```
Cycle 0: SPM read returns beat = {fp32_1, fp32_0}
Cycle 1: NoC store 0 = fp32_0  (beat[31:0])
Cycle 2: NoC store 1 = fp32_1  (beat[63:32])
```

- `state_words` counts 32-bit words.
- Even `state_words`: every beat is fully utilized.
- Odd `state_words`: the last beat's `beat[63:32]` is not sent. Pull Server stops after `state_words` stores.

### 9.3 Response Collector: NoC 32-bit → SPM 64-bit

Response Collector receives 32-bit data words from NoC and writes 64-bit beats to SPM STAGING.

```
NoC word 0  → buffer[31:0]
NoC word 1  → buffer[63:32]; write beat to SPM
NoC word 2  → buffer[31:0]
NoC word 3  → buffer[63:32]; write beat to SPM
...
```

- Internal 32→64 assembly buffer (1-entry register + valid flag).
- Even word count: last word pairs with previous word, writes full beat.
- Odd word count: last word sits in buffer alone. Write final beat with `wstrb = 8'b0000_1111` (only lower 4 bytes valid).

### 9.4 Write Stream Engine: Compute 32-bit → SPM 64-bit

Same pattern as Response Collector. Compute Unit outputs 32-bit FP32 words. Write Stream Engine assembles into 64-bit beats before writing to SPM.

### 9.5 Read Stream Engine: SPM 64-bit → Compute 32-bit

Read Stream Engine reads 64-bit beats from SPM and presents them to Compute Unit.

Option A (recommended): `read_stream_if.data` is 64-bit. Compute Unit internally unpacks `beat[31:0]` then `beat[63:32]`.

Option B: `read_stream_if.data` is 32-bit. Read Stream Engine internally unpacks 64-bit beat into two 32-bit cycles.

**Decision: Option A.** `read_stream_if.data` width = `BEAT_BITS = 64`. Compute Unit has simpler timing control when it manages its own word-level unpacking.

---

## 10. Related Documents

| Document | Content |
|----------|---------|
| `00_WRITING_GUIDE.md` | How to write architecture documents: structure, granularity, style |
| `01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `03_NOC_PROTOCOL.md` | NoC adaptation layer, mailbox encoding, manycore store-based messaging |
| `04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters, open items |
| `05_INTERFACES.md` | Port-level interfaces, state machines, timing paths |
| `06_PE_CONTROL_FLOW.md` | PE-level control flow, pipeline stages, module handshakes |
| `verification/README.md` | Verification documentation index and test templates |
