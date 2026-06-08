# Module Implementation Order

## Status Overview

| Phase | Scope | Modules | Status |
|-------|-------|---------|--------|
| Phase 0 | Foundation (already done) | `noc_adapter`, `phase_controller`, `metadata_scanner`, `scoreboard_prefetcher`, `pull_client`, `pull_server`, `response_collector`, `neighbor_state_accumulator`, `writeback_controller` | ✅ 9/9 Done |
| Phase 1 | Package + Control | `gbp_pkg`, `node_scheduler` | ✅ Done |
| Phase 2 | Compute datapath | `compute_unit`, `read_stream_engine`, `write_stream_engine`, `agu` | ✅ Done |
| Phase 3 | Memory subsystem | `spm_arbiter`, `spm_subsystem` | ✅ Done |
| Phase 4 | Subsystem wrappers + top-level | `gbp_pe_control_subsystem`, `gbp_pe_compute_subsystem`, `gbp_pe_memory_subsystem`, `gbp_pe_fetch_subsystem`, `gbp_pe` | 🔄 Pending |
| Phase 5 | Cleanup | Delete legacy files, update tests | 🔄 Pending |

---

## Dependency Graph

```
                                    gbp_pkg (update first)
                                         │
       ┌─────────────────────────────────┼─────────────────────────────────┐
       │                                 │                                 │
 phase_controller ──┐            node_scheduler                     spm_arbiter
       │            │                  │                                 │
 metadata_scanner ──┼── scoreboard_prefetcher ─────┐                   │
       │            │           │                    │                   │
 pull_client ───────┘           │                    │                   │
                                │                    │                   │
 pull_server ───────────────────┼────────────────────┤                   │
                                │                    │                   │
 response_collector ────────────┘                    │                   │
                                                    │                   │
 neighbor_state_accumulator ────────────────────────┘                   │
                                │                                       │
   compute_unit ◄───────────────┘                                       │
         │                                                             │
   read_stream_engine ──► agu ◄─── write_stream_engine                 │
         │                                                             │
         └──────────────────────┬──────────────────────────────────────┘
                                │
                ┌───────────────┼───────────────┐
                ▼               ▼               ▼
   gbp_pe_control_subsystem   gbp_pe_compute_subsystem   gbp_pe_memory_subsystem
                │               │                           │
                │               │                           │
                └───────────────┼───────────────────────────┘
                                │
                ┌───────────────┼───────────────┐
                ▼               ▼               ▼
   gbp_pe_fetch_subsystem   writeback_controller   gbp_pe_noc_adapter
                │               │                   │
                └───────────────┼───────────────────┘
                                │
                                ▼
                            gbp_pe (top)
```

**Key rule**: `gbp_pkg` must be updated before any module that imports it. `node_scheduler` must be done before `compute_unit` integration (it provides `sched_valid`/`sched_node_id`). `spm_arbiter` must be done before `read_stream_engine` and `write_stream_engine` verification.

---

## Phase 1: Package Update + Missing Control Module

### 1.1 `gbp_pkg.sv` — Modify Old File

**File**: `v/gbp_pe/gbp_pkg.sv`

**Current state**: Outdated. Defines `SPM_ADDR_W = 20` (byte address), `BEAT_BYTES = 32`, `BEAT_BITS = 256`, old `desc_t` with byte-oriented fields.

**Required changes**:

| Item | Old Value | New Value | Notes |
|------|-----------|-----------|-------|
| `SPM_ADDR_W` | 20 | 18 | Word address for 1MB SPM / 4B words |
| `BEAT_BYTES` | 32 | 8 | 64-bit beat |
| `BEAT_BITS` | 256 | 64 | Derived from BEAT_BYTES |
| `WORD_BYTES` | (implicit 4) | 4 | Explicit parameter |
| `ROW_ADDR_W` | 12 | 14 | `$clog2(SPM_BYTES_PER_PE / NUM_BANKS / BEAT_BYTES)` |
| `WORD_OFF_W` | 3 | 1 | 2 words per 64-bit beat |
| `BYTE_OFF_W` | 2 | 2 | Unchanged (byte offset in 32-bit word) |
| `WSTRB_W` | 32 | 8 | Byte mask width = `BEAT_BYTES` |
| `BEAT_MAX` | Derived from 32B beat | Re-derived from 8B beat | `$clog2` adjustment |
| Address type | Byte address | Word address | All `base_addr` fields |

