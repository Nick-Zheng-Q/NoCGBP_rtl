# SPM and Metadata Design

## 1. SPM Logical Layout

SPM is physically banked memory (8 banks). Logically it has three regions:

```
SPM
├── META region
│   ├── S1NodeHeader array          // Forward CSR: one per local node
│   ├── FwdEdgeArray                // Forward CSR: edge descriptors
│   ├── RevKeyHash                  // Reverse CSR: neighbor_id -> rev_id lookup
│   ├── RevHeader array             // Reverse CSR: one per unique neighbor
│   └── RevEntryArray               // Reverse CSR: (local_id, fwd_edge_idx) pairs
│
├── STATE region
│   └── owner-node variable-length state blocks
│
└── STAGING region
    └── pull response temporary blocks
        lifetime = one fetch/compute batch
        not a cache (no tag/hit/miss/replacement/coherence)
```

**Design principle**: The PE stores a subset `S1` of the global graph nodes. For the edges incident to `S1`, we maintain **two CSR views**:
- **Forward CSR**: `S1 node -> its neighbors` (for compute / pull)
- **Reverse CSR**: `neighbor node -> affected S1 nodes` (for notification / dirty propagation)

Full edge descriptors live only in **FwdEdgeArray**. Reverse entries store lightweight references (`local_id`, `fwd_edge_idx`).

Region base/limit are loader-configured registers, exclusive upper bound:

```
s1_header_base,    s1_header_limit
fwd_edge_base,     fwd_edge_limit
rev_key_base,      rev_key_limit
rev_header_base,   rev_header_limit
rev_entry_base,    rev_entry_limit
state_base,        state_limit
staging_base,      staging_limit
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

## 3. Forward CSR: S1NodeHeader + FwdEdgeArray

### 3.1 S1NodeHeader

One entry per local node. Indexed by `local_id` (0 .. NUM_LOCAL_NODES-1).

```systemverilog
typedef struct packed {
    logic [NODE_ID_W-1:0]        node_id;        // global node ID
    logic [DOF_W-1:0]            dof;
    logic [ADJ_COUNT_W-1:0]      fwd_len;        // degree = number of neighbors
    logic [SPM_WORD_ADDR_W-1:0]  fwd_base;       // word addr of first FwdEdgeArray entry
    logic [SPM_WORD_ADDR_W-1:0]  state_base;     // start of STATE block (word addr)
    logic [STATE_WORDS_W-1:0]    state_words;    // word count of STATE block
} s1_node_header_t;
```

| Field | Meaning |
|-------|---------|
| `node_id` | Global node ID (factor_id or variable_id) |
| `dof` | Mathematical dimension |
| `fwd_len` | Number of adjacent nodes (degree) |
| `fwd_base` | Start word address of this node's edge list in FwdEdgeArray |
| `state_base` | Start word address of this node's STATE block |
| `state_words` | Total word count of STATE block |

**Address**: `s1_header_base + local_id * HEADER_WORDS` (HEADER_WORDS = 2 beats = 64 bits)

### 3.2 FwdEdgeArray

One entry per edge incident to a local node. Stored as CSR: all edges of local node 0, then node 1, etc.

```systemverilog
typedef struct packed {
    logic [NODE_ID_W-1:0]      neighbor_id;    // global neighbor node ID
    logic [X_CORD_W-1:0]       neighbor_x;     // PE x coordinate of neighbor's owner
    logic [Y_CORD_W-1:0]       neighbor_y;     // PE y coordinate of neighbor's owner
    logic [EDGE_IDX_W-1:0]     edge_slot;      // index into edge state / message table
    logic                      is_local;       // 1 if neighbor is on same PE
} fwd_edge_t;
```

| Field | Meaning |
|-------|---------|
| `neighbor_id` | Global ID of adjacent node |
| `neighbor_x` | NoC x coordinate of the PE that owns this neighbor |
| `neighbor_y` | NoC y coordinate of the PE that owns this neighbor |
| `edge_slot` | Runtime edge index (used by scoreboard_prefetcher, message table) |
| `is_local` | Pre-computed `(neighbor_x == my_x) && (neighbor_y == my_y)` |

**Address**: `fwd_edge_base + fwd_base + i` for edge i of node local_id.

**Local/remote classification** is pre-computed at load time and stored in `is_local`. Metadata Scanner does not recompute it at runtime.

---

## 4. Reverse CSR: RevKeyHash + RevHeader + RevEntryArray

The Reverse CSR answers: **"Given a neighbor global ID v, which local nodes in S1 are affected?"**

### 4.1 RevKeyHash

Maps `neighbor_global_id` -> `rev_id` (index into RevHeader).

```systemverilog
typedef struct packed {
    logic                    valid;
    logic [NODE_ID_W-1:0]    key;              // neighbor_global_id
    logic [REV_ID_W-1:0]     rev_id;           // index into RevHeader
} rev_key_entry_t;
```

**Implementation options** (chosen at compile time based on graph size):
- **Direct array**: If max global node ID is small, `RevDirectTable[global_id] -> rev_id`
- **Hash table**: For sparse keys, use cuckoo hash or open-addressing hash
- **Small CAM**: If each PE only sees tens/hundreds of unique neighbors

**Query flow**:
```
input: neighbor_global_id
  -> hash(neighbor_global_id) -> bucket
  -> compare key
  -> hit: rev_id
  -> miss: no S1 node depends on this neighbor
