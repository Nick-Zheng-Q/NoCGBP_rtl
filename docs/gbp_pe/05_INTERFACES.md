# Module Interfaces and Control Logic

## 1. Global Interface Conventions

All inter-module interfaces use valid/ready handshake:

```
sender:  xxx_valid  (output)
         xxx_data   (output)
receiver: xxx_ready (output)

transfer happens when xxx_valid && xxx_ready on rising edge.
```

Clock/reset: all modules share `clk`, `rst_n`.

---

## 2. Module Port Definitions

### 2.1 Phase Controller

```systemverilog
module phase_controller (
    input  logic clk,
    input  logic rst_n,

    // Node Scheduler feedback
    input  logic sched_valid,           // scheduler selected a node this cycle
    input  logic no_schedulable_nodes,  // current phase has no ready nodes

    // Writeback Controller feedback
    input  logic wb_done,               // node update complete, allow next schedule

    // Phase output
    output logic phase_factor_first,    // 1 = factor phase, 0 = variable phase
    output logic phase_switch_pulse     // single-cycle pulse on phase switch
);
```

### 2.2 Node Scheduler

```systemverilog
module node_scheduler (
    input  logic clk,
    input  logic rst_n,

    // Phase Controller
    input  logic phase_factor_first,

    // ScoreboardPrefetcher
    input  logic [NUM_NODES-1:0] node_ready,  // one bit per node

    // Phase coverage
    input  logic [NUM_NODES-1:0] visited_mask, // already computed this phase

    // Metadata Scanner backpressure
    input  logic sched_ready,         // Metadata Scanner accepted command

    // Output
    output logic                 sched_valid,   // selected a node
    output logic [NODE_ID_W-1:0] sched_node_id,
    output logic                 sched_is_factor,
    output logic                 no_schedulable_nodes
);
```

### 2.3 Metadata Scanner

```systemverilog
module metadata_scanner (
    input  logic clk,
    input  logic rst_n,

    // Command from scheduler
    input  logic                 cmd_valid,
    input  logic [NODE_ID_W-1:0] cmd_node_id,
    input  logic                 cmd_is_factor,
    output logic                 cmd_ready,

    // SPM read port (to SPM Arbiter)
    output logic                 spm_rd_valid,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr,
    input  logic                 spm_rd_ready,
    input  logic [BEAT_BITS-1:0] spm_rd_data,

    // AdjEntry output stream
    output logic                 adj_valid,
    input  logic                 adj_ready,
    output logic [NODE_ID_W-1:0] adj_neighbor_id,
    output logic [PE_ID_W-1:0]   adj_neighbor_pe_id,
    output logic                 adj_is_local,
    output logic                 adj_last,       // last adjacency entry
    output logic [ADJ_COUNT_W-1:0] adj_edge_idx, // current edge index within node

    // Node info output
    output logic                 info_valid,
    output logic [DOF_W-1:0]     info_dof,
    output logic [ADJ_COUNT_W-1:0] info_adj_count,
    output logic [SPM_ADDR_W-1:0] info_state_base,
    output logic [STATE_WORDS_W-1:0] info_state_words
);
```

### 2.4 ScoreboardPrefetcher

```systemverilog
module scoreboard_prefetcher #(
    parameter SCOREBOARD_DEPTH = 64,
    parameter TXN_ID_W = $clog2(SCOREBOARD_DEPTH)
)(
    input  logic clk,
    input  logic rst_n,

    // Notification ingress (from NoC Adapter RX)
    input  logic                 rx_notif_valid,
    output logic                 rx_notif_ready,
    input  logic [NODE_ID_W-1:0] rx_notif_source_node_id,
    input  logic [NODE_ID_W-1:0] rx_notif_target_node_id,
    input  logic                 rx_notif_is_factor,
    input  logic [PE_ID_W-1:0]   rx_notif_source_pe,

    // Fetch request output (to Pull Client, then to NoC Adapter TX)
    output logic                 fetch_req_valid,
    input  logic                 fetch_req_ready,
    output logic [NODE_ID_W-1:0] fetch_req_target_node_id,
    output logic [NODE_ID_W-1:0] fetch_req_consumer_node_id,
    output logic                 fetch_req_is_factor,
    output logic [PE_ID_W-1:0]   fetch_req_target_pe,
    output logic [TXN_ID_W-1:0]  fetch_req_txn_id,       // edge_index, used for response matching

    // Fetch response completion (from Response Collector)
    input  logic                 complete_valid,
    input  logic [TXN_ID_W-1:0]  complete_txn_id,        // direct edge match via txn_id

    // STAGING Allocator coordination (to/from Response Collector)
    output logic                 staging_reserve_valid,
    output logic [STATE_WORDS_W-1:0] staging_reserve_words,
    input  logic                 staging_reserve_ready,
    input  logic                 staging_batch_closed,

    // Node readiness output (to Node Scheduler)
    output logic [NUM_NODES-1:0] node_ready,

    // Edge classification from Metadata Scanner (adj stream → local edge READY)
    input  logic                 adj_valid,
    input  logic [NODE_ID_W-1:0] adj_neighbor_id,
    input  logic [PE_ID_W-1:0]   adj_neighbor_pe_id,
    input  logic                 adj_is_local,
    input  logic                 adj_last,
    input  logic [ADJ_COUNT_W-1:0] adj_edge_idx,          // current edge index within node

    // Post-compute reset (from Writeback Controller)
    input  logic                 reset_valid,
    input  logic [NODE_ID_W-1:0] reset_node_id,
    input  logic                 reset_is_factor,

    // Scoreboard status
    output logic [$clog2(SCOREBOARD_DEPTH):0] scoreboard_occupancy,
    output logic scoreboard_full
);
```

