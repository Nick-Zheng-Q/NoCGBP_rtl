// full_pull_cycle_top.sv
// Integration test: complete pull cycle.
// PE_A: writeback_controller + pull_server + noc_adapter + fake SPM
// PE_B: scoreboard_prefetcher + pull_client + response_collector + noc_adapter

`include "bsg_manycore_defines.svh"

import bsg_manycore_pkg::*;

module full_pull_cycle_top (
    input logic clk,
    input logic rst_n,

    // PE_A writeback trigger
    input logic        pe_a_done_valid,
    input logic [gbp_pkg::NODE_ID_W-1:0] pe_a_done_node_id,
    input logic        pe_a_done_is_factor,

    // PE_A writeback adjacency info (for done_node_id)
    input logic [gbp_pkg::ADJ_COUNT_W-1:0] pe_a_adj_count,
    input logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::NODE_ID_W-1:0] pe_a_adj_neighbor_ids,
    input logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::X_CORD_W-1:0]  pe_a_adj_neighbor_xs,
    input logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::Y_CORD_W-1:0]  pe_a_adj_neighbor_ys,
    input logic [gbp_pkg::MAX_ADJ_COUNT-1:0]                        pe_a_adj_is_local,

    // PE_A fake SPM
    input logic        pe_a_spm_ready,
    input logic [gbp_pkg::BEAT_BITS-1:0] pe_a_spm_data,
    output logic [gbp_pkg::SPM_ADDR_W-1:0] pe_a_spm_rd_addr,

    // PE_B adjacency registration
    input logic        pe_b_adj_valid,
    input logic [gbp_pkg::NODE_ID_W-1:0] pe_b_adj_neighbor_id,
    input logic [gbp_pkg::X_CORD_W-1:0]  pe_b_adj_neighbor_x,
    input logic [gbp_pkg::Y_CORD_W-1:0]  pe_b_adj_neighbor_y,
    input logic        pe_b_adj_is_local,
    input logic [gbp_pkg::NODE_ID_W-1:0] pe_b_adj_current_node_id,

    // PE_B outputs
    output logic [gbp_pkg::NUM_NODES_PER_PE-1:0] pe_b_node_ready,
    output logic [$clog2(gbp_pkg::SCOREBOARD_DEPTH):0] pe_b_scoreboard_occupancy,
    output logic       pe_b_remote_valid,
    output logic [gbp_pkg::NOC_DATA_W-1:0] pe_b_remote_data,
    output logic       pe_b_remote_last,
    output logic       pe_b_complete_valid,
    output logic [gbp_pkg::TXN_ID_W-1:0]  pe_b_complete_txn_id,
    output logic [gbp_pkg::NODE_ID_W-1:0] pe_b_complete_node_id,
    output logic [gbp_pkg::NODE_ID_W-1:0] pe_b_complete_consumer_node_id,
    output logic       pe_b_tx_busy
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

  assign pe_a_link_in = pe_b_link_out;
  assign pe_b_link_in = pe_a_link_out;

  // -------------------------------------------------------------------------
  // PE_A: Writeback Controller + Pull Server + NoC Adapter
  // -------------------------------------------------------------------------
  logic                  pe_a_wb_tx_valid;
  logic                  pe_a_wb_tx_ready;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_wb_tx_source_node_id;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_wb_tx_target_node_id;
  logic                  pe_a_wb_tx_is_factor;
  logic [X_W-1:0]        pe_a_wb_tx_target_x;
  logic [Y_W-1:0]        pe_a_wb_tx_target_y;

  writeback_controller u_pe_a_wb (
    .clk_i(clk)
    ,.rst_i(rst_i)
    ,.done_valid_i(pe_a_done_valid)
    ,.done_node_id_i(pe_a_done_node_id)
    ,.done_is_factor_i(pe_a_done_is_factor)
    ,.adj_count_i(pe_a_adj_count)
    ,.adj_neighbor_ids_i(pe_a_adj_neighbor_ids)
    ,.adj_neighbor_xs_i(pe_a_adj_neighbor_xs)
    ,.adj_neighbor_ys_i(pe_a_adj_neighbor_ys)
    ,.adj_is_local_i(pe_a_adj_is_local)
    ,.tx_valid_o(pe_a_wb_tx_valid)
    ,.tx_ready_i(pe_a_wb_tx_ready)
    ,.tx_source_node_id_o(pe_a_wb_tx_source_node_id)
    ,.tx_target_node_id_o(pe_a_wb_tx_target_node_id)
    ,.tx_is_factor_o(pe_a_wb_tx_is_factor)
    ,.tx_target_x_o(pe_a_wb_tx_target_x)
    ,.tx_target_y_o(pe_a_wb_tx_target_y)
    ,.reset_valid_o()
    ,.reset_node_id_o()
    ,.reset_is_factor_o()
    ,.wb_done_o()
  );

  logic                  pe_a_ps_tx_valid;
  logic                  pe_a_ps_tx_ready;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_ps_tx_node_id;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_ps_tx_consumer_node_id;
  logic                  pe_a_ps_tx_is_factor;
  logic [gbp_pkg::STATE_WORDS_W-1:0] pe_a_ps_tx_state_words;
  logic [gbp_pkg::NOC_DATA_W-1:0]    pe_a_ps_tx_data;
  logic                  pe_a_ps_tx_data_valid;
  logic                  pe_a_ps_tx_last;
  logic [gbp_pkg::TXN_ID_W-1:0]      pe_a_ps_tx_txn_id;

  logic                  pe_a_ps_spm_rd_valid;
  logic [gbp_pkg::SPM_ADDR_W-1:0] pe_a_ps_spm_rd_addr_w;

  pull_server u_pe_a_pull_server (
    .clk_i(clk)
    ,.rst_i(rst_i)
    ,.req_valid_i(pe_a_noc_rx_fetch_req_valid)
    ,.req_ready_o(pe_a_noc_rx_fetch_req_ready)
    ,.req_target_node_id_i(pe_a_noc_rx_fetch_req_target_node_id)
    ,.req_consumer_node_id_i(pe_a_noc_rx_fetch_req_consumer_node_id)
    ,.req_is_factor_i(pe_a_noc_rx_fetch_req_is_factor)
    ,.req_fetch_src_x_i(pe_a_noc_rx_fetch_req_src_x)
    ,.req_fetch_src_y_i(pe_a_noc_rx_fetch_req_src_y)
    ,.req_txn_id_i(pe_a_noc_rx_fetch_req_txn_id)
    ,.spm_rd_valid_o(pe_a_ps_spm_rd_valid)
    ,.spm_rd_addr_o(pe_a_ps_spm_rd_addr_w)
    ,.spm_rd_ready_i(pe_a_spm_ready)
    ,.spm_rd_data_i(pe_a_spm_data)
    ,.tx_valid_o(pe_a_ps_tx_valid)
    ,.tx_ready_i(pe_a_ps_tx_ready)
    ,.tx_node_id_o(pe_a_ps_tx_node_id)
    ,.tx_consumer_node_id_o(pe_a_ps_tx_consumer_node_id)
    ,.tx_is_factor_o(pe_a_ps_tx_is_factor)
    ,.tx_state_words_o(pe_a_ps_tx_state_words)
    ,.tx_data_o(pe_a_ps_tx_data)
    ,.tx_data_valid_o(pe_a_ps_tx_data_valid)
    ,.tx_last_o(pe_a_ps_tx_last)
    ,.tx_txn_id_o(pe_a_ps_tx_txn_id)
  );

  assign pe_a_spm_rd_addr = pe_a_ps_spm_rd_addr_w;

  logic                  pe_a_noc_rx_fetch_req_valid;
  logic                  pe_a_noc_rx_fetch_req_ready;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_noc_rx_fetch_req_target_node_id;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_noc_rx_fetch_req_consumer_node_id;
  logic                  pe_a_noc_rx_fetch_req_is_factor;
  logic [X_W-1:0]        pe_a_noc_rx_fetch_req_src_x;
  logic [Y_W-1:0]        pe_a_noc_rx_fetch_req_src_y;
  logic [gbp_pkg::TXN_ID_W-1:0] pe_a_noc_rx_fetch_req_txn_id;

  noc_adapter u_pe_a_noc (
    .clk_i(clk)
    ,.reset_i(rst_i)
    ,.link_sif_i(pe_a_link_in)
    ,.link_sif_o(pe_a_link_out)
    ,.my_x_i(X_W'(0))
    ,.my_y_i(Y_W'(0))

    ,.tx_notif_valid_i(pe_a_wb_tx_valid)
    ,.tx_notif_ready_o(pe_a_wb_tx_ready)
    ,.tx_notif_source_node_id_i(pe_a_wb_tx_source_node_id)
    ,.tx_notif_target_node_id_i(pe_a_wb_tx_target_node_id)
    ,.tx_notif_is_factor_i(pe_a_wb_tx_is_factor)
    ,.tx_notif_target_x_i(pe_a_wb_tx_target_x)
    ,.tx_notif_target_y_i(pe_a_wb_tx_target_y)

    ,.tx_fetch_req_valid_i(1'b0)
    ,.tx_fetch_req_ready_o()
    ,.tx_fetch_req_target_node_id_i('0)
    ,.tx_fetch_req_consumer_node_id_i('0)
    ,.tx_fetch_req_is_factor_i(1'b0)
    ,.tx_fetch_req_target_x_i('0)
    ,.tx_fetch_req_target_y_i('0)
    ,.tx_fetch_req_txn_id_i('0)
    ,.tx_fetch_req_store_idx_i('0)

    ,.tx_fetch_resp_valid_i(pe_a_ps_tx_valid)
    ,.tx_fetch_resp_ready_o(pe_a_ps_tx_ready)
    ,.tx_fetch_resp_node_id_i(pe_a_ps_tx_node_id)
    ,.tx_fetch_resp_consumer_node_id_i(pe_a_ps_tx_consumer_node_id)
    ,.tx_fetch_resp_is_factor_i(pe_a_ps_tx_is_factor)
    ,.tx_fetch_resp_state_words_i(pe_a_ps_tx_state_words)
    ,.tx_fetch_resp_data_i(pe_a_ps_tx_data)
    ,.tx_fetch_resp_data_valid_i(pe_a_ps_tx_data_valid)
    ,.tx_fetch_resp_last_i(pe_a_ps_tx_last)
    ,.tx_fetch_resp_txn_id_i(pe_a_ps_tx_txn_id)

    ,.rx_notif_valid_o()
    ,.rx_notif_ready_i(1'b1)
    ,.rx_notif_source_node_id_o()
    ,.rx_notif_is_factor_o()
    ,.rx_notif_source_x_o()
    ,.rx_notif_source_y_o()

    ,.rx_fetch_req_valid_o(pe_a_noc_rx_fetch_req_valid)
    ,.rx_fetch_req_ready_i(pe_a_noc_rx_fetch_req_ready)
    ,.rx_fetch_req_target_node_id_o(pe_a_noc_rx_fetch_req_target_node_id)
    ,.rx_fetch_req_consumer_node_id_o(pe_a_noc_rx_fetch_req_consumer_node_id)
    ,.rx_fetch_req_is_factor_o(pe_a_noc_rx_fetch_req_is_factor)
    ,.rx_fetch_req_src_x_o(pe_a_noc_rx_fetch_req_src_x)
    ,.rx_fetch_req_src_y_o(pe_a_noc_rx_fetch_req_src_y)
    ,.rx_fetch_req_txn_id_o(pe_a_noc_rx_fetch_req_txn_id)

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
  // PE_B: ScoreboardPrefetcher + PullClient + ResponseCollector + NoC Adapter
  // -------------------------------------------------------------------------
  logic                  pe_b_sb_fetch_req_valid;
  logic                  pe_b_sb_fetch_req_ready;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_b_sb_fetch_req_target_node_id;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_b_sb_fetch_req_consumer_node_id;
  logic                  pe_b_sb_fetch_req_is_factor;
  logic [X_W-1:0]        pe_b_sb_fetch_req_target_x;
  logic [Y_W-1:0]        pe_b_sb_fetch_req_target_y;
  logic [gbp_pkg::TXN_ID_W-1:0] pe_b_sb_fetch_req_txn_id;

  logic                  pe_b_pc_tx_valid;
  logic                  pe_b_pc_tx_ready;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_b_pc_tx_target_node_id;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_b_pc_tx_consumer_node_id;
  logic                  pe_b_pc_tx_is_factor;
  logic [X_W-1:0]        pe_b_pc_tx_target_x;
  logic [Y_W-1:0]        pe_b_pc_tx_target_y;
  logic [gbp_pkg::TXN_ID_W-1:0] pe_b_pc_tx_txn_id;
  logic [1:0]            pe_b_pc_tx_store_idx;

  logic                  pe_b_rx_notif_valid;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_b_rx_notif_source_node_id;
  logic                  pe_b_rx_notif_is_factor;
  logic [X_W-1:0]        pe_b_rx_notif_source_x;
  logic [Y_W-1:0]        pe_b_rx_notif_source_y;

  scoreboard_prefetcher u_pe_b_scoreboard (
    .clk_i(clk)
    ,.rst_i(rst_i)
    ,.rx_notif_valid_i(pe_b_rx_notif_valid)
    ,.rx_notif_ready_o()
    ,.rx_notif_source_node_id_i(pe_b_rx_notif_source_node_id)
    ,.rx_notif_is_factor_i(pe_b_rx_notif_is_factor)
    ,.rx_notif_source_x_i(pe_b_rx_notif_source_x)
    ,.rx_notif_source_y_i(pe_b_rx_notif_source_y)
    ,.fetch_req_valid_o(pe_b_sb_fetch_req_valid)
    ,.fetch_req_ready_i(pe_b_sb_fetch_req_ready)
    ,.fetch_req_target_node_id_o(pe_b_sb_fetch_req_target_node_id)
    ,.fetch_req_consumer_node_id_o(pe_b_sb_fetch_req_consumer_node_id)
    ,.fetch_req_is_factor_o(pe_b_sb_fetch_req_is_factor)
    ,.fetch_req_target_x_o(pe_b_sb_fetch_req_target_x)
    ,.fetch_req_target_y_o(pe_b_sb_fetch_req_target_y)
    ,.fetch_req_txn_id_o(pe_b_sb_fetch_req_txn_id)
    ,.complete_valid_i(pe_b_complete_valid)
    ,.complete_txn_id_i(pe_b_complete_txn_id)
    ,.complete_node_id_i(pe_b_complete_node_id)
    ,.complete_consumer_node_id_i(pe_b_complete_consumer_node_id)
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

  pull_client u_pe_b_pull_client (
    .clk_i(clk)
    ,.rst_i(rst_i)
    ,.req_valid_i(pe_b_sb_fetch_req_valid)
    ,.req_ready_o(pe_b_sb_fetch_req_ready)
    ,.req_target_node_id_i(pe_b_sb_fetch_req_target_node_id)
    ,.req_consumer_node_id_i(pe_b_sb_fetch_req_consumer_node_id)
    ,.req_is_factor_i(pe_b_sb_fetch_req_is_factor)
    ,.req_target_x_i(pe_b_sb_fetch_req_target_x)
    ,.req_target_y_i(pe_b_sb_fetch_req_target_y)
    ,.req_txn_id_i(pe_b_sb_fetch_req_txn_id)
    ,.tx_valid_o(pe_b_pc_tx_valid)
    ,.tx_ready_i(pe_b_pc_tx_ready)
    ,.tx_target_node_id_o(pe_b_pc_tx_target_node_id)
    ,.tx_consumer_node_id_o(pe_b_pc_tx_consumer_node_id)
    ,.tx_is_factor_o(pe_b_pc_tx_is_factor)
    ,.tx_target_x_o(pe_b_pc_tx_target_x)
    ,.tx_target_y_o(pe_b_pc_tx_target_y)
    ,.tx_txn_id_o(pe_b_pc_tx_txn_id)
    ,.tx_store_idx_o(pe_b_pc_tx_store_idx)
  );

  logic                  pe_b_noc_rx_fetch_resp_valid;
  logic                  pe_b_noc_rx_fetch_resp_is_factor;
  logic [gbp_pkg::STATE_WORDS_W-1:0] pe_b_noc_rx_fetch_resp_state_words;
  logic [DATA_W-1:0]     pe_b_noc_rx_fetch_resp_data;
  logic                  pe_b_noc_rx_fetch_resp_data_valid;
  logic                  pe_b_noc_rx_fetch_resp_last;
  logic                  pe_b_noc_rx_fetch_resp_done_valid;
  logic [gbp_pkg::TXN_ID_W-1:0] pe_b_noc_rx_fetch_resp_txn_id;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_b_noc_rx_fetch_resp_node_id;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_b_noc_rx_fetch_resp_consumer_node_id;

  noc_adapter u_pe_b_noc (
    .clk_i(clk)
    ,.reset_i(rst_i)
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

    ,.tx_fetch_req_valid_i(pe_b_pc_tx_valid)
    ,.tx_fetch_req_ready_o(pe_b_pc_tx_ready)
    ,.tx_fetch_req_target_node_id_i(pe_b_pc_tx_target_node_id)
    ,.tx_fetch_req_consumer_node_id_i(pe_b_pc_tx_consumer_node_id)
    ,.tx_fetch_req_is_factor_i(pe_b_pc_tx_is_factor)
    ,.tx_fetch_req_target_x_i(pe_b_pc_tx_target_x)
    ,.tx_fetch_req_target_y_i(pe_b_pc_tx_target_y)
    ,.tx_fetch_req_txn_id_i(pe_b_pc_tx_txn_id)
    ,.tx_fetch_req_store_idx_i(pe_b_pc_tx_store_idx)

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
    ,.rx_notif_ready_i(1'b1)
    ,.rx_notif_source_node_id_o(pe_b_rx_notif_source_node_id)
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

    ,.rx_fetch_resp_valid_o(pe_b_noc_rx_fetch_resp_valid)
    ,.rx_fetch_resp_is_factor_o(pe_b_noc_rx_fetch_resp_is_factor)
    ,.rx_fetch_resp_state_words_o(pe_b_noc_rx_fetch_resp_state_words)
    ,.rx_fetch_resp_data_o(pe_b_noc_rx_fetch_resp_data)
    ,.rx_fetch_resp_data_valid_o(pe_b_noc_rx_fetch_resp_data_valid)
    ,.rx_fetch_resp_last_o(pe_b_noc_rx_fetch_resp_last)
    ,.rx_fetch_resp_done_valid_o(pe_b_noc_rx_fetch_resp_done_valid)
    ,.rx_fetch_resp_txn_id_o(pe_b_noc_rx_fetch_resp_txn_id)
    ,.rx_fetch_resp_node_id_o(pe_b_noc_rx_fetch_resp_node_id)
    ,.rx_fetch_resp_consumer_node_id_o(pe_b_noc_rx_fetch_resp_consumer_node_id)

    ,.tx_busy_o(pe_b_tx_busy)
  );

  response_collector u_pe_b_response_collector (
    .clk_i(clk)
    ,.rst_i(rst_i)
    ,.rx_valid_i(pe_b_noc_rx_fetch_resp_valid)
    ,.rx_ready_o()
    ,.rx_is_factor_i(pe_b_noc_rx_fetch_resp_is_factor)
    ,.rx_state_words_i(pe_b_noc_rx_fetch_resp_state_words)
    ,.rx_data_i(pe_b_noc_rx_fetch_resp_data)
    ,.rx_data_valid_i(pe_b_noc_rx_fetch_resp_data_valid)
    ,.rx_last_i(pe_b_noc_rx_fetch_resp_last)
    ,.rx_done_valid_i(pe_b_noc_rx_fetch_resp_done_valid)
    ,.rx_txn_id_i(pe_b_noc_rx_fetch_resp_txn_id)
    ,.rx_node_id_i(pe_b_noc_rx_fetch_resp_node_id)
    ,.rx_consumer_node_id_i(pe_b_noc_rx_fetch_resp_consumer_node_id)
    ,.staging_wr_valid_o()
    ,.staging_wr_ready_i(1'b1)
    ,.staging_wr_addr_o()
    ,.staging_wr_data_o()
    ,.remote_valid_o(pe_b_remote_valid)
    ,.remote_ready_i(1'b1)
    ,.remote_data_o(pe_b_remote_data)
    ,.remote_last_o(pe_b_remote_last)
    ,.staging_reserve_valid_i(1'b0)
    ,.staging_reserve_words_i('0)
    ,.staging_reserve_ready_o()
    ,.staging_batch_closed_o()
    ,.staging_batch_done_i(1'b0)
    ,.complete_valid_o(pe_b_complete_valid)
    ,.complete_txn_id_o(pe_b_complete_txn_id)
    ,.complete_node_id_o(pe_b_complete_node_id)
    ,.complete_consumer_node_id_o(pe_b_complete_consumer_node_id)
  );

endmodule
