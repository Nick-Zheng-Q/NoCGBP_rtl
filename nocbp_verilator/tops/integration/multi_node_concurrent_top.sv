// multi_node_concurrent_top.sv
// Integration test: concurrent multi-node fetch on single PE.
// DUT: scoreboard_prefetcher + pull_client + response_collector

`include "bsg_manycore_defines.svh"

import bsg_manycore_pkg::*;

module multi_node_concurrent_top (
    input logic clk,
    input logic rst_n,

    // Notification inputs (from remote PEs)
    input  logic        rx_notif_valid,
    output logic        rx_notif_ready,
    input  logic [gbp_pkg::NODE_ID_W-1:0] rx_notif_source_node_id,
    input  logic        rx_notif_is_factor,
    input  logic [gbp_pkg::X_CORD_W-1:0]  rx_notif_source_x,
    input  logic [gbp_pkg::Y_CORD_W-1:0]  rx_notif_source_y,

    // Adjacency registration
    input  logic        adj_valid,
    output logic        adj_ready,
    input  logic [gbp_pkg::NODE_ID_W-1:0] adj_neighbor_id,
    input  logic [gbp_pkg::X_CORD_W-1:0]  adj_neighbor_x,
    input  logic [gbp_pkg::Y_CORD_W-1:0]  adj_neighbor_y,
    input  logic        adj_is_local,
    input  logic [gbp_pkg::NODE_ID_W-1:0] adj_current_node_id,

    // Fetch request outputs (to pull_client)
    output logic        fetch_req_valid,
    input  logic        fetch_req_ready,
    output logic [gbp_pkg::NODE_ID_W-1:0] fetch_req_target_node_id,
    output logic [gbp_pkg::NODE_ID_W-1:0] fetch_req_consumer_node_id,
    output logic        fetch_req_is_factor,
    output logic [gbp_pkg::X_CORD_W-1:0]  fetch_req_target_x,
    output logic [gbp_pkg::Y_CORD_W-1:0]  fetch_req_target_y,
    output logic [gbp_pkg::TXN_ID_W-1:0]  fetch_req_txn_id,

    // Fetch response inputs (injected by testbench)
    input  logic        rx_resp_valid,
    output logic        rx_resp_ready,
    input  logic        rx_resp_is_factor,
    input  logic [gbp_pkg::STATE_WORDS_W-1:0] rx_resp_state_words,
    input  logic [31:0] rx_resp_data,
    input  logic        rx_resp_data_valid,
    input  logic        rx_resp_last,
    input  logic        rx_resp_done_valid,
    input  logic [gbp_pkg::TXN_ID_W-1:0] rx_resp_txn_id,
    input  logic [gbp_pkg::NODE_ID_W-1:0] rx_resp_node_id,
    input  logic [gbp_pkg::NODE_ID_W-1:0] rx_resp_consumer_node_id,

    // Complete outputs (to scoreboard)
    output logic        complete_valid,
    output logic [gbp_pkg::TXN_ID_W-1:0] complete_txn_id,
    output logic [gbp_pkg::NODE_ID_W-1:0] complete_node_id,
    output logic [gbp_pkg::NODE_ID_W-1:0] complete_consumer_node_id,

    // Node ready outputs
    output logic [gbp_pkg::NUM_NODES_PER_PE-1:0] node_ready,
    output logic [$clog2(gbp_pkg::SCOREBOARD_DEPTH):0] scoreboard_occupancy,
    output logic        scoreboard_full,

    // Staging / remote outputs (from response_collector)
    output logic        remote_valid,
    input  logic        remote_ready,
    output logic [gbp_pkg::NOC_DATA_W-1:0] remote_data,
    output logic        remote_last
);

  logic rst_i;
  assign rst_i = ~rst_n;

  // Scoreboard -> Pull Client
  logic                  sb_fetch_req_valid;
  logic                  sb_fetch_req_ready;
  logic [gbp_pkg::NODE_ID_W-1:0] sb_fetch_req_target_node_id;
  logic [gbp_pkg::NODE_ID_W-1:0] sb_fetch_req_consumer_node_id;
  logic                  sb_fetch_req_is_factor;
  logic [gbp_pkg::X_CORD_W-1:0]  sb_fetch_req_target_x;
  logic [gbp_pkg::Y_CORD_W-1:0]  sb_fetch_req_target_y;
  logic [gbp_pkg::TXN_ID_W-1:0]  sb_fetch_req_txn_id;

  // Response Collector -> Scoreboard
  logic                  rc_complete_valid;
  logic [gbp_pkg::TXN_ID_W-1:0] rc_complete_txn_id;
  logic [gbp_pkg::NODE_ID_W-1:0] rc_complete_node_id;
  logic [gbp_pkg::NODE_ID_W-1:0] rc_complete_consumer_node_id;

  scoreboard_prefetcher u_scoreboard (
    .clk_i(clk)
    ,.rst_i(rst_i)
    ,.rx_notif_valid_i(rx_notif_valid)
    ,.rx_notif_ready_o(rx_notif_ready)
    ,.rx_notif_source_node_id_i(rx_notif_source_node_id)
    ,.rx_notif_is_factor_i(rx_notif_is_factor)
    ,.rx_notif_source_x_i(rx_notif_source_x)
    ,.rx_notif_source_y_i(rx_notif_source_y)
    ,.fetch_req_valid_o(sb_fetch_req_valid)
    ,.fetch_req_ready_i(sb_fetch_req_ready)
    ,.fetch_req_target_node_id_o(sb_fetch_req_target_node_id)
    ,.fetch_req_consumer_node_id_o(sb_fetch_req_consumer_node_id)
    ,.fetch_req_is_factor_o(sb_fetch_req_is_factor)
    ,.fetch_req_target_x_o(sb_fetch_req_target_x)
    ,.fetch_req_target_y_o(sb_fetch_req_target_y)
    ,.fetch_req_txn_id_o(sb_fetch_req_txn_id)
    ,.complete_valid_i(rc_complete_valid)
    ,.complete_txn_id_i(rc_complete_txn_id)
    ,.complete_node_id_i(rc_complete_node_id)
    ,.complete_consumer_node_id_i(rc_complete_consumer_node_id)
    ,.staging_reserve_valid_o()
    ,.staging_reserve_words_o()
    ,.staging_reserve_ready_i(1'b1)
    ,.staging_batch_closed_i(1'b0)
    ,.adj_valid_i(adj_valid)
    ,.adj_ready_o(adj_ready)
    ,.adj_neighbor_id_i(adj_neighbor_id)
    ,.adj_neighbor_x_i(adj_neighbor_x)
    ,.adj_neighbor_y_i(adj_neighbor_y)
    ,.adj_is_local_i(adj_is_local)
    ,.adj_last_i(1'b1)
    ,.adj_edge_idx_i('0)
    ,.adj_current_node_id_i(adj_current_node_id)
    ,.reset_valid_i(1'b0)
    ,.reset_node_id_i('0)
    ,.reset_is_factor_i(1'b0)
    ,.node_ready_o(node_ready)
    ,.scoreboard_occupancy_o(scoreboard_occupancy)
    ,.scoreboard_full_o(scoreboard_full)
  );

  pull_client u_pull_client (
    .clk_i(clk)
    ,.rst_i(rst_i)
    ,.req_valid_i(sb_fetch_req_valid)
    ,.req_ready_o(sb_fetch_req_ready)
    ,.req_target_node_id_i(sb_fetch_req_target_node_id)
    ,.req_consumer_node_id_i(sb_fetch_req_consumer_node_id)
    ,.req_is_factor_i(sb_fetch_req_is_factor)
    ,.req_target_x_i(sb_fetch_req_target_x)
    ,.req_target_y_i(sb_fetch_req_target_y)
    ,.req_txn_id_i(sb_fetch_req_txn_id)
    ,.tx_valid_o(fetch_req_valid)
    ,.tx_ready_i(fetch_req_ready)
    ,.tx_target_node_id_o(fetch_req_target_node_id)
    ,.tx_consumer_node_id_o(fetch_req_consumer_node_id)
    ,.tx_is_factor_o(fetch_req_is_factor)
    ,.tx_target_x_o(fetch_req_target_x)
    ,.tx_target_y_o(fetch_req_target_y)
    ,.tx_txn_id_o(fetch_req_txn_id)
    ,.tx_store_idx_o()
  );

  response_collector u_response_collector (
    .clk_i(clk)
    ,.rst_i(rst_i)
    ,.rx_valid_i(rx_resp_valid)
    ,.rx_ready_o(rx_resp_ready)
    ,.rx_is_factor_i(rx_resp_is_factor)
    ,.rx_state_words_i(rx_resp_state_words)
    ,.rx_data_i(rx_resp_data)
    ,.rx_data_valid_i(rx_resp_data_valid)
    ,.rx_last_i(rx_resp_last)
    ,.rx_done_valid_i(rx_resp_done_valid)
    ,.rx_txn_id_i(rx_resp_txn_id)
    ,.rx_node_id_i(rx_resp_node_id)
    ,.rx_consumer_node_id_i(rx_resp_consumer_node_id)
    ,.staging_wr_valid_o()
    ,.staging_wr_ready_i(1'b1)
    ,.staging_wr_addr_o()
    ,.staging_wr_data_o()
    ,.remote_valid_o(remote_valid)
    ,.remote_ready_i(remote_ready)
    ,.remote_data_o(remote_data)
    ,.remote_last_o(remote_last)
    ,.staging_reserve_valid_i(1'b0)
    ,.staging_reserve_words_i('0)
    ,.staging_reserve_ready_o()
    ,.staging_batch_closed_o()
    ,.staging_batch_done_i(1'b0)
    ,.complete_valid_o(rc_complete_valid)
    ,.complete_txn_id_o(rc_complete_txn_id)
    ,.complete_node_id_o(rc_complete_node_id)
    ,.complete_consumer_node_id_o(rc_complete_consumer_node_id)
  );

  assign complete_valid = rc_complete_valid;
  assign complete_txn_id = rc_complete_txn_id;
  assign complete_node_id = rc_complete_node_id;
  assign complete_consumer_node_id = rc_complete_consumer_node_id;

endmodule