### 2.5 Pull Client

Sends FETCH_REQUEST to producer PE via NoC Adapter. Issues 3 stores: {is_factor, consumer_id}, {target_id}, {txn_id}.

```systemverilog
module pull_client #(
    parameter SCOREBOARD_DEPTH = 64,
    parameter TXN_ID_W = $clog2(SCOREBOARD_DEPTH)
)(
    input  logic clk,
    input  logic rst_n,

    // Request from ScoreboardPrefetcher
    input  logic                 req_valid,
    output logic                 req_ready,
    input  logic [NODE_ID_W-1:0] req_target_node_id,
    input  logic [NODE_ID_W-1:0] req_consumer_node_id,
    input  logic                 req_is_factor,
    input  logic [PE_ID_W-1:0]   req_target_pe,
    input  logic [TXN_ID_W-1:0]  req_txn_id,

    // To NoC Adapter (FETCH_REQUEST TX, 3-store sequence)
    output logic                 tx_fetch_req_valid,
    input  logic                 tx_fetch_req_ready,
    output logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id,
    output logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id,
    output logic                 tx_fetch_req_is_factor,
    output logic [PE_ID_W-1:0]   tx_fetch_req_target_pe,
    output logic [TXN_ID_W-1:0]  tx_fetch_req_txn_id
);
```

### 2.6 Pull Server

Responds to FETCH_REQUEST from other PEs, sends FETCH_RESPONSE via NoC Adapter. Echoes txn_id from request in response done store.

```systemverilog
module pull_server #(
    parameter SCOREBOARD_DEPTH = 64,
    parameter TXN_ID_W = $clog2(SCOREBOARD_DEPTH)
)(
    input  logic clk,
    input  logic rst_n,

    // Fetch request ingress (from NoC Adapter RX)
    input  logic                 req_valid,
    output logic                 req_ready,
    input  logic [NODE_ID_W-1:0] req_target_node_id,
    input  logic [NODE_ID_W-1:0] req_consumer_node_id,
    input  logic                 req_is_factor,
    input  logic [PE_ID_W-1:0]   req_fetch_src_pe,
    input  logic [TXN_ID_W-1:0]  req_txn_id,           // echoed from FETCH_REQUEST store 3

    // SPM read port (to SPM Arbiter)
    output logic                 spm_rd_valid,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr,
    input  logic                 spm_rd_ready,
    input  logic [BEAT_BITS-1:0] spm_rd_data,

    // To NoC Adapter (FETCH_RESPONSE TX)
    // Sends 3 types of stores: metadata (MBX_RESP_META), data (MBX_RESP_DATA × state_words), done (MBX_RESP_DONE)
    output logic                     tx_fetch_resp_valid,
    input  logic                     tx_fetch_resp_ready,
    output logic [NODE_ID_W-1:0]     tx_fetch_resp_node_id,
    output logic [NODE_ID_W-1:0]     tx_fetch_resp_consumer_node_id,
    output logic                     tx_fetch_resp_is_factor,
    output logic [STATE_WORDS_W-1:0] tx_fetch_resp_state_words,
    output logic [DATA_WIDTH-1:0]    tx_fetch_resp_data,
    output logic                     tx_fetch_resp_data_valid,
    output logic                     tx_fetch_resp_last,
    output logic [TXN_ID_W-1:0]      tx_fetch_resp_txn_id  // echoed in done store
);
```

