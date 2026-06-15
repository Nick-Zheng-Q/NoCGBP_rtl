# Module Interfaces and Control Logic

## 1. Global Interface Conventions

All inter-module interfaces use valid/ready handshake:

```
sender:  xxx_valid  (output)
         xxx_data   (output)
receiver: xxx_ready (output)

transfer happens when xxx_valid && xxx_ready on rising edge.
```

Clock/reset: all modules share `clk_i`, `rst_n_i` (leaf modules) or `clk`, `rst_n` (subsystem wrappers).

---

## 1.5 Descriptor Format

Read and write descriptors carry the information needed by the AGU to generate a sequence of SPM addresses.

```systemverilog
typedef struct packed {
    logic [SPM_ADDR_W-1:0]       base_addr;     // start word address (STATE or STAGING region)
    logic [15:0]                 word_count;    // number of 32-bit words to transfer
    logic                        is_staging;    // 1 = STAGING region, 0 = STATE region
} stream_descriptor_t;
```

**Field semantics:**

| Field | Width | Description |
|-------|-------|-------------|
| `base_addr` | `SPM_ADDR_W` | Word address of the first word. Must be 8B-aligned (even word address). |
| `word_count` | 16 | Number of 32-bit words. Max 65535 words (256KB). |
| `is_staging` | 1 | Region select. Compute Unit reads remote neighbor states from STAGING, local states from STATE. |

**Descriptor transfer:**
- Descriptors are sent via dedicated valid/ready ports (not multiplexed onto data channels).
- `desc_valid && desc_ready` latches the descriptor into the stream engine.
- The stream engine ACKs the descriptor immediately and begins address generation.

---

## 2. Module Port Definitions

### 2.1 Phase Controller

```systemverilog
module phase_controller #(
    parameter int NUM_NODES = 1024
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Node Scheduler feedback
    input  logic sched_valid_i,           // scheduler selected a node this cycle
    input  logic no_schedulable_nodes_i,  // current phase has no ready nodes

    // Writeback Controller feedback
    input  logic wb_done_i,               // node update complete, allow next schedule

    // Phase output
    output logic phase_factor_first_o,    // 1 = factor phase, 0 = variable phase
    output logic phase_switch_pulse_o,    // single-cycle pulse on phase switch
    output logic [NUM_NODES-1:0] visited_mask_o  // nodes already computed this phase
);
```

### 2.2 Node Scheduler

```systemverilog
module node_scheduler #(
    parameter int NUM_NODES = 1024,
    parameter int NODE_ID_W = 10
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Phase Controller
    input  logic phase_factor_first_i,

    // ScoreboardPrefetcher
    input  logic [NUM_NODES-1:0] node_ready_i,  // one bit per node

    // Phase coverage
    input  logic [NUM_NODES-1:0] visited_mask_i, // already computed this phase

    // Metadata Scanner backpressure
    input  logic sched_ready_i,         // Metadata Scanner accepted command

    // Output
    output logic                 sched_valid_o,   // selected a node
    output logic [NODE_ID_W-1:0] sched_node_id_o,
    output logic                 sched_is_factor_o,
    output logic                 no_schedulable_nodes_o
);
```

### 2.3 Metadata Scanner

```systemverilog
module metadata_scanner #(
    parameter int NODE_ID_W     = 10,
    parameter int SPM_ADDR_W    = 18,   // word address for 1MB SPM (32-bit words)
    parameter int STATE_WORDS_W = 9,
    parameter int ADJ_COUNT_W   = 4,
    parameter int DOF_W         = 4,
    parameter int BEAT_BITS     = 64    // 8-byte beat
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Command from scheduler
    input  logic                 cmd_valid_i,
    input  logic [NODE_ID_W-1:0] cmd_node_id_i,
    input  logic                 cmd_is_factor_i,
    output logic                 cmd_ready_o,

    // SPM read port (to SPM Arbiter)
    output logic                 spm_rd_valid_o,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic                 spm_rd_ready_i,
    input  logic [BEAT_BITS-1:0]  spm_rd_data_i,

    // AdjEntry output stream
    output logic                 adj_valid_o,
    input  logic                 adj_ready_i,
    output logic [NODE_ID_W-1:0] adj_neighbor_id_o,
    output logic [X_CORD_W-1:0]   adj_neighbor_x_o,
    output logic [Y_CORD_W-1:0]   adj_neighbor_y_o,
    output logic [DOF_W-1:0]     adj_neighbor_dof_o,  // DOF of adjacent variable node
    output logic                 adj_is_local_o,
    output logic                 adj_last_o,       // last adjacency entry
    output logic [ADJ_COUNT_W-1:0] adj_edge_idx_o, // current edge index within node
    output logic [NODE_ID_W-1:0] adj_current_node_id_o  // node ID being scanned (for fetch subsystem)

    // Node info output
    output logic                 info_valid_o,
    output logic [DOF_W-1:0]     info_dof_o,
    output logic [ADJ_COUNT_W-1:0] info_adj_count_o,
    output logic [SPM_ADDR_W-1:0] info_state_base_o,
    output logic [STATE_WORDS_W-1:0] info_state_words_o
);
```

### 2.3b Reverse Index Lookup

Queries Reverse CSR to find all local nodes affected by a NOTIFICATION.

```systemverilog
module reverse_index_lookup #(
    parameter int NODE_ID_W     = 10,
    parameter int SPM_ADDR_W    = 18,
    parameter int REV_ID_W      = 8,
    parameter int BEAT_BITS     = 64
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Notification ingress (from NoC Adapter RX)
    input  logic                 rx_notif_valid_i,
    output logic                 rx_notif_ready_o,
    input  logic [NODE_ID_W-1:0] rx_notif_source_node_id_i,

    // Affected node output (to Node Scheduler pending_queue)
    output logic                 affected_valid_o,
    input  logic                 affected_ready_i,
    output logic [NODE_ID_W-1:0] affected_local_id_o,
    output logic                 affected_last_o,  // last affected node for this notification

    // SPM read port (to memory subsystem — Reverse CSR region)
    output logic                 spm_rd_valid_o,
    input  logic                 spm_rd_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic [BEAT_BITS-1:0]  spm_rd_data_i
);
```

