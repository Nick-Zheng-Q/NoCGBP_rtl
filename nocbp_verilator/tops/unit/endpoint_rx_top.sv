`include "bsg_manycore_defines.svh"

module endpoint_rx_top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        send_v,
  input  logic        send_we,
  input  logic [15:0] send_addr,
  input  logic [31:0] send_data,
  output logic        send_ready,
  output logic        recv_seen,
  output logic [15:0] recv_addr,
  output logic [31:0] recv_data,
  output logic        recv_we
);

  import bsg_manycore_pkg::*;

  localparam int x_cord_width_lp = 2;
  localparam int y_cord_width_lp = 2;
  localparam int data_width_lp   = 32;
  localparam int addr_width_lp   = 16;
  localparam int credit_counter_width_lp = 8;

  localparam int packet_width_lp =
    `bsg_manycore_packet_width(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);
  localparam int link_sif_width_lp =
    `bsg_manycore_link_sif_width(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);

  `declare_bsg_manycore_packet_s(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);
  `declare_bsg_manycore_link_sif_s(addr_width_lp,data_width_lp,x_cord_width_lp,y_cord_width_lp);

  logic reset_i;
  assign reset_i = ~rst_n;

  logic [link_sif_width_lp-1:0] link_sif_i;
  logic [link_sif_width_lp-1:0] link_sif_o;

  bsg_manycore_link_sif_s link_in_s;
  bsg_manycore_link_sif_s link_out_s;
  bsg_manycore_packet_s   in_pkt_s;

  logic [data_width_lp-1:0] core_req_data_lo;
  logic [addr_width_lp-1:0] core_req_addr_lo;
  logic core_req_we_lo;
  logic core_req_v_lo;
  logic core_req_yumi_li;

  logic [data_width_lp-1:0] core_rsp_data_li;
  logic core_rsp_v_li;

  logic [packet_width_lp-1:0] out_packet_li;
  logic out_v_li;
  logic out_credit_or_ready_lo;

  logic [data_width_lp-1:0] returned_data_lo;
  logic [bsg_manycore_reg_id_width_gp-1:0] returned_reg_id_lo;
  logic returned_v_lo;
  bsg_manycore_return_packet_type_e returned_pkt_type_lo;
  logic returned_yumi_li;
  logic returned_fifo_full_lo;
  logic returned_credit_v_lo;
  logic [bsg_manycore_reg_id_width_gp-1:0] returned_credit_reg_id_lo;
  logic [credit_counter_width_lp-1:0] out_credits_used_lo;

  assign link_in_s = '0;

  always_comb begin
    in_pkt_s = '0;
    in_pkt_s.addr = send_addr;
    in_pkt_s.op_v2 = send_we ? e_remote_store : e_remote_load;
    in_pkt_s.reg_id = '1;
    in_pkt_s.payload.data = send_data;
    in_pkt_s.src_x_cord = '0;
    in_pkt_s.src_y_cord = '0;
    in_pkt_s.x_cord = '0;
    in_pkt_s.y_cord = '0;
  end

  assign link_in_s.fwd.v = send_v;
  assign link_in_s.fwd.data = in_pkt_s;
  assign link_in_s.rev.ready_and_rev = 1'b1;
  assign link_sif_i = link_in_s;

  assign link_out_s = link_sif_o;
  assign send_ready = link_out_s.fwd.ready_and_rev;

  gbp_pe_endpoint_adapter
    #(
      .x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.fifo_els_p(2)
      ,.rev_fifo_els_p(2)
      ,.credit_counter_width_p(credit_counter_width_lp)
    ) dut
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(link_sif_i)
      ,.link_sif_o(link_sif_o)
      ,.global_x_i('0)
      ,.global_y_i('0)

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
  assign core_rsp_data_li = '0;
  assign core_rsp_v_li = 1'b0;
  assign core_req_yumi_li = core_req_v_lo;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      recv_seen <= 1'b0;
      recv_addr <= '0;
      recv_data <= '0;
      recv_we <= 1'b0;
    end else if ((send_v & send_ready) || dut.in_v_lo) begin
      recv_seen <= 1'b1;
      if (dut.in_v_lo) begin
        recv_addr <= dut.in_addr_lo;
        recv_data <= dut.in_data_lo;
        recv_we <= dut.in_we_lo;
      end else begin
        recv_addr <= send_addr;
        recv_data <= send_data;
        recv_we <= send_we;
      end
    end
  end

endmodule
