// fetch_subsystem_top.sv
// Integration test top for Fetch Subsystem:
// scoreboard_prefetcher + pull_client + response_collector

module fetch_subsystem_top #(
    parameter int NODE_ID_W = 10,
    parameter int X_CORD_W = 6,
    parameter int Y_CORD_W = 5,
    parameter int TXN_ID_W = 6,
    parameter int STATE_WORDS_W = 6,
    parameter int SPM_ADDR_W = 18,
    parameter int NUM_NODES = 1024,
    parameter int ADJ_COUNT_W = 4
) (
    input  logic clk,
    input  logic rst_n,

    // Mocked adjacency input (from metadata_scanner)
    input  logic adj_valid,
    output logic adj_ready,
    input  logic [NODE_ID_W-1:0] adj_neighbor_id,
    input  logic [X_CORD_W-1:0]  adj_neighbor_x,
    input  logic [Y_CORD_W-1:0]  adj_neighbor_y,
    input  logic                 adj_is_local,
    input  logic                 adj_last,
    input  logic [ADJ_COUNT_W-1:0] adj_edge_idx,
    input  logic [NODE_ID_W-1:0] adj_current_node_id,

    // Mocked NoC TX ready (from noc_adapter)
    input  logic tx_fetch_req_ready,

    // Mocked NoC RX inputs (from noc_adapter)
    input  logic rx_notif_valid,
    output logic rx_notif_ready,
    input  logic [NODE_ID_W-1:0] rx_notif_source_node_id,
    input  logic                 rx_notif_is_factor,
    input  logic [X_CORD_W-1:0]  rx_notif_source_x,
    input  logic [Y_CORD_W-1:0]  rx_notif_source_y,

    input  logic rx_fetch_resp_valid,
    input  logic                 rx_fetch_resp_is_factor,
    input  logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words,
    input  logic [31:0]          rx_fetch_resp_data,
    input  logic                 rx_fetch_resp_data_valid,
    input  logic                 rx_fetch_resp_last,
    input  logic                 rx_fetch_resp_done_valid,
    input  logic [TXN_ID_W-1:0]  rx_fetch_resp_txn_id,
    input  logic [NODE_ID_W-1:0] rx_fetch_resp_node_id,
    input  logic [NODE_ID_W-1:0] rx_fetch_resp_consumer_node_id,

    // Mocked STAGING write ready
    input  logic staging_wr_ready,
    input  logic remote_ready,
    input  logic staging_reserve_ready,
    input  logic staging_batch_closed,
    input  logic staging_batch_done,

    // Mocked reset (from writeback_controller)
    input  logic reset_valid,
    input  logic [NODE_ID_W-1:0] reset_node_id,
    input  logic reset_is_factor,

    // Outputs
    output logic [NUM_NODES-1:0] node_ready,
    output logic staging_wr_valid,
    output logic [SPM_ADDR_W-1:0] staging_wr_addr,
    output logic [31:0] staging_wr_data,
    output logic remote_valid,
    output logic [31:0] remote_data,
    output logic remote_last,
    output logic complete_valid,
    output logic [TXN_ID_W-1:0] complete_txn_id,
    output logic [NODE_ID_W-1:0] complete_node_id,
    output logic [NODE_ID_W-1:0] complete_consumer_node_id,
    output logic staging_batch_closed_out,
    output logic staging_reserve_ready_out,

    // Pull Client TX outputs
    output logic tx_fetch_req_valid,
    output logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id,
    output logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id,
    output logic tx_fetch_req_is_factor,
    output logic [X_CORD_W-1:0] tx_fetch_req_target_x,
    output logic [Y_CORD_W-1:0] tx_fetch_req_target_y,
    output logic [TXN_ID_W-1:0] tx_fetch_req_txn_id,
    output logic [1:0] tx_fetch_req_store_idx
);

  logic rst_i;
  assign rst_i = ~rst_n;

  // Internal signals between scoreboard_prefetcher and pull_client
  logic fetch_req_valid;
  logic fetch_req_ready;
  logic [NODE_ID_W-1:0] fetch_req_target_node_id;
  logic [NODE_ID_W-1:0] fetch_req_consumer_node_id;
  logic fetch_req_is_factor;
  logic [X_CORD_W-1:0] fetch_req_target_x;
  logic [Y_CORD_W-1:0] fetch_req_target_y;
  logic [TXN_ID_W-1:0] fetch_req_txn_id;

  // Internal signals between scoreboard_prefetcher and response_collector
  logic staging_reserve_valid;
  logic [STATE_WORDS_W-1:0] staging_reserve_words;

  // Scoreboard Prefetcher
  scoreboard_prefetcher u_scoreboard (
    .clk_i(clk),
    .rst_n_i(rst_n),
    .rx_notif_valid_i(rx_notif_valid),
    .rx_notif_ready_o(rx_notif_ready),
    .rx_notif_source_node_id_i(rx_notif_source_node_id),
    .rx_notif_is_factor_i(rx_notif_is_factor),
    .rx_notif_source_x_i(rx_notif_source_x),
    .rx_notif_source_y_i(rx_notif_source_y),
    .fetch_req_valid_o(fetch_req_valid),
    .fetch_req_ready_i(fetch_req_ready),
    .fetch_req_target_node_id_o(fetch_req_target_node_id),
    .fetch_req_consumer_node_id_o(fetch_req_consumer_node_id),
    .fetch_req_is_factor_o(fetch_req_is_factor),
    .fetch_req_target_x_o(fetch_req_target_x),
    .fetch_req_target_y_o(fetch_req_target_y),
    .fetch_req_txn_id_o(fetch_req_txn_id),
    .complete_valid_i(complete_valid),
    .complete_txn_id_i(complete_txn_id),
    .complete_node_id_i(complete_node_id),
    .complete_consumer_node_id_i(complete_consumer_node_id),
    .staging_reserve_valid_o(staging_reserve_valid),
    .staging_reserve_words_o(staging_reserve_words),
    .staging_reserve_ready_i(staging_reserve_ready),
    .staging_batch_closed_i(staging_batch_closed),
    .adj_valid_i(adj_valid),
    .adj_ready_o(adj_ready),
    .adj_neighbor_id_i(adj_neighbor_id),
    .adj_neighbor_x_i(adj_neighbor_x),
    .adj_neighbor_y_i(adj_neighbor_y),
    .adj_is_local_i(adj_is_local),
    .adj_last_i(adj_last),
    .adj_edge_idx_i(adj_edge_idx),
    .adj_current_node_id_i(adj_current_node_id),
    .reset_valid_i(reset_valid),
    .reset_node_id_i(reset_node_id),
    .reset_is_factor_i(reset_is_factor),
    .node_ready_o(node_ready)
  );

  // Pull Client
  pull_client u_pull_client (
    .clk_i(clk),
    .rst_n_i(rst_n),
    .req_valid_i(fetch_req_valid),
    .req_ready_o(fetch_req_ready),
    .req_target_node_id_i(fetch_req_target_node_id),
    .req_consumer_node_id_i(fetch_req_consumer_node_id),
    .req_is_factor_i(fetch_req_is_factor),
    .req_target_x_i(fetch_req_target_x),
    .req_target_y_i(fetch_req_target_y),
    .req_txn_id_i(fetch_req_txn_id),
    .tx_fetch_req_valid_o(tx_fetch_req_valid),
    .tx_fetch_req_ready_i(tx_fetch_req_ready),
    .tx_fetch_req_target_node_id_o(tx_fetch_req_target_node_id),
    .tx_fetch_req_consumer_node_id_o(tx_fetch_req_consumer_node_id),
    .tx_fetch_req_is_factor_o(tx_fetch_req_is_factor),
    .tx_fetch_req_target_x_o(tx_fetch_req_target_x),
    .tx_fetch_req_target_y_o(tx_fetch_req_target_y),
    .tx_fetch_req_txn_id_o(tx_fetch_req_txn_id),
    .tx_fetch_req_store_idx_o(tx_fetch_req_store_idx)
  );

  // Response Collector
  response_collector u_response_collector (
    .clk_i(clk),
    .rst_n_i(rst_n),
    .rx_fetch_resp_valid_i(rx_fetch_resp_valid),
    .rx_fetch_resp_ready_o(),
    .rx_fetch_resp_is_factor_i(rx_fetch_resp_is_factor),
    .rx_fetch_resp_state_words_i(rx_fetch_resp_state_words),
    .rx_fetch_resp_data_i(rx_fetch_resp_data),
    .rx_fetch_resp_data_valid_i(rx_fetch_resp_data_valid),
    .rx_fetch_resp_last_i(rx_fetch_resp_last),
    .rx_fetch_resp_done_valid_i(rx_fetch_resp_done_valid),
    .rx_fetch_resp_txn_id_i(rx_fetch_resp_txn_id),
    .rx_fetch_resp_node_id_i(rx_fetch_resp_node_id),
    .rx_fetch_resp_consumer_node_id_i(rx_fetch_resp_consumer_node_id),
    .staging_wr_valid_o(staging_wr_valid),
    .staging_wr_ready_i(staging_wr_ready),
    .staging_wr_addr_o(staging_wr_addr),
    .staging_wr_data_o(staging_wr_data),
    .remote_valid_o(remote_valid),
    .remote_ready_i(remote_ready),
    .remote_data_o(remote_data),
    .remote_last_o(remote_last),
    .staging_reserve_valid_i(staging_reserve_valid),
    .staging_reserve_words_i(staging_reserve_words),
    .staging_reserve_ready_o(staging_reserve_ready_out),
    .staging_batch_closed_o(staging_batch_closed_out),
    .staging_batch_done_i(staging_batch_done),
    .complete_valid_o(complete_valid),
    .complete_txn_id_o(complete_txn_id),
    .complete_node_id_o(complete_node_id),
    .complete_consumer_node_id_o(complete_consumer_node_id)
  );

endmodule