**Struct rewrite**:
- Delete old `desc_t` fields: `xfer_bytes`, `addr_step_bytes`, `operand_id[3:0]`, `wstrb_mode`, `dim`, `y_count`, `y_stride_bytes`, `addr_src`.
- Replace with `stream_descriptor_t`:
  ```systemverilog
  typedef struct packed {
      logic [SPM_ADDR_W-1:0]    base_addr;     // word address
      logic [15:0]              word_count;    // 32-bit words to transfer
      logic                     is_staging;    // 1=STAGING, 0=STATE
  } stream_descriptor_t;
  ```

**Delete**:
- `STREAM_META`, `STREAM_VEC`, `STREAM_MESSAGE` enums (no longer used).
- `read_stream_if`, `write_stream_if`, `stream_dispatcher_if`, `stream_control_if`, `mic_spm_arbiter_if`, `mic_spm_arbiter_wr_if`, `spm_bank_if` interface definitions (moved to explicit ports per `05_INTERFACES.md`).

**Keep / add**:
- `TXN_ID_W`, `SCOREBOARD_DEPTH`, `NODE_ID_W`, `STATE_WORDS_W`, `GBP_BASE_ADDR`.
- Add `BEAT_BYTES = 8`, `FP32_W = 32`.

---

### 1.2 `node_scheduler.sv` — New Implementation

**File**: `v/gbp_pe/node_scheduler.sv` (new file)

**Old file**: None exists. Spec and verification doc exist (`02_node_scheduler.md`).

**Interface**: Already defined in `05_INTERFACES.md` §2.2.

**Implementation notes**:
- ~150 lines.
- Round-robin scan over `node_ready` vector (1024 bits).
- Must respect `phase_factor_first` and `visited_mask`.
- 1-cycle latency: `node_ready` → `sched_valid`/`sched_node_id`.
- `no_schedulable_nodes` asserted when scan completes with no match.

**Deliverables**:
1. `v/gbp_pe/node_scheduler.sv`
2. `nocbp_verilator/tops/unit/node_scheduler_top.sv`
3. `nocbp_verilator/tests/unit/node_scheduler.cc`
4. `nocbp_verilator/tests/unit/node_scheduler.yaml`
5. `nocbp_verilator/tests/unit/node_scheduler.f`

---

## Phase 2: Compute Datapath

All four modules can be developed in parallel **after** `gbp_pkg` is updated. `compute_unit` has the longest critical path (HardFloat integration); stream engines depend on AGU.

### 2.1 `compute_unit.sv` — Modify Old File

**File**: `v/gbp_pe/compute_unit.sv`

**What to keep**:
- HardFloat FP32 datapath: 16 lanes of `fNToRecFN` → `addRecFN`/`mulRecFN`/`divSqrtFN` → `recFNToFN`.
- `GBP_CORE_PER_PE` parameter (rename to `NUM_LANES` if desired).
- Internal lane loop `genvar i` structure.

**What to delete / replace**:

