`include "bsg_manycore_defines.svh"

module gbp_pe_mesh_2pe_gbp
  import bsg_manycore_pkg::*;
  import bsg_noc_pkg::*;
(
  input  logic        clk,
  input  logic        rst_n,

  input  logic        send0_v,
  input  logic        send0_we,
  input  logic [15:0] send0_addr,
  input  logic [31:0] send0_data,
  output logic        send0_ready,

  input  logic        send1_v,
  input  logic        send1_we,
  input  logic [15:0] send1_addr,
  input  logic [31:0] send1_data,
  output logic        send1_ready,

  output logic        link_activity_o,
  output logic        decode_error_seen_o,
  output logic [31:0] pe0_ingress_wr_count_o,
  output logic [31:0] pe1_ingress_wr_count_o,
  output logic [31:0] pe0_cmd_accept_count_o,
  output logic [31:0] pe1_cmd_accept_count_o,
  output logic [31:0] pe0_host_tx_count_o,
  output logic [31:0] pe1_host_tx_count_o,
  output logic [31:0] pe0_dut_tx_count_o,
  output logic [31:0] pe1_dut_tx_count_o,
  output logic [31:0] pe0_compute_done_count_o,
  output logic [31:0] pe1_compute_done_count_o,
  output logic [bsg_manycore_reg_id_width_gp-1:0] pe0_last_dut_txn_id_o,
  output logic [bsg_manycore_reg_id_width_gp-1:0] pe1_last_dut_txn_id_o,
  output logic [31:0] pe0_state_b1_row0_word0_o,
  output logic [31:0] pe0_state_b2_row0_word0_o,
  output logic [31:0] pe0_state_b3_row0_word0_o,
  output logic [255:0] pe0_state_b1_row0_words_o,
  output logic [255:0] pe0_state_b2_row0_words_o,
  output logic [255:0] pe0_state_b3_row0_words_o,
  output logic [31:0] pe1_state_b1_row0_word0_o,
  output logic [31:0] pe1_state_b2_row0_word0_o,
  output logic [31:0] pe1_state_b3_row0_word0_o,
  output logic [255:0] pe1_state_b1_row0_words_o,
  output logic [255:0] pe1_state_b2_row0_words_o,
  output logic [255:0] pe1_state_b3_row0_words_o,
  output logic [31:0] pe0_message_b4_row0_word0_o,
  output logic [31:0] pe0_message_b5_row0_word0_o,
  output logic [31:0] pe0_message_b6_row0_word0_o,
  output logic [31:0] pe0_message_b7_row0_word0_o,
  output logic [255:0] pe0_message_b4_row0_words_o,
  output logic [255:0] pe0_message_b5_row0_words_o,
  output logic [255:0] pe0_message_b6_row0_words_o,
  output logic [255:0] pe0_message_b7_row0_words_o,
  output logic [31:0] pe1_message_b4_row0_word0_o,
  output logic [31:0] pe1_message_b5_row0_word0_o,
  output logic [31:0] pe1_message_b6_row0_word0_o,
  output logic [31:0] pe1_message_b7_row0_word0_o,
  output logic [255:0] pe1_message_b4_row0_words_o,
  output logic [255:0] pe1_message_b5_row0_words_o,
  output logic [255:0] pe1_message_b6_row0_words_o,
  output logic [255:0] pe1_message_b7_row0_words_o,
  output logic [31:0] pe0_adapter_payload_plane0_row0_o,
  output logic [31:0] pe0_adapter_payload_plane1_row0_o,
  output logic [31:0] pe0_adapter_payload_plane2_row0_o,
  output logic [31:0] pe0_adapter_payload_plane3_row0_o,
  output logic [31:0] pe1_adapter_payload_plane0_row0_o,
  output logic [31:0] pe1_adapter_payload_plane1_row0_o,
  output logic [31:0] pe1_adapter_payload_plane2_row0_o,
  output logic [31:0] pe1_adapter_payload_plane3_row0_o,
  output logic [7:0] pe0_adapter_credit_q0_o,
  output logic [7:0] pe1_adapter_credit_q0_o,
  output logic [7:0] pe0_adapter_tail_q0_o,
  output logic [7:0] pe1_adapter_tail_q0_o,
  output logic pe0_wr_req_valid_o,
  output logic [15:0] pe0_wr_req_addr_o,
  output logic [31:0] pe0_wr_req_data_o,
  output logic pe1_wr_req_valid_o,
  output logic [15:0] pe1_wr_req_addr_o,
  output logic [31:0] pe1_wr_req_data_o,
  output logic pe0_ingress_wr_req_valid_o,
  output logic [15:0] pe0_ingress_wr_req_addr_o,
  output logic [31:0] pe0_ingress_wr_req_data_o,
  output logic pe1_ingress_wr_req_valid_o,
  output logic [15:0] pe1_ingress_wr_req_addr_o,
  output logic [31:0] pe1_ingress_wr_req_data_o,
  output logic pe0_semantic_payload_seen_o,
  output logic [2:0] pe0_semantic_payload_bank_o,
  output logic [7:0] pe0_semantic_payload_row_o,
  output logic [31:0] pe0_semantic_payload_first_word_o,
  output logic pe1_semantic_payload_seen_o,
  output logic [2:0] pe1_semantic_payload_bank_o,
  output logic [7:0] pe1_semantic_payload_row_o,
  output logic [31:0] pe1_semantic_payload_first_word_o
);

  localparam int x_cord_width_lp = 2;
  localparam int y_cord_width_lp = 2;
  localparam int data_width_lp = 32;
  localparam int addr_width_lp = 16;
  localparam int credit_counter_width_lp = 8;
  localparam int num_tiles_x_lp = 2;
  localparam int num_tiles_y_lp = 1;
  localparam int barrier_ruche_factor_x_lp = 1;
  localparam int dims_lp = 2;
  localparam int packet_width_lp =
      `bsg_manycore_packet_width(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);
  localparam int link_sif_width_lp =
      `bsg_manycore_link_sif_width(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);

  `declare_bsg_manycore_packet_s(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);

  logic reset_i;
  assign reset_i = ~rst_n;

  logic [link_sif_width_lp-1:0] tx0_to_mesh;
  logic [link_sif_width_lp-1:0] mesh_to_tx0;
  logic [link_sif_width_lp-1:0] tx1_to_mesh;
  logic [link_sif_width_lp-1:0] mesh_to_tx1;
  bsg_manycore_packet_s tx0_host_pkt_s;
  bsg_manycore_packet_s tx1_host_pkt_s;
  bsg_manycore_packet_s tx0_dut_pkt_s;
  bsg_manycore_packet_s tx1_dut_pkt_s;
  logic [packet_width_lp-1:0] tx0_host_pkt_bits;
  logic [packet_width_lp-1:0] tx1_host_pkt_bits;
  logic [packet_width_lp-1:0] tx0_dut_pkt_bits;
  logic [packet_width_lp-1:0] tx1_dut_pkt_bits;
  logic [packet_width_lp-1:0] tx0_mux_pkt_bits;
  logic [packet_width_lp-1:0] tx1_mux_pkt_bits;
  logic tx0_out_v;
  logic tx1_out_v;
  logic tx0_out_ready;
  logic tx1_out_ready;
  logic tx0_sel_host;
  logic tx0_sel_dut;
  logic tx1_sel_host;
  logic tx1_sel_dut;
  logic tx0_fire;
  logic tx1_fire;
  logic tx0_host_fire;
  logic tx1_host_fire;
  logic tx0_dut_fire;
  logic tx1_dut_fire;
  logic pe0_host_prio_r;
  logic pe1_host_prio_r;
  logic pe0_dut_pending_r;
  logic pe1_dut_pending_r;
  logic [addr_width_lp-1:0] pe0_dut_addr_r;
  logic [addr_width_lp-1:0] pe1_dut_addr_r;
  logic [data_width_lp-1:0] pe0_dut_data_r;
  logic [data_width_lp-1:0] pe1_dut_data_r;
  logic [bsg_manycore_reg_id_width_gp-1:0] pe0_dut_txn_id_r;
  logic [bsg_manycore_reg_id_width_gp-1:0] pe1_dut_txn_id_r;

  `declare_bsg_manycore_link_sif_s(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);

  logic [E:W][num_tiles_y_lp-1:0][link_sif_width_lp-1:0] hor_link_sif_i;
  logic [E:W][num_tiles_y_lp-1:0][link_sif_width_lp-1:0] hor_link_sif_o;
  logic [S:N][num_tiles_x_lp-1:0][link_sif_width_lp-1:0] ver_link_sif_i;
  logic [S:N][num_tiles_x_lp-1:0][link_sif_width_lp-1:0] ver_link_sif_o;
  bsg_manycore_link_sif_s [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][S:W] tile_link_i;
  bsg_manycore_link_sif_s [num_tiles_y_lp-1:0][num_tiles_x_lp-1:0][S:W] tile_link_o;

  logic [S:N][num_tiles_x_lp-1:0] ver_barrier_link_i;
  logic [S:N][num_tiles_x_lp-1:0] ver_barrier_link_o;
  logic [E:W][num_tiles_y_lp-1:0] hor_barrier_link_i;
  logic [E:W][num_tiles_y_lp-1:0] hor_barrier_link_o;
  logic [E:W][num_tiles_y_lp-1:0][barrier_ruche_factor_x_lp-1:0] barrier_ruche_link_i;
  logic [E:W][num_tiles_y_lp-1:0][barrier_ruche_factor_x_lp-1:0] barrier_ruche_link_o;

  logic [num_tiles_x_lp-1:0][x_cord_width_lp-1:0] global_x_i;
  logic [num_tiles_x_lp-1:0][y_cord_width_lp-1:0] global_y_i;
  logic [num_tiles_x_lp-1:0][x_cord_width_lp-1:0] global_x_o;
  logic [num_tiles_x_lp-1:0][y_cord_width_lp-1:0] global_y_o;
  logic tile0_reset_o;
  logic tile1_reset_o;

  logic [S:W] tile0_barrier_link_i;
  logic [S:W] tile0_barrier_link_o;
  logic [S:W] tile1_barrier_link_i;
  logic [S:W] tile1_barrier_link_o;
  logic [barrier_ruche_factor_x_lp-1:0][E:W] tile0_barrier_ruche_link_i;
  logic [barrier_ruche_factor_x_lp-1:0][E:W] tile0_barrier_ruche_link_o;
  logic [barrier_ruche_factor_x_lp-1:0][E:W] tile1_barrier_ruche_link_i;
  logic [barrier_ruche_factor_x_lp-1:0][E:W] tile1_barrier_ruche_link_o;

  logic link_activity_r;
  logic link_activity_now;
  logic decode_error_seen_r;
  logic decode_error_pe0_lo;
  logic decode_error_pe1_lo;

  logic [31:0] pe0_ingress_wr_count_r;
  logic [31:0] pe1_ingress_wr_count_r;
  logic [31:0] pe0_cmd_accept_count_r;
  logic [31:0] pe1_cmd_accept_count_r;
  logic [31:0] pe0_host_tx_count_r;
  logic [31:0] pe1_host_tx_count_r;
  logic [31:0] pe0_dut_tx_count_r;
  logic [31:0] pe1_dut_tx_count_r;
  logic [31:0] pe0_compute_done_count_r;
  logic [31:0] pe1_compute_done_count_r;
  logic [bsg_manycore_reg_id_width_gp-1:0] pe0_last_dut_txn_id_r;
  logic [bsg_manycore_reg_id_width_gp-1:0] pe1_last_dut_txn_id_r;
  logic pe0_semantic_payload_seen_r;
  logic pe1_semantic_payload_seen_r;
  logic [2:0] pe0_semantic_payload_bank_r;
  logic [2:0] pe1_semantic_payload_bank_r;
  logic [7:0] pe0_semantic_payload_row_r;
  logic [7:0] pe1_semantic_payload_row_r;
  logic [31:0] pe0_semantic_payload_first_word_r;
  logic [31:0] pe1_semantic_payload_first_word_r;

  always_comb begin
    tx0_host_pkt_s = '0;
    tx0_host_pkt_s.addr = send0_addr;
    tx0_host_pkt_s.op_v2 = send0_we ? e_remote_store : e_remote_load;
    tx0_host_pkt_s.reg_id = '1;
    tx0_host_pkt_s.payload.data = send0_data;
    tx0_host_pkt_s.src_x_cord = 2'd0;
    tx0_host_pkt_s.src_y_cord = 2'd0;
    tx0_host_pkt_s.x_cord = 2'd1;
    tx0_host_pkt_s.y_cord = 2'd0;

    tx1_host_pkt_s = '0;
    tx1_host_pkt_s.addr = send1_addr;
    tx1_host_pkt_s.op_v2 = send1_we ? e_remote_store : e_remote_load;
    tx1_host_pkt_s.reg_id = '1;
    tx1_host_pkt_s.payload.data = send1_data;
    tx1_host_pkt_s.src_x_cord = 2'd1;
    tx1_host_pkt_s.src_y_cord = 2'd0;
    tx1_host_pkt_s.x_cord = 2'd0;
    tx1_host_pkt_s.y_cord = 2'd0;

    tx0_dut_pkt_s = '0;
    tx0_dut_pkt_s.addr = pe0_dut_addr_r;
    tx0_dut_pkt_s.op_v2 = e_remote_store;
    tx0_dut_pkt_s.reg_id = pe0_dut_txn_id_r;
    tx0_dut_pkt_s.payload.data = pe0_dut_data_r;
    tx0_dut_pkt_s.src_x_cord = 2'd0;
    tx0_dut_pkt_s.src_y_cord = 2'd0;
    tx0_dut_pkt_s.x_cord = 2'd1;
    tx0_dut_pkt_s.y_cord = 2'd0;

    tx1_dut_pkt_s = '0;
    tx1_dut_pkt_s.addr = pe1_dut_addr_r;
    tx1_dut_pkt_s.op_v2 = e_remote_store;
    tx1_dut_pkt_s.reg_id = pe1_dut_txn_id_r;
    tx1_dut_pkt_s.payload.data = pe1_dut_data_r;
    tx1_dut_pkt_s.src_x_cord = 2'd1;
    tx1_dut_pkt_s.src_y_cord = 2'd0;
    tx1_dut_pkt_s.x_cord = 2'd0;
    tx1_dut_pkt_s.y_cord = 2'd0;
  end

  assign tx0_host_pkt_bits = tx0_host_pkt_s;
  assign tx1_host_pkt_bits = tx1_host_pkt_s;
  assign tx0_dut_pkt_bits = tx0_dut_pkt_s;
  assign tx1_dut_pkt_bits = tx1_dut_pkt_s;

  always_comb begin
    tx0_sel_host = 1'b0;
    tx0_sel_dut = 1'b0;
    if (send0_v && pe0_dut_pending_r) begin
      tx0_sel_host = pe0_host_prio_r;
      tx0_sel_dut = ~pe0_host_prio_r;
    end else if (send0_v) begin
      tx0_sel_host = 1'b1;
    end else if (pe0_dut_pending_r) begin
      tx0_sel_dut = 1'b1;
    end

    tx1_sel_host = 1'b0;
    tx1_sel_dut = 1'b0;
    if (send1_v && pe1_dut_pending_r) begin
      tx1_sel_host = pe1_host_prio_r;
      tx1_sel_dut = ~pe1_host_prio_r;
    end else if (send1_v) begin
      tx1_sel_host = 1'b1;
    end else if (pe1_dut_pending_r) begin
      tx1_sel_dut = 1'b1;
    end
  end

  assign tx0_mux_pkt_bits = tx0_sel_host ? tx0_host_pkt_bits : tx0_dut_pkt_bits;
  assign tx1_mux_pkt_bits = tx1_sel_host ? tx1_host_pkt_bits : tx1_dut_pkt_bits;
  assign tx0_out_v = send0_v | pe0_dut_pending_r;
  assign tx1_out_v = send1_v | pe1_dut_pending_r;
  assign send0_ready = tx0_out_ready & tx0_sel_host;
  assign send1_ready = tx1_out_ready & tx1_sel_host;
  assign tx0_fire = tx0_out_v & tx0_out_ready;
  assign tx1_fire = tx1_out_v & tx1_out_ready;
  assign tx0_host_fire = tx0_fire & tx0_sel_host;
  assign tx1_host_fire = tx1_fire & tx1_sel_host;
  assign tx0_dut_fire = tx0_fire & tx0_sel_dut;
  assign tx1_dut_fire = tx1_fire & tx1_sel_dut;

  gbp_pe_endpoint_adapter
    #(
      .x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.fifo_els_p(2)
      ,.rev_fifo_els_p(2)
      ,.credit_counter_width_p(credit_counter_width_lp)
    ) tx0_ep
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(mesh_to_tx0)
      ,.link_sif_o(tx0_to_mesh)
      ,.global_x_i(2'd0)
      ,.global_y_i(2'd0)
      ,.out_packet_i(tx0_mux_pkt_bits)
      ,.out_v_i(tx0_out_v)
      ,.out_credit_or_ready_o(tx0_out_ready)
      ,.returned_data_r_o()
      ,.returned_reg_id_r_o()
      ,.returned_v_r_o()
      ,.returned_pkt_type_r_o()
      ,.returned_yumi_i(1'b1)
      ,.returned_fifo_full_o()
      ,.returned_credit_v_r_o()
      ,.returned_credit_reg_id_r_o()
      ,.out_credits_used_o()
      ,.core_req_data_o()
      ,.core_req_addr_o()
      ,.core_req_we_o()
      ,.core_req_v_o()
      ,.core_req_yumi_i(1'b0)
      ,.core_rsp_data_i('0)
      ,.core_rsp_v_i(1'b0)
    );

  gbp_pe_endpoint_adapter
    #(
      .x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.fifo_els_p(2)
      ,.rev_fifo_els_p(2)
      ,.credit_counter_width_p(credit_counter_width_lp)
    ) tx1_ep
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(mesh_to_tx1)
      ,.link_sif_o(tx1_to_mesh)
      ,.global_x_i(2'd1)
      ,.global_y_i(2'd0)
      ,.out_packet_i(tx1_mux_pkt_bits)
      ,.out_v_i(tx1_out_v)
      ,.out_credit_or_ready_o(tx1_out_ready)
      ,.returned_data_r_o()
      ,.returned_reg_id_r_o()
      ,.returned_v_r_o()
      ,.returned_pkt_type_r_o()
      ,.returned_yumi_i(1'b1)
      ,.returned_fifo_full_o()
      ,.returned_credit_v_r_o()
      ,.returned_credit_reg_id_r_o()
      ,.out_credits_used_o()
      ,.core_req_data_o()
      ,.core_req_addr_o()
      ,.core_req_we_o()
      ,.core_req_v_o()
      ,.core_req_yumi_i(1'b0)
      ,.core_rsp_data_i('0)
      ,.core_rsp_v_i(1'b0)
    );

  bsg_mesh_stitch
    #(
      .width_p(link_sif_width_lp)
      ,.x_max_p(num_tiles_x_lp)
      ,.y_max_p(num_tiles_y_lp)
    ) link_stitch
    (
      .outs_i(tile_link_o)
      ,.ins_o(tile_link_i)
      ,.hor_i(hor_link_sif_i)
      ,.hor_o(hor_link_sif_o)
      ,.ver_i(ver_link_sif_i)
      ,.ver_o(ver_link_sif_o)
    );

  bsg_manycore_tile_compute_mesh
    #(
      .dmem_size_p(1024)
      ,.x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.pod_x_cord_width_p(1)
      ,.pod_y_cord_width_p(1)
      ,.num_tiles_x_p(num_tiles_x_lp)
      ,.num_tiles_y_p(num_tiles_y_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.barrier_ruche_factor_X_p(barrier_ruche_factor_x_lp)
      ,.ipoly_hashing_p(0)
      ,.dims_p(dims_lp)
      ,.barrier_dirs_p(7)
      ,.hetero_type_p(8)
      ,.debug_p(0)
    ) tile0
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.reset_o(tile0_reset_o)
      ,.link_i(tile_link_i[0][0])
      ,.link_o(tile_link_o[0][0])
      ,.barrier_link_i(tile0_barrier_link_i)
      ,.barrier_link_o(tile0_barrier_link_o)
      ,.barrier_ruche_link_i(tile0_barrier_ruche_link_i)
      ,.barrier_ruche_link_o(tile0_barrier_ruche_link_o)
      ,.global_x_i(global_x_i[0])
      ,.global_y_i(global_y_i[0])
      ,.global_x_o(global_x_o[0])
      ,.global_y_o(global_y_o[0])
    );

  bsg_manycore_tile_compute_mesh
    #(
      .dmem_size_p(1024)
      ,.x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.pod_x_cord_width_p(1)
      ,.pod_y_cord_width_p(1)
      ,.num_tiles_x_p(num_tiles_x_lp)
      ,.num_tiles_y_p(num_tiles_y_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.barrier_ruche_factor_X_p(barrier_ruche_factor_x_lp)
      ,.ipoly_hashing_p(0)
      ,.dims_p(dims_lp)
      ,.barrier_dirs_p(7)
      ,.hetero_type_p(8)
      ,.debug_p(0)
    ) tile1
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.reset_o(tile1_reset_o)
      ,.link_i(tile_link_i[0][1])
      ,.link_o(tile_link_o[0][1])
      ,.barrier_link_i(tile1_barrier_link_i)
      ,.barrier_link_o(tile1_barrier_link_o)
      ,.barrier_ruche_link_i(tile1_barrier_ruche_link_i)
      ,.barrier_ruche_link_o(tile1_barrier_ruche_link_o)
      ,.global_x_i(global_x_i[1])
      ,.global_y_i(global_y_i[1])
      ,.global_x_o(global_x_o[1])
      ,.global_y_o(global_y_o[1])
    );

  always_comb begin
    hor_link_sif_i = '{default:'0};
    ver_link_sif_i = '{default:'0};
    ver_barrier_link_i = '0;
    hor_barrier_link_i = '0;
    barrier_ruche_link_i = '0;

    tile0_barrier_link_i = '0;
    tile1_barrier_link_i = '0;
    tile0_barrier_ruche_link_i = '0;
    tile1_barrier_ruche_link_i = '0;

    global_x_i[0] = 2'd0;
    global_x_i[1] = 2'd1;
    global_y_i[0] = 2'd0;
    global_y_i[1] = 2'd0;

    hor_link_sif_i[W][0] = tx0_to_mesh;
    hor_link_sif_i[E][0] = tx1_to_mesh;
  end

  assign mesh_to_tx0 = hor_link_sif_o[W][0];
  assign mesh_to_tx1 = hor_link_sif_o[E][0];
  assign decode_error_pe0_lo = tile0.proc.h.z.bridge_decode_error_lo;
  assign decode_error_pe1_lo = tile1.proc.h.z.bridge_decode_error_lo;

  always_comb begin
    link_activity_now = (|hor_link_sif_o[E][0])
      | (|hor_link_sif_o[W][0])
      | (|hor_link_sif_i[E][0])
      | (|hor_link_sif_i[W][0]);
  end

  always_ff @(posedge clk) begin
    if (reset_i) begin
      link_activity_r <= 1'b0;
      decode_error_seen_r <= 1'b0;
      pe0_ingress_wr_count_r <= '0;
      pe1_ingress_wr_count_r <= '0;
      pe0_cmd_accept_count_r <= '0;
      pe1_cmd_accept_count_r <= '0;
      pe0_host_tx_count_r <= '0;
      pe1_host_tx_count_r <= '0;
      pe0_dut_tx_count_r <= '0;
      pe1_dut_tx_count_r <= '0;
      pe0_compute_done_count_r <= '0;
      pe1_compute_done_count_r <= '0;
      pe0_last_dut_txn_id_r <= '0;
      pe1_last_dut_txn_id_r <= '0;
      pe0_host_prio_r <= 1'b1;
      pe1_host_prio_r <= 1'b1;
      pe0_dut_pending_r <= 1'b0;
      pe1_dut_pending_r <= 1'b0;
      pe0_dut_addr_r <= '0;
      pe1_dut_addr_r <= '0;
      pe0_dut_data_r <= '0;
      pe1_dut_data_r <= '0;
      pe0_dut_txn_id_r <= '0;
      pe1_dut_txn_id_r <= '0;
      pe0_semantic_payload_seen_r <= 1'b0;
      pe1_semantic_payload_seen_r <= 1'b0;
      pe0_semantic_payload_bank_r <= '0;
      pe1_semantic_payload_bank_r <= '0;
      pe0_semantic_payload_row_r <= '0;
      pe1_semantic_payload_row_r <= '0;
      pe0_semantic_payload_first_word_r <= '0;
      pe1_semantic_payload_first_word_r <= '0;
    end else begin
      link_activity_r <= link_activity_r
        | tx0_fire
        | tx1_fire
        | link_activity_now;
      decode_error_seen_r <= decode_error_seen_r | decode_error_pe0_lo | decode_error_pe1_lo;

      if (tx0_host_fire) begin
        pe0_host_tx_count_r <= pe0_host_tx_count_r + 1'b1;
      end
      if (tx1_host_fire) begin
        pe1_host_tx_count_r <= pe1_host_tx_count_r + 1'b1;
      end
      if (tx0_dut_fire) begin
        pe0_dut_tx_count_r <= pe0_dut_tx_count_r + 1'b1;
        pe0_last_dut_txn_id_r <= pe0_dut_txn_id_r;
      end
      if (tx1_dut_fire) begin
        pe1_dut_tx_count_r <= pe1_dut_tx_count_r + 1'b1;
        pe1_last_dut_txn_id_r <= pe1_dut_txn_id_r;
      end

      if (tile0.proc.h.z.pe_compute_done_lo) begin
        pe0_compute_done_count_r <= pe0_compute_done_count_r + 1'b1;
      end
      if (tile1.proc.h.z.pe_compute_done_lo) begin
        pe1_compute_done_count_r <= pe1_compute_done_count_r + 1'b1;
      end

      if (tx0_fire && send0_v && pe0_dut_pending_r) begin
        pe0_host_prio_r <= ~tx0_sel_host;
      end
      if (tx1_fire && send1_v && pe1_dut_pending_r) begin
        pe1_host_prio_r <= ~tx1_sel_host;
      end

      if (tile0.proc.h.z.pe_wr_req_valid_lo && (!pe0_dut_pending_r || tx0_dut_fire)) begin
        pe0_dut_pending_r <= 1'b1;
        pe0_dut_addr_r <= tile0.proc.h.z.pe_wr_req_addr_lo;
        pe0_dut_data_r <= tile0.proc.h.z.pe_wr_req_data_low_lo;
        pe0_dut_txn_id_r <= bsg_manycore_reg_id_width_gp'(tile0.proc.h.z.pe_wr_txn_id_lo);
      end else if (tx0_dut_fire) begin
        pe0_dut_pending_r <= 1'b0;
      end

      if (tile1.proc.h.z.pe_wr_req_valid_lo && (!pe1_dut_pending_r || tx1_dut_fire)) begin
        pe1_dut_pending_r <= 1'b1;
        pe1_dut_addr_r <= tile1.proc.h.z.pe_wr_req_addr_lo;
        pe1_dut_data_r <= tile1.proc.h.z.pe_wr_req_data_low_lo;
        pe1_dut_txn_id_r <= bsg_manycore_reg_id_width_gp'(tile1.proc.h.z.pe_wr_txn_id_lo);
      end else if (tx1_dut_fire) begin
        pe1_dut_pending_r <= 1'b0;
      end

      if (tile0.proc.h.z.pe_ingress_wr_req_valid_lo) begin
        pe0_ingress_wr_count_r <= pe0_ingress_wr_count_r + 1'b1;
        if (tile0.proc.h.z.pe_ingress_wr_req_addr_lo[7:5] >= 3'd4) begin
          pe0_semantic_payload_seen_r <= 1'b1;
          pe0_semantic_payload_bank_r <= tile0.proc.h.z.pe_ingress_wr_req_addr_lo[7:5];
          pe0_semantic_payload_row_r <= tile0.proc.h.z.pe_ingress_wr_req_addr_lo[15:8];
          pe0_semantic_payload_first_word_r <= tile0.proc.h.z.pe_ingress_wr_req_data_low_lo;
        end
      end
      if (tile1.proc.h.z.pe_ingress_wr_req_valid_lo) begin
        pe1_ingress_wr_count_r <= pe1_ingress_wr_count_r + 1'b1;
        if (tile1.proc.h.z.pe_ingress_wr_req_addr_lo[7:5] >= 3'd4) begin
          pe1_semantic_payload_seen_r <= 1'b1;
          pe1_semantic_payload_bank_r <= tile1.proc.h.z.pe_ingress_wr_req_addr_lo[7:5];
          pe1_semantic_payload_row_r <= tile1.proc.h.z.pe_ingress_wr_req_addr_lo[15:8];
          pe1_semantic_payload_first_word_r <= tile1.proc.h.z.pe_ingress_wr_req_data_low_lo;
        end
      end

      if (tile0.proc.h.z.sideband_cmd_valid_lo && tile0.proc.h.z.sideband_cmd_ready_lo) begin
        pe0_cmd_accept_count_r <= pe0_cmd_accept_count_r + 1'b1;
      end
      if (tile1.proc.h.z.sideband_cmd_valid_lo && tile1.proc.h.z.sideband_cmd_ready_lo) begin
        pe1_cmd_accept_count_r <= pe1_cmd_accept_count_r + 1'b1;
      end
    end
  end

  assign link_activity_o = link_activity_r;
  assign decode_error_seen_o = decode_error_seen_r;
  assign pe0_ingress_wr_count_o = pe0_ingress_wr_count_r;
  assign pe1_ingress_wr_count_o = pe1_ingress_wr_count_r;
  assign pe0_cmd_accept_count_o = pe0_cmd_accept_count_r;
  assign pe1_cmd_accept_count_o = pe1_cmd_accept_count_r;
  assign pe0_host_tx_count_o = pe0_host_tx_count_r;
  assign pe1_host_tx_count_o = pe1_host_tx_count_r;
  assign pe0_dut_tx_count_o = pe0_dut_tx_count_r;
  assign pe1_dut_tx_count_o = pe1_dut_tx_count_r;
  assign pe0_compute_done_count_o = pe0_compute_done_count_r;
  assign pe1_compute_done_count_o = pe1_compute_done_count_r;
  assign pe0_last_dut_txn_id_o = pe0_last_dut_txn_id_r;
  assign pe1_last_dut_txn_id_o = pe1_last_dut_txn_id_r;

  assign pe0_state_b1_row0_word0_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[1].u_spm_bank.mem_r[0][31:0];
  assign pe0_state_b2_row0_word0_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[2].u_spm_bank.mem_r[0][31:0];
  assign pe0_state_b3_row0_word0_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[3].u_spm_bank.mem_r[0][31:0];
  assign pe0_state_b1_row0_words_o = tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[1].u_spm_bank.mem_r[0];
  assign pe0_state_b2_row0_words_o = tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[2].u_spm_bank.mem_r[0];
  assign pe0_state_b3_row0_words_o = tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[3].u_spm_bank.mem_r[0];
  assign pe1_state_b1_row0_word0_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[1].u_spm_bank.mem_r[0][31:0];
  assign pe1_state_b2_row0_word0_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[2].u_spm_bank.mem_r[0][31:0];
  assign pe1_state_b3_row0_word0_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[3].u_spm_bank.mem_r[0][31:0];
  assign pe1_state_b1_row0_words_o = tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[1].u_spm_bank.mem_r[0];
  assign pe1_state_b2_row0_words_o = tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[2].u_spm_bank.mem_r[0];
  assign pe1_state_b3_row0_words_o = tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[3].u_spm_bank.mem_r[0];

  assign pe0_message_b4_row0_word0_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[4].u_spm_bank.mem_r[0][31:0];
  assign pe0_message_b5_row0_word0_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[5].u_spm_bank.mem_r[0][31:0];
  assign pe0_message_b6_row0_word0_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[6].u_spm_bank.mem_r[0][31:0];
  assign pe0_message_b7_row0_word0_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[7].u_spm_bank.mem_r[0][31:0];
  assign pe0_message_b4_row0_words_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[4].u_spm_bank.mem_r[0];
  assign pe0_message_b5_row0_words_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[5].u_spm_bank.mem_r[0];
  assign pe0_message_b6_row0_words_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[6].u_spm_bank.mem_r[0];
  assign pe0_message_b7_row0_words_o =
    tile0.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[7].u_spm_bank.mem_r[0];
  assign pe1_message_b4_row0_word0_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[4].u_spm_bank.mem_r[0][31:0];
  assign pe1_message_b5_row0_word0_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[5].u_spm_bank.mem_r[0][31:0];
  assign pe1_message_b6_row0_word0_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[6].u_spm_bank.mem_r[0][31:0];
  assign pe1_message_b7_row0_word0_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[7].u_spm_bank.mem_r[0][31:0];
  assign pe1_message_b4_row0_words_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[4].u_spm_bank.mem_r[0];
  assign pe1_message_b5_row0_words_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[5].u_spm_bank.mem_r[0];
  assign pe1_message_b6_row0_words_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[6].u_spm_bank.mem_r[0];
  assign pe1_message_b7_row0_words_o =
    tile1.proc.h.z.pe.u_spm_subsystem.u_spm_bank_array.banks[7].u_spm_bank.mem_r[0];

  assign pe0_adapter_payload_plane0_row0_o = tile0.proc.h.z.adapter.payload_mem_r[0][0];
  assign pe0_adapter_payload_plane1_row0_o = tile0.proc.h.z.adapter.payload_mem_r[1][0];
  assign pe0_adapter_payload_plane2_row0_o = tile0.proc.h.z.adapter.payload_mem_r[2][0];
  assign pe0_adapter_payload_plane3_row0_o = tile0.proc.h.z.adapter.payload_mem_r[3][0];
  assign pe1_adapter_payload_plane0_row0_o = tile1.proc.h.z.adapter.payload_mem_r[0][0];
  assign pe1_adapter_payload_plane1_row0_o = tile1.proc.h.z.adapter.payload_mem_r[1][0];
  assign pe1_adapter_payload_plane2_row0_o = tile1.proc.h.z.adapter.payload_mem_r[2][0];
  assign pe1_adapter_payload_plane3_row0_o = tile1.proc.h.z.adapter.payload_mem_r[3][0];
  assign pe0_adapter_credit_q0_o = tile0.proc.h.z.adapter.q_credit_r[0];
  assign pe1_adapter_credit_q0_o = tile1.proc.h.z.adapter.q_credit_r[0];
  assign pe0_adapter_tail_q0_o = tile0.proc.h.z.adapter.q_tail_r[0];
  assign pe1_adapter_tail_q0_o = tile1.proc.h.z.adapter.q_tail_r[0];

  assign pe0_wr_req_valid_o = tile0.proc.h.z.pe_wr_req_valid_lo;
  assign pe0_wr_req_addr_o = tile0.proc.h.z.pe_wr_req_addr_lo;
  assign pe0_wr_req_data_o = tile0.proc.h.z.pe_wr_req_data_low_lo;
  assign pe1_wr_req_valid_o = tile1.proc.h.z.pe_wr_req_valid_lo;
  assign pe1_wr_req_addr_o = tile1.proc.h.z.pe_wr_req_addr_lo;
  assign pe1_wr_req_data_o = tile1.proc.h.z.pe_wr_req_data_low_lo;
  assign pe0_ingress_wr_req_valid_o = tile0.proc.h.z.pe_ingress_wr_req_valid_lo;
  assign pe0_ingress_wr_req_addr_o = tile0.proc.h.z.pe_ingress_wr_req_addr_lo;
  assign pe0_ingress_wr_req_data_o = tile0.proc.h.z.pe_ingress_wr_req_data_low_lo;
  assign pe1_ingress_wr_req_valid_o = tile1.proc.h.z.pe_ingress_wr_req_valid_lo;
  assign pe1_ingress_wr_req_addr_o = tile1.proc.h.z.pe_ingress_wr_req_addr_lo;
  assign pe1_ingress_wr_req_data_o = tile1.proc.h.z.pe_ingress_wr_req_data_low_lo;
  assign pe0_semantic_payload_seen_o = pe0_semantic_payload_seen_r;
  assign pe0_semantic_payload_bank_o = pe0_semantic_payload_bank_r;
  assign pe0_semantic_payload_row_o = pe0_semantic_payload_row_r;
  assign pe0_semantic_payload_first_word_o = pe0_semantic_payload_first_word_r;
  assign pe1_semantic_payload_seen_o = pe1_semantic_payload_seen_r;
  assign pe1_semantic_payload_bank_o = pe1_semantic_payload_bank_r;
  assign pe1_semantic_payload_row_o = pe1_semantic_payload_row_r;
  assign pe1_semantic_payload_first_word_o = pe1_semantic_payload_first_word_r;

endmodule

module gbp_pe_mesh_2pe_convergence
  import bsg_manycore_pkg::*;
  import bsg_noc_pkg::*;
(
  input  logic        clk,
  input  logic        rst_n,
  input  logic        send0_v,
  input  logic        send0_we,
  input  logic [15:0] send0_addr,
  input  logic [31:0] send0_data,
  output logic        send0_ready,
  input  logic        send1_v,
  input  logic        send1_we,
  input  logic [15:0] send1_addr,
  input  logic [31:0] send1_data,
  output logic        send1_ready,
  output logic        link_activity_o,
  output logic        decode_error_seen_o,
  output logic [31:0] pe0_ingress_wr_count_o,
  output logic [31:0] pe1_ingress_wr_count_o,
  output logic [31:0] pe0_cmd_accept_count_o,
  output logic [31:0] pe1_cmd_accept_count_o,
  output logic [31:0] pe0_host_tx_count_o,
  output logic [31:0] pe1_host_tx_count_o,
  output logic [31:0] pe0_dut_tx_count_o,
  output logic [31:0] pe1_dut_tx_count_o,
  output logic [31:0] pe0_compute_done_count_o,
  output logic [31:0] pe1_compute_done_count_o,
  output logic [bsg_manycore_reg_id_width_gp-1:0] pe0_last_dut_txn_id_o,
  output logic [bsg_manycore_reg_id_width_gp-1:0] pe1_last_dut_txn_id_o,
  output logic [31:0] pe0_state_b1_row0_word0_o,
  output logic [31:0] pe0_state_b2_row0_word0_o,
  output logic [31:0] pe0_state_b3_row0_word0_o,
  output logic [255:0] pe0_state_b1_row0_words_o,
  output logic [255:0] pe0_state_b2_row0_words_o,
  output logic [255:0] pe0_state_b3_row0_words_o,
  output logic [31:0] pe1_state_b1_row0_word0_o,
  output logic [31:0] pe1_state_b2_row0_word0_o,
  output logic [31:0] pe1_state_b3_row0_word0_o,
  output logic [255:0] pe1_state_b1_row0_words_o,
  output logic [255:0] pe1_state_b2_row0_words_o,
  output logic [255:0] pe1_state_b3_row0_words_o,
  output logic [31:0] pe0_message_b4_row0_word0_o,
  output logic [31:0] pe0_message_b5_row0_word0_o,
  output logic [31:0] pe0_message_b6_row0_word0_o,
  output logic [31:0] pe0_message_b7_row0_word0_o,
  output logic [255:0] pe0_message_b4_row0_words_o,
  output logic [255:0] pe0_message_b5_row0_words_o,
  output logic [255:0] pe0_message_b6_row0_words_o,
  output logic [255:0] pe0_message_b7_row0_words_o,
  output logic [31:0] pe1_message_b4_row0_word0_o,
  output logic [31:0] pe1_message_b5_row0_word0_o,
  output logic [31:0] pe1_message_b6_row0_word0_o,
  output logic [31:0] pe1_message_b7_row0_word0_o,
  output logic [255:0] pe1_message_b4_row0_words_o,
  output logic [255:0] pe1_message_b5_row0_words_o,
  output logic [255:0] pe1_message_b6_row0_words_o,
  output logic [255:0] pe1_message_b7_row0_words_o,
  output logic [31:0] pe0_adapter_payload_plane0_row0_o,
  output logic [31:0] pe0_adapter_payload_plane1_row0_o,
  output logic [31:0] pe0_adapter_payload_plane2_row0_o,
  output logic [31:0] pe0_adapter_payload_plane3_row0_o,
  output logic [31:0] pe1_adapter_payload_plane0_row0_o,
  output logic [31:0] pe1_adapter_payload_plane1_row0_o,
  output logic [31:0] pe1_adapter_payload_plane2_row0_o,
  output logic [31:0] pe1_adapter_payload_plane3_row0_o,
  output logic [7:0] pe0_adapter_credit_q0_o,
  output logic [7:0] pe1_adapter_credit_q0_o,
  output logic [7:0] pe0_adapter_tail_q0_o,
  output logic [7:0] pe1_adapter_tail_q0_o,
  output logic pe0_wr_req_valid_o,
  output logic [15:0] pe0_wr_req_addr_o,
  output logic [31:0] pe0_wr_req_data_o,
  output logic pe1_wr_req_valid_o,
  output logic [15:0] pe1_wr_req_addr_o,
  output logic [31:0] pe1_wr_req_data_o,
  output logic pe0_ingress_wr_req_valid_o,
  output logic [15:0] pe0_ingress_wr_req_addr_o,
  output logic [31:0] pe0_ingress_wr_req_data_o,
  output logic pe1_ingress_wr_req_valid_o,
  output logic [15:0] pe1_ingress_wr_req_addr_o,
  output logic [31:0] pe1_ingress_wr_req_data_o,
  output logic pe0_semantic_payload_seen_o,
  output logic [2:0] pe0_semantic_payload_bank_o,
  output logic [7:0] pe0_semantic_payload_row_o,
  output logic [31:0] pe0_semantic_payload_first_word_o,
  output logic pe1_semantic_payload_seen_o,
  output logic [2:0] pe1_semantic_payload_bank_o,
  output logic [7:0] pe1_semantic_payload_row_o,
  output logic [31:0] pe1_semantic_payload_first_word_o
);

  gbp_pe_mesh_2pe_gbp dut (
    .clk(clk),
    .rst_n(rst_n),
    .send0_v(send0_v),
    .send0_we(send0_we),
    .send0_addr(send0_addr),
    .send0_data(send0_data),
    .send0_ready(send0_ready),
    .send1_v(send1_v),
    .send1_we(send1_we),
    .send1_addr(send1_addr),
    .send1_data(send1_data),
    .send1_ready(send1_ready),
    .link_activity_o(link_activity_o),
    .decode_error_seen_o(decode_error_seen_o),
    .pe0_ingress_wr_count_o(pe0_ingress_wr_count_o),
    .pe1_ingress_wr_count_o(pe1_ingress_wr_count_o),
    .pe0_cmd_accept_count_o(pe0_cmd_accept_count_o),
    .pe1_cmd_accept_count_o(pe1_cmd_accept_count_o),
    .pe0_host_tx_count_o(pe0_host_tx_count_o),
    .pe1_host_tx_count_o(pe1_host_tx_count_o),
    .pe0_dut_tx_count_o(pe0_dut_tx_count_o),
    .pe1_dut_tx_count_o(pe1_dut_tx_count_o),
    .pe0_compute_done_count_o(pe0_compute_done_count_o),
    .pe1_compute_done_count_o(pe1_compute_done_count_o),
    .pe0_last_dut_txn_id_o(pe0_last_dut_txn_id_o),
    .pe1_last_dut_txn_id_o(pe1_last_dut_txn_id_o),
    .pe0_state_b1_row0_word0_o(pe0_state_b1_row0_word0_o),
    .pe0_state_b2_row0_word0_o(pe0_state_b2_row0_word0_o),
    .pe0_state_b3_row0_word0_o(pe0_state_b3_row0_word0_o),
    .pe0_state_b1_row0_words_o(pe0_state_b1_row0_words_o),
    .pe0_state_b2_row0_words_o(pe0_state_b2_row0_words_o),
    .pe0_state_b3_row0_words_o(pe0_state_b3_row0_words_o),
    .pe1_state_b1_row0_word0_o(pe1_state_b1_row0_word0_o),
    .pe1_state_b2_row0_word0_o(pe1_state_b2_row0_word0_o),
    .pe1_state_b3_row0_word0_o(pe1_state_b3_row0_word0_o),
    .pe1_state_b1_row0_words_o(pe1_state_b1_row0_words_o),
    .pe1_state_b2_row0_words_o(pe1_state_b2_row0_words_o),
    .pe1_state_b3_row0_words_o(pe1_state_b3_row0_words_o),
    .pe0_message_b4_row0_word0_o(pe0_message_b4_row0_word0_o),
    .pe0_message_b5_row0_word0_o(pe0_message_b5_row0_word0_o),
    .pe0_message_b6_row0_word0_o(pe0_message_b6_row0_word0_o),
    .pe0_message_b7_row0_word0_o(pe0_message_b7_row0_word0_o),
    .pe0_message_b4_row0_words_o(pe0_message_b4_row0_words_o),
    .pe0_message_b5_row0_words_o(pe0_message_b5_row0_words_o),
    .pe0_message_b6_row0_words_o(pe0_message_b6_row0_words_o),
    .pe0_message_b7_row0_words_o(pe0_message_b7_row0_words_o),
    .pe1_message_b4_row0_word0_o(pe1_message_b4_row0_word0_o),
    .pe1_message_b5_row0_word0_o(pe1_message_b5_row0_word0_o),
    .pe1_message_b6_row0_word0_o(pe1_message_b6_row0_word0_o),
    .pe1_message_b7_row0_word0_o(pe1_message_b7_row0_word0_o),
    .pe1_message_b4_row0_words_o(pe1_message_b4_row0_words_o),
    .pe1_message_b5_row0_words_o(pe1_message_b5_row0_words_o),
    .pe1_message_b6_row0_words_o(pe1_message_b6_row0_words_o),
    .pe1_message_b7_row0_words_o(pe1_message_b7_row0_words_o),
    .pe0_adapter_payload_plane0_row0_o(pe0_adapter_payload_plane0_row0_o),
    .pe0_adapter_payload_plane1_row0_o(pe0_adapter_payload_plane1_row0_o),
    .pe0_adapter_payload_plane2_row0_o(pe0_adapter_payload_plane2_row0_o),
    .pe0_adapter_payload_plane3_row0_o(pe0_adapter_payload_plane3_row0_o),
    .pe1_adapter_payload_plane0_row0_o(pe1_adapter_payload_plane0_row0_o),
    .pe1_adapter_payload_plane1_row0_o(pe1_adapter_payload_plane1_row0_o),
    .pe1_adapter_payload_plane2_row0_o(pe1_adapter_payload_plane2_row0_o),
    .pe1_adapter_payload_plane3_row0_o(pe1_adapter_payload_plane3_row0_o),
    .pe0_adapter_credit_q0_o(pe0_adapter_credit_q0_o),
    .pe1_adapter_credit_q0_o(pe1_adapter_credit_q0_o),
    .pe0_adapter_tail_q0_o(pe0_adapter_tail_q0_o),
    .pe1_adapter_tail_q0_o(pe1_adapter_tail_q0_o),
    .pe0_wr_req_valid_o(pe0_wr_req_valid_o),
    .pe0_wr_req_addr_o(pe0_wr_req_addr_o),
    .pe0_wr_req_data_o(pe0_wr_req_data_o),
    .pe1_wr_req_valid_o(pe1_wr_req_valid_o),
    .pe1_wr_req_addr_o(pe1_wr_req_addr_o),
    .pe1_wr_req_data_o(pe1_wr_req_data_o),
    .pe0_ingress_wr_req_valid_o(pe0_ingress_wr_req_valid_o),
    .pe0_ingress_wr_req_addr_o(pe0_ingress_wr_req_addr_o),
    .pe0_ingress_wr_req_data_o(pe0_ingress_wr_req_data_o),
    .pe1_ingress_wr_req_valid_o(pe1_ingress_wr_req_valid_o),
    .pe1_ingress_wr_req_addr_o(pe1_ingress_wr_req_addr_o),
    .pe1_ingress_wr_req_data_o(pe1_ingress_wr_req_data_o),
    .pe0_semantic_payload_seen_o(pe0_semantic_payload_seen_o),
    .pe0_semantic_payload_bank_o(pe0_semantic_payload_bank_o),
    .pe0_semantic_payload_row_o(pe0_semantic_payload_row_o),
    .pe0_semantic_payload_first_word_o(pe0_semantic_payload_first_word_o),
    .pe1_semantic_payload_seen_o(pe1_semantic_payload_seen_o),
    .pe1_semantic_payload_bank_o(pe1_semantic_payload_bank_o),
    .pe1_semantic_payload_row_o(pe1_semantic_payload_row_o),
    .pe1_semantic_payload_first_word_o(pe1_semantic_payload_first_word_o)
  );

endmodule
