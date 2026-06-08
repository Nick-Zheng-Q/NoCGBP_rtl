// gbp_pe_lint_top.sv
// Simple wrapper to allow standalone lint of gbp_pe.
// Provides default values for all undimensioned parameters.

module gbp_pe_lint_top;
  import bsg_manycore_pkg::*;
  import bsg_noc_pkg::*;

  localparam x_cord_width_p = 7;
  localparam y_cord_width_p = 7;
  localparam data_width_p   = 32;
  localparam addr_width_p   = 30;
  localparam dmem_size_p    = 1024;
  localparam num_tiles_x_p  = 4;
  localparam num_tiles_y_p  = 4;
  localparam pod_x_cord_width_p = 3;
  localparam pod_y_cord_width_p = 3;
  localparam fwd_fifo_els_p = 2;
  localparam rev_fifo_els_p = 2;
  localparam barrier_dirs_p = 4;
  localparam ipoly_hashing_p = 0;

  localparam link_sif_width_lp =
    `bsg_manycore_link_sif_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p);

  logic clk, reset;
  logic [link_sif_width_lp-1:0] link_sif_i, link_sif_o;
  logic barrier_data_i, barrier_data_o;
  logic [barrier_dirs_p-1:0] barrier_src_r_o;
  logic [$clog2(barrier_dirs_p+1)-1:0] barrier_dest_r_o;
  logic [$clog2(num_tiles_x_p)-1:0] my_x_i;
  logic [$clog2(num_tiles_y_p)-1:0] my_y_i;
  logic [pod_x_cord_width_p-1:0] pod_x_i;
  logic [pod_y_cord_width_p-1:0] pod_y_i;

  gbp_pe #(
    .x_cord_width_p(x_cord_width_p)
    ,.y_cord_width_p(y_cord_width_p)
    ,.data_width_p(data_width_p)
    ,.addr_width_p(addr_width_p)
    ,.dmem_size_p(dmem_size_p)
    ,.num_tiles_x_p(num_tiles_x_p)
    ,.num_tiles_y_p(num_tiles_y_p)
    ,.pod_x_cord_width_p(pod_x_cord_width_p)
    ,.pod_y_cord_width_p(pod_y_cord_width_p)
    ,.fwd_fifo_els_p(fwd_fifo_els_p)
    ,.rev_fifo_els_p(rev_fifo_els_p)
    ,.barrier_dirs_p(barrier_dirs_p)
    ,.ipoly_hashing_p(ipoly_hashing_p)
  ) u_gbp_pe (
    .clk_i(clk)
    ,.reset_i(reset)
    ,.link_sif_i(link_sif_i)
    ,.link_sif_o(link_sif_o)
    ,.barrier_data_i(barrier_data_i)
    ,.barrier_data_o(barrier_data_o)
    ,.barrier_src_r_o(barrier_src_r_o)
    ,.barrier_dest_r_o(barrier_dest_r_o)
    ,.my_x_i(my_x_i)
    ,.my_y_i(my_y_i)
    ,.pod_x_i(pod_x_i)
    ,.pod_y_i(pod_y_i)
  );

endmodule