| Old Interface | Replacement | Rationale |
|---------------|-------------|-----------|
| `read_stream_if.master if_stream_if_stream` | `rd_desc_valid`/`rd_desc_ready`/`rd_desc_base_addr`/`rd_desc_word_count`/`rd_desc_is_staging` + `rd_beat_valid`/`rd_beat_ready`/`rd_beat_data[63:0]` | Explicit ports per new spec |
| `write_stream_if.slave if_write_stream_if_stream` | `wr_desc_valid`/`wr_desc_ready`/`wr_desc_base_addr`/`wr_desc_word_count` + `wr_word_valid`/`wr_word_ready`/`wr_word_data[31:0]` | Explicit ports per new spec |
| `cmd_valid_i`/`cmd_kind_i`/`cmd_ready_o` | `cmd_valid`/`cmd_ready`/`cmd_node_id[9:0]`/`cmd_is_factor`/`cmd_dof`/`cmd_adj_count`/`cmd_state_words` | New command interface (Metadata Scanner params forwarded to CU) |
| `data_a_i[16][31:0]`/`data_b_i[16][31:0]`/`op_i[1:0]`/`valid_i` | `ns_valid`/`ns_ready`/`ns_data[31:0]`/`ns_last` | Neighbor state from accumulator |
| `compute_done_o`/`rsp_done_o`/`force_persistence_stall_i` | `done_valid`/`done_node_id`/`done_is_factor` + `batch_done` | New completion signaling |
| `data_o[16][31:0]`/`valid_o` | `wr_word_data`/`wr_word_valid` | Output to write stream engine |

**Control FSM rewrite**:
- Old: simple `OP_ADD`/`OP_SUB`/`OP_MUL`/`OP_DIV` dispatch with `busy_r` flag.
- New: descriptor-driven sequencer. On `cmd_valid`:
  1. Issue read descriptor(s) to `read_stream_engine` (STATE + STAGING).
  2. Consume `ns_data` from accumulator + `rd_beat_data` from stream engine.
  3. Schedule FP32 lanes for `MAT_ADD`/`MAT_MUL`/`MAT_INV` etc.
  4. Issue write descriptor to `write_stream_engine`.
  5. Stream `wr_word_data`.
  6. Assert `done_valid`.

**Note**: High-level ops (`MAT_ADD`, `MAT_INV`, `FACTOR_MSG_SOLVE`, etc.) are schedules over the same FP32 lanes. The control sequencer is new; the lane arithmetic is reusable.

**Approximate line delta**: Current 264 lines → ~400–500 lines after rewrite.

---

### 2.2 `agu.sv` — Modify Old File

**File**: `v/gbp_pe/agu.sv`

**Current state**: 75 lines. Takes `desc_t` (old struct), outputs beat-addresses with `step_bytes` stride.

**Required changes**:
1. **Replace input ports**:
   ```systemverilog
   // Old
   input desc_t descriptor_i,   // contains base_addr, xfer_bytes, addr_step_bytes, start, operand_id...
   input logic  ready_i,        // FIFO ready
   
   // New
   input logic                 start,
   input logic [SPM_ADDR_W-1:0] base_addr,
   input logic [15:0]          word_count,
   input logic                 addr_ready,    // from downstream (was ready_i)
   ```
2. **Change address semantics**: Output **word addresses** (not beat addresses). The SPM Arbiter / bank does the `word_addr → bank_id/row_addr` decomposition.
   ```systemverilog
   output logic [SPM_ADDR_W-1:0] addr,     // word address: base_addr + i
   ```
3. **Delete**:
   - `xfer_bytes`, `step_bytes`, `beat_count_max` calculation.
   - `next_desc_o` → rename to `last_addr`.
   - `operand_id` special casing.
4. **Simplify logic**:
   ```systemverilog
   // Sequential word addresses: base_addr, base_addr+1, base_addr+2, ...
   // Advance when addr_valid && addr_ready
   // last_addr = 1 when i == word_count - 1
   ```

**Approximate line delta**: 75 lines → ~60 lines (simpler).

---

### 2.3 `read_stream_engine.sv` — Modify Old File

**File**: `v/gbp_pe/read_stream_engine.sv`

**Current state**: 202 lines. Uses `stream_dispatcher_if`, `read_stream_if`, `stream_control_if`, `mic_spm_arbiter_if`. Has META capture logic (`meta_consume_i`, `meta_valid_o`, `meta_data_o`). Instantiates `agu`, `addr_fifo`, `data_fifo`, `mic_read`.

**Required changes**:

1. **Delete all SystemVerilog interfaces**:
   - `stream_dispatcher_if.slave`
   - `read_stream_if.slave`
   - `stream_control_if.master`
   - `mic_spm_arbiter_if.master`

