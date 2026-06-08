// noc_adapter_top.sv
// Unit test wrapper for noc_adapter
// Exposes simple ports for C++ testbench to drive/check

`include "bsg_manycore_defines.svh"

module noc_adapter_top (
    input  logic        clk
    , input  logic        rst_n

    // TX inputs (from internal GBP modules)
    , input  logic        tx_notif_valid_i
    , input  logic [12:0] tx_notif_source_node_id_i
    , input  logic        tx_notif_is_factor_i
    , input  logic [2:0]  tx_notif_target_x_i
    , input  logic [2:0]  tx_notif_target_y_i

    , input  logic        tx_fetch_req_valid_i
    , input  logic [12:0] tx_fetch_req_target_node_id_i
    , input  logic [12:0] tx_fetch_req_consumer_node_id_i
    , input  logic        tx_fetch_req_is_factor_i
    , input  logic [2:0]  tx_fetch_req_target_x_i
    , input  logic [2:0]  tx_fetch_req_target_y_i
    , input  logic [5:0]  tx_fetch_req_txn_id_i
    , input  logic [1:0]  tx_fetch_req_store_idx_i

    , input  logic        tx_fetch_resp_valid_i
    , input  logic [12:0] tx_fetch_resp_node_id_i
    , input  logic [12:0] tx_fetch_resp_consumer_node_id_i
    , input  logic        tx_fetch_resp_is_factor_i
    , input  logic [3:0]  tx_fetch_resp_state_words_i
    , input  logic [31:0] tx_fetch_resp_data_i
    , input  logic        tx_fetch_resp_data_valid_i
    , input  logic        tx_fetch_resp_last_i
    , input  logic [5:0]  tx_fetch_resp_txn_id_i

    // RX outputs (to internal GBP modules)
    , output logic        rx_notif_valid_o
    , output logic [12:0] rx_notif_source_node_id_o
    , output logic        rx_notif_is_factor_o
    , output logic [2:0]  rx_notif_source_x_o
    , output logic [2:0]  rx_notif_source_y_o

    , output logic        rx_fetch_req_valid_o
    , output logic [12:0] rx_fetch_req_target_node_id_o
    , output logic [12:0] rx_fetch_req_consumer_node_id_o
    , output logic        rx_fetch_req_is_factor_o
    , output logic [2:0]  rx_fetch_req_src_x_o
    , output logic [2:0]  rx_fetch_req_src_y_o
    , output logic [5:0]  rx_fetch_req_txn_id_o

    , output logic        rx_fetch_resp_valid_o
    , output logic        rx_fetch_resp_is_factor_o
    , output logic [3:0]  rx_fetch_resp_state_words_o
    , output logic [31:0] rx_fetch_resp_data_o
    , output logic        rx_fetch_resp_data_valid_o
    , output logic        rx_fetch_resp_last_o
    , output logic        rx_fetch_resp_done_valid_o
    , output logic [5:0]  rx_fetch_resp_txn_id_o

    // TX ready signals
    , output logic        tx_notif_ready_o
    , output logic        tx_fetch_req_ready_o
    , output logic        tx_fetch_resp_ready_o
    , output logic        tx_busy_o

    // External NoC link backpressure (credit/ready from downstream router)
    , input  logic        noc_out_ready_i

    // External NoC injection (for driving incoming stores)
    , input  logic        noc_inject_v_i
    , input  logic [15:0] noc_inject_addr_i
    , input  logic [31:0] noc_inject_data_i
    , input  logic [2:0]  noc_inject_src_x_i
    , input  logic [2:0]  noc_inject_src_y_i
    , output logic        noc_inject_ready_o

    // Exposed outgoing NoC packet fields for testbench inspection
    , output logic        noc_out_v_o
    , output logic [15:0] noc_out_addr_o
    , output logic [3:0]  noc_out_op_o
    , output logic [31:0] noc_out_payload_o
    , output logic [2:0]  noc_out_dst_x_o
    , output logic [2:0]  noc_out_dst_y_o
);

  import bsg_manycore_pkg::*;

  localparam int x_cord_width_lp = 3;
  localparam int y_cord_width_lp = 3;
  localparam int data_width_lp   = 32;
  localparam int addr_width_lp   = 16;
  localparam int NODE_ID_W_lp    = 13;
  localparam int STATE_WORDS_W_lp = 4;
  localparam int SCOREBOARD_DEPTH_lp = 64;
  localparam int TXN_ID_W_lp     = $clog2(SCOREBOARD_DEPTH_lp);
  localparam int credit_counter_width_lp = 8;

  localparam int link_sif_width_lp =
    `bsg_manycore_link_sif_width(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);
  localparam int packet_width_lp =
    `bsg_manycore_packet_width(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);

  `declare_bsg_manycore_packet_s(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);
  `declare_bsg_manycore_link_sif_s(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);

  logic reset_i;
  assign reset_i = ~rst_n;

  // ── Link SIF wiring ──
  bsg_manycore_link_sif_s link_in_s;
  bsg_manycore_link_sif_s link_out_s;
  bsg_manycore_packet_s   inject_pkt_s;

  // Build incoming packet from inject signals
  always_comb begin
    inject_pkt_s = '0;
    inject_pkt_s.addr = noc_inject_addr_i;
    inject_pkt_s.op_v2 = e_remote_sw;
    inject_pkt_s.payload = noc_inject_data_i;
    inject_pkt_s.src_x_cord = noc_inject_src_x_i;
    inject_pkt_s.src_y_cord = noc_inject_src_y_i;
    inject_pkt_s.x_cord = '0;  // destined to this PE (coordinates 0,0)
    inject_pkt_s.y_cord = '0;
  end

  assign link_in_s.fwd.v = noc_inject_v_i;
  assign link_in_s.fwd.data = inject_pkt_s;
  assign link_in_s.fwd.ready_and_rev = noc_out_ready_i;  // router ready/credit for TX path
  assign link_in_s.rev.ready_and_rev = 1'b1;

  logic [link_sif_width_lp-1:0] link_sif_i;
  logic [link_sif_width_lp-1:0] link_sif_o;

  assign link_sif_i = link_in_s;
  assign link_out_s = link_sif_o;
  assign noc_inject_ready_o = link_out_s.fwd.ready_and_rev;

  bsg_manycore_packet_s out_pkt_cast;
  assign out_pkt_cast = bsg_manycore_packet_s'(link_out_s.fwd.data);

  assign noc_out_v_o       = link_out_s.fwd.v;
  assign noc_out_addr_o    = out_pkt_cast.addr;
  assign noc_out_op_o      = out_pkt_cast.op_v2;
  assign noc_out_payload_o = out_pkt_cast.payload;
  assign noc_out_dst_x_o   = out_pkt_cast.x_cord;
  assign noc_out_dst_y_o   = out_pkt_cast.y_cord;

  // ── DUT instantiation ──
  noc_adapter #(
    .data_width_p(data_width_lp)
    ,.addr_width_p(addr_width_lp)
    ,.x_cord_width_p(x_cord_width_lp)
    ,.y_cord_width_p(y_cord_width_lp)
    ,.NODE_ID_W(NODE_ID_W_lp)
    ,.STATE_WORDS_W(STATE_WORDS_W_lp)
    ,.SCOREBOARD_DEPTH(SCOREBOARD_DEPTH_lp)
    ,.TXN_ID_W(TXN_ID_W_lp)
    ,.fifo_els_p(2)
    ,.rev_fifo_els_p(2)
    ,.credit_counter_width_p(credit_counter_width_lp)
    ,.GBP_BASE_ADDR(32'h0000_1000)
  ) dut (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.link_sif_i(link_sif_i)
    ,.link_sif_o(link_sif_o)
    ,.my_x_i(x_cord_width_lp'(0))
    ,.my_y_i(y_cord_width_lp'(0))

    // TX
    ,.tx_notif_valid_i(tx_notif_valid_i)
    ,.tx_notif_ready_o(tx_notif_ready_o)
    ,.tx_notif_source_node_id_i(tx_notif_source_node_id_i)
    ,.tx_notif_is_factor_i(tx_notif_is_factor_i)
    ,.tx_notif_target_x_i(tx_notif_target_x_i)
    ,.tx_notif_target_y_i(tx_notif_target_y_i)

    ,.tx_fetch_req_valid_i(tx_fetch_req_valid_i)
    ,.tx_fetch_req_ready_o(tx_fetch_req_ready_o)
    ,.tx_fetch_req_target_node_id_i(tx_fetch_req_target_node_id_i)
    ,.tx_fetch_req_consumer_node_id_i(tx_fetch_req_consumer_node_id_i)
    ,.tx_fetch_req_is_factor_i(tx_fetch_req_is_factor_i)
    ,.tx_fetch_req_target_x_i(tx_fetch_req_target_x_i)
    ,.tx_fetch_req_target_y_i(tx_fetch_req_target_y_i)
    ,.tx_fetch_req_txn_id_i(tx_fetch_req_txn_id_i)
    ,.tx_fetch_req_store_idx_i(tx_fetch_req_store_idx_i)

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

    // RX
    ,.rx_notif_valid_o(rx_notif_valid_o)
    ,.rx_notif_ready_i(1'b0)  // not ready by default
    ,.rx_notif_source_node_id_o(rx_notif_source_node_id_o)
    ,.rx_notif_is_factor_o(rx_notif_is_factor_o)
    ,.rx_notif_source_x_o(rx_notif_source_x_o)
    ,.rx_notif_source_y_o(rx_notif_source_y_o)

    ,.rx_fetch_req_valid_o(rx_fetch_req_valid_o)
    ,.rx_fetch_req_ready_i(1'b0)  // not ready by default - tests will pulse when needed
    ,.rx_fetch_req_target_node_id_o(rx_fetch_req_target_node_id_o)
    ,.rx_fetch_req_consumer_node_id_o(rx_fetch_req_consumer_node_id_o)
    ,.rx_fetch_req_is_factor_o(rx_fetch_req_is_factor_o)
    ,.rx_fetch_req_src_x_o(rx_fetch_req_src_x_o)
    ,.rx_fetch_req_src_y_o(rx_fetch_req_src_y_o)
    ,.rx_fetch_req_txn_id_o(rx_fetch_req_txn_id_o)

    ,.rx_fetch_resp_valid_o(rx_fetch_resp_valid_o)
    ,.rx_fetch_resp_is_factor_o(rx_fetch_resp_is_factor_o)
    ,.rx_fetch_resp_state_words_o(rx_fetch_resp_state_words_o)
    ,.rx_fetch_resp_data_o(rx_fetch_resp_data_o)
    ,.rx_fetch_resp_data_valid_o(rx_fetch_resp_data_valid_o)
    ,.rx_fetch_resp_last_o(rx_fetch_resp_last_o)
    ,.rx_fetch_resp_done_valid_o(rx_fetch_resp_done_valid_o)
    ,.rx_fetch_resp_txn_id_o(rx_fetch_resp_txn_id_o)

    ,.tx_busy_o(tx_busy_o)
  );

endmodule
