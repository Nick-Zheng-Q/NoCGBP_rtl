module bsg_manycore_proc_vanilla
  import bsg_manycore_pkg::*;
  #(
    parameter x_cord_width_p = 1,
    parameter y_cord_width_p = 1,
    parameter pod_x_cord_width_p = 1,
    parameter pod_y_cord_width_p = 1,
    parameter data_width_p = 32,
    parameter addr_width_p = 16,
    parameter dmem_size_p = 1024,
    parameter ipoly_hashing_p = 0,
    parameter num_tiles_x_p = 1,
    parameter num_tiles_y_p = 1,
    parameter rev_fifo_els_p = 2,
    parameter fwd_fifo_els_p = 2,
    parameter debug_p = 0,
    parameter barrier_dirs_p = 7,
    localparam barrier_lg_dirs_lp = `BSG_SAFE_CLOG2(barrier_dirs_p+1),
    localparam link_sif_width_lp =
      `bsg_manycore_link_sif_width(addr_width_p, data_width_p, x_cord_width_p, y_cord_width_p)
  )
  (
    input clk_i,
    input reset_i,
    input [link_sif_width_lp-1:0] link_sif_i,
    output logic [link_sif_width_lp-1:0] link_sif_o,
    input barrier_data_i,
    output logic barrier_data_o,
    output logic [barrier_dirs_p-1:0] barrier_src_r_o,
    output logic [barrier_lg_dirs_lp-1:0] barrier_dest_r_o,
    input [x_cord_width_p-1:0] my_x_i,
    input [y_cord_width_p-1:0] my_y_i,
    input [pod_x_cord_width_p-1:0] pod_x_i,
    input [pod_y_cord_width_p-1:0] pod_y_i
  );
  assign link_sif_o = '0;
  assign barrier_data_o = 1'b0;
  assign barrier_src_r_o = '0;
  assign barrier_dest_r_o = '0;
endmodule

module bsg_manycore_gather_scatter
  import bsg_manycore_pkg::*;
  #(
    parameter x_cord_width_p = 1,
    parameter y_cord_width_p = 1,
    parameter pod_x_cord_width_p = 1,
    parameter pod_y_cord_width_p = 1,
    parameter data_width_p = 32,
    parameter addr_width_p = 16,
    parameter dmem_size_p = 1024,
    parameter ipoly_hashing_p = 0,
    parameter num_tiles_x_p = 1,
    parameter num_tiles_y_p = 1,
    parameter rev_fifo_els_p = 2,
    parameter fwd_fifo_els_p = 2,
    parameter debug_p = 0,
    parameter barrier_dirs_p = 7,
    localparam barrier_lg_dirs_lp = `BSG_SAFE_CLOG2(barrier_dirs_p+1),
    localparam link_sif_width_lp =
      `bsg_manycore_link_sif_width(addr_width_p, data_width_p, x_cord_width_p, y_cord_width_p)
  )
  (
    input clk_i,
    input reset_i,
    input [link_sif_width_lp-1:0] link_sif_i,
    output logic [link_sif_width_lp-1:0] link_sif_o,
    input barrier_data_i,
    output logic barrier_data_o,
    output logic [barrier_dirs_p-1:0] barrier_src_r_o,
    output logic [barrier_lg_dirs_lp-1:0] barrier_dest_r_o,
    input [x_cord_width_p-1:0] my_x_i,
    input [y_cord_width_p-1:0] my_y_i,
    input [pod_x_cord_width_p-1:0] pod_x_i,
    input [pod_y_cord_width_p-1:0] pod_y_i
  );
  assign link_sif_o = '0;
  assign barrier_data_o = 1'b0;
  assign barrier_src_r_o = '0;
  assign barrier_dest_r_o = '0;
endmodule

module bsg_manycore_accel_default
  import bsg_manycore_pkg::*;
  #(
    parameter x_cord_width_p = 1,
    parameter y_cord_width_p = 1,
    parameter pod_x_cord_width_p = 1,
    parameter pod_y_cord_width_p = 1,
    parameter data_width_p = 32,
    parameter addr_width_p = 16,
    parameter dmem_size_p = 1024,
    parameter ipoly_hashing_p = 0,
    parameter num_tiles_x_p = 1,
    parameter num_tiles_y_p = 1,
    parameter rev_fifo_els_p = 2,
    parameter fwd_fifo_els_p = 2,
    parameter debug_p = 0,
    parameter barrier_dirs_p = 7,
    localparam barrier_lg_dirs_lp = `BSG_SAFE_CLOG2(barrier_dirs_p+1),
    localparam link_sif_width_lp =
      `bsg_manycore_link_sif_width(addr_width_p, data_width_p, x_cord_width_p, y_cord_width_p)
  )
  (
    input clk_i,
    input reset_i,
    input [link_sif_width_lp-1:0] link_sif_i,
    output logic [link_sif_width_lp-1:0] link_sif_o,
    input barrier_data_i,
    output logic barrier_data_o,
    output logic [barrier_dirs_p-1:0] barrier_src_r_o,
    output logic [barrier_lg_dirs_lp-1:0] barrier_dest_r_o,
    input [x_cord_width_p-1:0] my_x_i,
    input [y_cord_width_p-1:0] my_y_i,
    input [pod_x_cord_width_p-1:0] pod_x_i,
    input [pod_y_cord_width_p-1:0] pod_y_i
  );
  assign link_sif_o = '0;
  assign barrier_data_o = 1'b0;
  assign barrier_src_r_o = '0;
  assign barrier_dest_r_o = '0;
endmodule