### 2.7 Response Collector

Receives FETCH_RESPONSE data from NoC Adapter, writes to STAGING region, and notifies ScoreboardPrefetcher.

```systemverilog
module response_collector #(
    parameter STATE_WORDS_W    = 4,
    parameter SCOREBOARD_DEPTH = 64,
    parameter TXN_ID_W         = $clog2(SCOREBOARD_DEPTH),
    parameter SPM_ADDR_W       = 16,
    parameter DATA_WIDTH       = 32
)(
    input  logic clk,
    input  logic rst_n,

    // Fetch response ingress (from NoC Adapter RX)
    // Receives 3 types of stores: metadata (MBX_RESP_META), data (MBX_RESP_DATA × N), done (MBX_RESP_DONE)
    input  logic                     rx_fetch_resp_valid,
    output logic                     rx_fetch_resp_ready,
    input  logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words,
    input  logic [DATA_WIDTH-1:0]    rx_fetch_resp_data,
    input  logic                     rx_fetch_resp_data_valid,
    input  logic                     rx_fetch_resp_last,
    input  logic                     rx_fetch_resp_done_valid,  // MBX_RESP_DONE store received
    input  logic [TXN_ID_W-1:0]      rx_fetch_resp_txn_id,      // from done store payload

    // STAGING write port (to SPM Arbiter)
    output logic                 staging_wr_valid,
    input  logic                 staging_wr_ready,
    output logic [SPM_ADDR_W-1:0] staging_wr_addr,
    output logic [DATA_WIDTH-1:0] staging_wr_data,

    // To ScoreboardPrefetcher (completion notification)
    output logic                 complete_valid,
    output logic [TXN_ID_W-1:0] complete_txn_id,      // edge_index for edge matching

    // STAGING Allocator coordination (internal sub-component)
    // Reservation interface (from ScoreboardPrefetcher)
    input  logic                 staging_reserve_valid,
    input  logic [STATE_WORDS_W-1:0] staging_reserve_words,
    output logic                 staging_reserve_ready,

    // Batch control
    output logic                 staging_batch_closed,
    input  logic                 staging_batch_done
);
```

### 2.8 Neighbor State Accumulator

```systemverilog
module neighbor_state_accumulator (
    input  logic clk,
    input  logic rst_n,

    // Local SPM read
    input  logic                 local_valid,
    output logic                 local_ready,
    input  logic [BEAT_BITS-1:0] local_data,
    input  logic                 local_last,

    // Remote response (from Response Collector)
    input  logic                 remote_valid,
    output logic                 remote_ready,
    input  logic [FP32_W-1:0]    remote_data,
    input  logic                 remote_last,

    // To Compute Unit
    output logic                 out_valid,
    input  logic                 out_ready,
    output logic [FP32_W-1:0]    out_data,
    output logic                 out_last,

    // Pipeline control
    output logic                 accumulator_done  // all neighbors consumed, triggers COMPUTE
);
```

### 2.9 SPM Arbiter

```systemverilog
module spm_arbiter #(
    parameter NUM_BANKS = 8,
    parameter NUM_CLIENTS = 7  // MetadataScanner, CU_state_rd, CU_staging_rd, CU_wb, PullServer, RespCollector, DMA
)(
    input  logic clk,
    input  logic rst_n,

    // Client read ports
    input  logic [NUM_CLIENTS-1:0]              rd_valid,
    output logic [NUM_CLIENTS-1:0]              rd_ready,
    input  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] rd_addr,
    output logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0]   rd_data,

    // Client write ports
    input  logic [NUM_CLIENTS-1:0]              wr_valid,
    output logic [NUM_CLIENTS-1:0]              wr_ready,
    input  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] wr_addr,
    input  logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0]   wr_data,

    // Bank ports
    output logic [NUM_BANKS-1:0]                bank_rd_en,
    output logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0]  bank_rd_addr,
    input  logic [NUM_BANKS-1:0][BEAT_BITS-1:0]   bank_rd_data,

    output logic [NUM_BANKS-1:0]                bank_wr_en,
    output logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0]  bank_wr_addr,
    output logic [NUM_BANKS-1:0][BEAT_BITS-1:0]   bank_wr_data
);
```

### 2.10 Compute Unit (PEComputeEngine)

