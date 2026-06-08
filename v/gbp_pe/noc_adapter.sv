// noc_adapter.sv
// GBP PE NoC Adapter
// Wraps bsg_manycore_endpoint_standard and provides GBP mailbox-based messaging.
// Replaces gbp_pe_endpoint_adapter + gbp_pe_noc_bridge.
//
// RX: Decodes incoming e_remote_store to GBP mailboxes (NOTIFICATION, FETCH_REQUEST, FETCH_RESPONSE)
// TX: Arbitrates between 3 sources (notification, fetch_request, fetch_response) via round-robin

`include "bsg_manycore_defines.svh"

module noc_adapter
  import bsg_manycore_pkg::*;
  import gbp_pkg::*;
#(
    parameter int data_width_p     = 32
    , parameter int addr_width_p   = 16
    , parameter int x_cord_width_p = 6
    , parameter int y_cord_width_p = 5
    , parameter int NODE_ID_W      = gbp_pkg::NODE_ID_W
    , parameter int STATE_WORDS_W  = gbp_pkg::STATE_WORDS_W
    , parameter int SCOREBOARD_DEPTH = 64
    , parameter int TXN_ID_W       = $clog2(SCOREBOARD_DEPTH)
    , parameter int fifo_els_p     = 4
    , parameter int rev_fifo_els_p = 4
    , parameter int credit_counter_width_p = 32
    , parameter int unsigned GBP_BASE_ADDR = 32'h0000_1000

    , localparam link_sif_width_lp =
        `bsg_manycore_link_sif_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)
    , localparam packet_width_lp =
        `bsg_manycore_packet_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)
) (
    input  logic clk_i
    , input  logic rst_n_i

    // ── Manycore link_sif (external) ──
    , input  logic [link_sif_width_lp-1:0] link_sif_i
    , output logic [link_sif_width_lp-1:0] link_sif_o

    // ── Manycore coordinates ──
    , input  logic [x_cord_width_p-1:0] my_x_i
    , input  logic [y_cord_width_p-1:0] my_y_i

    // ── Internal TX: from Writeback Controller (NOTIFICATION) ──
    , input  logic                 tx_notif_valid_i
    , output logic                 tx_notif_ready_o
    , input  logic [NODE_ID_W-1:0] tx_notif_source_node_id_i
    , input  logic [NODE_ID_W-1:0] tx_notif_target_node_id_i
    , input  logic                 tx_notif_is_factor_i
    , input  logic [x_cord_width_p-1:0] tx_notif_target_x_i
    , input  logic [y_cord_width_p-1:0] tx_notif_target_y_i

    // ── Internal TX: from Pull Client (FETCH_REQUEST, 3-store sequence) ──
    , input  logic                 tx_fetch_req_valid_i
    , output logic                 tx_fetch_req_ready_o
    , input  logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id_i
    , input  logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id_i
    , input  logic                 tx_fetch_req_is_factor_i
    , input  logic [x_cord_width_p-1:0] tx_fetch_req_target_x_i
    , input  logic [y_cord_width_p-1:0] tx_fetch_req_target_y_i
    , input  logic [TXN_ID_W-1:0]  tx_fetch_req_txn_id_i
    , input  logic [1:0]           tx_fetch_req_store_idx_i  // 0/1/2 = which store

    // ── Internal TX: from Pull Server (FETCH_RESPONSE) ──
    , input  logic                     tx_fetch_resp_valid_i
    , output logic                     tx_fetch_resp_ready_o
    , input  logic [NODE_ID_W-1:0]     tx_fetch_resp_node_id_i
    , input  logic [NODE_ID_W-1:0]     tx_fetch_resp_consumer_node_id_i
    , input  logic                     tx_fetch_resp_is_factor_i
    , input  logic [STATE_WORDS_W-1:0] tx_fetch_resp_state_words_i
    , input  logic [data_width_p-1:0]  tx_fetch_resp_data_i
    , input  logic                     tx_fetch_resp_data_valid_i
    , input  logic                     tx_fetch_resp_last_i
    , input  logic [TXN_ID_W-1:0]      tx_fetch_resp_txn_id_i

    // ── Internal RX: to ScoreboardPrefetcher (NOTIFICATION) ──
    , output logic                 rx_notif_valid_o
    , input  logic                 rx_notif_ready_i
    , output logic [NODE_ID_W-1:0] rx_notif_source_node_id_o
    , output logic                 rx_notif_is_factor_o
    , output logic [x_cord_width_p-1:0] rx_notif_source_x_o
    , output logic [y_cord_width_p-1:0] rx_notif_source_y_o

    // ── Internal RX: to Pull Server (FETCH_REQUEST, 3-store latching) ──
    , output logic                 rx_fetch_req_valid_o
    , input  logic                 rx_fetch_req_ready_i
    , output logic [NODE_ID_W-1:0] rx_fetch_req_target_node_id_o
    , output logic [NODE_ID_W-1:0] rx_fetch_req_consumer_node_id_o
    , output logic                 rx_fetch_req_is_factor_o
    , output logic [x_cord_width_p-1:0] rx_fetch_req_src_x_o
    , output logic [y_cord_width_p-1:0] rx_fetch_req_src_y_o
    , output logic [TXN_ID_W-1:0]  rx_fetch_req_txn_id_o

    // ── Internal RX: to Response Collector (FETCH_RESPONSE) ──
    , output logic                     rx_fetch_resp_valid_o
    , output logic                     rx_fetch_resp_is_factor_o
    , output logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words_o
    , output logic [data_width_p-1:0]  rx_fetch_resp_data_o
    , output logic                     rx_fetch_resp_data_valid_o
    , output logic                     rx_fetch_resp_last_o
    , output logic                     rx_fetch_resp_done_valid_o
    , output logic [TXN_ID_W-1:0]      rx_fetch_resp_txn_id_o
    , output logic [NODE_ID_W-1:0]     rx_fetch_resp_node_id_o
    , output logic [NODE_ID_W-1:0]     rx_fetch_resp_consumer_node_id_o

    // ── Status ──
    , output logic tx_busy_o
);

  logic reset_i;
  assign reset_i = ~rst_n_i;

  // ── Endpoint Standard signals ──
  logic                            in_v_lo;
  logic [data_width_p-1:0]         in_data_lo;
  logic [(data_width_p>>3)-1:0]    in_mask_lo;
  logic [addr_width_p-1:0]         in_addr_lo;
  logic                            in_we_lo;
  bsg_manycore_load_info_s         in_load_info_lo;
  logic [x_cord_width_p-1:0]       in_src_x_lo;
  logic [y_cord_width_p-1:0]       in_src_y_lo;
  logic                            in_yumi_li;

  logic [data_width_p-1:0]         returning_data_li;
  logic                            returning_v_li;

  logic [packet_width_lp-1:0]      out_packet_li;
  logic                            out_v_li;
  logic                            out_credit_or_ready_lo;

  logic [data_width_p-1:0]         returned_data_lo;
  logic [bsg_manycore_reg_id_width_gp-1:0] returned_reg_id_lo;
  logic                            returned_v_lo;
  bsg_manycore_return_packet_type_e returned_pkt_type_lo;

  // ── Endpoint Standard instantiation ──
  bsg_manycore_endpoint_standard #(
    .x_cord_width_p(x_cord_width_p)
    ,.y_cord_width_p(y_cord_width_p)
    ,.fifo_els_p(fifo_els_p)
    ,.data_width_p(data_width_p)
    ,.addr_width_p(addr_width_p)
    ,.credit_counter_width_p(credit_counter_width_p)
    ,.rev_fifo_els_p(rev_fifo_els_p)
  ) ep_std (
    .clk_i(clk_i)
    ,.reset_i(reset_i)
    ,.link_sif_i(link_sif_i)
    ,.link_sif_o(link_sif_o)

    ,.in_v_o(in_v_lo)
    ,.in_data_o(in_data_lo)
    ,.in_mask_o(in_mask_lo)
    ,.in_addr_o(in_addr_lo)
    ,.in_we_o(in_we_lo)
    ,.in_load_info_o(in_load_info_lo)
    ,.in_src_x_cord_o(in_src_x_lo)
    ,.in_src_y_cord_o(in_src_y_lo)
    ,.in_yumi_i(in_yumi_li)

    ,.returning_data_i(returning_data_li)
    ,.returning_v_i(returning_v_li)

    ,.out_v_i(out_v_li)
    ,.out_packet_i(out_packet_li)
    ,.out_credit_or_ready_o(out_credit_or_ready_lo)

    ,.returned_data_r_o(returned_data_lo)
    ,.returned_reg_id_r_o(returned_reg_id_lo)
    ,.returned_v_r_o(returned_v_lo)
    ,.returned_pkt_type_r_o(returned_pkt_type_lo)
    ,.returned_yumi_i(returned_v_lo)  // auto-consume returns
    ,.returned_fifo_full_o()
    ,.returned_credit_v_r_o()
    ,.returned_credit_reg_id_r_o()
    ,.out_credits_used_o()

    ,.global_x_i(my_x_i)
    ,.global_y_i(my_y_i)
  );

  // Tie off unused returning path (GBP uses fwd stores only)
  assign returning_data_li = '0;
  assign returning_v_li = 1'b0;

  // ── TX path ──
  noc_adapter_tx #(
    .data_width_p(data_width_p)
    ,.addr_width_p(addr_width_p)
    ,.x_cord_width_p(x_cord_width_p)
    ,.y_cord_width_p(y_cord_width_p)
    ,.NODE_ID_W(NODE_ID_W)
    ,.STATE_WORDS_W(STATE_WORDS_W)
    ,.TXN_ID_W(TXN_ID_W)
    ,.GBP_BASE_ADDR(GBP_BASE_ADDR)
  ) tx (
    .clk_i(clk_i)
    ,.rst_n_i(rst_n_i)

    ,.my_x_i(my_x_i)
    ,.my_y_i(my_y_i)

    // Notification TX
    ,.tx_notif_valid_i(tx_notif_valid_i)
    ,.tx_notif_ready_o(tx_notif_ready_o)
    ,.tx_notif_source_node_id_i(tx_notif_source_node_id_i)
    ,.tx_notif_target_node_id_i(tx_notif_target_node_id_i)
    ,.tx_notif_is_factor_i(tx_notif_is_factor_i)
    ,.tx_notif_target_x_i(tx_notif_target_x_i)
    ,.tx_notif_target_y_i(tx_notif_target_y_i)

    // Fetch request TX
    ,.tx_fetch_req_valid_i(tx_fetch_req_valid_i)
    ,.tx_fetch_req_ready_o(tx_fetch_req_ready_o)
    ,.tx_fetch_req_target_node_id_i(tx_fetch_req_target_node_id_i)
    ,.tx_fetch_req_consumer_node_id_i(tx_fetch_req_consumer_node_id_i)
    ,.tx_fetch_req_is_factor_i(tx_fetch_req_is_factor_i)
    ,.tx_fetch_req_target_x_i(tx_fetch_req_target_x_i)
    ,.tx_fetch_req_target_y_i(tx_fetch_req_target_y_i)
    ,.tx_fetch_req_txn_id_i(tx_fetch_req_txn_id_i)
    ,.tx_fetch_req_store_idx_i(tx_fetch_req_store_idx_i)

    // Fetch response TX
    ,.tx_fetch_resp_valid_i(tx_fetch_resp_valid_i)
    ,.tx_fetch_resp_ready_o(tx_fetch_resp_ready_o)
    ,.tx_fetch_resp_node_id_i(tx_fetch_resp_node_id_i)
    ,.tx_fetch_resp_consumer_node_id_i(tx_fetch_resp_consumer_node_id_i)
    ,.tx_fetch_resp_is_factor_i(tx_fetch_resp_is_factor_i)
    ,.tx_fetch_resp_state_words_i(tx_fetch_resp_state_words_i)
    ,.tx_fetch_resp_data_i(tx_fetch_resp_data_i)
    ,.tx_fetch_resp_data_valid_i(tx_fetch_resp_data_valid_i)
    ,.tx_fetch_resp_last_i(tx_fetch_resp_last_i)
    ,.tx_fetch_resp_txn_id_i(tx_fetch_resp_txn_id_i)

    // To endpoint
    ,.out_packet_o(out_packet_li)
    ,.out_v_o(out_v_li)
    ,.out_credit_or_ready_i(out_credit_or_ready_lo)

    ,.tx_busy_o(tx_busy_o)
  );

  // ── RX path ──
  noc_adapter_rx #(
    .data_width_p(data_width_p)
    ,.addr_width_p(addr_width_p)
    ,.x_cord_width_p(x_cord_width_p)
    ,.y_cord_width_p(y_cord_width_p)
    ,.NODE_ID_W(NODE_ID_W)
    ,.STATE_WORDS_W(STATE_WORDS_W)
    ,.TXN_ID_W(TXN_ID_W)
    ,.GBP_BASE_ADDR(GBP_BASE_ADDR)
  ) rx (
    .clk_i(clk_i)
    ,.rst_n_i(rst_n_i)

    // From endpoint
    ,.in_v_i(in_v_lo)
    ,.in_data_i(in_data_lo)
    ,.in_addr_i(in_addr_lo)
    ,.in_we_i(in_we_lo)
    ,.in_src_x_i(in_src_x_lo)
    ,.in_src_y_i(in_src_y_lo)
    ,.in_yumi_o(in_yumi_li)

    // Notification RX
    ,.rx_notif_valid_o(rx_notif_valid_o)
    ,.rx_notif_ready_i(rx_notif_ready_i)
    ,.rx_notif_source_node_id_o(rx_notif_source_node_id_o)
    ,.rx_notif_is_factor_o(rx_notif_is_factor_o)
    ,.rx_notif_source_x_o(rx_notif_source_x_o)
    ,.rx_notif_source_y_o(rx_notif_source_y_o)

    // Fetch request RX
    ,.rx_fetch_req_valid_o(rx_fetch_req_valid_o)
    ,.rx_fetch_req_ready_i(rx_fetch_req_ready_i)
    ,.rx_fetch_req_target_node_id_o(rx_fetch_req_target_node_id_o)
    ,.rx_fetch_req_consumer_node_id_o(rx_fetch_req_consumer_node_id_o)
    ,.rx_fetch_req_is_factor_o(rx_fetch_req_is_factor_o)
    ,.rx_fetch_req_src_x_o(rx_fetch_req_src_x_o)
    ,.rx_fetch_req_src_y_o(rx_fetch_req_src_y_o)
    ,.rx_fetch_req_txn_id_o(rx_fetch_req_txn_id_o)

    // Fetch response RX
    ,.rx_fetch_resp_valid_o(rx_fetch_resp_valid_o)
    ,.rx_fetch_resp_is_factor_o(rx_fetch_resp_is_factor_o)
    ,.rx_fetch_resp_state_words_o(rx_fetch_resp_state_words_o)
    ,.rx_fetch_resp_data_o(rx_fetch_resp_data_o)
    ,.rx_fetch_resp_data_valid_o(rx_fetch_resp_data_valid_o)
    ,.rx_fetch_resp_last_o(rx_fetch_resp_last_o)
    ,.rx_fetch_resp_done_valid_o(rx_fetch_resp_done_valid_o)
    ,.rx_fetch_resp_txn_id_o(rx_fetch_resp_txn_id_o)
    ,.rx_fetch_resp_node_id_o(rx_fetch_resp_node_id_o)
    ,.rx_fetch_resp_consumer_node_id_o(rx_fetch_resp_consumer_node_id_o)
  );

endmodule
