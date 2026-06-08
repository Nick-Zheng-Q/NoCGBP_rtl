`include "bsg_manycore_defines.svh"

module gbp_pe
  import bsg_manycore_pkg::*;
  import gbp_pkg::*;
#(
    x_cord_width_p
    , y_cord_width_p
    , data_width_p
    , addr_width_p
    , dmem_size_p
    , debug_p = 0
    , num_tiles_x_p
    , num_tiles_y_p
    , pod_x_cord_width_p
    , pod_y_cord_width_p
    , fwd_fifo_els_p
    , rev_fifo_els_p
    , barrier_dirs_p
    , ipoly_hashing_p

    , localparam x_subcord_width_lp = `BSG_SAFE_CLOG2(num_tiles_x_p)
    , localparam y_subcord_width_lp = `BSG_SAFE_CLOG2(num_tiles_y_p)
    , localparam barrier_lg_dirs_lp = `BSG_SAFE_CLOG2(barrier_dirs_p+1)
    , localparam link_sif_width_lp =
      `bsg_manycore_link_sif_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)
    , localparam packet_width_lp =
      `bsg_manycore_packet_width(addr_width_p,data_width_p,x_cord_width_p,y_cord_width_p)
    , localparam credit_counter_width_lp = `BSG_WIDTH(32)
  )
  (
    input clk_i
    , input reset_i

    , input  [link_sif_width_lp-1:0] link_sif_i
    , output [link_sif_width_lp-1:0] link_sif_o

    , input  barrier_data_i
    , output barrier_data_o
    , output [barrier_dirs_p-1:0]     barrier_src_r_o
    , output [barrier_lg_dirs_lp-1:0] barrier_dest_r_o

    , input [x_subcord_width_lp-1:0] my_x_i
    , input [y_subcord_width_lp-1:0] my_y_i

    , input [pod_x_cord_width_p-1:0] pod_x_i
    , input [pod_y_cord_width_p-1:0] pod_y_i
`ifdef GBP_WHITEBOX_TEST
    , input logic wb_cmd_valid_i
    , input logic [NODE_ID_W-1:0] wb_cmd_node_id_i
    , input logic wb_cmd_is_factor_i
    , input logic [DOF_W-1:0] wb_cmd_dof_i
    , input logic [ADJ_COUNT_W-1:0] wb_cmd_adj_count_i
    , input logic [STATE_WORDS_W-1:0] wb_cmd_state_words_i
    , input logic [MAX_ADJ_COUNT-1:0] wb_cmd_adj_is_local_i
    , input logic wb_force_done_valid_i
    , output logic wb_cmd_ready_o
    , output logic wb_done_valid_o
    , output logic tx_notif_valid_o
    , output logic reset_valid_o