```systemverilog
module compute_unit (
    input  logic clk,
    input  logic rst_n,

    // Command (from Node Scheduler + Metadata Scanner, both valid when accumulator_done)
    input  logic                 cmd_valid,
    output logic                 cmd_ready,
    input  logic [NODE_ID_W-1:0] cmd_node_id,
    input  logic                 cmd_is_factor,
    input  logic [DOF_W-1:0]     cmd_dof,
    input  logic [ADJ_COUNT_W-1:0] cmd_adj_count,
    input  logic [SPM_ADDR_W-1:0] cmd_state_base,
    input  logic [STATE_WORDS_W-1:0] cmd_state_words,

    // Neighbor state input (from Accumulator)
    input  logic                 ns_valid,
    output logic                 ns_ready,
    input  logic [FP32_W-1:0]    ns_data,
    input  logic                 ns_last,

    // Own state read (from SPM via Arbiter)
    output logic                 self_rd_valid,
    input  logic                 self_rd_ready,
    output logic [SPM_ADDR_W-1:0] self_rd_addr,
    input  logic [BEAT_BITS-1:0] self_rd_data,

    // State writeback (to SPM via Arbiter)
    output logic                 wb_valid,
    input  logic                 wb_ready,
    output logic [SPM_ADDR_W-1:0] wb_addr,
    output logic [BEAT_BITS-1:0] wb_data,

    // Staging buffer (for MAT_INV, internal)
    // Not exposed as external port — internal SRAM

    // Completion
    output logic                 done_valid,
    output logic [NODE_ID_W-1:0] done_node_id,
    output logic                 done_is_factor,

    // Batch completion (for batched staging mode)
    output logic                 batch_done  // one batch compute complete, trigger STAGING reset
);
```

### 2.11 Writeback Controller

Sends NOTIFICATION to all consuming neighbors after compute completes.

```systemverilog
module writeback_controller (
    input  logic clk,
    input  logic rst_n,

    // Compute completion (from Compute Unit)
    input  logic                 done_valid,
    input  logic [NODE_ID_W-1:0] done_node_id,
    input  logic                 done_is_factor,

    // Adjacency info (from Metadata Scanner, latched at compute start)
    // MAX_ADJ_COUNT is defined in 04_PE_MICROARCHITECTURE.md as a parameter
    input  logic [ADJ_COUNT_W-1:0] adj_count,
    input  logic [MAX_ADJ_COUNT-1:0][NODE_ID_W-1:0] adj_neighbor_ids,
    input  logic [MAX_ADJ_COUNT-1:0][PE_ID_W-1:0]   adj_neighbor_pes,
    input  logic [MAX_ADJ_COUNT-1:0]                 adj_is_local,

    // To NoC Adapter (NOTIFICATION TX)
    output logic                 tx_notif_valid,
    input  logic                 tx_notif_ready,
    output logic [NODE_ID_W-1:0] tx_notif_source_node_id,
    output logic [NODE_ID_W-1:0] tx_notif_target_node_id,
    output logic                 tx_notif_is_factor,
    output logic [PE_ID_W-1:0]   tx_notif_target_pe,

    // Scoreboard reset trigger (to ScoreboardPrefetcher)
    output logic                 reset_valid,
    output logic [NODE_ID_W-1:0] reset_node_id,
    output logic                 reset_is_factor,

    // Done signal (to Phase Controller / Scheduler)
    output logic                 wb_done
);
```

### 2.12 NoC Adapter (noc_adapter)

Wraps `bsg_manycore_endpoint_standard` to bridge GBP internal interfaces with the manycore NoC.