**Flow**: RevKeyHash → RevHeader → RevEntryArray stream. See `04_PE_MICROARCHITECTURE.md` §2.3b for detailed state machine.

---

### 2.4 ScoreboardPrefetcher

```systemverilog
module scoreboard_prefetcher #(
    parameter int SCOREBOARD_DEPTH = 64,
    parameter int TXN_ID_W = $clog2(SCOREBOARD_DEPTH)
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Notification ingress (from NoC Adapter RX)
    // The notification packet only carries the SOURCE node ID and its PE coordinates.
    // The scoreboard matches all registered edges whose source_node equals the
    // received source_node_id. There is no per-consumer target_node_id in the packet.
    input  logic                 rx_notif_valid_i,
    output logic                 rx_notif_ready_o,
    input  logic [NODE_ID_W-1:0] rx_notif_source_node_id_i,
    input  logic                 rx_notif_is_factor_i,
    input  logic [X_CORD_W-1:0]   rx_notif_source_x_i,
    input  logic [Y_CORD_W-1:0]   rx_notif_source_y_i,

    // Fetch request output (to Pull Client, then to NoC Adapter TX)
    output logic                 fetch_req_valid_o,
    input  logic                 fetch_req_ready_i,
    output logic [NODE_ID_W-1:0] fetch_req_target_node_id_o,
    output logic [NODE_ID_W-1:0] fetch_req_consumer_node_id_o,
    output logic                 fetch_req_is_factor_o,
    output logic [X_CORD_W-1:0]   fetch_req_target_x_o,
    output logic [Y_CORD_W-1:0]   fetch_req_target_y_o,
    output logic [TXN_ID_W-1:0]  fetch_req_txn_id_o,       // edge_index, used for response matching

    // Fetch response completion (from Response Collector)
    input  logic                 complete_valid_i,
    input  logic [TXN_ID_W-1:0]  complete_txn_id_i,        // direct edge match via txn_id
    input  logic [NODE_ID_W-1:0] complete_node_id_i,       // reserved for debug/monitoring (unused in logic)
    input  logic [NODE_ID_W-1:0] complete_consumer_node_id_i, // used to decrement node_pending count

    // STAGING Allocator coordination (to/from Response Collector)
    output logic                 staging_reserve_valid_o,
    output logic [STATE_WORDS_W-1:0] staging_reserve_words_o,
    input  logic                 staging_reserve_ready_i,
    input  logic                 staging_batch_closed_i,

    // Node readiness output (to Node Scheduler)
    output logic [NUM_NODES-1:0] node_ready_o,

    // Edge classification from Metadata Scanner (adj stream → local edge READY)
    output logic                 adj_ready_o,          // backpressure to Metadata Scanner (deasserted when batch full)
    input  logic                 adj_valid_i,
    input  logic [NODE_ID_W-1:0] adj_neighbor_id_i,
    input  logic [X_CORD_W-1:0]   adj_neighbor_x_i,
    input  logic [Y_CORD_W-1:0]   adj_neighbor_y_i,
    input  logic                 adj_is_local_i,
    input  logic                 adj_last_i,
    input  logic [ADJ_COUNT_W-1:0] adj_edge_idx_i,          // current edge index within node

    // Post-compute reset (from Writeback Controller)
    input  logic                 reset_valid_i,
    input  logic [NODE_ID_W-1:0] reset_node_id_i,
    input  logic                 reset_is_factor_i,

    // Scoreboard status
    output logic [$clog2(SCOREBOARD_DEPTH):0] scoreboard_occupancy_o,
    output logic scoreboard_full_o
);
```

### 2.5 Pull Client

Sends FETCH_REQUEST to producer PE via NoC Adapter. Issues 3 stores: {is_factor, consumer_id}, {target_id}, {txn_id}.

```systemverilog
module pull_client #(
    parameter int SCOREBOARD_DEPTH = 64,
    parameter int TXN_ID_W = $clog2(SCOREBOARD_DEPTH)
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Request from ScoreboardPrefetcher
    input  logic                 req_valid_i,
    output logic                 req_ready_o,
    input  logic [NODE_ID_W-1:0] req_target_node_id_i,
    input  logic [NODE_ID_W-1:0] req_consumer_node_id_i,
    input  logic                 req_is_factor_i,
    input  logic [X_CORD_W-1:0]   req_target_x_i,
    input  logic [Y_CORD_W-1:0]   req_target_y_i,
    input  logic [TXN_ID_W-1:0]  req_txn_id_i,

    // To NoC Adapter (FETCH_REQUEST TX, 3-store sequence)
    output logic                 tx_fetch_req_valid_o,
    input  logic                 tx_fetch_req_ready_i,
    output logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id_o,
    output logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id_o,
    output logic                 tx_fetch_req_is_factor_o,
    output logic [X_CORD_W-1:0]   tx_fetch_req_target_x_o,
    output logic [Y_CORD_W-1:0]   tx_fetch_req_target_y_o,
    output logic [TXN_ID_W-1:0]  tx_fetch_req_txn_id_o,
    output logic [1:0]           tx_fetch_req_store_idx_o  // 0,1,2 = which store
);
```

### 2.6 Pull Server

Responds to FETCH_REQUEST from other PEs, sends FETCH_RESPONSE via NoC Adapter. Echoes txn_id from request in response done store.