2. **Delete META logic**:
   - `meta_consume_i`, `meta_valid_o`, `meta_data_o`, `meta_seq_o`.
   - `meta_desc_pending_r`, `meta_hold_valid_r`, `meta_capture_valid_lo`.
   - Rationale: `metadata_scanner` reads META independently via its own SPM arbiter port.

3. **Add explicit ports** (per `05_INTERFACES.md` §2.11):
   ```systemverilog
   // Descriptor from Compute Unit
   input logic                 desc_valid,
   output logic                desc_ready,
   input logic [SPM_ADDR_W-1:0] desc_base_addr,
   input logic [15:0]          desc_word_count,
   input logic                 desc_is_staging,   // RSE does not use internally; present per spec for debug/region checking
   
   // Data to Compute Unit (64-bit beats)
   output logic                 beat_valid,
   input logic                  beat_ready,
   output logic [BEAT_BITS-1:0] beat_data,
   
   // SPM read port (to SPM Arbiter)
   output logic                 spm_rd_valid,
   input logic                  spm_rd_ready,
   output logic [SPM_ADDR_W-1:0] spm_rd_addr,
   input logic [BEAT_BITS-1:0]  spm_rd_data
   ```

4. **Replace internal blocks**:
   - `agu`: keep instance, update port connections to match new `agu` interface.
   - `addr_fifo` → `bsg_fifo_1r1w_small` (BaseJump STL). Depth ~8.
   - `data_fifo` → `bsg_fifo_1r1w_small` (BaseJump STL). Depth ~16.
   - `mic_read` → **delete**. RSE drives `spm_rd_valid`/`spm_rd_addr` directly to SPM Arbiter. Response comes back on `spm_rd_data` with 1-cycle latency.

5. **Simplify descriptor handshake**:
   ```systemverilog
   desc_ready = ~active && addr_fifo_empty && data_fifo_empty;
   // Accept descriptor → start AGU → push addresses to addr_fifo → drive spm_rd_valid →
   // receive spm_rd_data → push to data_fifo → pop to beat_valid/beat_data
   ```

**Approximate line delta**: 202 lines → ~150 lines.

---

### 2.4 `write_stream_engine.sv` — Modify Old File

**File**: `v/gbp_pe/write_stream_engine.sv`

**Current state**: 131 lines. Uses `stream_control_if`, `stream_dispatcher_if`, `write_stream_if`, `mic_spm_arbiter_wr_if`. Instantiates `agu`, `addr_fifo`, `data_fifo`, `mic_write`.

**Required changes**:

1. **Delete all SystemVerilog interfaces** (same as RSE).

2. **Add explicit ports** (per `05_INTERFACES.md` §2.12):
   ```systemverilog
   // Descriptor from Compute Unit
   input logic                 desc_valid,
   output logic                desc_ready,
   input logic [SPM_ADDR_W-1:0] desc_base_addr,
   input logic [15:0]          desc_word_count,
   
   // Data from Compute Unit (32-bit FP32 words)
   input logic                 word_valid,
   output logic                word_ready,
   input logic [FP32_W-1:0]    word_data,
   
   // SPM write port (to SPM Arbiter)
   output logic                 spm_wr_valid,
   input logic                  spm_wr_ready,
   output logic [SPM_ADDR_W-1:0] spm_wr_addr,
   output logic [BEAT_BITS-1:0] spm_wr_data,
   output logic [7:0]           spm_wr_wstrb
   ```

3. **Add 32-bit → 64-bit assembly logic**:
   ```systemverilog
   logic [FP32_W-1:0] word_hold_r;
   logic              word_hold_valid_r;
   
   // On word_valid && word_ready:
   //   If word_hold_valid_r == 0: store word in hold, set valid
   //   Else: assemble {word_data, word_hold} → 64-bit beat, write to SPM
   // On last word with odd count: write with wstrb = 8'b0000_1111
   ```

4. **Replace internal blocks**:
   - `agu`: keep instance, update ports.
   - `addr_fifo` → `bsg_fifo_1r1w_small`.
   - `data_fifo` → **delete** (assembly logic replaces it).
   - `mic_write` → **delete**. WSE drives `spm_wr_valid` directly.

