// pull_server.sv
// Responds to FETCH_REQUEST from other PEs.
// Reads NodeHeader from SPM, then streams STATE data as FETCH_RESPONSE.
// FSM: IDLE → LOOKUP → SEND_DATA → SEND_DONE

module pull_server
  import gbp_pkg::*;
#(
    parameter int NODE_ID_W     = gbp_pkg::NODE_ID_W
    , parameter int DOF_W       = gbp_pkg::DOF_W
    , parameter int ADJ_COUNT_W = gbp_pkg::ADJ_COUNT_W
    , parameter int STATE_WORDS_W = gbp_pkg::STATE_WORDS_W
    , parameter int SPM_ADDR_W  = gbp_pkg::SPM_ADDR_W
    , parameter int BEAT_BITS   = gbp_pkg::BEAT_BITS
    , parameter int X_CORD_W    = gbp_pkg::X_CORD_W
    , parameter int Y_CORD_W    = gbp_pkg::Y_CORD_W
    , parameter int TXN_ID_W    = gbp_pkg::TXN_ID_W
    , parameter int DATA_WIDTH  = gbp_pkg::NOC_DATA_W
) (
    input  logic clk_i
    , input  logic rst_i

    // Fetch request ingress (from NoC Adapter RX)
    , input  logic                 req_valid_i
    , output logic                 req_ready_o
    , input  logic [NODE_ID_W-1:0] req_target_node_id_i
    , input  logic [NODE_ID_W-1:0] req_consumer_node_id_i
    , input  logic                 req_is_factor_i
    , input  logic [X_CORD_W-1:0]  req_fetch_src_x_i
    , input  logic [Y_CORD_W-1:0]  req_fetch_src_y_i
    , input  logic [TXN_ID_W-1:0]  req_txn_id_i

    // SPM read port (to SPM Arbiter)
    , output logic                 spm_rd_valid_o
    , output logic [SPM_ADDR_W-1:0] spm_rd_addr_o
    , input  logic                 spm_rd_ready_i
    , input  logic [BEAT_BITS-1:0] spm_rd_data_i

    // To NoC Adapter (FETCH_RESPONSE TX)
    , output logic                     tx_valid_o
    , input  logic                     tx_ready_i
    , output logic [NODE_ID_W-1:0]     tx_node_id_o
    , output logic [NODE_ID_W-1:0]     tx_consumer_node_id_o
    , output logic                     tx_is_factor_o
    , output logic [STATE_WORDS_W-1:0] tx_state_words_o
    , output logic [DATA_WIDTH-1:0]    tx_data_o
    , output logic                     tx_data_valid_o
    , output logic                     tx_last_o
    , output logic [TXN_ID_W-1:0]      tx_txn_id_o
);

  // FSM states
  localparam S_IDLE      = 3'd0;
  localparam S_LOOKUP    = 3'd1;  // read NodeHeader
  localparam S_SEND_DATA = 3'd2;  // stream state words
  localparam S_SEND_DONE = 3'd3;  // send done signal

  logic [2:0] state_r;

  // Latched request
  logic [NODE_ID_W-1:0] target_node_id_r;
  logic [NODE_ID_W-1:0] consumer_node_id_r;
  logic                  is_factor_r;
  logic [X_CORD_W-1:0]  src_x_r;
  logic [Y_CORD_W-1:0]  src_y_r;
  logic [TXN_ID_W-1:0]  txn_id_r;

  // Latched header fields
  logic [SPM_ADDR_W-1:0]    state_base_r;
  logic [STATE_WORDS_W-1:0] state_words_r;

  // Data word counter
  logic [STATE_WORDS_W-1:0] data_cnt_r;

  // Header address: node_id * HEADER_WORDS (HEADER_WORDS=1)
  wire [SPM_ADDR_W-1:0] header_addr_w = SPM_ADDR_W'(req_target_node_id_i);

  // State data address: state_base + data_cnt
  wire [SPM_ADDR_W-1:0] state_addr_w = state_base_r + SPM_ADDR_W'(data_cnt_r);

  // Output assignments
  assign req_ready_o = (state_r == S_IDLE);

  // SPM read
  assign spm_rd_valid_o = (state_r == S_LOOKUP) || (state_r == S_SEND_DATA);
  assign spm_rd_addr_o  = (state_r == S_LOOKUP) ? header_addr_w : state_addr_w;

  // TX output
  assign tx_valid_o           = (state_r == S_SEND_DATA) || (state_r == S_SEND_DONE);
  assign tx_node_id_o         = target_node_id_r;
  assign tx_consumer_node_id_o = consumer_node_id_r;
  assign tx_is_factor_o       = is_factor_r;
  assign tx_state_words_o     = state_words_r;
  assign tx_data_o            = spm_rd_data_i[DATA_WIDTH-1:0];  // low bits of SPM beat
  assign tx_data_valid_o      = (state_r == S_SEND_DATA);
  assign tx_last_o            = (state_r == S_SEND_DATA) && (data_cnt_r == state_words_r - 1);
  assign tx_txn_id_o          = txn_id_r;

  // FSM
  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      state_r <= S_IDLE;
      target_node_id_r <= '0;
      consumer_node_id_r <= '0;
      is_factor_r <= 1'b0;
      src_x_r <= '0;
      src_y_r <= '0;
      txn_id_r <= '0;
      state_base_r <= '0;
      state_words_r <= '0;
      data_cnt_r <= '0;
    end else begin
      case (state_r)
        S_IDLE: begin
          if (req_valid_i) begin
            // Latch request
            target_node_id_r <= req_target_node_id_i;
            consumer_node_id_r <= req_consumer_node_id_i;
            is_factor_r <= req_is_factor_i;
            src_x_r <= req_fetch_src_x_i;
            src_y_r <= req_fetch_src_y_i;
            txn_id_r <= req_txn_id_i;
            state_r <= S_LOOKUP;
          end
        end

        S_LOOKUP: begin
          // Wait for SPM to return header
          if (spm_rd_ready_i) begin
            // Parse header: same layout as metadata_scanner
            // NodeHeader layout (packed):
            // [NODE_ID_W-1:0]                    node_id
            // [NODE_ID_W+DOF_W-1:NODE_ID_W]     dof
            // [+: ADJ_COUNT_W]                   adj_count
            // [+: SPM_ADDR_W]                    adj_base
            // [+: SPM_ADDR_W]                    state_base
            // [+: STATE_WORDS_W]                 state_words
            localparam int HDR_STATE_BASE_OFF  = NODE_ID_W + DOF_W + ADJ_COUNT_W + SPM_ADDR_W;
            localparam int HDR_STATE_WORDS_OFF = HDR_STATE_BASE_OFF + SPM_ADDR_W;
            state_base_r  <= spm_rd_data_i[HDR_STATE_BASE_OFF +: SPM_ADDR_W];
            state_words_r <= spm_rd_data_i[HDR_STATE_WORDS_OFF +: STATE_WORDS_W];
            data_cnt_r <= '0;
            if (spm_rd_data_i[HDR_STATE_WORDS_OFF +: STATE_WORDS_W] == 0) begin
              // No state words, go directly to DONE
              state_r <= S_SEND_DONE;
            end else begin
              state_r <= S_SEND_DATA;
            end
          end
        end

        S_SEND_DATA: begin
          if (tx_ready_i && spm_rd_ready_i) begin
            data_cnt_r <= data_cnt_r + 1;
            if (data_cnt_r == state_words_r - 1) begin
              // Last data word sent, next is DONE
              state_r <= S_SEND_DONE;
            end
          end
        end

        S_SEND_DONE: begin
          if (tx_ready_i) begin
            state_r <= S_IDLE;
          end
        end

        default: state_r <= S_IDLE;
      endcase
    end
  end

endmodule