```systemverilog
module pull_server #(
    parameter int SCOREBOARD_DEPTH = 64,
    parameter int TXN_ID_W         = $clog2(SCOREBOARD_DEPTH),
    parameter int SPM_ADDR_W       = 18,   // word address
    parameter int BEAT_BITS        = 64,   // 8-byte beat
    parameter int DATA_WIDTH       = 32    // NoC data width
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Fetch request ingress (from NoC Adapter RX)
    input  logic                 req_valid_i,
    output logic                 req_ready_o,
    input  logic [NODE_ID_W-1:0] req_target_node_id_i,
    input  logic [NODE_ID_W-1:0] req_consumer_node_id_i,
    input  logic                 req_is_factor_i,
    input  logic [X_CORD_W-1:0]   req_fetch_src_x_i,
    input  logic [Y_CORD_W-1:0]   req_fetch_src_y_i,
    input  logic [TXN_ID_W-1:0]  req_txn_id_i,           // echoed from FETCH_REQUEST store 3

    // SPM read port (to SPM Arbiter)
    output logic                 spm_rd_valid_o,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic                 spm_rd_ready_i,
    input  logic [BEAT_BITS-1:0] spm_rd_data_i,

    // To NoC Adapter (FETCH_RESPONSE TX)
    // Sends 3 types of stores: metadata (MBX_RESP_META), data (MBX_RESP_DATA × state_words), done (MBX_RESP_DONE)
    output logic                     tx_fetch_resp_valid_o,
    input  logic                     tx_fetch_resp_ready_i,
    output logic [NODE_ID_W-1:0]     tx_fetch_resp_node_id_o,
    output logic [NODE_ID_W-1:0]     tx_fetch_resp_consumer_node_id_o,
    output logic                     tx_fetch_resp_is_factor_o,
    output logic [STATE_WORDS_W-1:0] tx_fetch_resp_state_words_o,
    output logic [DATA_WIDTH-1:0]    tx_fetch_resp_data_o,
    output logic                     tx_fetch_resp_data_valid_o,
    output logic                     tx_fetch_resp_last_o,
    output logic [TXN_ID_W-1:0]      tx_fetch_resp_txn_id_o  // echoed in done store
);
```

### 2.7 Response Collector

Receives FETCH_RESPONSE data from NoC Adapter, writes to STAGING region, and notifies ScoreboardPrefetcher.

```systemverilog
module response_collector #(
    parameter int STATE_WORDS_W    = 6,
    parameter int SCOREBOARD_DEPTH = 64,
    parameter int TXN_ID_W         = $clog2(SCOREBOARD_DEPTH),
    parameter int SPM_ADDR_W       = 18,   // word address
    parameter int BEAT_BITS        = 64,   // 8-byte beat
    parameter int DATA_WIDTH       = 32    // NoC data width
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Fetch response ingress (from NoC Adapter RX)
    // Receives 3 types of stores: metadata (MBX_RESP_META), data (MBX_RESP_DATA × N), done (MBX_RESP_DONE)
    input  logic                     rx_fetch_resp_valid_i,
    output logic                     rx_fetch_resp_ready_o,
    input  logic                     rx_fetch_resp_is_factor_i,
    input  logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words_i,
    input  logic [DATA_WIDTH-1:0]    rx_fetch_resp_data_i,
    input  logic                     rx_fetch_resp_data_valid_i,
    input  logic                     rx_fetch_resp_last_i,
    input  logic                     rx_fetch_resp_done_valid_i,  // MBX_RESP_DONE store received
    input  logic [TXN_ID_W-1:0]      rx_fetch_resp_txn_id_i,      // from done store payload

    // STAGING write port (to SPM Arbiter)
    output logic                 staging_wr_valid_o,
    input  logic                 staging_wr_ready_i,
    output logic [SPM_ADDR_W-1:0]  staging_wr_addr_o,
    output logic [BEAT_BITS-1:0]   staging_wr_data_o,    // 64-bit SPM beat
    output logic [BEAT_BYTES-1:0]  staging_wr_wstrb_o,   // byte mask (8B)

    // To ScoreboardPrefetcher (completion notification)
    output logic                 complete_valid_o,
    output logic [TXN_ID_W-1:0] complete_txn_id_o,      // edge_index for edge matching
    output logic [NODE_ID_W-1:0] complete_node_id_o,     // producer node ID (from done store, for debug)
    output logic [NODE_ID_W-1:0] complete_consumer_node_id_o, // consumer node ID (from done store, for debug)

    // Remote neighbor state stream to Accumulator
    // Streams pull response data as it arrives (also writes to STAGING in parallel)
    output logic                 remote_valid_o,
    input  logic                 remote_ready_i,
    output logic [FP32_W-1:0]    remote_data_o,
    output logic                 remote_last_o,

    // STAGING Allocator coordination (internal sub-component)
    // Reservation interface (from ScoreboardPrefetcher)
    input  logic                 staging_reserve_valid_i,
    input  logic [STATE_WORDS_W-1:0] staging_reserve_words_i,
    output logic                 staging_reserve_ready_o,

    // Batch control
    output logic                 staging_batch_closed_o,
    input  logic                 batch_done_i  // batch compute complete, trigger STAGING reset
);
```

### 2.8 Neighbor State Accumulator

```systemverilog
module neighbor_state_accumulator #(
    parameter int FP32_W = 32
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Local SPM read (from top-level PE controller, which reads STATE via SPM Arbiter)
    // The upstream control subsystem unpacks 64-bit SPM beats into 32-bit FP32 words
    // before presenting them to the accumulator.
    input  logic                 local_valid_i,
    output logic                 local_ready_o,
    input  logic [FP32_W-1:0]    local_data_i,   // 32-bit FP32 word
    input  logic                 local_last_i,

    // Remote response (from Response Collector, streaming pull data)
    input  logic                 remote_valid_i,
    output logic                 remote_ready_o,
    input  logic [FP32_W-1:0]    remote_data_i,
    input  logic                 remote_last_i,

    // To Compute Unit
    output logic                 out_valid_o,
    input  logic                 out_ready_i,
    output logic [FP32_W-1:0]    out_data_o,
    output logic                 out_last_o,

    // Pipeline control (consumed by top-level PE controller)
    output logic                 accumulator_done_o  // all neighbors consumed, triggers COMPUTE
);
```

### 2.9 SPM Arbiter

```systemverilog
module spm_arbiter #(
    parameter int NUM_BANKS   = 8,
    parameter int NUM_CLIENTS = 8,   // MetadataScanner, ReverseCSR, CU_state_rd, CU_staging_rd, CU_wb, PullServer, RespCollector, DMA
    parameter int SPM_ADDR_W  = 18,  // word address width
    parameter int BEAT_BITS   = 64,  // 8-byte beat
    parameter int ROW_ADDR_W  = 14   // bank row address (for 1MB SPM)
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Client read ports
    input  logic [NUM_CLIENTS-1:0]              rd_valid_i,
    output logic [NUM_CLIENTS-1:0]              rd_ready_o,
    input  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] rd_addr_i,
    output logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0]   rd_data_o,

    // Client write ports
    input  logic [NUM_CLIENTS-1:0]              wr_valid_i,
    output logic [NUM_CLIENTS-1:0]              wr_ready_o,
    input  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] wr_addr_i,
    input  logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0]   wr_data_i,

    // Bank ports
    output logic [NUM_BANKS-1:0]                bank_rd_en_o,
    output logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0]  bank_rd_addr_o,
    input  logic [NUM_BANKS-1:0][BEAT_BITS-1:0]   bank_rd_data_i,

    output logic [NUM_BANKS-1:0]                bank_wr_en_o,
    output logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0]  bank_wr_addr_o,
    output logic [NUM_BANKS-1:0][BEAT_BITS-1:0]   bank_wr_data_o
);
```