**Approximate line delta**: 131 lines → ~140 lines (assembly logic adds some).

---

## Phase 3: Memory Subsystem

### 3.1 `spm_arbiter.sv` — New Implementation (Replace Old File)

**File**: `v/gbp_pe/spm_arbiter.sv`

**Current state**: 45 lines. Only 2 clients (`rd_if`, `wr_if` via `mic_spm_arbiter_if`/`mic_spm_arbiter_wr_if`). Direct passthrough to `spm_bank_if`.

**Required changes**:
1. **New interface**: 7 clients, explicit ports (not SV interfaces).
   ```systemverilog
   module spm_arbiter #(
       parameter NUM_BANKS   = 8,
       parameter NUM_CLIENTS = 7,
       parameter SPM_ADDR_W  = 18,
       parameter BEAT_BITS   = 64,
       parameter ROW_ADDR_W  = 14
   )(...)
   ```
2. **Client index mapping** (fixed):
   | Index | Client | Direction | Notes |
   |-------|--------|-----------|-------|
   | 0 | Metadata Scanner | read | META region (NodeHeader, AdjEntry) |
   | 1 | Read Stream Engine | read | STATE + STAGING reads via descriptors (one client port; `is_staging` is a descriptor field, not a separate arbiter client) |
   | 2 | Write Stream Engine | write | STATE writeback |
   | 3 | Pull Server | read | STATE read for remote fetches |
   | 4 | Response Collector | write | STAGING write for pull responses |
   | 5 | DMA / Loader | read + write | Initialization |
   
   > **Note on RSE client count**: `04_PE_MICROARCHITECTURE.md` §2.11 originally listed STATE and STAGING reads as separate clients (2 and 3). However, `05_INTERFACES.md` defines a single `read_stream_engine` with one SPM read port. A single RSE instance sequentially executes descriptors (STATE first, then STAGING, or vice versa). If future pipelining requires concurrent STATE+STAGING reads, a second RSE instance can be added as an additional arbiter client without changing the arbiter module.

3. **Arbitration logic**:
   - Round-robin grant per cycle.
   - Separate grant vectors for read and write (a client can have both granted in different cycles).
   - Bank conflict detection: if multiple requests target the same bank, only one is granted; others retry next cycle.

4. **Port mapping**: Decompose each client's `SPM_ADDR_W` word address into:
   ```systemverilog
   bank_id = addr[3:1];           // 3 bits
   row_addr = addr[17:4];         // 14 bits
   ```

**Approximate lines**: ~150 lines.

**Note**: The old `spm_arbiter.sv` is only 45 lines and completely incompatible. It is simpler to overwrite than to incrementally modify.

---

### 3.2 `spm_subsystem.sv` — Modify Old File

**File**: `v/gbp_pe/spm_subsystem.sv`

**Current state**: 121 lines. Uses `mic_spm_arbiter_if` arrays (`rd_if[2*NB]`, `wr_if[2*NB]`), instantiates `spm_rd_arbiter`/`spm_wr_arbiter` per bank.

**Required changes**:
1. **Update top-level ports**:
   ```systemverilog
   // Old
   mic_spm_arbiter_if.slave rd_if[2*NB](),
   mic_spm_arbiter_wr_if.slave wr_if[2*NB](),
   
   // New — connect directly to spm_arbiter bank ports
   // (Actually, spm_subsystem should instantiate spm_arbiter internally)
   ```
   Better approach: `spm_subsystem` instantiates `spm_arbiter` + `spm_bank_array`.
   ```systemverilog
   module spm_subsystem #(...) (
       // 7-client ports from PE internal modules
       // ... or pass a single structured interface
   );
   ```
   Decision: Keep `spm_subsystem` as a wrapper that instantiates `spm_arbiter` and `spm_bank_array`. Its ports should mirror the 7-client interface of `spm_arbiter`.

2. **Delete** `spm_rd_arbiter` and `spm_wr_arbiter` instantiations. These are absorbed into the unified `spm_arbiter`.

3. **Keep** `spm_bank_array` instantiation.