```

### 4.2 RevHeader

One entry per unique neighbor that appears in any S1 node's adjacency list.

```systemverilog
typedef struct packed {
    logic [REV_LEN_W-1:0]       rev_len;        // number of affected S1 nodes
    logic [SPM_WORD_ADDR_W-1:0] rev_base;       // word addr of first RevEntryArray entry
} rev_header_t;
```

| Field | Meaning |
|-------|---------|
| `rev_len` | Number of S1 nodes that have this neighbor |
| `rev_base` | Start word address in RevEntryArray |

**Address**: `rev_header_base + rev_id * REV_HEADER_WORDS`

### 4.3 RevEntryArray

Lightweight entries. Each entry is a reference into the Forward CSR.

```systemverilog
typedef struct packed {
    logic [LOCAL_ID_W-1:0]   local_id;         // S1 local node index (consumer)
    logic [EDGE_IDX_W-1:0]   fwd_edge_idx;     // index into FwdEdgeArray
} rev_entry_t;
```

| Field | Meaning |
|-------|---------|
| `local_id` | The S1 node that depends on this neighbor |
| `fwd_edge_idx` | Index into FwdEdgeArray for the full edge descriptor |

**Address**: `rev_entry_base + rev_base + i` for entry i of rev_id.

**Memory overhead**: Only `O(|E_S1|)` small entries, not full edge descriptor duplication.

---

## 5. Reverse CSR Query Example

**Scenario**: PE0 receives `NOTIFICATION(source_node_id = 1003)`.

```
Step 1: RevKeyHash lookup
  key = 1003
  hash(1003) -> bucket 17
  compare: RevKeyHash[17].key == 1003? yes
  -> rev_id = 5

Step 2: RevHeader read
  RevHeader[5]: rev_len = 3, rev_base = 42

Step 3: Stream RevEntryArray[42..44]
  RevEntry[42]: local_id = 0,  fwd_edge_idx = 7
  RevEntry[43]: local_id = 5,  fwd_edge_idx = 23
  RevEntry[44]: local_id = 12, fwd_edge_idx = 41

Step 4: For each entry, mark local_id as dirty / enqueue for scheduler
  dirty[0]  = 1
  dirty[5]  = 1
  dirty[12] = 1
```

This is `O(affected_degree)` SPM reads, not `O(|S1| * degree)` full scan.

---

## 6. STATE Block Organization

STATE is a variable-length block per node. Contents differ between variable and factor nodes.

### 6.1 Variable Node STATE

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

### 6.2 Factor Node STATE

```
state_base
  ↓
[per-edge message words]   adj_count × (dof_i + dof_i²) × FP32
[measurement words]        measurement_dim × FP32
[Jacobian words]           measurement_dim × dof × FP32
  ↓
state_base + state_words
```

---

## 7. SPM Region Size Guidelines

For a PE with `N` local nodes and average degree `d`:

| Region | Size estimate |
|--------|--------------|
| S1NodeHeader | `N * sizeof(s1_node_header_t)` |
| FwdEdgeArray | `N * d * sizeof(fwd_edge_t)` |
| RevKeyHash | `O(N * d)` entries (unique neighbors) |
| RevHeader | `O(N * d)` entries |
| RevEntryArray | `N * d * sizeof(rev_entry_t)` |
| STATE | `sum(state_words[i])` for all local nodes |
| STAGING | `BATCH_SIZE * max(state_words)` |

**Total META**: ~2× Forward CSR size (full descriptors + lightweight reverse references).

---

## 8. Preprocessing Flow (Offline)

Given the global graph `G` and the PE's assigned node subset `S1`:

```
1. Assign local_id to each node in S1 (0 .. N-1)

2. Build Forward CSR:
   for each s in S1 (in local_id order):
       collect all neighbors v of s
       append fwd_edge_t for each neighbor to FwdEdgeArray
       fill S1NodeHeader[local_id].fwd_base, fwd_len

3. Build Reverse relation:
   for each edge (s, v) where s ∈ S1:
       append (local_id(s), fwd_edge_idx) to reverse_list[v]

4. Compress reverse keys:
   collect all unique v that appear in any forward adjacency
   assign rev_id to each unique v

5. Build RevKeyHash:
   for each unique v:
       insert (v_global_id -> rev_id) into hash table

6. Build RevHeader + RevEntryArray:
   for each rev_id:
       rev_len = length of reverse_list[v]
       rev_base = cumulative offset into RevEntryArray
       copy reverse_list[v] entries into RevEntryArray
```

This preprocessing is done offline (software / graph compiler). The PE loader writes the pre-built structures into SPM at startup.