### 2.10 Compute Unit (`compute_unit_wrapper` / `gbp_compute_core`)

> **Superseded by `08_NEW_COMPUTE_UNIT.md`**: The detailed compute-core interfaces, command/response structs, operand stream formats, and internal module hierarchy are specified in `08_NEW_COMPUTE_UNIT.md` v0.7. This section only describes the subsystem boundary.

The compute subsystem internally instantiates:

```text
compute_unit_wrapper
├── gbp_compute_core      (arithmetic only)
├── writeback_packer
└── stream framing logic
```

`gbp_compute_core` receives fully-assembled operand streams and produces `gbp_core_rsp_t` responses. It contains no SPM address-generation logic. `compute_unit_wrapper` handles descriptor issuance to the Read/Write Stream Engines, stream framing, writeback packing, and `cu_done_t` reporting.

**Command translation note**: The PE-level control command (`cmd_node_id`, `cmd_is_factor`, `cmd_dof`, `cmd_adj_count`, `cmd_state_words`, `cmd_state_base`, `damping_factor`) presented at the `gbp_pe_compute_subsystem` boundary must be translated into `cu_cmd_t` for `compute_unit_wrapper`. This translation (including factor-type encoding, dimension encoding, and operand-descriptor construction from adjacency metadata) is internal to the compute subsystem and is not specified in this document. See `08_NEW_COMPUTE_UNIT.md` §8 and §24 for the `gbp_core_req_t` / `cu_cmd_t` formats.

### 2.11 Read Stream Engine

Receives read descriptors from Compute Unit, generates SPM addresses via AGU, reads 64-bit beats from SPM, and returns them to Compute Unit.

```systemverilog
module read_stream_engine #(
    parameter int SPM_ADDR_W    = 18,
    parameter int BEAT_BITS     = 64
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Descriptor from Compute Unit
    input  logic                 desc_valid_i,
    output logic                 desc_ready_o,
    input  logic [SPM_ADDR_W-1:0] desc_base_addr_i,    // word address
    input  logic [15:0]          desc_word_count_i,    // 32-bit words to transfer (16-bit for general DMA; STATE_WORDS_W=6 applies only to NodeHeader state_words field)
    input  logic                 desc_is_staging_i,    // 1=STAGING, 0=STATE

    // Data to Compute Unit (64-bit beats)
    output logic                 beat_valid_o,
    input  logic                 beat_ready_i,
    output logic [BEAT_BITS-1:0] beat_data_o,

    // SPM read port (to SPM Arbiter)
    output logic                 spm_rd_valid_o,
    input  logic                 spm_rd_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic [BEAT_BITS-1:0] spm_rd_data_i
);
```

**Operation:**
1. Accept descriptor when `desc_valid && desc_ready`.
2. AGU generates sequential word addresses: `addr = base_addr, base_addr+1, base_addr+2, ...`
3. Drive `spm_rd_valid` + `spm_rd_addr` to SPM Arbiter.
4. Buffer returned `spm_rd_data` beats.
5. Present `beat_valid` + `beat_data` to Compute Unit.
6. Every 2 words = 1 beat (8 bytes). If `word_count` is odd, last beat contains only 1 valid word in `[31:0]`; Compute Unit handles unpacking.

### 2.12 Write Stream Engine

Receives write descriptors and 32-bit FP32 data words from Compute Unit, assembles into 64-bit beats, and writes to SPM.

```systemverilog
module write_stream_engine #(
    parameter int SPM_ADDR_W    = 18,
    parameter int BEAT_BITS     = 64,
    parameter int FP32_W        = 32
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Descriptor from Compute Unit
    input  logic                 desc_valid_i,
    output logic                 desc_ready_o,
    input  logic [SPM_ADDR_W-1:0] desc_base_addr_i,    // word address
    input  logic [15:0]          desc_word_count_i,    // 32-bit words to transfer (16-bit for general DMA; STATE_WORDS_W=6 applies only to NodeHeader state_words field)

    // Data from Compute Unit (32-bit FP32 words)
    input  logic                 word_valid_i,
    output logic                 word_ready_o,
    input  logic [FP32_W-1:0]    word_data_i,

    // SPM write port (to SPM Arbiter)
    output logic                 spm_wr_valid_o,
    input  logic                 spm_wr_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_wr_addr_o,
    output logic [BEAT_BITS-1:0] spm_wr_data_o,
    output logic [7:0]           spm_wr_wstrb_o    // byte mask
);
```

**Operation:**
1. Accept descriptor when `desc_valid && desc_ready`.
2. Accept 32-bit words from Compute Unit.
3. Assemble 2 words into 1 64-bit beat: `{word_1, word_0}`.
4. Write beat to SPM when 2 words received.
5. If `word_count` is odd, last word writes with `wstrb = 8'b0000_1111` (only lower 4 bytes valid).

### 2.13 Address Generation Unit (AGU)

Simple linear address generator. Produces sequential word addresses from a descriptor.

```systemverilog
module agu #(
    parameter int SPM_ADDR_W = 18
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Start trigger
    input  logic                 start_i,
    input  logic [SPM_ADDR_W-1:0] base_addr_i,
    input  logic [15:0]          word_count_i,     // max 64K words per descriptor

    // Address output (to SPM Arbiter or addr FIFO)
    output logic                 addr_valid_o,
    input  logic                 addr_ready_i,
    output logic [SPM_ADDR_W-1:0] addr_o,
    output logic                 last_addr_o        // last address of this descriptor
);
```

**Operation:**
1. On `start`, latch `base_addr` and `word_count`.
2. Assert `addr_valid` with `addr = base_addr + i` for `i = 0 ... word_count-1`.
3. `last_addr = 1` on the final address.
4. Advance when `addr_valid && addr_ready`.