**Approximate line delta**: 121 lines → ~80 lines (simpler with unified arbiter).

---

## Phase 4: Subsystem Wrappers + Top-Level

Instead of a flat top-level or a single `pe_top` shim, the PE is built from four subsystem wrappers plus two straddling leaf modules. See `04_PE_MICROARCHITECTURE.md` §1 and `05_INTERFACES.md` §2.16 for the exact boundaries and ports.

### 4.1 `gbp_pe_control_subsystem.sv` — New

Encapsulates: `phase_controller`, `node_scheduler`, `metadata_scanner`.

**External interfaces only:**
- `node_ready_i` from fetch subsystem
- `wb_done_i` from writeback controller
- Command/metadata outputs to compute subsystem
- Adjacency stream to fetch subsystem / accumulator
- `reset_valid_i` from writeback controller
- SPM read port to memory subsystem
- `my_x_i` / `my_y_i`

**Approximate lines**: ~200 lines (mostly instance wiring).

---

### 4.2 `gbp_pe_compute_subsystem.sv` — New

Encapsulates: `compute_unit`, `read_stream_engine`, `write_stream_engine`, internal `agu` instances.

**External interfaces only:**
- Command input from control subsystem
- `ns_*` from accumulator
- Two SPM read ports + one SPM write port to memory subsystem
- `done_*` and `batch_done_o` outputs

**Key internal detail**: `gbp_compute_engine` uses a 256-bit stream interface internally. The wrapper must assemble 64-bit SPM beats into 256-bit words at the compute engine input and disassemble 256-bit engine outputs into 64-bit beats for the write stream engine. This width conversion is local to this subsystem.

**Approximate lines**: ~250 lines.

---

### 4.3 `gbp_pe_memory_subsystem.sv` — New

Encapsulates: `spm_arbiter`, `spm_bank_array`.

Exposes the 7-client SPM read/write port vectors. SPM bank instances live inside this wrapper so that memory subsystem tests can exercise the full bank-interleaved path without pulling in the rest of the PE.

**Approximate lines**: ~100 lines.

---

### 4.4 `gbp_pe_fetch_subsystem.sv` — New

Encapsulates: `scoreboard_prefetcher`, `pull_client`, `response_collector`.

**External interfaces only:**
- Adjacency stream from control subsystem
- `node_ready_o` to control subsystem
- `tx_fetch_req_*` to NoC adapter
- `rx_fetch_resp_*` from NoC adapter
- SPM read (Pull Server) + SPM write (Response Collector) to memory subsystem
- `remote_*` stream to accumulator
- `batch_done_i` from compute subsystem

**Approximate lines**: ~250 lines.

---

### 4.5 `gbp_pe.sv` — Rewrite

**File**: `v/gbp_pe/gbp_pe.sv`

**Current state**: 289 lines. Instantiates `gbp_pe_endpoint_adapter`, `gbp_pe_noc_bridge`, `pe_top`.

**New architecture wiring**:

```systemverilog
module gbp_pe
  import bsg_manycore_pkg::*;
  #( ... same manycore tile parameters ... )
  (
    // Manycore Tile interface (keep exactly the same)
    input  [link_sif_width_lp-1:0] link_sif_i,
    output [link_sif_width_lp-1:0] link_sif_o,
    input  barrier_data_i,
    output barrier_data_o,
    ...
  );

  // Internal instances:
  // 1. noc_adapter (wraps endpoint_standard, already done)
  // 2. gbp_pe_control_subsystem
  // 3. gbp_pe_compute_subsystem
  // 4. gbp_pe_memory_subsystem
  // 5. gbp_pe_fetch_subsystem
  // 6. neighbor_state_accumulator
  // 7. writeback_controller
endmodule
```

