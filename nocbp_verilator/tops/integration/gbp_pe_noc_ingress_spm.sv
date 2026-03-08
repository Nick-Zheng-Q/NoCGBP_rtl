`include "bsg_manycore_defines.svh"

module gbp_pe_noc_ingress_spm
  import bsg_manycore_pkg::*;
  import gbp_pkg::*;
(
  input  logic                  clk,
  input  logic                  rst_n,
  input  logic                  send_v,
  input  logic                  send_we,
  input  logic [SPM_ADDR_W-1:0] send_addr,
  input  logic [31:0]           send_data,
  output logic                  send_ready,
  input  logic                  ingress_stall,
  output logic                  decode_error,
  output logic                  ingress_intent_v,
  output logic                  ingress_lane_ready,
  output logic [GBP_INGRESS_BANK_W-1:0] ingress_bank,
  output logic                  lane0_wr_req_valid,
  output logic [SPM_ADDR_W-1:0] lane0_wr_req_addr,
  output logic [31:0]           lane0_wr_req_data,
  output logic                  lane1_wr_req_valid,
  output logic [SPM_ADDR_W-1:0] lane1_wr_req_addr,
  output logic [31:0]           lane1_wr_req_data
);

  localparam int x_cord_width_lp = 2;
  localparam int y_cord_width_lp = 2;
  localparam int data_width_lp = 32;
  localparam int addr_width_lp = SPM_ADDR_W;
  localparam int credit_counter_width_lp = 8;
  localparam int packet_width_lp =
    `bsg_manycore_packet_width(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);
  localparam int link_sif_width_lp =
    `bsg_manycore_link_sif_width(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);

  logic reset_i;
  assign reset_i = ~rst_n;

  `declare_bsg_manycore_packet_s(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);
  `declare_bsg_manycore_link_sif_s(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);

  logic [link_sif_width_lp-1:0] tx_to_dut;
  logic [link_sif_width_lp-1:0] dut_to_tx;

  bsg_manycore_packet_s tx_pkt_s;
  logic [packet_width_lp-1:0] tx_pkt_bits;
  logic tx_out_v;
  logic tx_out_ready;
  logic [data_width_lp-1:0] tx_ret_data;
  logic [bsg_manycore_reg_id_width_gp-1:0] tx_ret_reg_id;
  logic tx_ret_v;
  bsg_manycore_return_packet_type_e tx_ret_type;
  logic tx_ret_fifo_full;
  logic tx_ret_credit_v;
  logic [bsg_manycore_reg_id_width_gp-1:0] tx_ret_credit_reg_id;
  logic [credit_counter_width_lp-1:0] tx_credits_used;
  logic dummy_core_req_v;

  always_comb begin
    tx_pkt_s = '0;
    tx_pkt_s.addr = send_addr;
    tx_pkt_s.op_v2 = send_we ? e_remote_store : e_remote_load;
    tx_pkt_s.reg_id = '1;
    tx_pkt_s.payload.data = send_data;
    tx_pkt_s.src_x_cord = 2'd0;
    tx_pkt_s.src_y_cord = 2'd0;
    tx_pkt_s.x_cord = 2'd1;
    tx_pkt_s.y_cord = 2'd0;
  end

  assign tx_pkt_bits = tx_pkt_s;

  gbp_pe_endpoint_adapter
    #(
      .x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.fifo_els_p(2)
      ,.rev_fifo_els_p(2)
      ,.credit_counter_width_p(credit_counter_width_lp)
    ) tx_ep
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(dut_to_tx)
      ,.link_sif_o(tx_to_dut)
      ,.global_x_i(2'd0)
      ,.global_y_i(2'd0)
      ,.out_packet_i(tx_pkt_bits)
      ,.out_v_i(tx_out_v)
      ,.out_credit_or_ready_o(tx_out_ready)
      ,.returned_data_r_o(tx_ret_data)
      ,.returned_reg_id_r_o(tx_ret_reg_id)
      ,.returned_v_r_o(tx_ret_v)
      ,.returned_pkt_type_r_o(tx_ret_type)
      ,.returned_yumi_i(tx_ret_v)
      ,.returned_fifo_full_o(tx_ret_fifo_full)
      ,.returned_credit_v_r_o(tx_ret_credit_v)
      ,.returned_credit_reg_id_r_o(tx_ret_credit_reg_id)
      ,.out_credits_used_o(tx_credits_used)
      ,.core_req_data_o()
      ,.core_req_addr_o()
      ,.core_req_we_o()
      ,.core_req_v_o(dummy_core_req_v)
      ,.core_req_yumi_i(dummy_core_req_v)
      ,.core_rsp_data_i('0)
      ,.core_rsp_v_i(1'b0)
    );

  assign tx_out_v = send_v;
  assign send_ready = tx_out_ready;

  gbp_pe
    #(
      .x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.dmem_size_p(1024)
      ,.num_tiles_x_p(2)
      ,.num_tiles_y_p(2)
      ,.pod_x_cord_width_p(1)
      ,.pod_y_cord_width_p(1)
      ,.fwd_fifo_els_p(2)
      ,.rev_fifo_els_p(2)
      ,.barrier_dirs_p(5)
      ,.ipoly_hashing_p(0)
    ) dut
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(tx_to_dut)
      ,.link_sif_o(dut_to_tx)
      ,.barrier_data_i(1'b0)
      ,.barrier_data_o()
      ,.barrier_src_r_o()
      ,.barrier_dest_r_o()
      ,.my_x_i(1'b1)
      ,.my_y_i('0)
      ,.pod_x_i('0)
      ,.pod_y_i('0)
    );

  assign ingress_intent_v = dut.ingress_intent_v_lo;
  assign ingress_lane_ready = dut.pe_ingress_wr_ready_lo & ~ingress_stall;
  assign ingress_bank = dut.ingress_intent_bank_lo;
  assign decode_error = dut.bridge_decode_error_lo;
  assign lane0_wr_req_valid = dut.pe_wr_req_valid_lo;
  assign lane0_wr_req_addr = dut.pe_wr_req_addr_lo;
  assign lane0_wr_req_data = dut.pe_wr_req_data_low_lo;
  assign lane1_wr_req_valid = dut.pe_ingress_wr_req_valid_lo;
  assign lane1_wr_req_addr = dut.pe_ingress_wr_req_addr_lo;
  assign lane1_wr_req_data = dut.pe_ingress_wr_req_data_low_lo;

endmodule