### 2.14 Writeback Controller

Sends NOTIFICATION to all consuming neighbors after compute completes.

```systemverilog
module writeback_controller #(
    parameter int NODE_ID_W     = 10,
    parameter int ADJ_COUNT_W   = 4,
    parameter int MAX_ADJ_COUNT = 8,
    parameter int X_CORD_W      = 6,
    parameter int Y_CORD_W      = 5
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Compute completion (from Compute Unit)
    input  logic                 done_valid_i,
    input  logic [NODE_ID_W-1:0] done_node_id_i,
    input  logic                 done_is_factor_i,

    // Adjacency info (from Metadata Scanner, latched at compute start)
    // MAX_ADJ_COUNT is defined in 04_PE_MICROARCHITECTURE.md as a parameter
    input  logic [ADJ_COUNT_W-1:0] adj_count_i,
    input  logic [MAX_ADJ_COUNT-1:0][NODE_ID_W-1:0] adj_neighbor_ids_i,
    input  logic [MAX_ADJ_COUNT-1:0][X_CORD_W-1:0]   adj_neighbor_xs_i,
    input  logic [MAX_ADJ_COUNT-1:0][Y_CORD_W-1:0]   adj_neighbor_ys_i,
    input  logic [MAX_ADJ_COUNT-1:0]                 adj_is_local_i,

    // To NoC Adapter (NOTIFICATION TX)
    // tx_notif_target_node_id is output for symmetry but not encoded into the
    // notification packet; the destination PE is selected by (target_x, target_y).
    output logic                 tx_notif_valid_o,
    input  logic                 tx_notif_ready_i,
    output logic [NODE_ID_W-1:0] tx_notif_source_node_id_o,
    output logic [NODE_ID_W-1:0] tx_notif_target_node_id_o,  // unused in packet payload
    output logic                 tx_notif_is_factor_o,
    output logic [X_CORD_W-1:0]   tx_notif_target_x_o,
    output logic [Y_CORD_W-1:0]   tx_notif_target_y_o,

    // Scoreboard reset trigger (to ScoreboardPrefetcher)
    output logic                 reset_valid_o,
    output logic [NODE_ID_W-1:0] reset_node_id_o,
    output logic                 reset_is_factor_o,

    // Done signal (to Phase Controller / Scheduler)
    output logic                 wb_done_o
);
```

### 2.15 NoC Adapter (noc_adapter)

Wraps `bsg_manycore_endpoint_standard` to bridge GBP internal interfaces with the manycore NoC.

```systemverilog
module noc_adapter #(
    parameter int DATA_WIDTH     = 32,
    parameter int ADDR_WIDTH     = 16,
    parameter int X_CORD_WIDTH   = 6,
    parameter int Y_CORD_WIDTH   = 5,
    parameter int NODE_ID_W      = 10,
    parameter int SCOREBOARD_DEPTH = 64,
    parameter int TXN_ID_W       = $clog2(SCOREBOARD_DEPTH),
    parameter int STATE_WORDS_W  = 6,
    parameter int GBP_BASE_ADDR  = 16'h1000  // resolved: 0x1000, no conflict with existing PE address map
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // ── Manycore link_sif (external) ──
    input  bsg_manycore_link_sif_s [S:N] link_sif_i,
    output bsg_manycore_link_sif_s [S:N] link_sif_o,

    // ── Manycore coordinates ──
    input  logic [X_CORD_WIDTH-1:0] my_x_i,
    input  logic [Y_CORD_WIDTH-1:0] my_y_i,

    // ── Internal TX: from Writeback Controller (NOTIFICATION) ──
    // Notification packet carries {is_factor, source_node_id}. The destination
    // PE is given by (target_x, target_y); there is no per-consumer target_node_id.
    input  logic                 tx_notif_valid_i,
    output logic                 tx_notif_ready_o,
    input  logic [NODE_ID_W-1:0] tx_notif_source_node_id_i,
    input  logic                 tx_notif_is_factor_i,
    input  logic [X_CORD_WIDTH-1:0] tx_notif_target_x_i,
    input  logic [Y_CORD_WIDTH-1:0] tx_notif_target_y_i,

    // ── Internal TX: from Pull Client (FETCH_REQUEST, 3-store sequence) ──
    input  logic                 tx_fetch_req_valid_i,
    output logic                 tx_fetch_req_ready_o,
    input  logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id_i,
    input  logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id_i,
    input  logic                 tx_fetch_req_is_factor_i,
    input  logic [X_CORD_WIDTH-1:0] tx_fetch_req_target_x_i,
    input  logic [Y_CORD_WIDTH-1:0] tx_fetch_req_target_y_i,
    input  logic [TXN_ID_W-1:0]  tx_fetch_req_txn_id_i,

    // ── Internal TX: from Pull Server (FETCH_RESPONSE) ──
    input  logic                     tx_fetch_resp_valid_i,
    output logic                     tx_fetch_resp_ready_o,
    input  logic [NODE_ID_W-1:0]     tx_fetch_resp_node_id_i,
    input  logic [NODE_ID_W-1:0]     tx_fetch_resp_consumer_node_id_i,
    input  logic                     tx_fetch_resp_is_factor_i,
    input  logic [STATE_WORDS_W-1:0] tx_fetch_resp_state_words_i,
    input  logic [DATA_WIDTH-1:0]    tx_fetch_resp_data_i,
    input  logic                     tx_fetch_resp_data_valid_i,
    input  logic                     tx_fetch_resp_last_i,
    input  logic [TXN_ID_W-1:0]      tx_fetch_resp_txn_id_i,

    // ── Internal RX: to ScoreboardPrefetcher (NOTIFICATION) ──
    // Notification payload is {is_factor, source_node_id}. There is no
    // per-consumer target_node_id in the packet; the destination PE is
    // identified by the packet's (x_cord, y_cord).
    output logic                 rx_notif_valid_o,
    input  logic                 rx_notif_ready_i,
    output logic [NODE_ID_W-1:0] rx_notif_source_node_id_o,
    output logic                 rx_notif_is_factor_o,
    output logic [X_CORD_WIDTH-1:0] rx_notif_source_x_o,
    output logic [Y_CORD_WIDTH-1:0] rx_notif_source_y_o,

    // ── Internal RX: to Pull Server (FETCH_REQUEST, 3-store latching) ──
    output logic                 rx_fetch_req_valid_o,
    input  logic                 rx_fetch_req_ready_i,
    output logic [NODE_ID_W-1:0] rx_fetch_req_target_node_id_o,
    output logic [NODE_ID_W-1:0] rx_fetch_req_consumer_node_id_o,
    output logic                 rx_fetch_req_is_factor_o,
    output logic [X_CORD_WIDTH-1:0] rx_fetch_req_src_x_o,
    output logic [Y_CORD_WIDTH-1:0] rx_fetch_req_src_y_o,
    output logic [TXN_ID_W-1:0]  rx_fetch_req_txn_id_o,

    // ── Internal RX: to Response Collector (FETCH_RESPONSE) ──
    // Sends 3 types of stores: metadata (MBX_RESP_META), data (MBX_RESP_DATA × N), done (MBX_RESP_DONE)
    // rx_fetch_resp_done_valid: asserted when MBX_RESP_DONE store is received
    output logic                     rx_fetch_resp_valid_o,
    input  logic                     rx_fetch_resp_ready_i,
    output logic                     rx_fetch_resp_is_factor_o,
    output logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words_o,
    output logic [DATA_WIDTH-1:0]    rx_fetch_resp_data_o,
    output logic                     rx_fetch_resp_data_valid_o,
    output logic                     rx_fetch_resp_last_o,
    output logic                     rx_fetch_resp_done_valid_o,
    output logic [TXN_ID_W-1:0]      rx_fetch_resp_txn_id_o,

    // ── Status ──
    output logic tx_busy_o
);
```