**Key wiring paths** (see `06_PE_CONTROL_FLOW.md`):
- `noc_adapter` RX → `gbp_pe_fetch_subsystem` (notif / fetch req / fetch resp).
- `noc_adapter` TX ← `gbp_pe_fetch_subsystem` (fetch req) / `writeback_controller` (notif).
- `gbp_pe_control_subsystem` ↔ `gbp_pe_fetch_subsystem` (`node_ready`, `adj_valid`, `reset_valid`).
- `gbp_pe_control_subsystem` → `gbp_pe_compute_subsystem` (compute command).
- `gbp_pe_compute_subsystem` ↔ `gbp_pe_memory_subsystem` (two read + one write ports).
- `gbp_pe_control_subsystem` ↔ `gbp_pe_memory_subsystem` (meta read port).
- `gbp_pe_fetch_subsystem` ↔ `gbp_pe_memory_subsystem` (pull read + staging write ports).
- `gbp_pe_fetch_subsystem` → `neighbor_state_accumulator` (remote data).
- `neighbor_state_accumulator` → `gbp_pe_compute_subsystem` (ns_valid/ns_data).
- `gbp_pe_compute_subsystem` → `writeback_controller` (done_valid).
- `writeback_controller` → `gbp_pe_fetch_subsystem` (reset_valid).
- `writeback_controller` → `noc_adapter` (notification TX).

**Manycore interface**: Keep identical to current `gbp_pe.sv` so it is a drop-in replacement in `bsg_manycore_tile_compute_mesh`.

**Whitebox test hooks**: Keep `GBP_WHITEBOX_TEST` ifdef for testbench observability. Whitebox command ports bypass `gbp_pe_control_subsystem` and drive `gbp_pe_compute_subsystem` directly.

**Approximate lines**: ~600–800 lines.

---

### 4.6 `pe_top.sv` — Delete

**File**: `v/gbp_pe/pe_top.sv`

**Rationale**: `pe_top` was an ad-hoc internal wrapper from the old architecture. The new architecture replaces it with four well-defined subsystem wrappers (`gbp_pe_*_subsystem`). There is no need for a separate `pe_top`.

**Action**: Delete `v/gbp_pe/pe_top.sv`. Update any references in build files (including `pe_top_integration` test, which should be renamed/replaced with subsystem-level tests).

---

## Phase 5: Legacy File Cleanup

After `gbp_pe` is rewritten and verified, delete the following files:

| File | Action | Replacement |
|------|--------|-------------|
| `v/gbp_pe/control_unit_gbp.sv` | **Delete** | `phase_controller` + `node_scheduler` |
| `v/gbp_pe/control_unit.sv` | **Delete** | `phase_controller` + `node_scheduler` |
| `v/gbp_pe/compute_unit_wrapper.sv` | **Delete** | inlined into new `compute_unit` |
| `v/gbp_pe/stream_dispatcher.sv` | **Delete** | functionality absorbed into `compute_unit` |
| `v/gbp_pe/mic_read.sv` | **Delete** | no equivalent (RSE drives arbiter directly) |
| `v/gbp_pe/mic_write.sv` | **Delete** | no equivalent (WSE drives arbiter directly) |
| `v/gbp_pe/addr_fifo.sv` | **Delete** | `bsg_fifo_1r1w_small` |
| `v/gbp_pe/data_fifo.sv` | **Delete** | `bsg_fifo_1r1w_small` |
| `v/gbp_pe/gbp_pe_noc_bridge.sv` | **Delete** | `noc_adapter` |
| `v/gbp_pe/gbp_pe_endpoint_adapter.sv` | **Delete** | `noc_adapter` |
| `v/gbp_pe/interfaces.sv` | **Delete** | explicit ports per `05_INTERFACES.md` |
| `v/gbp_pe/spm_rd_arbiter.sv` | **Delete** | merged into `spm_arbiter` |
| `v/gbp_pe/spm_wr_arbiter.sv` | **Delete** | merged into `spm_arbiter` |
| `v/gbp_pe/pe_top.sv` | **Delete** | replaced by `gbp_pe_*_subsystem` wrappers |

**Files to keep with minor edits**:
| File | Edit |
|------|------|
| `v/gbp_pe/spm_bank.sv` | Port renames only (`bank_rd_addr` → row address, `bank_wr_wstrb` width) |
| `v/gbp_pe/spm_bank_array.sv` | Port renames only |
| `v/gbp_pe/spm_bank_dpi.sv` | No change |

