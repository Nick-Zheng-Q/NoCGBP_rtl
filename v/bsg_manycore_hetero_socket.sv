// bsg_manycore_hetero_socket.sv
// Simplified version - only gbp_pe (hetero_type=8)

`include "bsg_manycore_defines.svh"

module bsg_manycore_hetero_socket
  import bsg_manycore_pkg::*;
  #(x_cord_width_p 
    , y_cord_width_p 
    , data_width_p 
    , addr_width_p 
    , dmem_size_p 
    , debug_p = 0
    , int hetero_type_p = 8
    , pod_x_cord_width_p
    , pod_y_cord_width_p
    , num_tiles_x_p
    , num_tiles_y_p
    , localparam x_subcord_width_lp = `BSG_SAFE_CLOG2(num_tiles_x_p)
    , localparam y_subcord_width_lp = `BSG_SAFE_CLOG2(num_tiles_y_p)
    , parameter fwd_fifo_els_p 
    , parameter rev_fifo_els_p 
    , parameter barrier_dirs_p 
    , parameter ipoly_hashing_p
    , localparam barrier_lg_dirs_lp=`BSG_SAFE_CLOG2(barrier_dirs_p+1)

    , bsg_manycore_link_sif_width_lp =
      `bsg_manycore_link_sif_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)
  )
  (
    input clk_i
    , input reset_i

    // input and output links
    , input [bsg_manycore_link_sif_width_lp-1:0] link_sif_i
    , output [bsg_manycore_link_sif_width_lp-1:0] link_sif_o

    // barrier interface
    , input  barrier_data_i
    , output barrier_data_o
    , output [barrier_dirs_p-1:0]     barrier_src_r_o
    , output [barrier_lg_dirs_lp-1:0] barrier_dest_r_o

    // tile coordinates
    , input [x_subcord_width_lp-1:0] my_x_i
    , input [y_subcord_width_lp-1:0] my_y_i

    , input [pod_x_cord_width_p-1:0] pod_x_i
    , input [pod_y_cord_width_p-1:0] pod_y_i
`ifdef GBP_WHITEBOX_TEST
    , input logic wb_cmd_valid_i
    , input logic [1:0] wb_cmd_kind_i
    , input logic [gbp_pkg::TXN_ID_W-1:0] wb_cmd_txn_id_i
    , output logic wb_cmd_ready_o
`endif
  );

  // Only gbp_pe instantiation (hetero_type=8)
  gbp_pe #(
    .x_cord_width_p(x_cord_width_p)
    ,.y_cord_width_p(y_cord_width_p)
    ,.data_width_p(data_width_p)
    ,.addr_width_p(addr_width_p)
    ,.dmem_size_p(dmem_size_p)
    ,.debug_p(debug_p)
    ,.num_tiles_x_p(num_tiles_x_p)
    ,.num_tiles_y_p(num_tiles_y_p)
    ,.pod_x_cord_width_p(pod_x_cord_width_p)
    ,.pod_y_cord_width_p(pod_y_cord_width_p)
    ,.fwd_fifo_els_p(fwd_fifo_els_p)
    ,.rev_fifo_els_p(rev_fifo_els_p)
    ,.barrier_dirs_p(barrier_dirs_p)
    ,.ipoly_hashing_p(ipoly_hashing_p)
  ) gbp_pe_inst (
    .clk_i(clk_i)
    ,.reset_i(reset_i)
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
`ifdef GBP_WHITEBOX_TEST
    ,.wb_cmd_valid_i(wb_cmd_valid_i)
    ,.wb_cmd_kind_i(wb_cmd_kind_i)
    ,.wb_cmd_txn_id_i(wb_cmd_txn_id_i)
    ,.wb_cmd_ready_o(wb_cmd_ready_o)
`endif
  );

endmodule

`BSG_ABSTRACT_MODULE(bsg_manycore_hetero_socket)