**Key implementation notes:**

1. Instantiates `bsg_manycore_endpoint_standard` internally
2. TX path: arbitrates between notif/fetch_req/fetch_resp → forms `bsg_manycore_packet_s` with `op=e_remote_store`
3. RX path: decodes `in_addr_o` against `GBP_BASE_ADDR` to route to appropriate module
4. Coordinate conversion: `(my_x_i, my_y_i)` used for `src_x_cord`/`src_y_cord` in outgoing packets
5. `tx_busy` indicates any TX channel is active (for debugging)
6. `tx_notif_target_node_id` is provided for symmetry but is not encoded into the notification packet.
7. `rx_fetch_resp_ready` is not present because the downstream `response_collector` implements a pass-through on the RX path (data is forwarded regardless of `remote_ready`). NoC RX backpressure for fetch responses is therefore not implemented in this version.

---

## 2.16 Subsystem Wrappers

The following wrappers group leaf modules defined in §2.1–2.15. They do not define new algorithms; they only hide intra-subsystem wiring and expose the minimal cross-subsystem interface required by `gbp_pe`.

### 2.16.1 Control Subsystem (`gbp_pe_control_subsystem`)

Encapsulates: `phase_controller`, `node_scheduler`, `metadata_scanner`, `reverse_index_lookup`.

```systemverilog
module gbp_pe_control_subsystem #(
    parameter int NUM_NODES = 1024,
    parameter int NODE_ID_W = 10,
    parameter int SPM_ADDR_W = 18,
    parameter int STATE_WORDS_W = 9,
    parameter int ADJ_COUNT_W = 4,
    parameter int DOF_W = 4,
    parameter int BEAT_BITS = 64,
    parameter int X_CORD_W = 6,
    parameter int Y_CORD_W = 5
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // From fetch subsystem: node readiness
    input  logic [NUM_NODES-1:0] node_ready_i,

    // From writeback controller: completion pulse
    input  logic wb_done_i,

    // To compute subsystem: command + metadata
    output logic                 cmd_valid_o,
    input  logic                 cmd_ready_i,
    output logic [NODE_ID_W-1:0] cmd_node_id_o,
    output logic                 cmd_is_factor_o,
    output logic [DOF_W-1:0]     cmd_dof_o,
    output logic [ADJ_COUNT_W-1:0] cmd_adj_count_o,
    output logic [STATE_WORDS_W-1:0] cmd_state_words_o,
    output logic [SPM_ADDR_W-1:0]  cmd_state_base_o,

    // Adjacency stream to fetch subsystem / accumulator
    output logic                 adj_valid_o,
    input  logic                 adj_ready_i,
    output logic [NODE_ID_W-1:0] adj_neighbor_id_o,
    output logic [X_CORD_W-1:0]  adj_neighbor_x_o,
    output logic [Y_CORD_W-1:0]  adj_neighbor_y_o,
    output logic [DOF_W-1:0]     adj_neighbor_dof_o,
    output logic                 adj_is_local_o,
    output logic                 adj_last_o,
    output logic [ADJ_COUNT_W-1:0] adj_edge_idx_o,
    output logic [NODE_ID_W-1:0] adj_current_node_id_o,

    // Scoreboard reset from writeback controller
    input  logic                 reset_valid_i,
    input  logic [NODE_ID_W-1:0] reset_node_id_i,
    input  logic                 reset_is_factor_i,

    // SPM read port 0: Metadata Scanner (Forward CSR — NodeHeader, AdjEntry)
    output logic                 spm_rd0_valid_o,
    input  logic                 spm_rd0_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd0_addr_o,
    input  logic [BEAT_BITS-1:0]  spm_rd0_data_i,

    // SPM read port 1: reverse_index_lookup (Reverse CSR — RevKeyHash, RevHeader, RevEntryArray)
    output logic                 spm_rd1_valid_o,
    input  logic                 spm_rd1_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd1_addr_o,
    input  logic [BEAT_BITS-1:0]  spm_rd1_data_i,

    // My coordinates (for local/remote classification — only used at graph compile time)
    input  logic [X_CORD_W-1:0] my_x_i,
    input  logic [Y_CORD_W-1:0] my_y_i
);
```

### 2.16.2 Compute Subsystem (`gbp_pe_compute_subsystem`)

Encapsulates: `compute_unit_wrapper` (see `08_NEW_COMPUTE_UNIT.md` v0.7), `read_stream_engine`, `write_stream_engine`, internal `agu` instances.

