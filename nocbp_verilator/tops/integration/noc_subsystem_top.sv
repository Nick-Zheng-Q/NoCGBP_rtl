// noc_subsystem_top.sv
// Integration test top for NoC Subsystem:
// noc_adapter + pull_server + writeback_controller

module noc_subsystem_top #(
    parameter int NODE_ID_W = 10,
    parameter int X_CORD_W = 6,
    parameter int Y_CORD_W = 5,
    parameter int TXN_ID_W = 6,
    parameter int STATE_WORDS_W = 6,
    parameter int SPM_ADDR_W = 18,
    parameter int ADJ_COUNT_W = 4,
    parameter int MAX_ADJ_COUNT = 16
) (
    input  logic clk,
    input  logic rst_n,

    // Manycore link
    input  logic [95:0] link_sif_i,
    output logic [95:0] link_sif_o,

    // My coordinates
    input  logic [X_CORD_W-1:0] my_x,
    input  logic [Y_CORD_W-1:0] my_y,

    // Mocked inputs for pull_server
    input  logic spm_rd_ready,
    input  logic [63:0] spm_rd_data,

    // Mocked inputs for writeback_controller
    input  logic done_valid,
    input  logic [NODE_ID_W-1:0] done_node_id,
    input  logic done_is_factor,
    input  logic [ADJ_COUNT_W-1:0] adj_count,
    input  logic [MAX_ADJ_COUNT-1:0][NODE_ID_W-1:0] adj_neighbor_ids,
    input  logic [MAX_ADJ_COUNT-1:0][X_CORD_W-1:0] adj_neighbor_xs,
    input  logic [MAX_ADJ_COUNT-1:0][Y_CORD_W-1:0] adj_neighbor_ys,
    input  logic [MAX_ADJ_COUNT-1:0] adj_is_local,

    // Outputs to mocked scoreboard_prefetcher
    output logic rx_notif_valid,
    output logic [NODE_ID_W-1:0] rx_notif_source_node_id,
    output logic rx_notif_is_factor,
    output logic [X_CORD_W-1:0] rx_notif_source_x,
    output logic [Y_CORD_W-1:0] rx_notif_source_y,

    // Outputs to mocked pull_client
    output logic rx_fetch_req_valid,
    output logic [NODE_ID_W-1:0] rx_fetch_req_target_node_id,
    output logic [NODE_ID_W-1:0] rx_fetch_req_consumer_node_id,
    output logic rx_fetch_req_is_factor,
    output logic [X_CORD_W-1:0] rx_fetch_req_src_x,
    output logic [Y_CORD_W-1:0] rx_fetch_req_src_y,
    output logic [TXN_ID_W-1:0] rx_fetch_req_txn_id,

    // Outputs to mocked response_collector
    output logic rx_fetch_resp_valid,
    output logic rx_fetch_resp_is_factor,
    output logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words,
    output logic [31:0] rx_fetch_resp_data,
    output logic rx_fetch_resp_data_valid,
    output logic rx_fetch_resp_last,
    output logic rx_fetch_resp_done_valid,
    output logic [TXN_ID_W-1:0] rx_fetch_resp_txn_id,
    output logic [NODE_ID_W-1:0] rx_fetch_resp_node_id,
    output logic [NODE_ID_W-1:0] rx_fetch_resp_consumer_node_id,

    // Pull Server outputs
    output logic pull_server_spm_rd_valid,
    output logic [SPM_ADDR_W-1:0] pull_server_spm_rd_addr,

    // Writeback Controller outputs
    output logic reset_valid,
    output logic [NODE_ID_W-1:0] reset_node_id,
    output logic reset_is_factor,
    output logic wb_done,

    // Status
    output logic tx_busy
);

  logic rst_i;
  assign rst_i = ~rst_n;

  // Internal signals
  logic rx_notif_ready_i;
  logic rx_fetch_req_ready_i;

  logic tx_notif_valid_i;
  logic tx_notif_ready_o;
  logic [NODE_ID_W-1:0] tx_notif_source_node_id_i;
  logic [NODE_ID_W-1:0] tx_notif_target_node_id_i;
  logic tx_notif_is_factor_i;
  logic [X_CORD_W-1:0] tx_notif_target_x_i;
  logic [Y_CORD_W-1:0] tx_notif_target_y_i;

  logic tx_fetch_req_valid_i;
  logic tx_fetch_req_ready_o;
  logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id_i;
  logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id_i;
  logic tx_fetch_req_is_factor_i;
  logic [X_CORD_W-1:0] tx_fetch_req_target_x_i;
  logic [Y_CORD_W-1:0] tx_fetch_req_target_y_i;
  logic [TXN_ID_W-1:0] tx_fetch_req_txn_id_i;
  logic [1:0] tx_fetch_req_store_idx_i;

  logic tx_fetch_resp_valid_i;
  logic tx_fetch_resp_ready_o;
  logic [NODE_ID_W-1:0] tx_fetch_resp_node_id_i;
  logic [NODE_ID_W-1:0] tx_fetch_resp_consumer_node_id_i;
  logic tx_fetch_resp_is_factor_i;
  logic [STATE_WORDS_W-1:0] tx_fetch_resp_state_words_i;
  logic [31:0] tx_fetch_resp_data_i;
  logic tx_fetch_resp_data_valid_i;
  logic tx_fetch_resp_last_i;
  logic [TXN_ID_W-1:0] tx_fetch_resp_txn_id_i;

  // Pull Server internal signals
  logic pull_server_req_valid;
  logic pull_server_req_ready;
  logic [NODE_ID_W-1:0] pull_server_req_target_node_id;
  logic [NODE_ID_W-1:0] pull_server_req_consumer_node_id;
  logic pull_server_req_is_factor;
  logic [X_CORD_W-1:0] pull_server_req_fetch_src_x;
  logic [Y_CORD_W-1:0] pull_server_req_fetch_src_y;
  logic [TXN_ID_W-1:0] pull_server_req_txn_id;

  logic pull_server_tx_valid;
  logic pull_server_tx_ready;
  logic [NODE_ID_W-1:0] pull_server_tx_node_id;
  logic [NODE_ID_W-1:0] pull_server_tx_consumer_node_id;
  logic pull_server_tx_is_factor;
  logic [STATE_WORDS_W-1:0] pull_server_tx_state_words;
  logic [31:0] pull_server_tx_data;
  logic pull_server_tx_data_valid;
  logic pull_server_tx_last;
  logic [TXN_ID_W-1:0] pull_server_tx_txn_id;

  // NoC Adapter
  noc_adapter u_noc_adapter (
    .clk_i(clk),
    .rst_n_i(rst_n),
    .link_sif_i(link_sif_i),
    .link_sif_o(link_sif_o),
    .my_x_i(my_x),
    .my_y_i(my_y),

    .tx_notif_valid_i(tx_notif_valid_i),
    .tx_notif_ready_o(tx_notif_ready_o),
    .tx_notif_source_node_id_i(tx_notif_source_node_id_i),
    .tx_notif_target_node_id_i(tx_notif_target_node_id_i),
    .tx_notif_is_factor_i(tx_notif_is_factor_i),
    .tx_notif_target_x_i(tx_notif_target_x_i),
    .tx_notif_target_y_i(tx_notif_target_y_i),

    .tx_fetch_req_valid_i(tx_fetch_req_valid_i),
    .tx_fetch_req_ready_o(tx_fetch_req_ready_o),
    .tx_fetch_req_target_node_id_i(tx_fetch_req_target_node_id_i),
    .tx_fetch_req_consumer_node_id_i(tx_fetch_req_consumer_node_id_i),
    .tx_fetch_req_is_factor_i(tx_fetch_req_is_factor_i),
    .tx_fetch_req_target_x_i(tx_fetch_req_target_x_i),
    .tx_fetch_req_target_y_i(tx_fetch_req_target_y_i),
    .tx_fetch_req_txn_id_i(tx_fetch_req_txn_id_i),
    .tx_fetch_req_store_idx_i(tx_fetch_req_store_idx_i),

    .tx_fetch_resp_valid_i(tx_fetch_resp_valid_i),
    .tx_fetch_resp_ready_o(tx_fetch_resp_ready_o),
    .tx_fetch_resp_node_id_i(tx_fetch_resp_node_id_i),
    .tx_fetch_resp_consumer_node_id_i(tx_fetch_resp_consumer_node_id_i),
    .tx_fetch_resp_is_factor_i(tx_fetch_resp_is_factor_i),
    .tx_fetch_resp_state_words_i(tx_fetch_resp_state_words_i),
    .tx_fetch_resp_data_i(tx_fetch_resp_data_i),
    .tx_fetch_resp_data_valid_i(tx_fetch_resp_data_valid_i),
    .tx_fetch_resp_last_i(tx_fetch_resp_last_i),
    .tx_fetch_resp_txn_id_i(tx_fetch_resp_txn_id_i),

    .rx_notif_valid_o(rx_notif_valid),
    .rx_notif_ready_i(rx_notif_ready_i),
    .rx_notif_source_node_id_o(rx_notif_source_node_id),
    .rx_notif_is_factor_o(rx_notif_is_factor),
    .rx_notif_source_x_o(rx_notif_source_x),
    .rx_notif_source_y_o(rx_notif_source_y),

    .rx_fetch_req_valid_o(rx_fetch_req_valid),
    .rx_fetch_req_ready_i(rx_fetch_req_ready_i),
    .rx_fetch_req_target_node_id_o(rx_fetch_req_target_node_id),
    .rx_fetch_req_consumer_node_id_o(rx_fetch_req_consumer_node_id),
    .rx_fetch_req_is_factor_o(rx_fetch_req_is_factor),
    .rx_fetch_req_src_x_o(rx_fetch_req_src_x),
    .rx_fetch_req_src_y_o(rx_fetch_req_src_y),
    .rx_fetch_req_txn_id_o(rx_fetch_req_txn_id),

    .rx_fetch_resp_valid_o(rx_fetch_resp_valid),
    .rx_fetch_resp_is_factor_o(rx_fetch_resp_is_factor),
    .rx_fetch_resp_state_words_o(rx_fetch_resp_state_words),
    .rx_fetch_resp_data_o(rx_fetch_resp_data),
    .rx_fetch_resp_data_valid_o(rx_fetch_resp_data_valid),
    .rx_fetch_resp_last_o(rx_fetch_resp_last),
    .rx_fetch_resp_done_valid_o(rx_fetch_resp_done_valid),
    .rx_fetch_resp_txn_id_o(rx_fetch_resp_txn_id),
    .rx_fetch_resp_node_id_o(rx_fetch_resp_node_id),
    .rx_fetch_resp_consumer_node_id_o(rx_fetch_resp_consumer_node_id),

    .tx_busy_o(tx_busy)
  );

  // Pull Server
  pull_server u_pull_server (
    .clk_i(clk),
    .rst_n_i(rst_n),
    .req_valid_i(rx_fetch_req_valid),
    .req_ready_o(rx_fetch_req_ready_i),
    .req_target_node_id_i(rx_fetch_req_target_node_id),
    .req_consumer_node_id_i(rx_fetch_req_consumer_node_id),
    .req_is_factor_i(rx_fetch_req_is_factor),
    .req_fetch_src_x_i(rx_fetch_req_src_x),
    .req_fetch_src_y_i(rx_fetch_req_src_y),
    .req_txn_id_i(rx_fetch_req_txn_id),
    .spm_rd_valid_o(pull_server_spm_rd_valid),
    .spm_rd_addr_o(pull_server_spm_rd_addr),
    .spm_rd_ready_i(spm_rd_ready),
    .spm_rd_data_i(spm_rd_data),
    .tx_fetch_resp_valid_o(tx_fetch_resp_valid_i),
    .tx_fetch_resp_ready_i(tx_fetch_resp_ready_o),
    .tx_fetch_resp_node_id_o(tx_fetch_resp_node_id_i),
    .tx_fetch_resp_consumer_node_id_o(tx_fetch_resp_consumer_node_id_i),
    .tx_fetch_resp_is_factor_o(tx_fetch_resp_is_factor_i),
    .tx_fetch_resp_state_words_o(tx_fetch_resp_state_words_i),
    .tx_fetch_resp_data_o(tx_fetch_resp_data_i),
    .tx_fetch_resp_data_valid_o(tx_fetch_resp_data_valid_i),
    .tx_fetch_resp_last_o(tx_fetch_resp_last_i),
    .tx_fetch_resp_txn_id_o(tx_fetch_resp_txn_id_i)
  );

  // Writeback Controller
  writeback_controller u_writeback (
    .clk_i(clk),
    .rst_n_i(rst_n),
    .done_valid_i(done_valid),
    .done_node_id_i(done_node_id),
    .done_is_factor_i(done_is_factor),
    .adj_count_i(adj_count),
    .adj_neighbor_ids_i(adj_neighbor_ids),
    .adj_neighbor_xs_i(adj_neighbor_xs),
    .adj_neighbor_ys_i(adj_neighbor_ys),
    .adj_is_local_i(adj_is_local),
    .tx_notif_valid_o(tx_notif_valid_i),
    .tx_notif_ready_i(1'b1),
    .tx_notif_source_node_id_o(tx_notif_source_node_id_i),
    .tx_notif_is_factor_o(tx_notif_is_factor_i),
    .tx_notif_target_x_o(tx_notif_target_x_i),
    .tx_notif_target_y_o(tx_notif_target_y_i),
    .reset_valid_o(reset_valid),
    .reset_node_id_o(reset_node_id),
    .reset_is_factor_o(reset_is_factor),
    .wb_done_o(wb_done)
  );

  // TX target node ID for writeback = consumer node ID (simplified)
  assign tx_notif_target_node_id_i = done_node_id;

endmodule
