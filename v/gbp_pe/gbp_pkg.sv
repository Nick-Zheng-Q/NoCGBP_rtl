package gbp_pkg;

  // ============================================================
  // New Architecture Parameters (June 2026 unified spec)
  // Source of truth for all GBP PE modules.
  // ============================================================

  // -- SPM physical parameters --
  parameter int unsigned SPM_BYTES_PER_PE = 1048576;   // 1MB per PE
  parameter int unsigned NUM_BANKS        = 8;         // 8 banks
  parameter int unsigned WORD_BYTES       = 4;         // 32-bit word
  parameter int unsigned BEAT_BYTES       = 8;         // 64-bit SPM beat (per spec)
  parameter int unsigned BEAT_BITS        = BEAT_BYTES * 8; // 64

  // -- Address widths (word address) --
  parameter int unsigned SPM_ADDR_W = $clog2(SPM_BYTES_PER_PE / WORD_BYTES); // 18
  parameter int unsigned ROW_ADDR_W = $clog2(SPM_BYTES_PER_PE / NUM_BANKS / BEAT_BYTES); // 14
  parameter int unsigned BANK_ID_W  = $clog2(NUM_BANKS); // 3

  // -- Graph / node parameters --
  parameter int unsigned NUM_NODES_PER_PE = 1024;
  parameter int unsigned NODE_ID_W        = $clog2(NUM_NODES_PER_PE); // 10
  parameter int unsigned MAX_ADJ_COUNT    = 8;
  parameter int unsigned ADJ_COUNT_W      = 4;  // >= $clog2(MAX_ADJ_COUNT)
  parameter int unsigned DOF_W            = 4;
  parameter int unsigned STATE_WORDS_W    = 9;  // max 512 words per state block

  // -- Data width --
  parameter int unsigned FP32_W    = 32;
  parameter int unsigned WSTRB_W   = BEAT_BYTES; // 8 (byte mask per beat)
  parameter int unsigned NOC_DATA_W = 32;

  // -- Reverse CSR parameters --
  parameter int unsigned REV_ID_W    = NODE_ID_W;  // max unique neighbors = NUM_NODES
  parameter int unsigned REV_LEN_W   = NODE_ID_W;  // max affected nodes per neighbor
  parameter int unsigned EDGE_IDX_W  = NODE_ID_W;  // max edges in FwdEdgeArray
  parameter int unsigned REV_KEY_ENTRIES = 2048;   // hash table size (power of 2)
  parameter int unsigned REV_MAX_PROBE   = 32;     // max linear probes

  // -- Fetch / scoreboard parameters --
  parameter int unsigned SCOREBOARD_DEPTH = 64;
  parameter int unsigned TXN_ID_W         = $clog2(SCOREBOARD_DEPTH); // 6
  parameter int unsigned FETCH_SERVER_MAX_PER_CYCLE = 1;

  // -- NoC coordinate parameters (overridden at top level) --
  parameter int unsigned X_CORD_W = 6;
  parameter int unsigned Y_CORD_W = 5;

  // -- Mailbox base address --
  parameter int unsigned GBP_BASE_ADDR = 32'h0000_1000;

  // ============================================================
  // New descriptor type (replaces legacy desc_t)
  // ============================================================
  typedef struct packed {
      logic [SPM_ADDR_W-1:0]    base_addr;     // word address (STATE or STAGING)
      logic [15:0]              word_count;    // 32-bit words to transfer
      logic                     is_staging;    // 1=STAGING, 0=STATE
  } stream_descriptor_t;

  // ============================================================
  // Reverse CSR types (SPM-resident)
  // ============================================================
  typedef struct packed {
      logic                    valid;
      logic [NODE_ID_W-1:0]    key;      // neighbor global ID
      logic [REV_ID_W-1:0]     rev_id;   // index into RevHeader
  } rev_key_entry_t;

  typedef struct packed {
      logic [REV_LEN_W-1:0]       rev_len;   // number of affected S1 nodes
      logic [SPM_ADDR_W-1:0]      rev_base;  // word addr of first RevEntry
  } rev_header_t;

  typedef struct packed {
      logic [NODE_ID_W-1:0]    local_id;     // S1 local node index (consumer)
      logic [EDGE_IDX_W-1:0]   fwd_edge_idx; // index into FwdEdgeArray
  } rev_entry_t;

  // ============================================================
  // DEPRECATED: Legacy parameters and types
  // These are kept temporarily for backward compatibility with
  // modules that have not yet been rewritten (agu, read_stream_engine,
  // write_stream_engine, etc.). They will be removed once all
  // legacy modules are deleted or updated.
  // ============================================================

  // Legacy alias
  parameter int unsigned NB = NUM_BANKS;

  // Byte/word offset widths (for bank interleave math)
  parameter int unsigned BYTE_OFF_W = $clog2(WORD_BYTES); // 2
  parameter int unsigned WORD_OFF_W = $clog2(BEAT_BYTES / WORD_BYTES); // 1 (2 words per 64-bit beat)

  // Legacy derived
  localparam int unsigned BEAT_MAX = (16 > $clog2(BEAT_BYTES))
      ? 16 - $clog2(BEAT_BYTES)
      : 1;

  // Legacy transfer/stride widths
  parameter int unsigned XFER_BYTES_W = 16;
  parameter int unsigned STEP_BYTES_W = 16;

  // Legacy FIFO depths
  parameter int unsigned DATA_ELS_DEPTH = 16;
  parameter int unsigned ADDR_ELS_DEPTH = 16;

  // Legacy enums
  typedef enum logic { OP_READ = 1'b0, OP_WRITE = 1'b1 } op_e;
  typedef enum logic [1:0] {
    WSTRB_FULL  = 2'b00,
    WSTRB_MASK  = 2'b01,
    WSTRB_CONST = 2'b10
  } wstrb_mode_e;
  typedef enum logic [1:0] {
    STREAM_META = 2'b00,
    STREAM_VEC  = 2'b01,
    STREAM_MESSAGE = 2'b10
  } stream_type_e;

  // Legacy descriptor (used by old agu/read_stream_engine/write_stream_engine)
  // NOTE: base_addr is now WORD ADDRESS (18 bits), not byte address (20 bits)
  typedef struct packed {
    op_e                     op;
    logic [TXN_ID_W-1:0]     txn_id;
    logic                    start;
    logic [SPM_ADDR_W-1:0]   base_addr;
    logic [XFER_BYTES_W-1:0] xfer_bytes;
    logic [STEP_BYTES_W-1:0] addr_step_bytes;
    logic [3:0]              operand_id;
    wstrb_mode_e             wstrb_mode;
    logic                    dim;
    logic [11:0]             y_count;
    logic [15:0]             y_stride_bytes;
    logic                    addr_src;
  } desc_t;

  // Legacy ingress constants (used by old gbp_pe/gbp_pe_noc_bridge)
  parameter int unsigned GBP_INGRESS_BANK_W = 3;
  parameter int unsigned GBP_INGRESS_ROW_BYTES_LG = BYTE_OFF_W + WORD_OFF_W;

  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_MMIO_BANK_B0 = 3'd0;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_FWD_BANK_B1 = 3'd1;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_FWD_BANK_B2 = 3'd2;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_FWD_BANK_B3 = 3'd3;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_PAYLOAD_BANK_B4 = 3'd4;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_PAYLOAD_BANK_B5 = 3'd5;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_PAYLOAD_BANK_B6 = 3'd6;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_PAYLOAD_BANK_B7 = 3'd7;

  localparam logic [2:0] GBP_MMIO_FIELD_Q_BASE_ADDR = 3'd0;
  localparam logic [2:0] GBP_MMIO_FIELD_Q_DEPTH = 3'd1;
  localparam logic [2:0] GBP_MMIO_FIELD_Q_HEAD = 3'd2;
  localparam logic [2:0] GBP_MMIO_FIELD_Q_TAIL = 3'd3;
  localparam logic [2:0] GBP_MMIO_FIELD_Q_CREDIT = 3'd4;
  localparam logic [2:0] GBP_MMIO_FIELD_Q_EPOCH_DOORBELL = 3'd5;

  localparam logic [XFER_BYTES_W-1:0] SPM_RD_REQ_BYTES_FULL_BEAT = XFER_BYTES_W'(BEAT_BYTES);

endpackage
