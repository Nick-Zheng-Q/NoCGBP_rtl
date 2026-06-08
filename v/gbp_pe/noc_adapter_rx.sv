// noc_adapter_rx.sv
// GBP PE NoC Adapter - RX Path
// Decodes incoming e_remote_store to GBP mailboxes:
//   MBX_NOTIFICATION  (0x00) → rx_notif_*
//   MBX_FETCH_REQ_0   (0x04) → latch {is_factor, consumer_node_id}
//   MBX_FETCH_REQ_1   (0x08) → latch {target_node_id}
//   MBX_FETCH_REQ_2   (0x0C) → latch {txn_id} → assert rx_fetch_req_valid
//   MBX_RESP_META     (0x10) → rx_fetch_resp_*
//   MBX_RESP_DATA     (0x14) → rx_fetch_resp_data_*
//   MBX_RESP_DONE     (0x18) → rx_fetch_resp_done_valid, rx_fetch_resp_txn_id

module noc_adapter_rx
  import bsg_manycore_pkg::*;
  import gbp_pkg::*;
#(
    parameter int data_width_p     = 32
    , parameter int addr_width_p   = 16
    , parameter int x_cord_width_p = 6
    , parameter int y_cord_width_p = 5
    , parameter int NODE_ID_W      = gbp_pkg::NODE_ID_W
    , parameter int STATE_WORDS_W  = gbp_pkg::STATE_WORDS_W
    , parameter int TXN_ID_W       = 8
    , parameter int unsigned GBP_BASE_ADDR = 32'h0000_1000
) (
    input  logic clk_i
    , input  logic rst_n_i

    // ── From endpoint ──
    , input  logic                 in_v_i
    , input  logic [data_width_p-1:0] in_data_i
    , input  logic [addr_width_p-1:0] in_addr_i
    , input  logic                 in_we_i
    , input  logic [x_cord_width_p-1:0] in_src_x_i
    , input  logic [y_cord_width_p-1:0] in_src_y_i
    , output logic                 in_yumi_o

    // ── Notification RX ──
    , output logic                 rx_notif_valid_o
    , input  logic                 rx_notif_ready_i
    , output logic [NODE_ID_W-1:0] rx_notif_source_node_id_o
    , output logic                 rx_notif_is_factor_o
    , output logic [x_cord_width_p-1:0] rx_notif_source_x_o
    , output logic [y_cord_width_p-1:0] rx_notif_source_y_o

    // ── Fetch request RX (3-store latching) ──
    , output logic                 rx_fetch_req_valid_o
    , input  logic                 rx_fetch_req_ready_i
    , output logic [NODE_ID_W-1:0] rx_fetch_req_target_node_id_o
    , output logic [NODE_ID_W-1:0] rx_fetch_req_consumer_node_id_o
    , output logic                 rx_fetch_req_is_factor_o
    , output logic [x_cord_width_p-1:0] rx_fetch_req_src_x_o
    , output logic [y_cord_width_p-1:0] rx_fetch_req_src_y_o
    , output logic [TXN_ID_W-1:0]  rx_fetch_req_txn_id_o

    // ── Fetch response RX ──
    , output logic                     rx_fetch_resp_valid_o
    , output logic                     rx_fetch_resp_is_factor_o
    , output logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words_o
    , output logic [data_width_p-1:0]  rx_fetch_resp_data_o
    , output logic                     rx_fetch_resp_data_valid_o
    , output logic                     rx_fetch_resp_last_o
    , output logic                     rx_fetch_resp_done_valid_o
    , output logic [TXN_ID_W-1:0]      rx_fetch_resp_txn_id_o
    , output logic [NODE_ID_W-1:0]     rx_fetch_resp_node_id_o       // from DONE store
    , output logic [NODE_ID_W-1:0]     rx_fetch_resp_consumer_node_id_o  // from DONE store
);

  logic reset_i;
  assign reset_i = ~rst_n_i;

  // ── Mailbox offsets ──
  localparam int unsigned MBX_NOTIFICATION  = 'h00;
  localparam int unsigned MBX_FETCH_REQ_0   = 'h04;
  localparam int unsigned MBX_FETCH_REQ_1   = 'h08;
  localparam int unsigned MBX_FETCH_REQ_2   = 'h0C;
  localparam int unsigned MBX_RESP_META     = 'h10;
  localparam int unsigned MBX_RESP_DATA     = 'h14;
  localparam int unsigned MBX_RESP_DONE     = 'h18;

  // ── Address decode ──
  logic is_gbp_w;
  logic [15:0] offset_w;

  assign is_gbp_w = in_v_i && in_we_i && (in_addr_i >= 16'(GBP_BASE_ADDR))
                     && (in_addr_i < 16'(GBP_BASE_ADDR + 'h20));
  assign offset_w = in_addr_i - 16'(GBP_BASE_ADDR);

  // ── Fetch request 3-store latch ──
  logic [NODE_ID_W-1:0] req_latch_is_factor_r;
  logic [NODE_ID_W-1:0] req_latch_consumer_id_r;
  logic [NODE_ID_W-1:0] req_latch_target_id_r;
  logic [TXN_ID_W-1:0]  req_latch_txn_id_r;
  logic [x_cord_width_p-1:0] req_latch_src_x_r;
  logic [y_cord_width_p-1:0] req_latch_src_y_r;
  logic [1:0] req_latch_count_r;

  // ── Response state tracking ──
  logic resp_active_r;
  logic [STATE_WORDS_W-1:0] resp_state_words_r;
  logic [STATE_WORDS_W-1:0] resp_data_cnt_r;
  logic resp_is_factor_r;

  // ── Output assignments ──
  // Notification
  assign rx_notif_valid_o = is_gbp_w && (offset_w == 16'(MBX_NOTIFICATION));
  assign rx_notif_source_node_id_o = NODE_ID_W'(in_data_i[NODE_ID_W-1:0]);
  assign rx_notif_is_factor_o = in_data_i[data_width_p-1];
  assign rx_notif_source_x_o = in_src_x_i;
  assign rx_notif_source_y_o = in_src_y_i;

  // Fetch request - valid after all 3 stores received
  assign rx_fetch_req_valid_o = (req_latch_count_r == 2'd3);
  assign rx_fetch_req_target_node_id_o = req_latch_target_id_r;
  assign rx_fetch_req_consumer_node_id_o = req_latch_consumer_id_r;
  assign rx_fetch_req_is_factor_o = req_latch_is_factor_r[0];
  assign rx_fetch_req_src_x_o = req_latch_src_x_r;
  assign rx_fetch_req_src_y_o = req_latch_src_y_r;
  assign rx_fetch_req_txn_id_o = req_latch_txn_id_r;

  // Fetch response
  assign rx_fetch_resp_valid_o = is_gbp_w && (
    (offset_w == 16'(MBX_RESP_META)) ||
    (offset_w == 16'(MBX_RESP_DATA)) ||
    (offset_w == 16'(MBX_RESP_DONE))
  );
  assign rx_fetch_resp_is_factor_o = resp_is_factor_r;
  assign rx_fetch_resp_state_words_o = resp_state_words_r;
  assign rx_fetch_resp_data_o = in_data_i;
  assign rx_fetch_resp_data_valid_o = is_gbp_w && (offset_w == 16'(MBX_RESP_DATA));
  assign rx_fetch_resp_last_o = rx_fetch_resp_data_valid_o && (resp_data_cnt_r == resp_state_words_r - 1);
  // DONE store payload: {txn_id, node_id, consumer_node_id}
  // consumer_node_id at LSB, node_id in middle, txn_id at MSB
  assign rx_fetch_resp_done_valid_o = is_gbp_w && (offset_w == 16'(MBX_RESP_DONE));
  assign rx_fetch_resp_txn_id_o = TXN_ID_W'(in_data_i[data_width_p-1 -: TXN_ID_W]);
  assign rx_fetch_resp_node_id_o = NODE_ID_W'(in_data_i[NODE_ID_W-1:0]);
  assign rx_fetch_resp_consumer_node_id_o = NODE_ID_W'(in_data_i[2*NODE_ID_W-1:NODE_ID_W]);

  // Accept all GBP writes
  assign in_yumi_o = is_gbp_w;

  // ── Fetch request latch FSM ──
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      req_latch_count_r <= '0;
      req_latch_is_factor_r <= '0;
      req_latch_consumer_id_r <= '0;
      req_latch_target_id_r <= '0;
      req_latch_txn_id_r <= '0;
      req_latch_src_x_r <= '0;
      req_latch_src_y_r <= '0;
    end else begin
      // Clear latch after request is consumed
      if (rx_fetch_req_valid_o && rx_fetch_req_ready_i) begin
        req_latch_count_r <= '0;
      end

      if (is_gbp_w) begin
        case (offset_w)
          16'(MBX_FETCH_REQ_0): begin
            req_latch_is_factor_r <= NODE_ID_W'(in_data_i[data_width_p-1]);
            req_latch_consumer_id_r <= NODE_ID_W'(in_data_i[NODE_ID_W-1:0]);
            req_latch_src_x_r <= in_src_x_i;
            req_latch_src_y_r <= in_src_y_i;
            req_latch_count_r <= 2'd1;
          end
          16'(MBX_FETCH_REQ_1): begin
            req_latch_target_id_r <= NODE_ID_W'(in_data_i[NODE_ID_W-1:0]);
            if (req_latch_count_r == 2'd1) begin
              req_latch_count_r <= 2'd2;
            end
          end
          16'(MBX_FETCH_REQ_2): begin
            req_latch_txn_id_r <= TXN_ID_W'(in_data_i);
            if (req_latch_count_r == 2'd2) begin
              req_latch_count_r <= 2'd3;
            end
          end
          default: ;
        endcase
      end
    end
  end

  // ── Response tracking ──
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      resp_active_r <= 1'b0;
      resp_state_words_r <= '0;
      resp_data_cnt_r <= '0;
      resp_is_factor_r <= 1'b0;
    end else begin
      if (is_gbp_w) begin
        case (offset_w)
          16'(MBX_RESP_META): begin
            resp_is_factor_r <= in_data_i[data_width_p-1];
            resp_state_words_r <= STATE_WORDS_W'(in_data_i[STATE_WORDS_W-1:0]);
            resp_data_cnt_r <= '0;
            resp_active_r <= 1'b1;
          end
          16'(MBX_RESP_DATA): begin
            if (resp_active_r) begin
              resp_data_cnt_r <= resp_data_cnt_r + 1;
            end
          end
          16'(MBX_RESP_DONE): begin
            resp_active_r <= 1'b0;
            resp_data_cnt_r <= '0;
          end
          default: ;
        endcase
      end
    end
  end

endmodule