`endif
  );

  logic rst_n;
  assign rst_n = ~reset_i;

  // My coordinates as seen by GBP subsystems (subcord width)
  wire [X_CORD_W-1:0] my_x_gbp = X_CORD_W'(my_x_i);
  wire [Y_CORD_W-1:0] my_y_gbp = Y_CORD_W'(my_y_i);

  // ========================================================================
  // NoC Adapter
  // ========================================================================
  // TX: Writeback Controller -> NoC (NOTIFICATION)
  logic                 tx_notif_valid;
  logic                 tx_notif_ready;
  logic [NODE_ID_W-1:0] tx_notif_source_node_id;
  logic [NODE_ID_W-1:0] tx_notif_target_node_id;
  logic                 tx_notif_is_factor;
  logic [x_cord_width_p-1:0] tx_notif_target_x;
  logic [y_cord_width_p-1:0] tx_notif_target_y;

  // TX: Fetch Subsystem -> NoC (FETCH_REQUEST)
  logic                 tx_fetch_req_valid;
  logic                 tx_fetch_req_ready;
  logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id;
  logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id;
  logic                 tx_fetch_req_is_factor;
  logic [x_cord_width_p-1:0] tx_fetch_req_target_x;
  logic [y_cord_width_p-1:0] tx_fetch_req_target_y;
  logic [TXN_ID_W-1:0]  tx_fetch_req_txn_id;
  logic [1:0]           tx_fetch_req_store_idx;

  // TX: Pull Server -> NoC (FETCH_RESPONSE) — not integrated; tied off
  logic                 tx_fetch_resp_valid;
  logic                 tx_fetch_resp_ready;
  logic [NODE_ID_W-1:0] tx_fetch_resp_node_id;
  logic [NODE_ID_W-1:0] tx_fetch_resp_consumer_node_id;
  logic                 tx_fetch_resp_is_factor;
  logic [STATE_WORDS_W-1:0] tx_fetch_resp_state_words;
  logic [data_width_p-1:0]  tx_fetch_resp_data;
  logic                 tx_fetch_resp_data_valid;
  logic                 tx_fetch_resp_last;
  logic [TXN_ID_W-1:0]  tx_fetch_resp_txn_id;

  // RX: NoC -> Fetch Subsystem (NOTIFICATION)
  logic                 rx_notif_valid;
  logic                 rx_notif_ready;
  logic [NODE_ID_W-1:0] rx_notif_source_node_id;
  logic                 rx_notif_is_factor;
  logic [x_cord_width_p-1:0] rx_notif_source_x;
  logic [y_cord_width_p-1:0] rx_notif_source_y;

  // RX: NoC -> Pull Server (FETCH_REQUEST) — not integrated; tied off
  logic                 rx_fetch_req_valid;
  logic                 rx_fetch_req_ready;
  logic [NODE_ID_W-1:0] rx_fetch_req_target_node_id;
  logic [NODE_ID_W-1:0] rx_fetch_req_consumer_node_id;
  logic                 rx_fetch_req_is_factor;
  logic [x_cord_width_p-1:0] rx_fetch_req_src_x;
  logic [y_cord_width_p-1:0] rx_fetch_req_src_y;
  logic [TXN_ID_W-1:0]  rx_fetch_req_txn_id;

  // RX: NoC -> Fetch Subsystem (FETCH_RESPONSE)
  logic                 rx_fetch_resp_valid;
  logic                 rx_fetch_resp_is_factor;
  logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words;
  logic [data_width_p-1:0]  rx_fetch_resp_data;
  logic                 rx_fetch_resp_data_valid;
  logic                 rx_fetch_resp_last;
  logic                 rx_fetch_resp_done_valid;
  logic [TXN_ID_W-1:0]  rx_fetch_resp_txn_id;
  logic [NODE_ID_W-1:0] rx_fetch_resp_node_id;
  logic [NODE_ID_W-1:0] rx_fetch_resp_consumer_node_id;

  noc_adapter #(
    .data_width_p(data_width_p)
    ,.addr_width_p(addr_width_p)
    ,.x_cord_width_p(x_cord_width_p)
    ,.y_cord_width_p(y_cord_width_p)
    ,.fifo_els_p(fwd_fifo_els_p)
    ,.rev_fifo_els_p(rev_fifo_els_p)
  ) u_noc_adapter (
    .clk_i(clk_i)
    ,.rst_n_i(rst_n)
    ,.link_sif_i(link_sif_i)
    ,.link_sif_o(link_sif_o)
    ,.my_x_i({pod_x_i, my_x_i})
    ,.my_y_i({pod_y_i, my_y_i})

    ,.tx_notif_valid_i(tx_notif_valid)
    ,.tx_notif_ready_o(tx_notif_ready)
    ,.tx_notif_source_node_id_i(tx_notif_source_node_id)
    ,.tx_notif_target_node_id_i(tx_notif_target_node_id)
    ,.tx_notif_is_factor_i(tx_notif_is_factor)
    ,.tx_notif_target_x_i(tx_notif_target_x)
    ,.tx_notif_target_y_i(tx_notif_target_y)

    ,.tx_fetch_req_valid_i(tx_fetch_req_valid)
    ,.tx_fetch_req_ready_o(tx_fetch_req_ready)
    ,.tx_fetch_req_target_node_id_i(tx_fetch_req_target_node_id)
    ,.tx_fetch_req_consumer_node_id_i(tx_fetch_req_consumer_node_id)
    ,.tx_fetch_req_is_factor_i(tx_fetch_req_is_factor)
    ,.tx_fetch_req_target_x_i(tx_fetch_req_target_x)
    ,.tx_fetch_req_target_y_i(tx_fetch_req_target_y)
    ,.tx_fetch_req_txn_id_i(tx_fetch_req_txn_id)
    ,.tx_fetch_req_store_idx_i(tx_fetch_req_store_idx)

    ,.tx_fetch_resp_valid_i(tx_fetch_resp_valid)
    ,.tx_fetch_resp_ready_o(tx_fetch_resp_ready)
    ,.tx_fetch_resp_node_id_i(tx_fetch_resp_node_id)
    ,.tx_fetch_resp_consumer_node_id_i(tx_fetch_resp_consumer_node_id)
    ,.tx_fetch_resp_is_factor_i(tx_fetch_resp_is_factor)
    ,.tx_fetch_resp_state_words_i(tx_fetch_resp_state_words)
    ,.tx_fetch_resp_data_i(tx_fetch_resp_data)
    ,.tx_fetch_resp_data_valid_i(tx_fetch_resp_data_valid)
    ,.tx_fetch_resp_last_i(tx_fetch_resp_last)
    ,.tx_fetch_resp_txn_id_i(tx_fetch_resp_txn_id)

    ,.rx_notif_valid_o(rx_notif_valid)
    ,.rx_notif_ready_i(rx_notif_ready)
    ,.rx_notif_source_node_id_o(rx_notif_source_node_id)
    ,.rx_notif_is_factor_o(rx_notif_is_factor)
    ,.rx_notif_source_x_o(rx_notif_source_x)
    ,.rx_notif_source_y_o(rx_notif_source_y)

    ,.rx_fetch_req_valid_o(rx_fetch_req_valid)
    ,.rx_fetch_req_ready_i(rx_fetch_req_ready)
    ,.rx_fetch_req_target_node_id_o(rx_fetch_req_target_node_id)
    ,.rx_fetch_req_consumer_node_id_o(rx_fetch_req_consumer_node_id)
    ,.rx_fetch_req_is_factor_o(rx_fetch_req_is_factor)
    ,.rx_fetch_req_src_x_o(rx_fetch_req_src_x)
    ,.rx_fetch_req_src_y_o(rx_fetch_req_src_y)
    ,.rx_fetch_req_txn_id_o(rx_fetch_req_txn_id)

    ,.rx_fetch_resp_valid_o(rx_fetch_resp_valid)
    ,.rx_fetch_resp_is_factor_o(rx_fetch_resp_is_factor)
    ,.rx_fetch_resp_state_words_o(rx_fetch_resp_state_words)
    ,.rx_fetch_resp_data_o(rx_fetch_resp_data)
    ,.rx_fetch_resp_data_valid_o(rx_fetch_resp_data_valid)
    ,.rx_fetch_resp_last_o(rx_fetch_resp_last)
    ,.rx_fetch_resp_done_valid_o(rx_fetch_resp_done_valid)
    ,.rx_fetch_resp_txn_id_o(rx_fetch_resp_txn_id)
    ,.rx_fetch_resp_node_id_o(rx_fetch_resp_node_id)
    ,.rx_fetch_resp_consumer_node_id_o(rx_fetch_resp_consumer_node_id)

    ,.tx_busy_o()
  );

  // ========================================================================
  // Control Subsystem
  // ========================================================================
  logic [NUM_NODES_PER_PE-1:0] ctrl_node_ready;
  logic                 ctrl_cmd_valid;
  logic                 ctrl_cmd_ready;
  logic [NODE_ID_W-1:0] ctrl_cmd_node_id;
  logic                 ctrl_cmd_is_factor;
  logic [DOF_W-1:0]     ctrl_cmd_dof;
  logic [ADJ_COUNT_W-1:0] ctrl_cmd_adj_count;
  logic [STATE_WORDS_W-1:0] ctrl_cmd_state_words;
  logic [SPM_ADDR_W-1:0]  ctrl_cmd_state_base;

  logic [ADJ_COUNT_W-1:0] ctrl_wb_adj_count;
  logic [MAX_ADJ_COUNT-1:0][NODE_ID_W-1:0] ctrl_wb_adj_neighbor_ids;
  logic [MAX_ADJ_COUNT-1:0][X_CORD_W-1:0]  ctrl_wb_adj_neighbor_xs;
  logic [MAX_ADJ_COUNT-1:0][Y_CORD_W-1:0]  ctrl_wb_adj_neighbor_ys;
  logic [MAX_ADJ_COUNT-1:0]                ctrl_wb_adj_is_local;

  logic [ADJ_COUNT_W-1:0] ctrl_wb_adj_count_ctrl;
  logic [MAX_ADJ_COUNT-1:0][NODE_ID_W-1:0] ctrl_wb_adj_neighbor_ids_ctrl;
  logic [MAX_ADJ_COUNT-1:0][X_CORD_W-1:0]  ctrl_wb_adj_neighbor_xs_ctrl;
  logic [MAX_ADJ_COUNT-1:0][Y_CORD_W-1:0]  ctrl_wb_adj_neighbor_ys_ctrl;
  logic [MAX_ADJ_COUNT-1:0]                ctrl_wb_adj_is_local_ctrl;

  logic                 ctrl_adj_valid;
  logic                 ctrl_adj_ready;
  logic [NODE_ID_W-1:0] ctrl_adj_neighbor_id;
  logic [X_CORD_W-1:0]  ctrl_adj_neighbor_x;
  logic [Y_CORD_W-1:0]  ctrl_adj_neighbor_y;
  logic                 ctrl_adj_is_local;
  logic                 ctrl_adj_last;
  logic [ADJ_COUNT_W-1:0] ctrl_adj_edge_idx;

  logic                 ctrl_spm_rd_valid;
  logic                 ctrl_spm_rd_ready;
  logic [SPM_ADDR_W-1:0] ctrl_spm_rd_addr;
  logic [BEAT_BITS-1:0] ctrl_spm_rd_data;

  logic                 ctrl_local_valid;
  logic                 ctrl_local_ready;
  logic [FP32_W-1:0]   ctrl_local_data;
  logic                 ctrl_local_last;

  logic                 ctrl_reset_valid;
  logic [NODE_ID_W-1:0] ctrl_reset_node_id;
  logic                 ctrl_reset_is_factor;

  logic                 ctrl_wb_done;

  gbp_pe_control_subsystem u_control_subsystem (
    .clk(clk_i)
    ,.rst_n(rst_n)
    ,.node_ready_i(ctrl_node_ready)
    ,.wb_done_i(ctrl_wb_done)
    ,.cmd_valid_o(ctrl_cmd_valid)
    ,.cmd_ready_i(ctrl_cmd_ready)
    ,.cmd_node_id_o(ctrl_cmd_node_id)
    ,.cmd_is_factor_o(ctrl_cmd_is_factor)
    ,.cmd_dof_o(ctrl_cmd_dof)
    ,.cmd_adj_count_o(ctrl_cmd_adj_count)
    ,.cmd_state_words_o(ctrl_cmd_state_words)
    ,.cmd_state_base_o(ctrl_cmd_state_base)
    ,.wb_adj_count_o(ctrl_wb_adj_count_ctrl)
    ,.wb_adj_neighbor_ids_o(ctrl_wb_adj_neighbor_ids_ctrl)
    ,.wb_adj_neighbor_xs_o(ctrl_wb_adj_neighbor_xs_ctrl)
    ,.wb_adj_neighbor_ys_o(ctrl_wb_adj_neighbor_ys_ctrl)
    ,.wb_adj_is_local_o(ctrl_wb_adj_is_local_ctrl)
    ,.adj_valid_o(ctrl_adj_valid)
    ,.adj_ready_i(ctrl_adj_ready)
    ,.adj_neighbor_id_o(ctrl_adj_neighbor_id)
    ,.adj_neighbor_x_o(ctrl_adj_neighbor_x)
    ,.adj_neighbor_y_o(ctrl_adj_neighbor_y)
    ,.adj_is_local_o(ctrl_adj_is_local)
    ,.adj_last_o(ctrl_adj_last)
    ,.adj_edge_idx_o(ctrl_adj_edge_idx)
    ,.reset_valid_i(ctrl_reset_valid)
    ,.reset_node_id_i(ctrl_reset_node_id)
    ,.reset_is_factor_i(ctrl_reset_is_factor)
    ,.spm_rd_valid_o(ctrl_spm_rd_valid)
    ,.spm_rd_ready_i(ctrl_spm_rd_ready)
    ,.spm_rd_addr_o(ctrl_spm_rd_addr)
    ,.spm_rd_data_i(ctrl_spm_rd_data)
    ,.local_valid_o(ctrl_local_valid)
    ,.local_ready_i(ctrl_local_ready)
    ,.local_data_o(ctrl_local_data)
    ,.local_last_o(ctrl_local_last)
    ,.my_x_i(my_x_gbp)
    ,.my_y_i(my_y_gbp)
  );

  // ========================================================================
  // Fetch Subsystem
  // ========================================================================
  logic                 fetch_tx_fetch_req_valid;
  logic                 fetch_tx_fetch_req_ready;
  logic [NODE_ID_W-1:0] fetch_tx_fetch_req_target_node_id;
  logic [NODE_ID_W-1:0] fetch_tx_fetch_req_consumer_node_id;
  logic                 fetch_tx_fetch_req_is_factor;
  logic [X_CORD_W-1:0]  fetch_tx_fetch_req_target_x;
  logic [Y_CORD_W-1:0]  fetch_tx_fetch_req_target_y;
  logic [TXN_ID_W-1:0]  fetch_tx_fetch_req_txn_id;

  logic                 fetch_spm_rd_valid;
  logic                 fetch_spm_rd_ready;
  logic [SPM_ADDR_W-1:0] fetch_spm_rd_addr;
  logic [BEAT_BITS-1:0] fetch_spm_rd_data;

  logic                 fetch_spm_wr_valid;
  logic                 fetch_spm_wr_ready;
  logic [SPM_ADDR_W-1:0] fetch_spm_wr_addr;
  logic [BEAT_BITS-1:0] fetch_spm_wr_data;
  logic [WSTRB_W-1:0]   fetch_spm_wr_wstrb;

  logic                 fetch_remote_valid;
  logic                 fetch_remote_ready;
  logic [data_width_p-1:0] fetch_remote_data;
  logic                 fetch_remote_last;

  gbp_pe_fetch_subsystem u_fetch_subsystem (
    .clk(clk_i)
    ,.rst_n(rst_n)
    ,.rx_notif_valid_i(rx_notif_valid)
    ,.rx_notif_ready_o(rx_notif_ready)
    ,.rx_notif_source_node_id_i(rx_notif_source_node_id)
    ,.rx_notif_is_factor_i(rx_notif_is_factor)
    ,.rx_notif_source_x_i(X_CORD_W'(rx_notif_source_x))
    ,.rx_notif_source_y_i(Y_CORD_W'(rx_notif_source_y))
    ,.adj_valid_i(ctrl_adj_valid)
    ,.adj_ready_o(ctrl_adj_ready)
    ,.adj_neighbor_id_i(ctrl_adj_neighbor_id)
    ,.adj_neighbor_x_i(ctrl_adj_neighbor_x)
    ,.adj_neighbor_y_i(ctrl_adj_neighbor_y)
    ,.adj_is_local_i(ctrl_adj_is_local)
    ,.adj_last_i(ctrl_adj_last)
    ,.adj_edge_idx_i(ctrl_adj_edge_idx)
    ,.adj_current_node_id_i(ctrl_cmd_node_id)
    ,.node_ready_o(ctrl_node_ready)
    ,.reset_valid_i(ctrl_reset_valid)
    ,.reset_node_id_i(ctrl_reset_node_id)
    ,.reset_is_factor_i(ctrl_reset_is_factor)
    ,.tx_fetch_req_valid_o(fetch_tx_fetch_req_valid)
    ,.tx_fetch_req_ready_i(fetch_tx_fetch_req_ready)
    ,.tx_fetch_req_target_node_id_o(fetch_tx_fetch_req_target_node_id)
    ,.tx_fetch_req_consumer_node_id_o(fetch_tx_fetch_req_consumer_node_id)
    ,.tx_fetch_req_is_factor_o(fetch_tx_fetch_req_is_factor)
    ,.tx_fetch_req_target_x_o(fetch_tx_fetch_req_target_x)
    ,.tx_fetch_req_target_y_o(fetch_tx_fetch_req_target_y)
    ,.tx_fetch_req_txn_id_o(fetch_tx_fetch_req_txn_id)
    ,.rx_fetch_resp_valid_i(rx_fetch_resp_valid)
    ,.rx_fetch_resp_is_factor_i(rx_fetch_resp_is_factor)
    ,.rx_fetch_resp_state_words_i(rx_fetch_resp_state_words)
    ,.rx_fetch_resp_data_i(rx_fetch_resp_data)
    ,.rx_fetch_resp_data_valid_i(rx_fetch_resp_data_valid)
    ,.rx_fetch_resp_last_i(rx_fetch_resp_last)
    ,.rx_fetch_resp_done_valid_i(rx_fetch_resp_done_valid)
    ,.rx_fetch_resp_txn_id_i(rx_fetch_resp_txn_id)
    ,.rx_fetch_resp_node_id_i(rx_fetch_resp_node_id)
    ,.rx_fetch_resp_consumer_node_id_i(rx_fetch_resp_consumer_node_id)
    ,.spm_rd_valid_o(fetch_spm_rd_valid)
    ,.spm_rd_ready_i(fetch_spm_rd_ready)
    ,.spm_rd_addr_o(fetch_spm_rd_addr)
    ,.spm_rd_data_i(fetch_spm_rd_data)
    ,.spm_wr_valid_o(fetch_spm_wr_valid)
    ,.spm_wr_ready_i(fetch_spm_wr_ready)
    ,.spm_wr_addr_o(fetch_spm_wr_addr)
    ,.spm_wr_data_o(fetch_spm_wr_data)
    ,.spm_wr_wstrb_o(fetch_spm_wr_wstrb)
    ,.remote_valid_o(fetch_remote_valid)
    ,.remote_ready_i(fetch_remote_ready)
    ,.remote_data_o(fetch_remote_data)
    ,.remote_last_o(fetch_remote_last)
    ,.batch_done_i(comp_batch_done)
  );

  // ========================================================================
  // Compute Subsystem
  // ========================================================================
  logic                 comp_cmd_valid;
  logic                 comp_cmd_ready;
  logic [NODE_ID_W-1:0] comp_cmd_node_id;
  logic                 comp_cmd_is_factor;
  logic [DOF_W-1:0]     comp_cmd_dof;
  logic [ADJ_COUNT_W-1:0] comp_cmd_adj_count;
  logic [STATE_WORDS_W-1:0] comp_cmd_state_words;
  logic [SPM_ADDR_W-1:0]  comp_cmd_state_base;

  logic                 comp_ns_valid;
  logic                 comp_ns_ready;
  logic [FP32_W-1:0]   comp_ns_data;
  logic                 comp_ns_last;

  logic                 comp_spm_rd0_valid;
  logic                 comp_spm_rd0_ready;
  logic [SPM_ADDR_W-1:0] comp_spm_rd0_addr;
  logic [BEAT_BITS-1:0] comp_spm_rd0_data;

  logic                 comp_spm_rd1_valid;
  logic                 comp_spm_rd1_ready;
  logic [SPM_ADDR_W-1:0] comp_spm_rd1_addr;
  logic [BEAT_BITS-1:0] comp_spm_rd1_data;

  logic                 comp_spm_wr_valid;
  logic                 comp_spm_wr_ready;
  logic [SPM_ADDR_W-1:0] comp_spm_wr_addr;
  logic [BEAT_BITS-1:0] comp_spm_wr_data;
  logic [WSTRB_W-1:0]   comp_spm_wr_wstrb;

  logic                 comp_done_valid;
  logic [NODE_ID_W-1:0] comp_done_node_id;
  logic                 comp_done_is_factor;
  logic                 comp_batch_done;

`ifdef GBP_WHITEBOX_TEST
  assign comp_cmd_valid        = wb_cmd_valid_i;
  assign comp_cmd_node_id      = wb_cmd_node_id_i;
  assign comp_cmd_is_factor    = wb_cmd_is_factor_i;
  assign comp_cmd_dof          = wb_cmd_dof_i;
  assign comp_cmd_adj_count    = wb_cmd_adj_count_i;
  assign comp_cmd_state_words  = wb_cmd_state_words_i;
  assign comp_cmd_state_base   = SPM_ADDR_W'(wb_cmd_node_id_i) << 4;
  assign wb_cmd_ready_o        = comp_cmd_ready;
  assign wb_done_valid_o       = comp_done_valid;
  assign tx_notif_valid_o      = tx_notif_valid;
  assign reset_valid_o         = ctrl_reset_valid;

  // In whitebox mode the control subsystem is bypassed; tie off its outputs
  // to prevent X propagation and keep lint clean.
  assign ctrl_cmd_ready        = 1'b0;
  assign ctrl_adj_ready        = 1'b0;
  assign ctrl_local_ready      = 1'b0;
  assign ctrl_spm_rd_ready     = 1'b0;
  assign ctrl_wb_done          = 1'b0;
  assign ctrl_wb_adj_count     = wb_cmd_adj_count_i;
  assign ctrl_wb_adj_is_local  = wb_cmd_adj_is_local_i;
  assign ctrl_wb_adj_neighbor_ids = '0;
  assign ctrl_wb_adj_neighbor_xs  = '0;
  assign ctrl_wb_adj_neighbor_ys  = '0;
  assign ctrl_node_ready       = 1'b0;
