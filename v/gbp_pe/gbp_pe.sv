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
  logic [packet_width_lp-1:0] compute_done_packet_lo;
  logic [packet_width_lp-1:0] egress_packet_r;
  logic egress_pending_r;
  logic pe_compute_done_r;
  logic pe_compute_done_pulse_lo;

  logic sideband_cmd_valid_lo;
  logic [1:0] sideband_cmd_kind_lo;
  logic [gbp_pkg::TXN_ID_W-1:0] sideband_cmd_txn_id_lo;
  logic sideband_cmd_ready_lo;
  logic sideband_rsp_done_lo;
  logic sideband_rsp_error_lo;

  logic ingress_intent_v_lo;
  logic [addr_width_p-1:0] ingress_intent_addr_lo;
  logic [data_width_p-1:0] ingress_intent_data_lo;
  logic [gbp_pkg::GBP_INGRESS_BANK_W-1:0] ingress_intent_bank_lo;
  logic [3:0] ingress_intent_qid_lo;
  logic ingress_intent_ready_lo;

  logic pe_rd_req_valid_lo;
  logic [gbp_pkg::SPM_ADDR_W-1:0] pe_rd_req_addr_lo;
  logic pe_wr_req_valid_lo;
  logic [gbp_pkg::SPM_ADDR_W-1:0] pe_wr_req_addr_lo;
  logic [31:0] pe_wr_req_data_low_lo;
  logic pe_compute_start_lo;
  logic pe_compute_done_lo;
  logic [gbp_pkg::TXN_ID_W-1:0] pe_wr_txn_id_lo;
  logic [gbp_pkg::TXN_ID_W-1:0] pe_cmd_txn_id_lo;
  logic pe_ingress_wr_ready_lo;
  logic pe_ingress_wr_req_valid_lo;
  logic [gbp_pkg::SPM_ADDR_W-1:0] pe_ingress_wr_req_addr_lo;
  logic [31:0] pe_ingress_wr_req_data_low_lo;

  logic bridge_decode_error_lo;

  `declare_bsg_manycore_packet_s(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p);
  bsg_manycore_packet_s compute_done_packet_cast_lo;

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
      ,.forward_local_writes_p(1'b1)
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

  gbp_pe_noc_bridge
    #(
      .data_width_p(data_width_p)
      ,.addr_width_p(addr_width_p)
    ) bridge
    (
      .clk_i(clk_i)
      ,.reset_i(reset_i)
      ,.core_req_data_i(core_req_data_lo)
      ,.core_req_addr_i(core_req_addr_lo)
      ,.core_req_we_i(core_req_we_lo)
      ,.core_req_v_i(core_req_v_lo)
      ,.core_req_yumi_o(core_req_yumi_li)
      ,.core_rsp_data_o(core_rsp_data_li)
      ,.core_rsp_v_o(core_rsp_v_li)
      ,.sideband_cmd_valid_o(sideband_cmd_valid_lo)
      ,.sideband_cmd_kind_o(sideband_cmd_kind_lo)
      ,.sideband_cmd_txn_id_o(sideband_cmd_txn_id_lo)
      ,.sideband_cmd_ready_i(sideband_cmd_ready_lo)
      ,.sideband_rsp_done_i(sideband_rsp_done_lo)
      ,.sideband_rsp_error_i(sideband_rsp_error_lo)
      ,.ingress_intent_v_o(ingress_intent_v_lo)
      ,.ingress_intent_addr_o(ingress_intent_addr_lo)
      ,.ingress_intent_data_o(ingress_intent_data_lo)
      ,.ingress_intent_bank_o(ingress_intent_bank_lo)
      ,.ingress_intent_qid_o(ingress_intent_qid_lo)
      ,.ingress_intent_ready_i(ingress_intent_ready_lo)
      ,.decode_error_o(bridge_decode_error_lo)
    );

  pe_top pe
    (
      .clk_i(clk_i)
      ,.reset_i(reset_i)
      ,.cmd_valid_i(sideband_cmd_valid_lo)
      ,.cmd_kind_i(sideband_cmd_kind_lo)
      ,.cmd_txn_id_i(sideband_cmd_txn_id_lo)
      ,.cmd_ready_o(sideband_cmd_ready_lo)
      ,.rsp_done_o(sideband_rsp_done_lo)
      ,.rsp_error_o(sideband_rsp_error_lo)
      ,.ingress_wr_valid_i(ingress_intent_v_lo)
      ,.ingress_wr_addr_i(gbp_pkg::SPM_ADDR_W'(ingress_intent_addr_lo))
      ,.ingress_wr_data_i({{(gbp_pkg::BEAT_BITS-data_width_p){1'b0}}, ingress_intent_data_lo})
      ,.ingress_wr_ready_o(pe_ingress_wr_ready_lo)
      ,.rd_req_valid_o(pe_rd_req_valid_lo)
      ,.rd_req_addr_o(pe_rd_req_addr_lo)
      ,.wr_req_valid_o(pe_wr_req_valid_lo)
      ,.wr_req_addr_o(pe_wr_req_addr_lo)
      ,.wr_req_data_low_o(pe_wr_req_data_low_lo)
      ,.ingress_wr_req_valid_o(pe_ingress_wr_req_valid_lo)
      ,.ingress_wr_req_addr_o(pe_ingress_wr_req_addr_lo)
      ,.ingress_wr_req_data_low_o(pe_ingress_wr_req_data_low_lo)
      ,.compute_start_o(pe_compute_start_lo)
      ,.compute_done_o(pe_compute_done_lo)
      ,.wr_txn_id_o(pe_wr_txn_id_lo)
      ,.cmd_txn_id_o(pe_cmd_txn_id_lo)
    );

  assign ingress_intent_ready_lo = pe_ingress_wr_ready_lo;

  assign compute_done_packet_cast_lo = '{
    addr: '0
    , op_v2: bsg_manycore_pkg::e_remote_sw
    , reg_id: bsg_manycore_reg_id_width_gp'(pe_cmd_txn_id_lo)
    , payload: data_width_p'(pe_cmd_txn_id_lo)
    , src_y_cord: {pod_y_i, my_y_i}
    , src_x_cord: {pod_x_i, my_x_i}
    , y_cord: {pod_y_i, my_y_i}
    , x_cord: {pod_x_i, my_x_i}
  };

  assign compute_done_packet_lo = compute_done_packet_cast_lo;
  assign pe_compute_done_pulse_lo = pe_compute_done_lo & ~pe_compute_done_r;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      egress_pending_r <= 1'b0;
      egress_packet_r <= '0;
      pe_compute_done_r <= 1'b0;
    end else begin
      pe_compute_done_r <= pe_compute_done_lo;

      if (pe_compute_done_pulse_lo) begin
        egress_pending_r <= 1'b1;
        egress_packet_r <= compute_done_packet_lo;
      end else if (egress_pending_r & out_credit_or_ready_lo) begin
        egress_pending_r <= 1'b0;
      end
    end
  end

  assign out_v_li = egress_pending_r;
  assign out_packet_li = egress_packet_r;
  assign returned_yumi_li = returned_v_lo;

  assign barrier_data_o = 1'b0;
  assign barrier_src_r_o = '0;
  assign barrier_dest_r_o = '0;

  logic unused_barrier_data;
  assign unused_barrier_data = barrier_data_i;
  logic unused_rst_n;
  assign unused_rst_n = rst_n;

  logic unused_pe_signals;
  assign unused_pe_signals = pe_rd_req_valid_lo | pe_wr_req_valid_lo | pe_compute_start_lo | pe_compute_done_lo
    | (^pe_rd_req_addr_lo) | (^pe_wr_req_addr_lo) | (^pe_wr_req_data_low_lo) | (^pe_wr_txn_id_lo)
    | bridge_decode_error_lo | (^ingress_intent_bank_lo) | (^ingress_intent_qid_lo)
    | pe_ingress_wr_req_valid_lo | (^pe_ingress_wr_req_addr_lo) | (^pe_ingress_wr_req_data_low_lo);

endmodule
