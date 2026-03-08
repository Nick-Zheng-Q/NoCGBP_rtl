package gbp_pkg;

  // ---- parameter widths (建议在顶层或更大 package 统一定义/override) ----
  parameter int unsigned NB = 8;  // bank count
  parameter int unsigned SPM_ADDR_W = 20;  // byte address width for 1MB
  parameter int unsigned ROW_ADDR_W = 12;  // e.g., 32B row and 128KB/bank
  parameter int unsigned WORD_OFF_W = 3;  // 8 words/row
  parameter int unsigned BYTE_OFF_W = 2;  // byte offset in 32b word

  parameter int unsigned BEAT_BYTES = 32;  // one SPM row per beat
  parameter int unsigned TXN_ID_W = 8;  // transaction id
  parameter int unsigned NODE_ID_W = 13;  // up to 8192 nodes/object ids
  parameter int unsigned EDGE_ID_W = 16;  // configurable
  parameter int unsigned XFER_BYTES_W = 16;  // transfer length in bytes
  parameter int unsigned STEP_BYTES_W = 8;  // stride per beat
  parameter int unsigned DATA_ELS_DEPTH = 16;  // depth of data fifo
  parameter int unsigned ADDR_ELS_DEPTH = 16;  // depth of address fifo

  // -------------------------
  // Derived params (do not override)
  // -------------------------
  localparam int unsigned BANK_ID_W = $clog2(NB);
  localparam int unsigned BEAT_BITS = BEAT_BYTES * 8;
  localparam int unsigned BEAT_MAX = (XFER_BYTES_W > $clog2(
      BEAT_BYTES
  )) ? XFER_BYTES_W - $clog2(
      BEAT_BYTES
  ) : 1;
  localparam int unsigned WSTRB_W = BEAT_BYTES;  // byte mask width per beat

  // ---- ingress/MMIO contract constants ----
  localparam int unsigned GBP_INGRESS_BANK_W = 3;
  localparam int unsigned GBP_INGRESS_ROW_BYTES_LG = BYTE_OFF_W + WORD_OFF_W;

  // Ingress address classes used by gbp_pe endpoint bridge.
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_MMIO_BANK_B0 = 3'd0;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_FWD_BANK_B1 = 3'd1;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_FWD_BANK_B2 = 3'd2;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_FWD_BANK_B3 = 3'd3;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_PAYLOAD_BANK_B4 = 3'd4;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_PAYLOAD_BANK_B5 = 3'd5;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_PAYLOAD_BANK_B6 = 3'd6;
  localparam logic [GBP_INGRESS_BANK_W-1:0] GBP_INGRESS_PAYLOAD_BANK_B7 = 3'd7;

  // MMIO field map for bank B0 (addr[4:2]).
  localparam logic [2:0] GBP_MMIO_FIELD_Q_BASE_ADDR = 3'd0;
  localparam logic [2:0] GBP_MMIO_FIELD_Q_DEPTH = 3'd1;
  localparam logic [2:0] GBP_MMIO_FIELD_Q_HEAD = 3'd2;
  localparam logic [2:0] GBP_MMIO_FIELD_Q_TAIL = 3'd3;
  localparam logic [2:0] GBP_MMIO_FIELD_Q_CREDIT = 3'd4;
  localparam logic [2:0] GBP_MMIO_FIELD_Q_EPOCH_DOORBELL = 3'd5;

  // Read request byte-count contract for full-beat SPM reads.
  localparam logic [XFER_BYTES_W-1:0] SPM_RD_REQ_BYTES_FULL_BEAT = XFER_BYTES_W'(BEAT_BYTES);

  // ---- enums (可选，但强烈建议) ----
  typedef enum logic {
    OP_READ  = 1'b0,
    OP_WRITE = 1'b1
  } op_e;

  typedef enum logic [1:0] {
    WSTRB_FULL  = 2'b00,
    WSTRB_MASK  = 2'b01,
    WSTRB_CONST = 2'b10
  } wstrb_mode_e;

  typedef enum logic [1:0] {
    STREAM_META = 2'b00,
    STREAM_VEC = 2'b01,
    STREAM_MESSAGE = 2'b10
  } stream_type_e;
  // 如果 operand_id 有明确语义，也可以枚举；否则保持 logic [3:0]
  // typedef enum logic [3:0] { ... } operand_e;

  // ---- packed descriptor payload ----
  typedef struct packed {
    op_e                     op;               // 2
    logic [TXN_ID_W-1:0]     txn_id;           // TXN_ID_W
    logic                    start;            // 1
    logic [SPM_ADDR_W-1:0]   base_addr;        // SPM_ADDR_W (byte addr)
    logic [XFER_BYTES_W-1:0] xfer_bytes;       // XFER_BYTES_W
    logic [STEP_BYTES_W-1:0] addr_step_bytes;  // STEP_BYTES_W
    logic [3:0]              operand_id;       // 4
    wstrb_mode_e             wstrb_mode;       // 2
    logic                    dim;              // 1 
    logic [11:0]             y_count;          // 12
    logic [15:0]             y_stride_bytes;   // 16
    logic                    addr_src;         // 1
  } desc_t;

endpackage
