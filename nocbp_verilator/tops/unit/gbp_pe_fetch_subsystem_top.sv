// gbp_pe_fetch_subsystem_top.sv
// Unit-test wrapper for fetch subsystem.
// Ties off NoC and SPM ports; primarily for lint and structural check.

module gbp_pe_fetch_subsystem_top (
    input logic clk
    , input logic rst_n

    // Notification RX
    , input logic        rx_notif_valid_i
    , output logic       rx_notif_ready_o
    , input logic [gbp_pkg::NODE_ID_W-1:0] rx_notif_source_node_id_i
    , input logic        rx_notif_is_factor_i
    , input logic [gbp_pkg::X_CORD_W-1:0]  rx_notif_source_x_i
    , input logic [gbp_pkg::Y_CORD_W-1:0]  rx_notif_source_y_i

    // Adjacency input
    , input logic        adj_valid_i
    , output logic       adj_ready_o
    , input logic [gbp_pkg::NODE_ID_W-1:0] adj_neighbor_id_i
    , input logic [gbp_pkg::X_CORD_W-1:0]  adj_neighbor_x_i
    , input logic [gbp_pkg::Y_CORD_W-1:0]  adj_neighbor_y_i
    , input logic        adj_is_local_i
    , input logic        adj_last_i
    , input logic [gbp_pkg::ADJ_COUNT_W-1:0] adj_edge_idx_i
    , input logic [gbp_pkg::NODE_ID_W-1:0] adj_current_node_id_i

    // Fetch request TX (observation)
    , output logic       tx_fetch_req_valid_o
    , input  logic       tx_fetch_req_ready_i
    , output logic [gbp_pkg::NODE_ID_W-1:0] tx_fetch_req_target_node_id_o

    // Fetch response RX (for response full-path testing)
    , input  logic        rx_fetch_resp_valid_i
    , input  logic        rx_fetch_resp_is_factor_i
    , input  logic [gbp_pkg::STATE_WORDS_W-1:0] rx_fetch_resp_state_words_i
    , input  logic [gbp_pkg::NOC_DATA_W-1:0] rx_fetch_resp_data_i
    , input  logic        rx_fetch_resp_data_valid_i
    , input  logic        rx_fetch_resp_last_i
    , input  logic        rx_fetch_resp_done_valid_i
    , input  logic [gbp_pkg::TXN_ID_W-1:0]      rx_fetch_resp_txn_id_i
    , input  logic [gbp_pkg::NODE_ID_W-1:0]     rx_fetch_resp_node_id_i
    , input  logic [gbp_pkg::NODE_ID_W-1:0]     rx_fetch_resp_consumer_node_id_i

    // Remote data to accumulator (observation)
    , output logic       remote_valid_o
    , input  logic       remote_ready_i
    , output logic [gbp_pkg::NOC_DATA_W-1:0] remote_data_o
    , output logic       remote_last_o

    // Scoreboard occupancy observation
    , output logic [$clog2(gbp_pkg::SCOREBOARD_DEPTH):0] scoreboard_occupancy_o
    , output logic [gbp_pkg::NUM_NODES_PER_PE-1:0] node_ready_o
);

  logic [gbp_pkg::SPM_ADDR_W-1:0] spm_rd_addr, spm_wr_addr;
  logic [gbp_pkg::BEAT_BITS-1:0]  spm_rd_data, spm_wr_data;
  logic [gbp_pkg::WSTRB_W-1:0]   spm_wr_wstrb;

  assign spm_rd_data = {32'd0, 32'(spm_rd_addr)};

  gbp_pe_fetch_subsystem u_dut (
    .clk(clk)
    ,.rst_n(rst_n)
    ,.rx_notif_valid_i(rx_notif_valid_i)
    ,.rx_notif_ready_o(rx_notif_ready_o)
    ,.rx_notif_source_node_id_i(rx_notif_source_node_id_i)
    ,.rx_notif_is_factor_i(rx_notif_is_factor_i)
    ,.rx_notif_source_x_i(rx_notif_source_x_i)
    ,.rx_notif_source_y_i(rx_notif_source_y_i)
    ,.adj_valid_i(adj_valid_i)
    ,.adj_ready_o(adj_ready_o)
    ,.adj_neighbor_id_i(adj_neighbor_id_i)
    ,.adj_neighbor_x_i(adj_neighbor_x_i)
    ,.adj_neighbor_y_i(adj_neighbor_y_i)
    ,.adj_is_local_i(adj_is_local_i)
    ,.adj_last_i(adj_last_i)
    ,.adj_edge_idx_i(adj_edge_idx_i)
    ,.adj_current_node_id_i(adj_current_node_id_i)
    ,.node_ready_o()
    ,.reset_valid_i(1'b0)
    ,.reset_node_id_i('0)
    ,.reset_is_factor_i(1'b0)
    ,.tx_fetch_req_valid_o(tx_fetch_req_valid_o)
    ,.tx_fetch_req_ready_i(tx_fetch_req_ready_i)
    ,.tx_fetch_req_target_node_id_o(tx_fetch_req_target_node_id_o)
    ,.tx_fetch_req_consumer_node_id_o()
    ,.tx_fetch_req_is_factor_o()
    ,.tx_fetch_req_target_x_o()
    ,.tx_fetch_req_target_y_o()
    ,.tx_fetch_req_txn_id_o()
    ,.rx_fetch_resp_valid_i(rx_fetch_resp_valid_i)
    ,.rx_fetch_resp_is_factor_i(rx_fetch_resp_is_factor_i)
    ,.rx_fetch_resp_state_words_i(rx_fetch_resp_state_words_i)
    ,.rx_fetch_resp_data_i(rx_fetch_resp_data_i)
    ,.rx_fetch_resp_data_valid_i(rx_fetch_resp_data_valid_i)
    ,.rx_fetch_resp_last_i(rx_fetch_resp_last_i)
    ,.rx_fetch_resp_done_valid_i(rx_fetch_resp_done_valid_i)
    ,.rx_fetch_resp_txn_id_i(rx_fetch_resp_txn_id_i)
    ,.rx_fetch_resp_node_id_i(rx_fetch_resp_node_id_i)
    ,.rx_fetch_resp_consumer_node_id_i(rx_fetch_resp_consumer_node_id_i)
    ,.spm_rd_valid_o()
    ,.spm_rd_ready_i(1'b1)
    ,.spm_rd_addr_o(spm_rd_addr)
    ,.spm_rd_data_i(spm_rd_data)
    ,.spm_wr_valid_o()
    ,.spm_wr_ready_i(1'b1)
    ,.spm_wr_addr_o(spm_wr_addr)
    ,.spm_wr_data_o(spm_wr_data)
    ,.spm_wr_wstrb_o(spm_wr_wstrb)
    ,.remote_valid_o(remote_valid_o)
    ,.remote_ready_i(remote_ready_i)
    ,.remote_data_o(remote_data_o)
    ,.remote_last_o(remote_last_o)
    ,.batch_done_i(1'b0)
  );

  assign scoreboard_occupancy_o = u_dut.u_scoreboard.scoreboard_occupancy_o;
  assign node_ready_o = u_dut.u_scoreboard.node_ready_o;

endmodule
