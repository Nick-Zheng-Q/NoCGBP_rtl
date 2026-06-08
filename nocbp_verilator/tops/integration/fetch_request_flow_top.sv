// fetch_request_flow_top.sv
// Integration test for fetch request flow: PE_B consumer -> PE_A producer.
// PE_B: scoreboard_prefetcher + pull_client + noc_adapter
// PE_A: noc_adapter + pull_server + fake_spm

`include "bsg_manycore_defines.svh"

import bsg_manycore_pkg::*;

module fetch_request_flow_top (
    input logic clk,
    input logic rst_n,

    // PE_B edge registration + notification
    input logic        pe_b_adj_valid,
    input logic [gbp_pkg::NODE_ID_W-1:0] pe_b_adj_neighbor_id,
    input logic [gbp_pkg::X_CORD_W-1:0]  pe_b_adj_neighbor_x,
    input logic [gbp_pkg::Y_CORD_W-1:0]  pe_b_adj_neighbor_y,
    input logic        pe_b_adj_is_local,
    input logic [gbp_pkg::NODE_ID_W-1:0] pe_b_adj_current_node_id,

    input logic        pe_b_rx_notif_valid,
    input logic [gbp_pkg::NODE_ID_W-1:0] pe_b_rx_notif_source_node_id,
    input logic        pe_b_rx_notif_is_factor,
    input logic [gbp_pkg::X_CORD_W-1:0]  pe_b_rx_notif_source_x,
    input logic [gbp_pkg::Y_CORD_W-1:0]  pe_b_rx_notif_source_y,

    // PE_A pull server outputs
    output logic       pe_a_rx_fetch_req_valid,
    output logic [gbp_pkg::NODE_ID_W-1:0] pe_a_rx_fetch_req_target_node_id,
    output logic [gbp_pkg::NODE_ID_W-1:0] pe_a_rx_fetch_req_consumer_node_id,
    output logic       pe_a_rx_fetch_req_is_factor,
    output logic [gbp_pkg::X_CORD_W-1:0]  pe_a_rx_fetch_req_src_x,
    output logic [gbp_pkg::Y_CORD_W-1:0]  pe_a_rx_fetch_req_src_y,
    output logic [gbp_pkg::TXN_ID_W-1:0]  pe_a_rx_fetch_req_txn_id,

    // PE_A SPM read
    output logic [gbp_pkg::SPM_ADDR_W-1:0] pe_a_spm_rd_addr,

    // Status
    output logic       pe_b_fetch_req_valid,
    output logic [$clog2(gbp_pkg::SCOREBOARD_DEPTH):0] pe_b_scoreboard_occupancy,
    output logic       pe_b_noc_tx_ready,
    output logic [1:0] pe_b_pull_client_state,
    output logic       pe_b_tx_busy,
    output logic       pe_a_rx_in_v,

    // Expose NoC TX packet for 3-store address verification
    output logic       pe_b_noc_tx_out_v,
    output logic [15:0] pe_b_noc_tx_out_addr
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
  // PE_B: ScoreboardPrefetcher + PullClient + NoC Adapter
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

  logic                  pe_b_rx_notif_ready;

  scoreboard_prefetcher u_pe_b_scoreboard (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.rx_notif_valid_i(pe_b_rx_notif_valid)
    ,.rx_notif_ready_o(pe_b_rx_notif_ready)
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
    ,.node_ready_o()
    ,.scoreboard_occupancy_o(pe_b_scoreboard_occupancy)
    ,.scoreboard_full_o()
  );

  pull_client u_pe_b_pull_client (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.req_valid_i(pe_b_sb_fetch_req_valid)
    ,.req_ready_o(pe_b_sb_fetch_req_ready)
    ,.req_target_node_id_i(pe_b_sb_fetch_req_target_node_id)
    ,.req_consumer_node_id_i(pe_b_sb_fetch_req_consumer_node_id)
    ,.req_is_factor_i(pe_b_sb_fetch_req_is_factor)
    ,.req_target_x_i(pe_b_sb_fetch_req_target_x)
    ,.req_target_y_i(pe_b_sb_fetch_req_target_y)
    ,.req_txn_id_i(pe_b_sb_fetch_req_txn_id)
    ,.tx_fetch_req_valid_o(pe_b_pc_tx_valid)
    ,.tx_fetch_req_ready_i(pe_b_pc_tx_ready)
    ,.tx_fetch_req_target_node_id_o(pe_b_pc_tx_target_node_id)
    ,.tx_fetch_req_consumer_node_id_o(pe_b_pc_tx_consumer_node_id)
    ,.tx_fetch_req_is_factor_o(pe_b_pc_tx_is_factor)
    ,.tx_fetch_req_target_x_o(pe_b_pc_tx_target_x)
    ,.tx_fetch_req_target_y_o(pe_b_pc_tx_target_y)
    ,.tx_fetch_req_txn_id_o(pe_b_pc_tx_txn_id)
    ,.tx_fetch_req_store_idx_o(pe_b_pc_tx_store_idx)
  );

  assign pe_b_fetch_req_valid = pe_b_sb_fetch_req_valid;

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

    ,.rx_notif_valid_o()
    ,.rx_notif_ready_i(1'b1)
    ,.rx_notif_source_node_id_o()
    ,.rx_notif_is_factor_o()
    ,.rx_notif_source_x_o()
    ,.rx_notif_source_y_o()

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
  // PE_A: NoC Adapter + Pull Server + Fake SPM
  // -------------------------------------------------------------------------
  logic                  pe_a_noc_rx_fetch_req_valid;
  logic                  pe_a_noc_rx_fetch_req_ready;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_noc_rx_fetch_req_target_node_id;
  logic [gbp_pkg::NODE_ID_W-1:0] pe_a_noc_rx_fetch_req_consumer_node_id;
  logic                  pe_a_noc_rx_fetch_req_is_factor;
  logic [X_W-1:0]        pe_a_noc_rx_fetch_req_src_x;
  logic [Y_W-1:0]        pe_a_noc_rx_fetch_req_src_y;
  logic [gbp_pkg::TXN_ID_W-1:0] pe_a_noc_rx_fetch_req_txn_id;

  logic                  pe_a_ps_spm_rd_valid;
  logic [gbp_pkg::SPM_ADDR_W-1:0] pe_a_ps_spm_rd_addr;
  logic                  pe_a_ps_spm_rd_ready;
  logic [gbp_pkg::BEAT_BITS-1:0]  pe_a_ps_spm_rd_data;

  assign pe_a_ps_spm_rd_ready = 1'b1;
  assign pe_a_ps_spm_rd_data = '0;

  noc_adapter u_pe_a_noc (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.link_sif_i(pe_a_link_in)
    ,.link_sif_o(pe_a_link_out)
    ,.my_x_i(X_W'(0))
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

  pull_server u_pe_a_pull_server (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.req_valid_i(pe_a_noc_rx_fetch_req_valid)
    ,.req_ready_o(pe_a_noc_rx_fetch_req_ready)
    ,.req_target_node_id_i(pe_a_noc_rx_fetch_req_target_node_id)
    ,.req_consumer_node_id_i(pe_a_noc_rx_fetch_req_consumer_node_id)
    ,.req_is_factor_i(pe_a_noc_rx_fetch_req_is_factor)
    ,.req_fetch_src_x_i(pe_a_noc_rx_fetch_req_src_x)
    ,.req_fetch_src_y_i(pe_a_noc_rx_fetch_req_src_y)
    ,.req_txn_id_i(pe_a_noc_rx_fetch_req_txn_id)
    ,.spm_rd_valid_o(pe_a_ps_spm_rd_valid)
    ,.spm_rd_addr_o(pe_a_ps_spm_rd_addr)
    ,.spm_rd_ready_i(pe_a_ps_spm_rd_ready)
    ,.spm_rd_data_i(pe_a_ps_spm_rd_data)
    ,.tx_fetch_resp_valid_o()
    ,.tx_fetch_resp_ready_i(1'b1)
    ,.tx_fetch_resp_node_id_o()
    ,.tx_fetch_resp_consumer_node_id_o()
    ,.tx_fetch_resp_is_factor_o()
    ,.tx_fetch_resp_state_words_o()
    ,.tx_fetch_resp_data_o()
    ,.tx_fetch_resp_data_valid_o()
    ,.tx_fetch_resp_last_o()
    ,.tx_fetch_resp_txn_id_o()
  );

  assign pe_a_rx_fetch_req_valid         = pe_a_noc_rx_fetch_req_valid;
  assign pe_a_rx_fetch_req_target_node_id   = pe_a_noc_rx_fetch_req_target_node_id;
  assign pe_a_rx_fetch_req_consumer_node_id = pe_a_noc_rx_fetch_req_consumer_node_id;
  assign pe_a_rx_fetch_req_is_factor       = pe_a_noc_rx_fetch_req_is_factor;
  assign pe_a_rx_fetch_req_src_x          = pe_a_noc_rx_fetch_req_src_x;
  assign pe_a_rx_fetch_req_src_y          = pe_a_noc_rx_fetch_req_src_y;
  assign pe_a_rx_fetch_req_txn_id         = pe_a_noc_rx_fetch_req_txn_id;

  assign pe_b_noc_tx_ready = pe_b_pc_tx_ready;
  assign pe_b_pull_client_state = u_pe_b_pull_client.state_r;
  assign pe_b_tx_busy = u_pe_b_noc.tx_busy_o;
  assign pe_a_spm_rd_addr = pe_a_ps_spm_rd_addr;
  assign pe_a_rx_in_v = u_pe_a_noc.ep_std.in_v_o;

  assign pe_b_noc_tx_out_v = u_pe_b_noc.tx.out_v_o;
  assign pe_b_noc_tx_out_addr = u_pe_b_noc.tx.packet_cast.addr[15:0];

endmodule
