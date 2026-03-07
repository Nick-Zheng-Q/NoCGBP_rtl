`include "bsg_manycore_defines.svh"

module gbp_pe
  import bsg_manycore_pkg::*;
  #(
    `BSG_INV_PARAM(x_cord_width_p)
    , `BSG_INV_PARAM(y_cord_width_p)
    , `BSG_INV_PARAM(data_width_p)
    , `BSG_INV_PARAM(addr_width_p)
    , `BSG_INV_PARAM(dmem_size_p)
    , debug_p = 0
    , `BSG_INV_PARAM(num_tiles_x_p)
    , `BSG_INV_PARAM(num_tiles_y_p)
    , `BSG_INV_PARAM(pod_x_cord_width_p)
    , `BSG_INV_PARAM(pod_y_cord_width_p)
    , `BSG_INV_PARAM(fwd_fifo_els_p)
    , `BSG_INV_PARAM(rev_fifo_els_p)
    , `BSG_INV_PARAM(barrier_dirs_p)
    , `BSG_INV_PARAM(ipoly_hashing_p)

    , localparam x_subcord_width_lp = `BSG_SAFE_CLOG2(num_tiles_x_p)
    , localparam y_subcord_width_lp = `BSG_SAFE_CLOG2(num_tiles_y_p)
    , localparam barrier_lg_dirs_lp = `BSG_SAFE_CLOG2(barrier_dirs_p+1)
    , localparam link_sif_width_lp =
      `bsg_manycore_link_sif_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)
    , localparam packet_width_lp =
      `bsg_manycore_packet_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)
    , localparam credit_counter_width_lp = `BSG_WIDTH(32)
  )
  (
    input clk_i
    , input reset_i

    , input  [link_sif_width_lp-1:0] link_sif_i
    , output [link_sif_width_lp-1:0] link_sif_o

    , input  barrier_data_i
    , output barrier_data_o
    , output [barrier_dirs_p-1:0]     barrier_src_r_o
    , output [barrier_lg_dirs_lp-1:0] barrier_dest_r_o

    , input [x_subcord_width_lp-1:0] my_x_i
    , input [y_subcord_width_lp-1:0] my_y_i

    , input [pod_x_cord_width_p-1:0] pod_x_i
    , input [pod_y_cord_width_p-1:0] pod_y_i
  );

  logic rst_n;
  assign rst_n = ~reset_i;

  logic [data_width_p-1:0] core_req_data_lo;
  logic [addr_width_p-1:0] core_req_addr_lo;
  logic core_req_we_lo;
  logic core_req_v_lo;
  logic core_req_yumi_li;
  logic [data_width_p-1:0] core_rsp_data_li;
  logic core_rsp_v_li;

  logic [packet_width_lp-1:0] out_packet_li;
  logic out_v_li;
  logic out_credit_or_ready_lo;

  logic [data_width_p-1:0] returned_data_lo;
  logic [bsg_manycore_reg_id_width_gp-1:0] returned_reg_id_lo;
  logic returned_v_lo;
  bsg_manycore_return_packet_type_e returned_pkt_type_lo;
  logic returned_yumi_li;
  logic returned_fifo_full_lo;
  logic returned_credit_v_lo;
  logic [bsg_manycore_reg_id_width_gp-1:0] returned_credit_reg_id_lo;
  logic [credit_counter_width_lp-1:0] out_credits_used_lo;

  gbp_pe_endpoint_adapter
    #(
      .x_cord_width_p(x_cord_width_p)
      ,.y_cord_width_p(y_cord_width_p)
      ,.data_width_p(data_width_p)
      ,.addr_width_p(addr_width_p)
      ,.fifo_els_p(fwd_fifo_els_p)
      ,.rev_fifo_els_p(rev_fifo_els_p)
      ,.credit_counter_width_p(credit_counter_width_lp)
    ) adapter
    (
      .clk_i(clk_i)
      ,.reset_i(reset_i)
      ,.link_sif_i(link_sif_i)
      ,.link_sif_o(link_sif_o)
      ,.global_x_i({pod_x_i, my_x_i})
      ,.global_y_i({pod_y_i, my_y_i})

      ,.out_packet_i(out_packet_li)
      ,.out_v_i(out_v_li)
      ,.out_credit_or_ready_o(out_credit_or_ready_lo)

      ,.returned_data_r_o(returned_data_lo)
      ,.returned_reg_id_r_o(returned_reg_id_lo)
      ,.returned_v_r_o(returned_v_lo)
      ,.returned_pkt_type_r_o(returned_pkt_type_lo)
      ,.returned_yumi_i(returned_yumi_li)
      ,.returned_fifo_full_o(returned_fifo_full_lo)
      ,.returned_credit_v_r_o(returned_credit_v_lo)
      ,.returned_credit_reg_id_r_o(returned_credit_reg_id_lo)
      ,.out_credits_used_o(out_credits_used_lo)

      ,.core_req_data_o(core_req_data_lo)
      ,.core_req_addr_o(core_req_addr_lo)
      ,.core_req_we_o(core_req_we_lo)
      ,.core_req_v_o(core_req_v_lo)
      ,.core_req_yumi_i(core_req_yumi_li)
      ,.core_rsp_data_i(core_rsp_data_li)
      ,.core_rsp_v_i(core_rsp_v_li)
    );

  assign out_v_li = 1'b0;
  assign out_packet_li = '0;
  assign returned_yumi_li = returned_v_lo;

  assign core_req_yumi_li = core_req_v_lo;
  assign core_rsp_data_li = '0;
  assign core_rsp_v_li = 1'b0;

  assign barrier_data_o = 1'b0;
  assign barrier_src_r_o = '0;
  assign barrier_dest_r_o = '0;

  logic unused_barrier_data;
  assign unused_barrier_data = barrier_data_i;
  logic unused_rst_n;
  assign unused_rst_n = rst_n;

endmodule