`else
  assign comp_cmd_valid        = ctrl_cmd_valid;
  assign comp_cmd_node_id      = ctrl_cmd_node_id;
  assign comp_cmd_is_factor    = ctrl_cmd_is_factor;
  assign comp_cmd_dof          = ctrl_cmd_dof;
  assign comp_cmd_adj_count    = ctrl_cmd_adj_count;
  assign comp_cmd_state_words  = ctrl_cmd_state_words;
  assign comp_cmd_state_base   = ctrl_cmd_state_base;
  assign ctrl_cmd_ready        = comp_cmd_ready;
  assign ctrl_wb_adj_count     = ctrl_wb_adj_count_ctrl;
  assign ctrl_wb_adj_is_local  = ctrl_wb_adj_is_local_ctrl;
  assign ctrl_wb_adj_neighbor_ids = ctrl_wb_adj_neighbor_ids_ctrl;
  assign ctrl_wb_adj_neighbor_xs  = ctrl_wb_adj_neighbor_xs_ctrl;
  assign ctrl_wb_adj_neighbor_ys  = ctrl_wb_adj_neighbor_ys_ctrl;
`endif

  gbp_pe_compute_subsystem u_compute_subsystem (
    .clk(clk_i)
    ,.rst_n(rst_n)
    ,.cmd_valid_i(comp_cmd_valid)
    ,.cmd_ready_o(comp_cmd_ready)
    ,.cmd_node_id_i(comp_cmd_node_id)
    ,.cmd_is_factor_i(comp_cmd_is_factor)
    ,.cmd_dof_i(comp_cmd_dof)
    ,.cmd_adj_count_i(comp_cmd_adj_count)
    ,.cmd_state_words_i(comp_cmd_state_words)
    ,.cmd_state_base_i(comp_cmd_state_base)
    ,.ns_valid_i(comp_ns_valid)
    ,.ns_ready_o(comp_ns_ready)
    ,.ns_data_i(comp_ns_data)
    ,.ns_last_i(comp_ns_last)
    ,.spm_rd0_valid_o(comp_spm_rd0_valid)
    ,.spm_rd0_ready_i(comp_spm_rd0_ready)
    ,.spm_rd0_addr_o(comp_spm_rd0_addr)
    ,.spm_rd0_data_i(comp_spm_rd0_data)
    ,.spm_rd1_valid_o(comp_spm_rd1_valid)
    ,.spm_rd1_ready_i(comp_spm_rd1_ready)
    ,.spm_rd1_addr_o(comp_spm_rd1_addr)
    ,.spm_rd1_data_i(comp_spm_rd1_data)
    ,.spm_wr_valid_o(comp_spm_wr_valid)
    ,.spm_wr_ready_i(comp_spm_wr_ready)
    ,.spm_wr_addr_o(comp_spm_wr_addr)
    ,.spm_wr_data_o(comp_spm_wr_data)
    ,.spm_wr_wstrb_o(comp_spm_wr_wstrb)
    ,.done_valid_o(comp_done_valid)
    ,.done_node_id_o(comp_done_node_id)
    ,.done_is_factor_o(comp_done_is_factor)
    ,.batch_done_o(comp_batch_done)
  );

  // ========================================================================
  // Neighbor State Accumulator
  // ========================================================================
  neighbor_state_accumulator u_accumulator (
    .clk_i(clk_i)
    ,.rst_n_i(rst_n)
    ,.local_valid_i(ctrl_local_valid)
    ,.local_ready_o(ctrl_local_ready)
    ,.local_data_i(ctrl_local_data)
    ,.local_last_i(ctrl_local_last)
    ,.remote_valid_i(fetch_remote_valid)
    ,.remote_ready_o(fetch_remote_ready)
    ,.remote_data_i(fetch_remote_data)
    ,.remote_last_i(fetch_remote_last)
    ,.out_valid_o(comp_ns_valid)
    ,.out_ready_i(comp_ns_ready)
    ,.out_data_o(comp_ns_data)
    ,.out_last_o(comp_ns_last)
    ,.start_i(ctrl_cmd_valid && comp_cmd_ready)
    ,.accumulator_done_o()
  );

  // ========================================================================
  // Writeback Controller
  // ========================================================================
  logic wb_done_valid;