```systemverilog
module noc_adapter #(
    parameter DATA_WIDTH     = 32,
    parameter ADDR_WIDTH     = 16,
    parameter X_CORD_WIDTH   = 6,
    parameter Y_CORD_WIDTH   = 5,
    parameter NODE_ID_W      = 8,
    parameter PE_ID_W        = 4,
    parameter SCOREBOARD_DEPTH = 64,
    parameter TXN_ID_W       = $clog2(SCOREBOARD_DEPTH),
    parameter GBP_BASE_ADDR  = 16'h1000
)(
    input  logic clk,
    input  logic rst_n,

    // ── Manycore link_sif (external) ──
    input  bsg_manycore_link_sif_s [S:N] link_sif_i,
    output bsg_manycore_link_sif_s [S:N] link_sif_o,

    // ── Manycore coordinates ──
    input  logic [X_CORD_WIDTH-1:0] my_x_i,
    input  logic [Y_CORD_WIDTH-1:0] my_y_i,

    // ── Internal TX: from Writeback Controller (NOTIFICATION) ──
    input  logic                 tx_notif_valid,
    output logic                 tx_notif_ready,
    input  logic [NODE_ID_W-1:0] tx_notif_source_node_id,
    input  logic [NODE_ID_W-1:0] tx_notif_target_node_id,
    input  logic                 tx_notif_is_factor,
    input  logic [PE_ID_W-1:0]   tx_notif_target_pe,

    // ── Internal TX: from Pull Client (FETCH_REQUEST, 3-store sequence) ──
    input  logic                 tx_fetch_req_valid,
    output logic                 tx_fetch_req_ready,
    input  logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id,
    input  logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id,
    input  logic                 tx_fetch_req_is_factor,
    input  logic [PE_ID_W-1:0]   tx_fetch_req_target_pe,
    input  logic [TXN_ID_W-1:0]  tx_fetch_req_txn_id,

    // ── Internal TX: from Pull Server (FETCH_RESPONSE) ──
    input  logic                     tx_fetch_resp_valid,
    output logic                     tx_fetch_resp_ready,
    input  logic [NODE_ID_W-1:0]     tx_fetch_resp_node_id,
    input  logic [NODE_ID_W-1:0]     tx_fetch_resp_consumer_node_id,
    input  logic                     tx_fetch_resp_is_factor,
    input  logic [STATE_WORDS_W-1:0] tx_fetch_resp_state_words,
    input  logic [DATA_WIDTH-1:0]    tx_fetch_resp_data,
    input  logic                     tx_fetch_resp_data_valid,
    input  logic                     tx_fetch_resp_last,
    input  logic [TXN_ID_W-1:0]      tx_fetch_resp_txn_id,

    // ── Internal RX: to ScoreboardPrefetcher (NOTIFICATION) ──
    output logic                 rx_notif_valid,
    input  logic                 rx_notif_ready,
    output logic [NODE_ID_W-1:0] rx_notif_source_node_id,
    output logic [NODE_ID_W-1:0] rx_notif_target_node_id,
    output logic                 rx_notif_is_factor,
    output logic [PE_ID_W-1:0]   rx_notif_source_pe,

    // ── Internal RX: to Pull Server (FETCH_REQUEST, 3-store latching) ──
    output logic                 rx_fetch_req_valid,
    input  logic                 rx_fetch_req_ready,
    output logic [NODE_ID_W-1:0] rx_fetch_req_target_node_id,
    output logic [NODE_ID_W-1:0] rx_fetch_req_consumer_node_id,
    output logic                 rx_fetch_req_is_factor,
    output logic [PE_ID_W-1:0]   rx_fetch_req_src_pe,
    output logic [TXN_ID_W-1:0]  rx_fetch_req_txn_id,

    // ── Internal RX: to Response Collector (FETCH_RESPONSE) ──
    // Sends 3 types of stores: metadata (MBX_RESP_META), data (MBX_RESP_DATA × N), done (MBX_RESP_DONE)
    // rx_fetch_resp_done_valid: asserted when MBX_RESP_DONE store is received
    output logic                     rx_fetch_resp_valid,
    input  logic                     rx_fetch_resp_ready,
    output logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words,
    output logic [DATA_WIDTH-1:0]    rx_fetch_resp_data,
    output logic                     rx_fetch_resp_data_valid,
    output logic                     rx_fetch_resp_last,
    output logic                     rx_fetch_resp_done_valid,
    output logic [TXN_ID_W-1:0]      rx_fetch_resp_txn_id,

    // ── Status ──
    output logic tx_busy
);
```

**Key implementation notes:**

1. Instantiates `bsg_manycore_endpoint_standard` internally
2. TX path: arbitrates between notif/fetch_req/fetch_resp → forms `bsg_manycore_packet_s` with `op=e_remote_store`
3. RX path: decodes `in_addr_o` against `GBP_BASE_ADDR` to route to appropriate module
4. Coordinate conversion: `(my_x_i, my_y_i)` used for `src_x_cord`/`src_y_cord` in outgoing packets
5. `tx_busy` indicates any TX channel is active (for debugging)

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
IDLE → NOTIFIED:    rx_notif_valid && matches edge
IDLE → READY:       adj_valid && adj_is_local && matches edge  (local edge, skip lifecycle)
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
        │ SEND_HDR │ (send header)         │
        └────┬─────┘                       │
             │ tx_fetch_resp_ready          │
             ▼                              │
        ┌──────────┐                       │
        │ SEND_DATA│ (stream data words)   │
        └────┬─────┘                       │
             │ all words sent               │
             └──────────────────────────────┘
```

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
