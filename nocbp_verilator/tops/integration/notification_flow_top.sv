// notification_flow_top.sv
// Integration test top for notification flow across 2 PEs.
// PE_A: writeback_controller + noc_adapter
// PE_B: noc_adapter + scoreboard_prefetcher

`include "bsg_manycore_defines.svh"

import bsg_manycore_pkg::*;

module notification_flow_top (
    input logic clk,
    input logic rst_n,

    // PE_A controls
    input logic        pe_a_done_valid,
    input logic [gbp_pkg::NODE_ID_W-1:0] pe_a_done_node_id,
    input logic        pe_a_done_is_factor,
    input logic [gbp_pkg::ADJ_COUNT_W-1:0] pe_a_adj_count,
    input logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::NODE_ID_W-1:0] pe_a_adj_neighbor_ids,
    input logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::X_CORD_W-1:0]  pe_a_adj_neighbor_xs,
    input logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::Y_CORD_W-1:0]  pe_a_adj_neighbor_ys,
    input logic [gbp_pkg::MAX_ADJ_COUNT-1:0] pe_a_adj_is_local,

    // PE_B scoreboard edge registration
    input logic        pe_b_adj_valid,
    input logic [gbp_pkg::NODE_ID_W-1:0] pe_b_adj_neighbor_id,
    input logic [gbp_pkg::X_CORD_W-1:0]  pe_b_adj_neighbor_x,
    input logic [gbp_pkg::Y_CORD_W-1:0]  pe_b_adj_neighbor_y,
    input logic        pe_b_adj_is_local,
    input logic [gbp_pkg::NODE_ID_W-1:0] pe_b_adj_current_node_id,

    // Outputs
    output logic       pe_b_rx_notif_valid,
    output logic [gbp_pkg::NODE_ID_W-1:0] pe_b_rx_notif_source_node_id,

    output logic       pe_b_fetch_req_valid,
    output logic [gbp_pkg::NODE_ID_W-1:0] pe_b_fetch_req_target_node_id,
    output logic [gbp_pkg::NODE_ID_W-1:0] pe_b_fetch_req_consumer_node_id,
    output logic [gbp_pkg::TXN_ID_W-1:0]  pe_b_fetch_req_txn_id,

    output logic [gbp_pkg::NUM_NODES_PER_PE-1:0] pe_b_node_ready,
    output logic [$clog2(gbp_pkg::SCOREBOARD_DEPTH):0] pe_b_scoreboard_occupancy,
    output logic pe_a_wb_done,
    output logic pe_b_noc_rx_notif_valid,
    output logic [gbp_pkg::X_CORD_W-1:0] pe_b_noc_rx_notif_source_x,
    output logic [gbp_pkg::Y_CORD_W-1:0] pe_b_noc_rx_notif_source_y,

    // Test control: force PE_A tx_ready low to simulate backpressure
    input logic pe_a_tx_notif_ready_force_low,

    // Expose wb_controller tx_valid for backpressure detection
    output logic pe_a_wb_tx_valid
);

  localparam int ADDR_W = 16;
  localparam int DATA_W = 32;
  localparam int X_W    = 6;
  localparam int Y_W    = 5;
  localparam int LINK_W = `bsg_manycore_link_sif_width(ADDR_W, DATA_W, X_W, Y_W);

  `declare_bsg_manycore_link_sif_s(ADDR_W, DATA_W, X_W, Y_W);

  logic rst_i;
  assign rst_i = ~rst_n;

  bsg_manycore_link_sif_s pe_a_link_out, pe_b_link_out;
  bsg_manycore_link_sif_s pe_a_link_in,  pe_b_link_in;

  // Direct cross-connect: A.fwd -> B.fwd, B.rev -> A.rev and vice versa
  assign pe_a_link_in = pe_b_link_out;
  assign pe_b_link_in = pe_a_link_out;

  // -------------------------------------------------------------------------
  // PE_A: Writeback Controller + NoC Adapter
  // -------------------------------------------------------------------------
  logic                  pe_a_tx_notif_valid;
  logic                  pe_a_tx_notif_ready;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_tx_notif_source_node_id;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_tx_notif_target_node_id;
  logic                  pe_a_tx_notif_is_factor;
  logic [X_W-1:0]        pe_a_tx_notif_target_x;
  logic [Y_W-1:0]        pe_a_tx_notif_target_y;
  logic                  pe_a_reset_valid;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_reset_node_id;

  writeback_controller u_pe_a_wb (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.done_valid_i(pe_a_done_valid)
    ,.done_node_id_i(pe_a_done_node_id)
    ,.done_is_factor_i(pe_a_done_is_factor)
    ,.adj_count_i(pe_a_adj_count)
    ,.adj_neighbor_ids_i(pe_a_adj_neighbor_ids)
    ,.adj_neighbor_xs_i(pe_a_adj_neighbor_xs)
    ,.adj_neighbor_ys_i(pe_a_adj_neighbor_ys)
    ,.adj_is_local_i(pe_a_adj_is_local)
    ,.tx_notif_valid_o(pe_a_tx_notif_valid)
    ,.tx_notif_ready_i(pe_a_tx_notif_ready)
    ,.tx_notif_source_node_id_o(pe_a_tx_notif_source_node_id)
    ,.tx_notif_target_node_id_o(pe_a_tx_notif_target_node_id)
    ,.tx_notif_is_factor_o(pe_a_tx_notif_is_factor)
    ,.tx_notif_target_x_o(pe_a_tx_notif_target_x)
    ,.tx_notif_target_y_o(pe_a_tx_notif_target_y)
    ,.reset_valid_o(pe_a_reset_valid)
    ,.reset_node_id_o(pe_a_reset_node_id)
    ,.reset_is_factor_o()
    ,.wb_done_o(pe_a_wb_done)
  );

  noc_adapter u_pe_a_noc (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.link_sif_i(pe_a_link_in)
    ,.link_sif_o(pe_a_link_out)
    ,.my_x_i(X_W'(0))
    ,.my_y_i(Y_W'(0))

    ,.tx_notif_valid_i(pe_a_tx_notif_valid & ~pe_a_tx_notif_ready_force_low)
    ,.tx_notif_ready_o(pe_a_tx_notif_ready)
    ,.tx_notif_source_node_id_i(pe_a_tx_notif_source_node_id)
    ,.tx_notif_target_node_id_i(pe_a_tx_notif_target_node_id)
    ,.tx_notif_is_factor_i(pe_a_tx_notif_is_factor)
    ,.tx_notif_target_x_i(pe_a_tx_notif_target_x)
    ,.tx_notif_target_y_i(pe_a_tx_notif_target_y)

    ,.tx_fetch_req_valid_i(1'b0)
    ,.tx_fetch_req_ready_o()
    ,.tx_fetch_req_target_node_id_i('0)
    ,.tx_fetch_req_consumer_node_id_i('0)
    ,.tx_fetch_req_is_factor_i(1'b0)
    ,.tx_fetch_req_target_x_i('0)
    ,.tx_fetch_req_target_y_i('0)
    ,.tx_fetch_req_txn_id_i('0)
    ,.tx_fetch_req_store_idx_i('0)

    ,.tx_fetch_resp_valid_i(1'b0)
    ,.tx_fetch_resp_ready_o()
    ,.tx_fetch_resp_node_id_i('0)
    ,.tx_fetch_resp_consumer_node_id_i('0)
    ,.tx_fetch_resp_is_factor_i(1'b0)
    ,.tx_fetch_resp_state_words_i('0)
    ,.tx_fetch_resp_data_i('0)
    ,.tx_fetch_resp_data_valid_i(1'b0)
    ,.tx_fetch_resp_last_i(1'b0)
    ,.tx_fetch_resp_txn_id_i('0)

    ,.rx_notif_valid_o(pe_b_noc_rx_notif_valid)
    ,.rx_notif_ready_i(1'b1)
    ,.rx_notif_source_node_id_o(pe_b_rx_notif_source_node_id)
    ,.rx_notif_is_factor_o()
    ,.rx_notif_source_x_o(pe_b_noc_rx_notif_source_x)
    ,.rx_notif_source_y_o(pe_b_noc_rx_notif_source_y)

    ,.rx_fetch_req_valid_o()
    ,.rx_fetch_req_ready_i(1'b1)
    ,.rx_fetch_req_target_node_id_o()
    ,.rx_fetch_req_consumer_node_id_o()
    ,.rx_fetch_req_is_factor_o()
    ,.rx_fetch_req_src_x_o()
    ,.rx_fetch_req_src_y_o()
    ,.rx_fetch_req_txn_id_o()

    ,.rx_fetch_resp_valid_o()
    ,.rx_fetch_resp_is_factor_o()
    ,.rx_fetch_resp_state_words_o()
    ,.rx_fetch_resp_data_o()
    ,.rx_fetch_resp_data_valid_o()
    ,.rx_fetch_resp_last_o()
    ,.rx_fetch_resp_done_valid_o()
    ,.rx_fetch_resp_txn_id_o()
    ,.rx_fetch_resp_node_id_o()
    ,.rx_fetch_resp_consumer_node_id_o()

    ,.tx_busy_o()
  );

  // -------------------------------------------------------------------------
  // PE_B: NoC Adapter + ScoreboardPrefetcher
  // -------------------------------------------------------------------------
  logic                  pe_b_rx_notif_ready;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_b_rx_notif_source_node_id_internal;
  logic                  pe_b_rx_notif_is_factor;
  logic [X_W-1:0]        pe_b_rx_notif_source_x;
  logic [Y_W-1:0]        pe_b_rx_notif_source_y;
  logic                  pe_b_fetch_req_ready;

  noc_adapter u_pe_b_noc (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.link_sif_i(pe_b_link_in)
    ,.link_sif_o(pe_b_link_out)
    ,.my_x_i(X_W'(1))
    ,.my_y_i(Y_W'(0))

    ,.tx_notif_valid_i(1'b0)
    ,.tx_notif_ready_o()
    ,.tx_notif_source_node_id_i('0)
    ,.tx_notif_target_node_id_i('0)
    ,.tx_notif_is_factor_i(1'b0)
    ,.tx_notif_target_x_i('0)
    ,.tx_notif_target_y_i('0)

    ,.tx_fetch_req_valid_i(1'b0)
    ,.tx_fetch_req_ready_o()
    ,.tx_fetch_req_target_node_id_i('0)
    ,.tx_fetch_req_consumer_node_id_i('0)
    ,.tx_fetch_req_is_factor_i(1'b0)
    ,.tx_fetch_req_target_x_i('0)
    ,.tx_fetch_req_target_y_i('0)
    ,.tx_fetch_req_txn_id_i('0)
    ,.tx_fetch_req_store_idx_i('0)

    ,.tx_fetch_resp_valid_i(1'b0)
    ,.tx_fetch_resp_ready_o()
    ,.tx_fetch_resp_node_id_i('0)
    ,.tx_fetch_resp_consumer_node_id_i('0)
    ,.tx_fetch_resp_is_factor_i(1'b0)
    ,.tx_fetch_resp_state_words_i('0)
    ,.tx_fetch_resp_data_i('0)
    ,.tx_fetch_resp_data_valid_i(1'b0)
    ,.tx_fetch_resp_last_i(1'b0)
    ,.tx_fetch_resp_txn_id_i('0)

    ,.rx_notif_valid_o(pe_b_rx_notif_valid)
    ,.rx_notif_ready_i(pe_b_rx_notif_ready)
    ,.rx_notif_source_node_id_o(pe_b_rx_notif_source_node_id_internal)
    ,.rx_notif_is_factor_o(pe_b_rx_notif_is_factor)
    ,.rx_notif_source_x_o(pe_b_rx_notif_source_x)
    ,.rx_notif_source_y_o(pe_b_rx_notif_source_y)

    ,.rx_fetch_req_valid_o()
    ,.rx_fetch_req_ready_i(1'b1)
    ,.rx_fetch_req_target_node_id_o()
    ,.rx_fetch_req_consumer_node_id_o()
    ,.rx_fetch_req_is_factor_o()
    ,.rx_fetch_req_src_x_o()
    ,.rx_fetch_req_src_y_o()
    ,.rx_fetch_req_txn_id_o()

    ,.rx_fetch_resp_valid_o()
    ,.rx_fetch_resp_is_factor_o()
    ,.rx_fetch_resp_state_words_o()
    ,.rx_fetch_resp_data_o()
    ,.rx_fetch_resp_data_valid_o()
    ,.rx_fetch_resp_last_o()
    ,.rx_fetch_resp_done_valid_o()
    ,.rx_fetch_resp_txn_id_o()
    ,.rx_fetch_resp_node_id_o()
    ,.rx_fetch_resp_consumer_node_id_o()

    ,.tx_busy_o()
  );

  scoreboard_prefetcher u_pe_b_scoreboard (
    .clk_i(clk)
    ,.rst_n_i(rst_n)

    ,.rx_notif_valid_i(pe_b_rx_notif_valid)
    ,.rx_notif_ready_o(pe_b_rx_notif_ready)
    ,.rx_notif_source_node_id_i(pe_b_rx_notif_source_node_id_internal)
    ,.rx_notif_is_factor_i(pe_b_rx_notif_is_factor)
    ,.rx_notif_source_x_i(pe_b_rx_notif_source_x)
    ,.rx_notif_source_y_i(pe_b_rx_notif_source_y)

    ,.fetch_req_valid_o(pe_b_fetch_req_valid)
    ,.fetch_req_ready_i(pe_b_fetch_req_ready)
    ,.fetch_req_target_node_id_o(pe_b_fetch_req_target_node_id)
    ,.fetch_req_consumer_node_id_o(pe_b_fetch_req_consumer_node_id)
    ,.fetch_req_is_factor_o()
    ,.fetch_req_target_x_o()
    ,.fetch_req_target_y_o()
    ,.fetch_req_txn_id_o(pe_b_fetch_req_txn_id)

    ,.complete_valid_i(1'b0)
    ,.complete_txn_id_i('0)
    ,.complete_node_id_i('0)
    ,.complete_consumer_node_id_i('0)

    ,.staging_reserve_valid_o()
    ,.staging_reserve_words_o()
    ,.staging_reserve_ready_i(1'b1)
    ,.staging_batch_closed_i(1'b0)

    ,.adj_valid_i(pe_b_adj_valid)
    ,.adj_ready_o()
    ,.adj_neighbor_id_i(pe_b_adj_neighbor_id)
    ,.adj_neighbor_x_i(pe_b_adj_neighbor_x)
    ,.adj_neighbor_y_i(pe_b_adj_neighbor_y)
    ,.adj_is_local_i(pe_b_adj_is_local)
    ,.adj_last_i(1'b1)
    ,.adj_edge_idx_i('0)
    ,.adj_current_node_id_i(pe_b_adj_current_node_id)

    ,.reset_valid_i(1'b0)
    ,.reset_node_id_i('0)
    ,.reset_is_factor_i(1'b0)

    ,.node_ready_o(pe_b_node_ready)
    ,.scoreboard_occupancy_o(pe_b_scoreboard_occupancy)
    ,.scoreboard_full_o()
  );

  assign pe_b_fetch_req_ready = 1'b1;
  assign pe_a_wb_tx_valid = pe_a_tx_notif_valid;

endmodule
