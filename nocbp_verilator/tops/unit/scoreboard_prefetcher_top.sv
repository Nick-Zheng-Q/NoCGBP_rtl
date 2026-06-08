// scoreboard_prefetcher_top.sv
// Unit test wrapper for scoreboard_prefetcher

module scoreboard_prefetcher_top (
    input  logic        clk
    , input  logic        rst_n

    // Notification
    , input  logic        rx_notif_valid_i
    , input  logic [12:0] rx_notif_source_node_id_i
    , input  logic        rx_notif_is_factor_i
    , input  logic [2:0]  rx_notif_source_x_i
    , input  logic [2:0]  rx_notif_source_y_i
    , output logic        rx_notif_ready_o

    // Fetch request
    , output logic        fetch_req_valid_o
    , input  logic        fetch_req_ready_i
    , output logic [12:0] fetch_req_target_node_id_o
    , output logic [12:0] fetch_req_consumer_node_id_o
    , output logic        fetch_req_is_factor_o
    , output logic [2:0]  fetch_req_target_x_o
    , output logic [2:0]  fetch_req_target_y_o
    , output logic [5:0]  fetch_req_txn_id_o

    // Completion
    , input  logic        complete_valid_i
    , input  logic [5:0]  complete_txn_id_i
    , input  logic [12:0] complete_node_id_i
    , input  logic [12:0] complete_consumer_node_id_i

    // STAGING coordination
    , output logic        staging_reserve_valid_o
    , output logic [3:0]  staging_reserve_words_o
    , input  logic        staging_reserve_ready_i
    , input  logic        staging_batch_closed_i

    // Adj stream
    , input  logic        adj_valid_i
    , output logic        adj_ready_o
    , input  logic [12:0] adj_neighbor_id_i
    , input  logic [2:0]  adj_neighbor_x_i
    , input  logic [2:0]  adj_neighbor_y_i
    , input  logic        adj_is_local_i
    , input  logic        adj_last_i
    , input  logic [3:0]  adj_edge_idx_i
    , input  logic [12:0] adj_current_node_id_i

    // Reset
    , input  logic        reset_valid_i
    , input  logic [12:0] reset_node_id_i
    , input  logic        reset_is_factor_i

    // Node readiness
    , output logic [63:0] node_ready_o

    // Scoreboard
    , output logic [6:0]  scoreboard_occupancy_o
    , output logic        scoreboard_full_o

    // Debug
    , output logic        debug_node_has_edge_20
    , output logic [3:0]  debug_node_pending_20
);

  scoreboard_prefetcher #(
    .NODE_ID_W(10)
    ,.X_CORD_W(6)
    ,.Y_CORD_W(5)
    ,.ADJ_COUNT_W(4)
    ,.TXN_ID_W(6)
    ,.STATE_WORDS_W(6)
    ,.SCOREBOARD_DEPTH(64)
    ,.NUM_NODES(1024)
  ) dut (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.rx_notif_valid_i(rx_notif_valid_i)
    ,.rx_notif_ready_o(rx_notif_ready_o)
    ,.rx_notif_source_node_id_i(rx_notif_source_node_id_i)
    ,.rx_notif_is_factor_i(rx_notif_is_factor_i)
    ,.rx_notif_source_x_i(rx_notif_source_x_i)
    ,.rx_notif_source_y_i(rx_notif_source_y_i)
    ,.fetch_req_valid_o(fetch_req_valid_o)
    ,.fetch_req_ready_i(fetch_req_ready_i)
    ,.fetch_req_target_node_id_o(fetch_req_target_node_id_o)
    ,.fetch_req_consumer_node_id_o(fetch_req_consumer_node_id_o)
    ,.fetch_req_is_factor_o(fetch_req_is_factor_o)
    ,.fetch_req_target_x_o(fetch_req_target_x_o)
    ,.fetch_req_target_y_o(fetch_req_target_y_o)
    ,.fetch_req_txn_id_o(fetch_req_txn_id_o)
    ,.complete_valid_i(complete_valid_i)
    ,.complete_txn_id_i(complete_txn_id_i)
    ,.complete_node_id_i(complete_node_id_i)
    ,.complete_consumer_node_id_i(complete_consumer_node_id_i)
    ,.staging_reserve_valid_o(staging_reserve_valid_o)
    ,.staging_reserve_words_o(staging_reserve_words_o)
    ,.staging_reserve_ready_i(staging_reserve_ready_i)
    ,.staging_batch_closed_i(staging_batch_closed_i)
    ,.adj_valid_i(adj_valid_i)
    ,.adj_ready_o(adj_ready_o)
    ,.adj_neighbor_id_i(adj_neighbor_id_i)
    ,.adj_neighbor_x_i(adj_neighbor_x_i)
    ,.adj_neighbor_y_i(adj_neighbor_y_i)
    ,.adj_is_local_i(adj_is_local_i)
    ,.adj_last_i(adj_last_i)
    ,.adj_edge_idx_i(adj_edge_idx_i)
    ,.adj_current_node_id_i(adj_current_node_id_i)
    ,.reset_valid_i(reset_valid_i)
    ,.reset_node_id_i(reset_node_id_i)
    ,.reset_is_factor_i(reset_is_factor_i)
    ,.node_ready_o(node_ready_o)
    ,.scoreboard_occupancy_o(scoreboard_occupancy_o)
    ,.scoreboard_full_o(scoreboard_full_o)
  );

  assign debug_node_has_edge_20 = dut.node_has_edge_r[20];
  assign debug_node_pending_20 = dut.node_pending_r[20*4 +: 4];

endmodule
