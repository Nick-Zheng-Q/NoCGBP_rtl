// writeback_controller_top.sv
// Unit test wrapper for writeback_controller

module writeback_controller_top (
    input  logic        clk
    , input  logic        rst_n

    // Compute completion
    , input  logic        done_valid_i
    , input  logic [9:0] done_node_id_i
    , input  logic        done_is_factor_i

    // Adjacency info (directly driven by test)
    , input  logic [3:0]  adj_count_i
    , input  logic [9:0] adj_neighbor_ids_0_i
    , input  logic [9:0] adj_neighbor_ids_1_i
    , input  logic [5:0]  adj_neighbor_xs_0_i
    , input  logic [5:0]  adj_neighbor_xs_1_i
    , input  logic [4:0]  adj_neighbor_ys_0_i
    , input  logic [4:0]  adj_neighbor_ys_1_i
    , input  logic        adj_is_local_0_i
    , input  logic        adj_is_local_1_i
    , input  logic [9:0] adj_neighbor_ids_2_i
    , input  logic [9:0] adj_neighbor_ids_3_i
    , input  logic [9:0] adj_neighbor_ids_4_i
    , input  logic [9:0] adj_neighbor_ids_5_i
    , input  logic [9:0] adj_neighbor_ids_6_i
    , input  logic [9:0] adj_neighbor_ids_7_i
    , input  logic [5:0]  adj_neighbor_xs_2_i
    , input  logic [5:0]  adj_neighbor_xs_3_i
    , input  logic [5:0]  adj_neighbor_xs_4_i
    , input  logic [5:0]  adj_neighbor_xs_5_i
    , input  logic [5:0]  adj_neighbor_xs_6_i
    , input  logic [5:0]  adj_neighbor_xs_7_i
    , input  logic [4:0]  adj_neighbor_ys_2_i
    , input  logic [4:0]  adj_neighbor_ys_3_i
    , input  logic [4:0]  adj_neighbor_ys_4_i
    , input  logic [4:0]  adj_neighbor_ys_5_i
    , input  logic [4:0]  adj_neighbor_ys_6_i
    , input  logic [4:0]  adj_neighbor_ys_7_i
    , input  logic        adj_is_local_2_i
    , input  logic        adj_is_local_3_i
    , input  logic        adj_is_local_4_i
    , input  logic        adj_is_local_5_i
    , input  logic        adj_is_local_6_i
    , input  logic        adj_is_local_7_i

    // TX notification
    , output logic        tx_valid_o
    , input  logic        tx_ready_i
    , output logic [9:0] tx_source_node_id_o
    , output logic [9:0] tx_target_node_id_o
    , output logic        tx_is_factor_o
    , output logic [5:0]  tx_target_x_o
    , output logic [4:0]  tx_target_y_o

    // Scoreboard reset
    , output logic        reset_valid_o
    , output logic [9:0] reset_node_id_o
    , output logic        reset_is_factor_o

    // Done
    , output logic        wb_done_o
);

  // Pack adjacency arrays
  logic [7:0][9:0] adj_neighbor_ids;
  logic [7:0][5:0]  adj_neighbor_xs;
  logic [7:0][4:0]  adj_neighbor_ys;
  logic [7:0]       adj_is_local;

  assign adj_neighbor_ids[0] = adj_neighbor_ids_0_i;
  assign adj_neighbor_ids[1] = adj_neighbor_ids_1_i;
  assign adj_neighbor_ids[2] = adj_neighbor_ids_2_i;
  assign adj_neighbor_ids[3] = adj_neighbor_ids_3_i;
  assign adj_neighbor_ids[4] = adj_neighbor_ids_4_i;
  assign adj_neighbor_ids[5] = adj_neighbor_ids_5_i;
  assign adj_neighbor_ids[6] = adj_neighbor_ids_6_i;
  assign adj_neighbor_ids[7] = adj_neighbor_ids_7_i;

  assign adj_neighbor_xs[0] = adj_neighbor_xs_0_i;
  assign adj_neighbor_xs[1] = adj_neighbor_xs_1_i;
  assign adj_neighbor_xs[2] = adj_neighbor_xs_2_i;
  assign adj_neighbor_xs[3] = adj_neighbor_xs_3_i;
  assign adj_neighbor_xs[4] = adj_neighbor_xs_4_i;
  assign adj_neighbor_xs[5] = adj_neighbor_xs_5_i;
  assign adj_neighbor_xs[6] = adj_neighbor_xs_6_i;
  assign adj_neighbor_xs[7] = adj_neighbor_xs_7_i;

  assign adj_neighbor_ys[0] = adj_neighbor_ys_0_i;
  assign adj_neighbor_ys[1] = adj_neighbor_ys_1_i;
  assign adj_neighbor_ys[2] = adj_neighbor_ys_2_i;
  assign adj_neighbor_ys[3] = adj_neighbor_ys_3_i;
  assign adj_neighbor_ys[4] = adj_neighbor_ys_4_i;
  assign adj_neighbor_ys[5] = adj_neighbor_ys_5_i;
  assign adj_neighbor_ys[6] = adj_neighbor_ys_6_i;
  assign adj_neighbor_ys[7] = adj_neighbor_ys_7_i;

  assign adj_is_local[0] = adj_is_local_0_i;
  assign adj_is_local[1] = adj_is_local_1_i;
  assign adj_is_local[2] = adj_is_local_2_i;
  assign adj_is_local[3] = adj_is_local_3_i;
  assign adj_is_local[4] = adj_is_local_4_i;
  assign adj_is_local[5] = adj_is_local_5_i;
  assign adj_is_local[6] = adj_is_local_6_i;
  assign adj_is_local[7] = adj_is_local_7_i;

  writeback_controller #(
    .NODE_ID_W(10)
    ,.ADJ_COUNT_W(4)
    ,.MAX_ADJ_COUNT(8)
    ,.X_CORD_W(6)
    ,.Y_CORD_W(5)
  ) dut (
    .clk_i(clk)
    ,.rst_i(~rst_n)
    ,.done_valid_i(done_valid_i)
    ,.done_node_id_i(done_node_id_i)
    ,.done_is_factor_i(done_is_factor_i)
    ,.adj_count_i(adj_count_i)
    ,.adj_neighbor_ids_i(adj_neighbor_ids)
    ,.adj_neighbor_xs_i(adj_neighbor_xs)
    ,.adj_neighbor_ys_i(adj_neighbor_ys)
    ,.adj_is_local_i(adj_is_local)
    ,.tx_valid_o(tx_valid_o)
    ,.tx_ready_i(tx_ready_i)
    ,.tx_source_node_id_o(tx_source_node_id_o)
    ,.tx_target_node_id_o(tx_target_node_id_o)
    ,.tx_is_factor_o(tx_is_factor_o)
    ,.tx_target_x_o(tx_target_x_o)
    ,.tx_target_y_o(tx_target_y_o)
    ,.reset_valid_o(reset_valid_o)
    ,.reset_node_id_o(reset_node_id_o)
    ,.reset_is_factor_o(reset_is_factor_o)
    ,.wb_done_o(wb_done_o)
  );

endmodule