```systemverilog
module gbp_pe_compute_subsystem #(
    parameter int NODE_ID_W = 10,
    parameter int SPM_ADDR_W = 18,
    parameter int STATE_WORDS_W = 9,
    parameter int ADJ_COUNT_W = 4,
    parameter int DOF_W = 4,
    parameter int BEAT_BITS = 64,
    parameter int FP32_W = 32
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Command from control subsystem
    input  logic                 cmd_valid_i,
    output logic                 cmd_ready_o,
    input  logic [NODE_ID_W-1:0] cmd_node_id_i,
    input  logic                 cmd_is_factor_i,
    input  logic [DOF_W-1:0]     cmd_dof_i,
    input  logic [ADJ_COUNT_W-1:0] cmd_adj_count_i,
    input  logic [STATE_WORDS_W-1:0] cmd_state_words_i,
    input  logic [SPM_ADDR_W-1:0]  cmd_state_base_i,
    input  logic [FP32_W-1:0]      damping_factor_i,

    // Neighbor state from accumulator
    input  logic                 ns_valid_i,
    output logic                 ns_ready_o,
    input  logic [FP32_W-1:0]    ns_data_i,
    input  logic                 ns_last_i,

    // SPM read/write ports (to memory subsystem)
    // Two independent read ports: STATE and STAGING
    output logic                 spm_rd0_valid_o,
    input  logic                 spm_rd0_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd0_addr_o,
    input  logic [BEAT_BITS-1:0]  spm_rd0_data_i,

    output logic                 spm_rd1_valid_o,
    input  logic                 spm_rd1_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd1_addr_o,
    input  logic [BEAT_BITS-1:0]  spm_rd1_data_i,

    output logic                 spm_wr_valid_o,
    input  logic                 spm_wr_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_wr_addr_o,
    output logic [BEAT_BITS-1:0]  spm_wr_data_o,
    output logic [BEAT_BITS/8-1:0] spm_wr_wstrb_o,

    // Completion
    output logic                 done_valid_o,
    output logic [NODE_ID_W-1:0] done_node_id_o,
    output logic                 done_is_factor_o,

    // Batch completion (to fetch subsystem / response collector)
    output logic                 batch_done_o
);
```

**Note:** `read_stream_engine` and `write_stream_engine` each instantiate their own `agu` internally; the AGU is not exposed outside this subsystem.

### 2.16.3 Memory Subsystem (`gbp_pe_memory_subsystem`)

Encapsulates: `spm_arbiter`, `spm_bank_array`.

```systemverilog
module gbp_pe_memory_subsystem #(
    parameter int NUM_BANKS = 8,
    parameter int NUM_CLIENTS = 8,
    parameter int SPM_ADDR_W = 18,
    parameter int BEAT_BITS = 64,
    parameter int ROW_ADDR_W = 14
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Client read ports
    input  logic [NUM_CLIENTS-1:0]              rd_valid_i,
    output logic [NUM_CLIENTS-1:0]              rd_ready_o,
    input  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] rd_addr_i,
    output logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0]   rd_data_o,

    // Client write ports
    input  logic [NUM_CLIENTS-1:0]              wr_valid_i,
    output logic [NUM_CLIENTS-1:0]              wr_ready_o,
    input  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] wr_addr_i,
    input  logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0]   wr_data_i,
    input  logic [NUM_CLIENTS-1:0][BEAT_BITS/8-1:0] wr_wstrb_i
);
```

**Client index mapping (fixed):**

| Index | Direction | Source Subsystem | Purpose |
|-------|-----------|------------------|---------|
| 0 | read | control | Metadata Scanner META reads |
| 1 | read | control | reverse_index_lookup Reverse CSR reads |
| 2 | read | compute | Read Stream Engine 0 (STATE) |
| 3 | read | compute | Read Stream Engine 1 (STAGING) |
| 4 | write | compute | Write Stream Engine (STATE writeback) |
| 5 | read | fetch | Pull Server STATE reads |
| 6 | write | fetch | Response Collector STAGING writes |
| 7 | read+write | external | DMA / loader |

### 2.16.4 Fetch Subsystem (`gbp_pe_fetch_subsystem`)

Encapsulates: `scoreboard_prefetcher`, `pull_client`, `response_collector`.

```systemverilog
module gbp_pe_fetch_subsystem #(
    parameter int NODE_ID_W = 10,
    parameter int X_CORD_W = 6,
    parameter int Y_CORD_W = 5,
    parameter int ADJ_COUNT_W = 4,
    parameter int STATE_WORDS_W = 9,
    parameter int TXN_ID_W = 6,
    parameter int SCOREBOARD_DEPTH = 64,
    parameter int NUM_NODES = 1024,
    parameter int SPM_ADDR_W = 18,
    parameter int BEAT_BITS = 64
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Adjacency stream from control subsystem
    input  logic                 adj_valid_i,
    output logic                 adj_ready_o,
    input  logic [NODE_ID_W-1:0] adj_neighbor_id_i,
    input  logic [X_CORD_W-1:0]  adj_neighbor_x_i,
    input  logic [Y_CORD_W-1:0]  adj_neighbor_y_i,
    input  logic                 adj_is_local_i,
    input  logic                 adj_last_i,
    input  logic [ADJ_COUNT_W-1:0] adj_edge_idx_i,
    input  logic [NODE_ID_W-1:0] adj_current_node_id_i,

    // Node readiness to control subsystem
    output logic [NUM_NODES-1:0] node_ready_o,

    // Scoreboard reset from writeback controller
    input  logic                 reset_valid_i,
    input  logic [NODE_ID_W-1:0] reset_node_id_i,
    input  logic                 reset_is_factor_i,

    // NoC fetch request TX (to noc_adapter)
    output logic                 tx_fetch_req_valid_o,
    input  logic                 tx_fetch_req_ready_i,
    output logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id_o,
    output logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id_o,
    output logic                 tx_fetch_req_is_factor_o,
    output logic [X_CORD_W-1:0]  tx_fetch_req_target_x_o,
    output logic [Y_CORD_W-1:0]  tx_fetch_req_target_y_o,
    output logic [TXN_ID_W-1:0]  tx_fetch_req_txn_id_o,

    // NoC fetch response RX (from noc_adapter)
    input  logic                     rx_fetch_resp_valid_i,
    input  logic                     rx_fetch_resp_is_factor_i,
    input  logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words_i,
    input  logic [31:0]              rx_fetch_resp_data_i,
    input  logic                     rx_fetch_resp_data_valid_i,
    input  logic                     rx_fetch_resp_last_i,
    input  logic                     rx_fetch_resp_done_valid_i,
    input  logic [TXN_ID_W-1:0]      rx_fetch_resp_txn_id_i,

    // SPM read/write ports (to memory subsystem)
    output logic                 spm_rd_valid_o,
    input  logic                 spm_rd_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic [BEAT_BITS-1:0]  spm_rd_data_i,

    output logic                 spm_wr_valid_o,
    input  logic                 spm_wr_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_wr_addr_o,
    output logic [BEAT_BITS-1:0]  spm_wr_data_o,
    output logic [BEAT_BITS/8-1:0] spm_wr_wstrb_o,

    // Remote neighbor data to accumulator
    output logic                 remote_valid_o,
    input  logic                 remote_ready_i,
    output logic [31:0]          remote_data_o,
    output logic                 remote_last_o,

    // Batch completion (from compute subsystem)
    input  logic                 batch_done_i
);
```