---

## Implementation Order Summary Table

| Order | Module | Action | File | Complexity | Blockers | Notes |
|-------|--------|--------|------|-----------|----------|-------|
| 1 | `gbp_pkg` | **Modify** | `v/gbp_pe/gbp_pkg.sv` | ~100 lines changed | None | Must be first — all modules import it |
| 2 | `node_scheduler` | **New** | `v/gbp_pe/node_scheduler.sv` | ~150 lines | `gbp_pkg` | No old file |
| 3 | `agu` | **Modify** | `v/gbp_pe/agu.sv` | ~60 lines | `gbp_pkg` | Simplifies old logic |
| 4 | `spm_arbiter` | **New** (replace) | `v/gbp_pe/spm_arbiter.sv` | ~150 lines | `gbp_pkg` | 7-client RR |
| 5 | `spm_subsystem` | **Modify** | `v/gbp_pe/spm_subsystem.sv` | ~80 lines | `spm_arbiter` | Delete old per-bank rd/wr arbiters |
| 6 | `read_stream_engine` | **Modify** | `v/gbp_pe/read_stream_engine.sv` | ~150 lines | `gbp_pkg`, `agu`, `spm_arbiter` | Delete interfaces, delete META logic |
| 7 | `write_stream_engine` | **Modify** | `v/gbp_pe/write_stream_engine.sv` | ~140 lines | `gbp_pkg`, `agu`, `spm_arbiter` | Add 32→64 assembly |
| 8 | `compute_unit` | **Modify** | `v/gbp_pe/compute_unit.sv` | ~400–500 lines | `gbp_pkg`, `node_scheduler` | Keep HardFloat lanes; 64→256 width conversion stays inside `gbp_pe_compute_subsystem` |
| 9 | `gbp_pe_control_subsystem` | **New** | `v/gbp_pe/gbp_pe_control_subsystem.sv` | ~200 lines | All control modules done | Encapsulates phase + scheduler + scanner |
| 10 | `gbp_pe_compute_subsystem` | **New** | `v/gbp_pe/gbp_pe_compute_subsystem.sv` | ~250 lines | CU + RSE + WSE done | Encapsulates compute datapath |
| 11 | `gbp_pe_memory_subsystem` | **New** | `v/gbp_pe/gbp_pe_memory_subsystem.sv` | ~100 lines | `spm_arbiter` + banks done | Encapsulates arbiter + SPM banks |
| 12 | `gbp_pe_fetch_subsystem` | **New** | `v/gbp_pe/gbp_pe_fetch_subsystem.sv` | ~250 lines | Scoreboard + pull + response done | Encapsulates remote fetch lifecycle |
| 13 | `gbp_pe` | **Rewrite** | `v/gbp_pe/gbp_pe.sv` | ~600–800 lines | All subsystems + accumulator + writeback done | Top-level wiring |
| 14 | Cleanup | **Delete** | 14 legacy files | — | `gbp_pe` verified | See Phase 5 list above |

**Parallelization**:
- After `gbp_pkg` is done, `node_scheduler`, `agu`, and `spm_arbiter` can proceed in parallel.
- After `agu` + `spm_arbiter`, `read_stream_engine` and `write_stream_engine` can proceed in parallel.
- `compute_unit` can start after `gbp_pkg` + `node_scheduler`, but final integration with stream engines requires both RSE/WSE done.
- The four subsystem wrappers (Phase 4) can be developed in parallel once their leaf modules are done.
- `gbp_pe` top must wait for all subsystems.

---

## Per-Module Deliverables

For each modified or new module:
1. RTL file in `v/gbp_pe/`
2. Test wrapper in `nocbp_verilator/tops/unit/`
3. C++ testbench in `nocbp_verilator/tests/unit/`
4. YAML config in `nocbp_verilator/tests/unit/`
5. `.f` file list in `nocbp_verilator/tests/unit/`
6. Implementation log in `docs/implementation/<NN>_<module>.md`
7. Verification doc signal names updated if interface changed
