`include "bsg_manycore_defines.svh"

module gbp_pe_compute_done_egress
  import bsg_manycore_pkg::*;
(
  input  logic clk,
  input  logic rst_n,
  input  logic force_spm_stall_i,
  input  logic noc_ready_i,
  output logic compute_done_o,
  output logic persistence_done_o,
  output logic [gbp_pkg::TXN_ID_W-1:0] compute_txn_id_o,
  output logic egress_v_o,
  output logic [bsg_manycore_reg_id_width_gp-1:0] egress_txn_id_o,
  output logic egress_pending_o
);

  localparam int x_cord_width_lp = 2;
  localparam int y_cord_width_lp = 2;
  localparam int data_width_lp = 32;
  localparam int addr_width_lp = gbp_pkg::SPM_ADDR_W;
  localparam int num_tiles_x_lp = 2;
  localparam int num_tiles_y_lp = 2;
  localparam int pod_x_cord_width_lp = 1;
  localparam int pod_y_cord_width_lp = 1;
  localparam int barrier_dirs_lp = 5;
  localparam int link_sif_width_lp =
    `bsg_manycore_link_sif_width(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);

  `declare_bsg_manycore_link_sif_s(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);
  `declare_bsg_manycore_packet_s(addr_width_lp, data_width_lp, x_cord_width_lp, y_cord_width_lp);

  logic reset_i;
  logic [link_sif_width_lp-1:0] link_sif_i_bits;
  logic [link_sif_width_lp-1:0] link_sif_o_bits;
  bsg_manycore_link_sif_s link_sif_i_cast;
  bsg_manycore_link_sif_s link_sif_o_cast;
  bsg_manycore_packet_s egress_packet_s;

  assign reset_i = ~rst_n;
  assign link_sif_i_bits = link_sif_i_cast;
  assign link_sif_o_cast = bsg_manycore_link_sif_s'(link_sif_o_bits);
  assign egress_packet_s = bsg_manycore_packet_s'(link_sif_o_cast.fwd.data);

  always_comb begin
    link_sif_i_cast = '0;
    link_sif_i_cast.fwd.ready_and_rev = noc_ready_i;
    link_sif_i_cast.rev.ready_and_rev = 1'b1;
    dut.force_persistence_stall_lo = force_spm_stall_i;
  end

  gbp_pe
    #(
      .x_cord_width_p(x_cord_width_lp)
      ,.y_cord_width_p(y_cord_width_lp)
      ,.data_width_p(data_width_lp)
      ,.addr_width_p(addr_width_lp)
      ,.dmem_size_p(1024)
      ,.num_tiles_x_p(num_tiles_x_lp)
      ,.num_tiles_y_p(num_tiles_y_lp)
      ,.pod_x_cord_width_p(pod_x_cord_width_lp)
      ,.pod_y_cord_width_p(pod_y_cord_width_lp)
      ,.fwd_fifo_els_p(2)
      ,.rev_fifo_els_p(2)
      ,.barrier_dirs_p(barrier_dirs_lp)
      ,.ipoly_hashing_p(0)
    ) dut
    (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(link_sif_i_bits)
      ,.link_sif_o(link_sif_o_bits)
      ,.barrier_data_i(1'b0)
      ,.barrier_data_o()
      ,.barrier_src_r_o()
      ,.barrier_dest_r_o()
      ,.my_x_i('0)
      ,.my_y_i('0)
      ,.pod_x_i('0)
      ,.pod_y_i('0)
    );

  assign compute_done_o = dut.pe_compute_done_lo;
  assign persistence_done_o = dut.pe.compute_rsp_done_lo;
  assign compute_txn_id_o = dut.pe_wr_txn_id_lo;
  assign egress_v_o = link_sif_o_cast.fwd.v;
  assign egress_txn_id_o = egress_packet_s.reg_id;
  assign egress_pending_o = dut.egress_pending_r;

endmodule