`ifdef GBP_WHITEBOX_TEST
  assign wb_done_valid = wb_force_done_valid_i | comp_done_valid;
`else
  assign wb_done_valid = comp_done_valid;
`endif

  writeback_controller u_writeback_controller (
    .clk_i(clk_i)
    ,.rst_n_i(rst_n)
    ,.done_valid_i(wb_done_valid)
    ,.done_node_id_i(comp_done_node_id)
    ,.done_is_factor_i(comp_done_is_factor)
    ,.adj_count_i(ctrl_wb_adj_count)
    ,.adj_neighbor_ids_i(ctrl_wb_adj_neighbor_ids)
    ,.adj_neighbor_xs_i(ctrl_wb_adj_neighbor_xs)
    ,.adj_neighbor_ys_i(ctrl_wb_adj_neighbor_ys)
    ,.adj_is_local_i(ctrl_wb_adj_is_local)
    ,.tx_notif_valid_o(tx_notif_valid)
    ,.tx_notif_ready_i(tx_notif_ready)
    ,.tx_notif_source_node_id_o(tx_notif_source_node_id)
    ,.tx_notif_target_node_id_o(tx_notif_target_node_id)
    ,.tx_notif_is_factor_o(tx_notif_is_factor)
    ,.tx_notif_target_x_o(tx_notif_target_x)
    ,.tx_notif_target_y_o(tx_notif_target_y)
    ,.reset_valid_o(ctrl_reset_valid)
    ,.reset_node_id_o(ctrl_reset_node_id)
    ,.reset_is_factor_o(ctrl_reset_is_factor)
    ,.wb_done_o(ctrl_wb_done)
  );

  // ========================================================================
  // Memory Subsystem
  // ========================================================================
  // Client mapping:
  //   0: control (rd)
  //   1: compute rd0 (STATE)
  //   2: compute rd1 (STAGING)
  //   3: compute wr
  //   4: fetch rd (tied off — pull_server not integrated)
  //   5: fetch wr
  //   6: DMA (tied off)
  localparam int MEM_CLIENTS = 7;
  logic [MEM_CLIENTS-1:0]                 mem_rd_valid;
  logic [MEM_CLIENTS-1:0]                 mem_rd_ready;
  logic [MEM_CLIENTS-1:0][SPM_ADDR_W-1:0] mem_rd_addr;
  logic [MEM_CLIENTS-1:0][BEAT_BITS-1:0]  mem_rd_data;

  logic [MEM_CLIENTS-1:0]                 mem_wr_valid;
  logic [MEM_CLIENTS-1:0]                 mem_wr_ready;
  logic [MEM_CLIENTS-1:0][SPM_ADDR_W-1:0] mem_wr_addr;
  logic [MEM_CLIENTS-1:0][BEAT_BITS-1:0]  mem_wr_data;
  logic [MEM_CLIENTS-1:0][WSTRB_W-1:0]   mem_wr_wstrb;

  assign mem_rd_valid[0] = ctrl_spm_rd_valid;
  assign ctrl_spm_rd_ready = mem_rd_ready[0];
  assign mem_rd_addr[0]  = ctrl_spm_rd_addr;
  assign ctrl_spm_rd_data  = mem_rd_data[0];

  assign mem_rd_valid[1] = comp_spm_rd0_valid;
  assign comp_spm_rd0_ready = mem_rd_ready[1];
  assign mem_rd_addr[1]  = comp_spm_rd0_addr;
  assign comp_spm_rd0_data  = mem_rd_data[1];

  assign mem_rd_valid[2] = comp_spm_rd1_valid;
  assign comp_spm_rd1_ready = mem_rd_ready[2];
  assign mem_rd_addr[2]  = comp_spm_rd1_addr;
  assign comp_spm_rd1_data  = mem_rd_data[2];

  assign mem_wr_valid[3] = comp_spm_wr_valid;
  assign comp_spm_wr_ready = mem_wr_ready[3];
  assign mem_wr_addr[3]  = comp_spm_wr_addr;
  assign mem_wr_data[3]  = comp_spm_wr_data;
  assign mem_wr_wstrb[3] = comp_spm_wr_wstrb;

  // Client 4: pull_server (STATE read for serving remote fetches)
  assign mem_rd_valid[4] = ps_spm_rd_valid;
  assign ps_spm_rd_ready = mem_rd_ready[4];
  assign mem_rd_addr[4]  = ps_spm_rd_addr;
  assign ps_spm_rd_data  = mem_rd_data[4];

  // Fetch subsystem read port unused (pull_server handles local STATE reads)
  assign fetch_spm_rd_ready = 1'b0;

  assign mem_wr_valid[5] = fetch_spm_wr_valid;
  assign fetch_spm_wr_ready = mem_wr_ready[5];
  assign mem_wr_addr[5]  = fetch_spm_wr_addr;
  assign mem_wr_data[5]  = fetch_spm_wr_data;
  assign mem_wr_wstrb[5] = fetch_spm_wr_wstrb;

  assign mem_rd_valid[6] = 1'b0;
  assign mem_rd_addr[6]  = '0;
  assign mem_wr_valid[6] = 1'b0;
  assign mem_wr_addr[6]  = '0;
  assign mem_wr_data[6]  = '0;
  assign mem_wr_wstrb[6] = '0;

  gbp_pe_memory_subsystem #(
    .NUM_CLIENTS(MEM_CLIENTS)
  ) u_memory_subsystem (
    .clk(clk_i)
    ,.rst_n(rst_n)
    ,.rd_valid_i(mem_rd_valid)
    ,.rd_ready_o(mem_rd_ready)
    ,.rd_addr_i(mem_rd_addr)
    ,.rd_data_o(mem_rd_data)
    ,.wr_valid_i(mem_wr_valid)
    ,.wr_ready_o(mem_wr_ready)
    ,.wr_addr_i(mem_wr_addr)
    ,.wr_data_i(mem_wr_data)
    ,.wr_wstrb_i(mem_wr_wstrb)
  );

  // ========================================================================
  // NoC fetch request mapping (fetch subsystem -> noc_adapter)
  // ========================================================================
  assign tx_fetch_req_valid           = fetch_tx_fetch_req_valid;
  assign fetch_tx_fetch_req_ready     = tx_fetch_req_ready;
  assign tx_fetch_req_target_node_id  = fetch_tx_fetch_req_target_node_id;
  assign tx_fetch_req_consumer_node_id= fetch_tx_fetch_req_consumer_node_id;
  assign tx_fetch_req_is_factor       = fetch_tx_fetch_req_is_factor;
  assign tx_fetch_req_target_x        = x_cord_width_p'(fetch_tx_fetch_req_target_x);
  assign tx_fetch_req_target_y        = y_cord_width_p'(fetch_tx_fetch_req_target_y);
  assign tx_fetch_req_txn_id          = fetch_tx_fetch_req_txn_id;
  assign tx_fetch_req_store_idx       = '0; // single STAGING store

  // ========================================================================
  // Pull Server — serves FETCH_REQUEST from other PEs
  // ========================================================================
  logic                 ps_spm_rd_valid;
  logic                 ps_spm_rd_ready;
  logic [SPM_ADDR_W-1:0] ps_spm_rd_addr;
  logic [BEAT_BITS-1:0] ps_spm_rd_data;

  pull_server u_pull_server (
    .clk_i(clk_i)
    ,.rst_n_i(rst_n)
    ,.req_valid_i(rx_fetch_req_valid)
    ,.req_ready_o(rx_fetch_req_ready)
    ,.req_target_node_id_i(rx_fetch_req_target_node_id)
    ,.req_consumer_node_id_i(rx_fetch_req_consumer_node_id)
    ,.req_is_factor_i(rx_fetch_req_is_factor)
    ,.req_fetch_src_x_i(X_CORD_W'(rx_fetch_req_src_x))
    ,.req_fetch_src_y_i(Y_CORD_W'(rx_fetch_req_src_y))
    ,.req_txn_id_i(rx_fetch_req_txn_id)
    ,.spm_rd_valid_o(ps_spm_rd_valid)
    ,.spm_rd_addr_o(ps_spm_rd_addr)
    ,.spm_rd_ready_i(ps_spm_rd_ready)
    ,.spm_rd_data_i(ps_spm_rd_data)
    ,.tx_fetch_resp_valid_o(tx_fetch_resp_valid)
    ,.tx_fetch_resp_ready_i(tx_fetch_resp_ready)
    ,.tx_fetch_resp_node_id_o(tx_fetch_resp_node_id)
    ,.tx_fetch_resp_consumer_node_id_o(tx_fetch_resp_consumer_node_id)
    ,.tx_fetch_resp_is_factor_o(tx_fetch_resp_is_factor)
    ,.tx_fetch_resp_state_words_o(tx_fetch_resp_state_words)
    ,.tx_fetch_resp_data_o(tx_fetch_resp_data)
    ,.tx_fetch_resp_data_valid_o(tx_fetch_resp_data_valid)
    ,.tx_fetch_resp_last_o(tx_fetch_resp_last)
    ,.tx_fetch_resp_txn_id_o(tx_fetch_resp_txn_id)
  );

  // ========================================================================
  // Barrier (unused)
  // ========================================================================
  assign barrier_data_o = 1'b0;
  assign barrier_src_r_o = '0;
  assign barrier_dest_r_o = '0;

  // ========================================================================
  // Unused signals
  // ========================================================================
  logic unused_signals;
  assign unused_signals = barrier_data_i | debug_p | dmem_size_p | ipoly_hashing_p
    | ^pod_x_i | ^pod_y_i;

endmodule
