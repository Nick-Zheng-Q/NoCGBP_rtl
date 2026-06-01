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
  output logic egress_pending_o,
  output logic [31:0] obs_compute_done_pulse_count_o,
  output logic [31:0] obs_rsp_done_pulse_count_o,
  output logic [31:0] obs_egress_accept_count_o,
  output logic [31:0] obs_ingress_write_count_o,
  output logic [31:0] obs_ingress_consume_count_o,
  output logic [31:0] obs_iteration_id_o,
  output logic [31:0] obs_last_egress_accept_iteration_o
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
  logic prev_compute_done_r;
  logic prev_persistence_done_r;
  logic [31:0] obs_compute_done_pulse_count_r;
  logic [31:0] obs_rsp_done_pulse_count_r;
  logic [31:0] obs_egress_accept_count_r;
  logic [31:0] obs_ingress_write_count_r;
  logic [31:0] obs_ingress_consume_count_r;
  logic [31:0] obs_iteration_id_r;
  logic [31:0] obs_last_egress_accept_iteration_r;
  logic compute_done_lo;
  logic persistence_done_lo;
  logic compute_done_pulse_lo;
  logic persistence_done_pulse_lo;
  logic egress_accept_lo;
  logic ingress_write_lo;
  logic ingress_consume_lo;

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

  assign compute_done_lo = dut.pe_compute_done_lo;
  assign persistence_done_lo = dut.pe.compute_rsp_done_lo;
  assign ingress_write_lo = dut.pe_ingress_wr_req_valid_lo;
  assign ingress_consume_lo = dut.pe.ingress_data_fifo_unqueue_lo;
  assign egress_accept_lo = dut.egress_pending_r & dut.out_credit_or_ready_lo;
  assign compute_done_pulse_lo = compute_done_lo & ~prev_compute_done_r;
  assign persistence_done_pulse_lo = persistence_done_lo & ~prev_persistence_done_r;

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

  always_ff @(posedge clk) begin
    if (reset_i) begin
      prev_compute_done_r <= 1'b0;
      prev_persistence_done_r <= 1'b0;
      obs_compute_done_pulse_count_r <= '0;
      obs_rsp_done_pulse_count_r <= '0;
      obs_egress_accept_count_r <= '0;
      obs_ingress_write_count_r <= '0;
      obs_ingress_consume_count_r <= '0;
      obs_iteration_id_r <= '0;
      obs_last_egress_accept_iteration_r <= '0;
    end else begin
      prev_compute_done_r <= compute_done_lo;
      prev_persistence_done_r <= persistence_done_lo;

      if (compute_done_pulse_lo) begin
        obs_compute_done_pulse_count_r <= obs_compute_done_pulse_count_r + 1'b1;
        obs_iteration_id_r <= obs_iteration_id_r + 1'b1;
      end

      if (persistence_done_pulse_lo) begin
        obs_rsp_done_pulse_count_r <= obs_rsp_done_pulse_count_r + 1'b1;
      end

      if (ingress_write_lo) begin
        obs_ingress_write_count_r <= obs_ingress_write_count_r + 1'b1;
      end

      if (ingress_consume_lo) begin
        obs_ingress_consume_count_r <= obs_ingress_consume_count_r + 1'b1;
      end

      if (egress_accept_lo) begin
        obs_egress_accept_count_r <= obs_egress_accept_count_r + 1'b1;
        obs_last_egress_accept_iteration_r <= obs_iteration_id_r + (compute_done_pulse_lo ? 1'b1 : 1'b0);
      end
    end
  end

  assign compute_done_o = compute_done_lo;
  assign persistence_done_o = persistence_done_lo;
  assign compute_txn_id_o = dut.pe_wr_txn_id_lo;
  assign egress_v_o = link_sif_o_cast.fwd.v;
  assign egress_txn_id_o = egress_packet_s.reg_id;
  assign egress_pending_o = dut.egress_pending_r;
  assign obs_compute_done_pulse_count_o = obs_compute_done_pulse_count_r;
  assign obs_rsp_done_pulse_count_o = obs_rsp_done_pulse_count_r;
  assign obs_egress_accept_count_o = obs_egress_accept_count_r;
  assign obs_ingress_write_count_o = obs_ingress_write_count_r;
  assign obs_ingress_consume_count_o = obs_ingress_consume_count_r;
  assign obs_iteration_id_o = obs_iteration_id_r;
  assign obs_last_egress_accept_iteration_o = obs_last_egress_accept_iteration_r;

endmodule