---

## 3. State Machine Definitions

### 3.1 Phase Controller FSM

```
        ┌──────────────────┐
        │  FACTOR_PHASE    │◄─────────────────────┐
        └────────┬─────────┘                      │
                 │ no_schedulable_nodes            │
                 ▼                                 │
        ┌──────────────────┐                      │
        │  SWITCH_TO_VAR   │ (1 cycle)            │
        └────────┬─────────┘                      │
                 │ phase_switch_pulse              │
                 ▼                                 │
        ┌──────────────────┐                      │
        │  VARIABLE_PHASE  │                      │
        └────────┬─────────┘                      │
                 │ no_schedulable_nodes            │
                 ▼                                 │
        ┌──────────────────┐                      │
        │  SWITCH_TO_FAC   │ (1 cycle)            │
        └────────┬─────────┘                      │
                  │ phase_switch_pulse             │
                  └────────────────────────────────┘
```

No synchronization with neighbors. Switch is immediate.

### 3.2 ScoreboardPrefetcher Edge State Machine

Per-edge, 4 states encoded as 2-bit register:

```
IDLE (2'b00)
  │ rx_notif_valid && edge matches
  ▼
NOTIFIED (2'b01)
  │ prefetcher issues fetch_req && fetch_req_ready
  ▼
IN_FLIGHT (2'b10)
  │ complete_valid && edge matches
  ▼
READY (2'b11)
  │ reset_valid && node matches → reset to IDLE
  │ (local edges: always READY, never leave)
```

State encoding:

```systemverilog
localparam EDGE_IDLE     = 2'b00;
localparam EDGE_NOTIFIED = 2'b01;
localparam EDGE_IN_FLIGHT = 2'b10;
localparam EDGE_READY    = 2'b11;
```

Transition conditions:

```
IDLE → NOTIFIED:    adj_valid && !adj_is_local && matches edge  (remote edge, SCAN triggers)
IDLE → READY:       adj_valid && adj_is_local && matches edge   (local edge, skip lifecycle)
NOTIFIED → IN_FLIGHT: fetch_req_valid && fetch_req_ready && matches edge
IN_FLIGHT → READY:  complete_valid && matches edge (via txn_id)
READY → IDLE:       reset_valid && matches node (remote edges only; local edges remain READY)
```

### 3.3 Pull Server FSM

```
        ┌──────────┐
        │   IDLE   │◄──────────────────────┐
        └────┬─────┘                       │
             │ req_valid                    │
             ▼                              │
        ┌──────────┐                       │
        │ LOOKUP   │ (read NodeHeader)     │
        └────┬─────┘                       │
             │ spm_rd_data valid            │
             ▼                              │
        ┌──────────┐                       │
        │ SEND_DATA│ (stream data words)   │
        └────┬─────┘                       │
             │ all words sent               │
             └──────────────────────────────┘
```

Note: Metadata store (`MBX_RESP_META`) is inserted automatically by the NoC Adapter TX Response FSM, not by the Pull Server.

### 3.4 Response Collector FSM

```
        ┌──────────┐
        │   IDLE   │◄──────────────────────┐
        └────┬─────┘                       │
             │ rx_fetch_resp_valid          │
             │ (metadata or data)           │
             ▼                              │
        ┌──────────┐                       │
        │ RECEIVE  │ (collect data words)  │
        └────┬─────┘                       │
             │ rx_fetch_resp_done_valid     │
             │ (MBX_RESP_DONE received)     │
             ├──────────────────────────────┤
             │ emit complete_valid          │
             │ feed data to accumulator     │
             └──────────────────────────────┘
```

---

## 4. Key Timing Paths

### 4.1 Lookahead Fetch (hide latency)

```
Notification arrives at cycle T:
  T+0: ScoreboardPrefetcher records NOTIFIED
  T+1: Prefetcher issues FETCH_REQUEST (if scoreboard not full)
  T+2..T+K: NoC latency (request travel)
  T+K+1: Pull Server processes request
  T+K+2..T+K+N: Response data stores stream back
  T+K+N+1: Response Collector completes, marks READY

Node may not be scheduled until T+K+N+1, but fetch was issued at T+1.
If node was already scheduled and waiting, fetch latency is hidden.
```

### 4.2 Compute → Notify → Next Fetch

```
Node M computed at cycle C:
  C+0: Writeback Controller sends NOTIFICATION to all consumers
  C+1: Consumers' ScoreboardPrefetchers mark edges NOTIFIED
  C+2: Consumers issue FETCH_REQUEST (lookahead)
  ...  (consumer may not schedule M's neighbor until much later)
```

### 4.3 SPM Read Latency

```
SPM request at cycle T:
  T+0: spm_rd_valid asserted
  T+1: spm_rd_data available (fixed 1-cycle read latency)
```

---

## 5. Related Documents

| Document | Content |
|----------|---------|
| `00_WRITING_GUIDE.md` | How to write architecture documents: structure, granularity, style |
| `01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `02_SPM_AND_METADATA.md` | SPM layout, metadata structures, state block organization, STAGING design |
| `03_NOC_PROTOCOL.md` | NoC adaptation layer, mailbox encoding, manycore store-based messaging |
| `04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters, open items |
| `06_PE_CONTROL_FLOW.md` | PE-level control flow, pipeline stages, module handshakes |
| `verification/README.md` | Verification documentation index and test templates |
