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
    logic [ADJ_COUNT_W-1:0]      adj_count;      // degree = number of neighbors
    logic [SPM_WORD_ADDR_W-1:0]  adj_base;       // word addr of first FwdEdgeArray entry
    logic [SPM_WORD_ADDR_W-1:0]  state_base;     // start of STATE block (word addr)
    logic [STATE_WORDS_W-1:0]    state_words;    // word count of STATE block
} s1_node_header_t;
```

| Field | Meaning |
|-------|---------|
| `node_id` | Global node ID (factor_id or variable_id) |
| `dof` | Mathematical dimension |
| `adj_count` | Number of adjacent nodes (degree) |
| `adj_base` | Start word address of this node's edge list in FwdEdgeArray |
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
    logic [DOF_W-1:0]          neighbor_dof;   // DOF of adjacent variable node (for factor message sizing)
    logic                      is_local;       // 1 if neighbor is on same PE
} fwd_edge_t;
```

| Field | Meaning |
|-------|---------|
| `neighbor_id` | Global ID of adjacent node |
| `neighbor_x` | NoC x coordinate of the PE that owns this neighbor |
| `neighbor_y` | NoC y coordinate of the PE that owns this neighbor |
| `edge_slot` | Runtime edge index (used by scoreboard_prefetcher, message table) |
| `neighbor_dof` | DOF of the adjacent variable node; used by factor nodes to compute per-edge message size and offset |
| `is_local` | Pre-computed `(neighbor_x == my_x) && (neighbor_y == my_y)` |

**Address**: `fwd_edge_base + adj_base + i` for edge i of node local_id.

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

Variable node STATE contains the current belief, which acts as the prior for the next message-passing round. It stores `eta + Lambda` in compact form. Incoming messages from factor neighbors are **not** stored in the STATE block; they are received on-demand via the accumulator during each compute cycle. After a belief update, `compute_unit_wrapper` writes the new `eta + Lambda` back to the same STATE slot (see `08_NEW_COMPUTE_UNIT.md` §22.6 for writeback payload order).

```
state_base
  ↓
[eta words]      dof × FP32
[Lambda words]   dof × (dof+1) / 2 × FP32  (upper-triangular compact form)
  ↓
state_base + state_words
```

**Compact form** (used for NoC transfer and STATE storage):

```
compact_words(dof) = dof + dof*(dof+1)/2
```

(eta vector + upper-triangular Lambda matrix)

> **Compact form is frozen**. The full matrix is never stored in STATE or transferred over NoC. Only the upper-triangular compact representation is used.
>
> **Bit-width constraint**: `STATE_WORDS_W` must be ≥ `$clog2(max_state_words + 1)`.
>
> Typical factor (degree=2, DOF=6): 2×27 + measurement(6) + Jacobian(36) = 96 words.
> Large factor (degree=4, DOF=8): 4×44 + measurement(6) + Jacobian(192) = 374 words.
> **STATE_WORDS_W = 9** (max 512 words) covers all typical and most large factors.
> The graph compiler must ensure `state_words ≤ 512` for every node.

### 6.2 Factor Node STATE

Factor node STATE contains **per-edge old messages** (for damping), plus the measurement and Jacobian.

**Per-edge message layout** (cumulative offset, variable size per edge):

```
state_base
  ↓
[message_0]       compact_words(dof_0) words   // outgoing message to adjacent variable 0
[message_1]       compact_words(dof_1) words   // outgoing message to adjacent variable 1
...
[message_{adj_count-1}]  compact_words(dof_{adj_count-1}) words
[measurement]     measurement_dim × FP32
[Jacobian]        measurement_dim × Σdof_i × FP32
  ↓
state_base + state_words
```

**Message offset formula**:

```
compact_words(d) = d + d*(d+1)/2
msg_offset[i]    = Σ compact_words(dof_j) for j = 0 .. i-1
msg_addr[i]      = state_base + msg_offset[i]
msg_words[i]     = compact_words(dof_i)
```

`dof_i` is read from `fwd_edge_t.neighbor_dof` during SCAN. The control FSM accumulates `msg_offset[i]` and `msg_words[i]` in registers during the adjacency scan, then uses them to issue read/write descriptors for each edge's message.

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
       fill S1NodeHeader[local_id].adj_base, adj_count

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
