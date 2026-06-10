`include "bsg_manycore_defines.svh"

module gbp_pe_top (
    input  logic clk
  , input  logic rst_n

  // NoC link_sif (flattened for Verilator)
  , input  logic [link_sif_width_lp-1:0] link_sif_i
  , output logic [link_sif_width_lp-1:0] link_sif_o

  // Whitebox command ports
  , input  logic wb_cmd_valid_i
  , input  logic [9:0] wb_cmd_node_id_i
  , input  logic wb_cmd_is_factor_i
  , input  logic [3:0] wb_cmd_dof_i
  , input  logic [3:0] wb_cmd_adj_count_i
  , input  logic [7:0][2:0] wb_cmd_neighbor_dofs_i
  , input  logic [5:0] wb_cmd_state_words_i
  , input  logic [7:0] wb_cmd_adj_is_local_i
  , input  logic wb_force_done_valid_i
  , output logic wb_cmd_ready_o
  , output logic wb_done_valid_o
  , output logic tx_notif_valid_o
  , output logic reset_valid_o
  , output logic rx_notif_valid_o
  , output logic [9:0] rx_notif_source_node_id_o
  , output logic rx_notif_is_factor_o
  , output logic rx_fetch_req_valid_o
  , output logic rx_fetch_resp_valid_o
  , output logic tx_fetch_req_valid_o
  , output logic tx_fetch_resp_valid_o

  // Fetch response injection (tied off in normal test mode)
  , input logic wb_inject_fetch_resp_valid_i
  , input logic wb_inject_fetch_resp_data_valid_i
  , input logic [31:0] wb_inject_fetch_resp_data_i
  , input logic wb_inject_fetch_resp_last_i
  , input logic wb_inject_fetch_resp_done_valid_i
  , input logic [5:0] wb_inject_fetch_resp_txn_id_i
  , input logic [9:0] wb_inject_fetch_resp_node_id_i
  , input logic [9:0] wb_inject_fetch_resp_consumer_node_id_i
);

  import bsg_manycore_pkg::*;
  import gbp_pkg::*;

  localparam int x_cord_width_p = 6;
  localparam int y_cord_width_p = 5;
  localparam int data_width_p = 32;
  localparam int addr_width_p = 16;
  localparam int dmem_size_p = 1024;
  localparam int debug_p = 0;
  localparam int num_tiles_x_p = 2;
  localparam int num_tiles_y_p = 2;
  localparam int pod_x_cord_width_p = 1;
  localparam int pod_y_cord_width_p = 1;
  localparam int fwd_fifo_els_p = 4;
  localparam int rev_fifo_els_p = 4;
  localparam int barrier_dirs_p = 4;
  localparam int ipoly_hashing_p = 0;

  localparam int x_subcord_width_lp = (num_tiles_x_p <= 1) ? 1 : $clog2(num_tiles_x_p);
  localparam int y_subcord_width_lp = (num_tiles_y_p <= 1) ? 1 : $clog2(num_tiles_y_p);
  localparam int link_sif_width_lp =
    `bsg_manycore_link_sif_width(addr_width_p, data_width_p, x_cord_width_p, y_cord_width_p);
  localparam int barrier_lg_dirs_lp = $clog2(barrier_dirs_p + 1);

  logic barrier_data_i;
  logic barrier_data_o;
  logic [barrier_dirs_p-1:0]     barrier_src_r_o;
  logic [barrier_lg_dirs_lp-1:0] barrier_dest_r_o;

  assign barrier_data_i = '0;

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
  ) dut (
    .clk_i(clk)
    ,.reset_i(~rst_n)
    ,.link_sif_i(link_sif_i)
    ,.link_sif_o(link_sif_o)
    ,.barrier_data_i(barrier_data_i)
    ,.barrier_data_o(barrier_data_o)
    ,.barrier_src_r_o(barrier_src_r_o)
    ,.barrier_dest_r_o(barrier_dest_r_o)
    ,.my_x_i(x_subcord_width_lp'(2))
    ,.my_y_i(y_subcord_width_lp'(1))
    ,.pod_x_i(pod_x_cord_width_p'(0))
    ,.pod_y_i(pod_y_cord_width_p'(0))
    ,.wb_cmd_valid_i(wb_cmd_valid_i)
    ,.wb_cmd_node_id_i(wb_cmd_node_id_i)
    ,.wb_cmd_is_factor_i(wb_cmd_is_factor_i)
    ,.wb_cmd_dof_i(wb_cmd_dof_i)
    ,.wb_cmd_adj_count_i(wb_cmd_adj_count_i)
    ,.wb_cmd_neighbor_dofs_i(wb_cmd_neighbor_dofs_i)
    ,.wb_cmd_state_words_i(wb_cmd_state_words_i)
    ,.wb_cmd_adj_is_local_i(wb_cmd_adj_is_local_i)
    ,.wb_cmd_adj_neighbor_xs_i('0)
    ,.wb_cmd_adj_neighbor_ys_i('0)
    ,.wb_force_done_valid_i(wb_force_done_valid_i)
    ,.wb_cmd_ready_o(wb_cmd_ready_o)
    ,.wb_done_valid_o(wb_done_valid_o)
    ,.tx_notif_valid_o(tx_notif_valid_o)
    ,.reset_valid_o(reset_valid_o)
    ,.rx_notif_valid_o(rx_notif_valid_o)
    ,.rx_notif_source_node_id_o(rx_notif_source_node_id_o)
    ,.rx_notif_is_factor_o(rx_notif_is_factor_o)
    ,.rx_fetch_req_valid_o(rx_fetch_req_valid_o)
    ,.rx_fetch_resp_valid_o(rx_fetch_resp_valid_o)
    ,.tx_fetch_req_valid_o(tx_fetch_req_valid_o)
    ,.tx_fetch_resp_valid_o(tx_fetch_resp_valid_o)
    ,.wb_inject_fetch_resp_valid_i(wb_inject_fetch_resp_valid_i)
    ,.wb_inject_fetch_resp_data_valid_i(wb_inject_fetch_resp_data_valid_i)
    ,.wb_inject_fetch_resp_data_i(wb_inject_fetch_resp_data_i)
    ,.wb_inject_fetch_resp_last_i(wb_inject_fetch_resp_last_i)
    ,.wb_inject_fetch_resp_done_valid_i(wb_inject_fetch_resp_done_valid_i)
    ,.wb_inject_fetch_resp_txn_id_i(wb_inject_fetch_resp_txn_id_i)
    ,.wb_inject_fetch_resp_node_id_i(wb_inject_fetch_resp_node_id_i)
    ,.wb_inject_fetch_resp_consumer_node_id_i(wb_inject_fetch_resp_consumer_node_id_i)
  );

endmodule
