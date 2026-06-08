// gbp_pe_fetch_subsystem.sv
// Encapsulates the background remote-neighbor fetch lifecycle:
//   scoreboard_prefetcher + pull_client + response_collector

module gbp_pe_fetch_subsystem
  import gbp_pkg::*;
#(
    parameter int NODE_ID_W     = gbp_pkg::NODE_ID_W,
    parameter int X_CORD_W      = gbp_pkg::X_CORD_W,
    parameter int Y_CORD_W      = gbp_pkg::Y_CORD_W,
    parameter int ADJ_COUNT_W   = gbp_pkg::ADJ_COUNT_W,
    parameter int STATE_WORDS_W = gbp_pkg::STATE_WORDS_W,
    parameter int TXN_ID_W      = gbp_pkg::TXN_ID_W,
    parameter int SCOREBOARD_DEPTH = gbp_pkg::SCOREBOARD_DEPTH,
    parameter int NUM_NODES     = gbp_pkg::NUM_NODES_PER_PE,
    parameter int SPM_ADDR_W    = gbp_pkg::SPM_ADDR_W,
    parameter int BEAT_BITS     = gbp_pkg::BEAT_BITS,
    parameter int DATA_WIDTH    = gbp_pkg::NOC_DATA_W
)(
    input  logic clk,
    input  logic rst_n,

    // Notification RX from noc_adapter
    input  logic                 rx_notif_valid_i,
    output logic                 rx_notif_ready_o,
    input  logic [NODE_ID_W-1:0] rx_notif_source_node_id_i,
    input  logic                 rx_notif_is_factor_i,
    input  logic [X_CORD_W-1:0]  rx_notif_source_x_i,
    input  logic [Y_CORD_W-1:0]  rx_notif_source_y_i,

    // Adjacency stream from control subsystem
    input  logic                 adj_valid_i,
    output logic                 adj_ready_o,
    input  logic [NODE_ID_W-1:0] adj_neighbor_id_i,
    input  logic [X_CORD_W-1:0]  adj_neighbor_x_i,
    input  logic [Y_CORD_W-1:0]  adj_neighbor_y_i,
    input  logic                 adj_is_local_i,
    input  logic                 adj_last_i,
    input  logic [ADJ_COUNT_W-1:0] adj_edge_idx_i,
    input  logic [NODE_ID_W-1:0] adj_current_node_id_i,

    // Node readiness to control subsystem
    output logic [NUM_NODES-1:0] node_ready_o,

    // Scoreboard reset from writeback controller
    input  logic                 reset_valid_i,
    input  logic [NODE_ID_W-1:0] reset_node_id_i,
    input  logic                 reset_is_factor_i,

    // NoC fetch request TX (to noc_adapter)
    output logic                 tx_fetch_req_valid_o,
    input  logic                 tx_fetch_req_ready_i,
    output logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id_o,
    output logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id_o,
    output logic                 tx_fetch_req_is_factor_o,
    output logic [X_CORD_W-1:0]  tx_fetch_req_target_x_o,
    output logic [Y_CORD_W-1:0]  tx_fetch_req_target_y_o,
    output logic [TXN_ID_W-1:0]  tx_fetch_req_txn_id_o,

    // NoC fetch response RX (from noc_adapter)
    input  logic                     rx_fetch_resp_valid_i,
    input  logic                     rx_fetch_resp_is_factor_i,
    input  logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words_i,
    input  logic [DATA_WIDTH-1:0]    rx_fetch_resp_data_i,
    input  logic                     rx_fetch_resp_data_valid_i,
    input  logic                     rx_fetch_resp_last_i,
    input  logic                     rx_fetch_resp_done_valid_i,
    input  logic [TXN_ID_W-1:0]      rx_fetch_resp_txn_id_i,
    input  logic [NODE_ID_W-1:0]     rx_fetch_resp_node_id_i,
    input  logic [NODE_ID_W-1:0]     rx_fetch_resp_consumer_node_id_i,

    // SPM read/write ports (to memory subsystem)
    output logic                 spm_rd_valid_o,
    input  logic                 spm_rd_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic [BEAT_BITS-1:0]  spm_rd_data_i,

    output logic                 spm_wr_valid_o,
    input  logic                 spm_wr_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_wr_addr_o,
    output logic [BEAT_BITS-1:0]  spm_wr_data_o,
    output logic [WSTRB_W-1:0]    spm_wr_wstrb_o,

    // Remote neighbor data to accumulator
    output logic                 remote_valid_o,
    input  logic                 remote_ready_i,
    output logic [DATA_WIDTH-1:0] remote_data_o,
    output logic                 remote_last_o,

    // Batch completion (from compute subsystem)
    input  logic                 batch_done_i
);

  // ========================================================================
  // Scoreboard Prefetcher
  // ========================================================================
  logic                 sp_fetch_req_valid;
  logic                 sp_fetch_req_ready;
  logic [NODE_ID_W-1:0] sp_fetch_req_target_node_id;
  logic [NODE_ID_W-1:0] sp_fetch_req_consumer_node_id;
  logic                 sp_fetch_req_is_factor;
  logic [X_CORD_W-1:0]  sp_fetch_req_target_x;
  logic [Y_CORD_W-1:0]  sp_fetch_req_target_y;
  logic [TXN_ID_W-1:0]  sp_fetch_req_txn_id;

  logic                 sp_complete_valid;
  logic [TXN_ID_W-1:0]  sp_complete_txn_id;
  logic [NODE_ID_W-1:0] sp_complete_node_id;
  logic [NODE_ID_W-1:0] sp_complete_consumer_node_id;

  logic                 sp_staging_reserve_valid;
  logic [STATE_WORDS_W-1:0] sp_staging_reserve_words;
  logic                 sp_staging_reserve_ready;
  logic                 sp_staging_batch_closed;

  logic                 sp_adj_ready;

  scoreboard_prefetcher u_scoreboard (
    .clk_i(clk)
    ,.rst_i(~rst_n)
    ,.rx_notif_valid_i(rx_notif_valid_i)
    ,.rx_notif_ready_o(rx_notif_ready_o)
    ,.rx_notif_source_node_id_i(rx_notif_source_node_id_i)
    ,.rx_notif_is_factor_i(rx_notif_is_factor_i)
    ,.rx_notif_source_x_i(rx_notif_source_x_i)
    ,.rx_notif_source_y_i(rx_notif_source_y_i)
    ,.fetch_req_valid_o(sp_fetch_req_valid)
    ,.fetch_req_ready_i(sp_fetch_req_ready)
    ,.fetch_req_target_node_id_o(sp_fetch_req_target_node_id)
    ,.fetch_req_consumer_node_id_o(sp_fetch_req_consumer_node_id)
    ,.fetch_req_is_factor_o(sp_fetch_req_is_factor)
    ,.fetch_req_target_x_o(sp_fetch_req_target_x)
    ,.fetch_req_target_y_o(sp_fetch_req_target_y)
    ,.fetch_req_txn_id_o(sp_fetch_req_txn_id)
    ,.complete_valid_i(sp_complete_valid)
    ,.complete_txn_id_i(sp_complete_txn_id)
    ,.complete_node_id_i(sp_complete_node_id)
    ,.complete_consumer_node_id_i(sp_complete_consumer_node_id)
    ,.staging_reserve_valid_o(sp_staging_reserve_valid)
    ,.staging_reserve_words_o(sp_staging_reserve_words)
    ,.staging_reserve_ready_i(sp_staging_reserve_ready)
    ,.staging_batch_closed_i(sp_staging_batch_closed)
    ,.adj_valid_i(adj_valid_i)
    ,.adj_ready_o(sp_adj_ready)
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
    ,.scoreboard_occupancy_o()
    ,.scoreboard_full_o()
  );

  assign adj_ready_o = sp_adj_ready;

  // ========================================================================
  // Pull Client
  // ========================================================================
  logic [1:0] pc_tx_store_idx;

  pull_client u_pull_client (
    .clk_i(clk)
    ,.rst_i(~rst_n)
    ,.req_valid_i(sp_fetch_req_valid)
    ,.req_ready_o(sp_fetch_req_ready)
    ,.req_target_node_id_i(sp_fetch_req_target_node_id)
    ,.req_consumer_node_id_i(sp_fetch_req_consumer_node_id)
    ,.req_is_factor_i(sp_fetch_req_is_factor)
    ,.req_target_x_i(sp_fetch_req_target_x)
    ,.req_target_y_i(sp_fetch_req_target_y)
    ,.req_txn_id_i(sp_fetch_req_txn_id)
    ,.tx_valid_o(tx_fetch_req_valid_o)
    ,.tx_ready_i(tx_fetch_req_ready_i)
    ,.tx_target_node_id_o(tx_fetch_req_target_node_id_o)
    ,.tx_consumer_node_id_o(tx_fetch_req_consumer_node_id_o)
    ,.tx_is_factor_o(tx_fetch_req_is_factor_o)
    ,.tx_target_x_o(tx_fetch_req_target_x_o)
    ,.tx_target_y_o(tx_fetch_req_target_y_o)
    ,.tx_txn_id_o(tx_fetch_req_txn_id_o)
    ,.tx_store_idx_o(pc_tx_store_idx)
  );

  // ========================================================================
  // Response Collector
  // ========================================================================
  response_collector u_response_collector (
    .clk_i(clk)
    ,.rst_i(~rst_n)
    ,.rx_valid_i(rx_fetch_resp_valid_i)
    ,.rx_ready_o()
    ,.rx_is_factor_i(rx_fetch_resp_is_factor_i)
    ,.rx_state_words_i(rx_fetch_resp_state_words_i)
    ,.rx_data_i(rx_fetch_resp_data_i)
    ,.rx_data_valid_i(rx_fetch_resp_data_valid_i)
    ,.rx_last_i(rx_fetch_resp_last_i)
    ,.rx_done_valid_i(rx_fetch_resp_done_valid_i)
    ,.rx_txn_id_i(rx_fetch_resp_txn_id_i)
    ,.rx_node_id_i(rx_fetch_resp_node_id_i)
    ,.rx_consumer_node_id_i(rx_fetch_resp_consumer_node_id_i)
    ,.staging_wr_valid_o(spm_wr_valid_o)
    ,.staging_wr_ready_i(spm_wr_ready_i)
    ,.staging_wr_addr_o(spm_wr_addr_o)
    ,.staging_wr_data_o(spm_wr_data_o)
    ,.staging_wr_wstrb_o(spm_wr_wstrb_o)
    ,.remote_valid_o(remote_valid_o)
    ,.remote_ready_i(remote_ready_i)
    ,.remote_data_o(remote_data_o)
    ,.remote_last_o(remote_last_o)
    ,.staging_reserve_valid_i(sp_staging_reserve_valid)
    ,.staging_reserve_words_i(sp_staging_reserve_words)
    ,.staging_reserve_ready_o(sp_staging_reserve_ready)
    ,.staging_batch_closed_o(sp_staging_batch_closed)
    ,.staging_batch_done_i(batch_done_i)
    ,.complete_valid_o(sp_complete_valid)
    ,.complete_txn_id_o(sp_complete_txn_id)
    ,.complete_node_id_o(sp_complete_node_id)
    ,.complete_consumer_node_id_o(sp_complete_consumer_node_id)
  );

  // SPM read port: Pull Server uses it to read local STATE when serving fetches
  // For now tie off — Pull Server not yet integrated into this subsystem.
  // TODO: integrate pull_server and its SPM read port when fetch response path
  // is fully implemented.
  assign spm_rd_valid_o = 1'b0;
  assign spm_rd_addr_o  = '0;

  // Unused input tie-off
  logic unused_signals;
  assign unused_signals = ^spm_rd_data_i;

endmodule
