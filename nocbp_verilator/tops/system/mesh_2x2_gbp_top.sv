`include "bsg_manycore_defines.svh"

module mesh_2x2_gbp_top (
    input  logic clk
  , input  logic rst_n

  // ---- 1-bit whitebox ports (packed [3:0], C++ uses bit-mask) ----
  , input  logic [3:0] wb_cmd_valid_i
  , input  logic [3:0] wb_cmd_is_factor_i
  , input  logic [3:0] wb_force_done_valid_i
  , output logic [3:0] wb_cmd_ready_o
  , output logic [3:0] wb_done_valid_o
  , output logic [3:0] tx_notif_valid_o
  , output logic [3:0] reset_valid_o
  , output logic [3:0] rx_notif_valid_o
  , output logic [3:0] rx_fetch_req_valid_o
  , output logic [3:0] rx_fetch_resp_valid_o
  , output logic [3:0] tx_fetch_req_valid_o
  , output logic [3:0] tx_fetch_resp_valid_o

  // ---- RX notification payload (one per PE to avoid Verilator flatten) ----
  , output logic [gbp_pkg::NODE_ID_W-1:0] rx_notif_source_node_id_pe0_o
  , output logic [gbp_pkg::NODE_ID_W-1:0] rx_notif_source_node_id_pe1_o
  , output logic [gbp_pkg::NODE_ID_W-1:0] rx_notif_source_node_id_pe2_o
  , output logic [gbp_pkg::NODE_ID_W-1:0] rx_notif_source_node_id_pe3_o

  , output logic rx_notif_is_factor_pe0_o
  , output logic rx_notif_is_factor_pe1_o
  , output logic rx_notif_is_factor_pe2_o
  , output logic rx_notif_is_factor_pe3_o

  // ---- Vector whitebox ports (one per PE to avoid Verilator flatten) ----
  , input  logic [gbp_pkg::NODE_ID_W-1:0] wb_cmd_node_id_pe0_i
  , input  logic [gbp_pkg::NODE_ID_W-1:0] wb_cmd_node_id_pe1_i
  , input  logic [gbp_pkg::NODE_ID_W-1:0] wb_cmd_node_id_pe2_i
  , input  logic [gbp_pkg::NODE_ID_W-1:0] wb_cmd_node_id_pe3_i

  , input  logic [gbp_pkg::DOF_W-1:0] wb_cmd_dof_pe0_i
  , input  logic [gbp_pkg::DOF_W-1:0] wb_cmd_dof_pe1_i
  , input  logic [gbp_pkg::DOF_W-1:0] wb_cmd_dof_pe2_i
  , input  logic [gbp_pkg::DOF_W-1:0] wb_cmd_dof_pe3_i

  , input  logic [gbp_pkg::ADJ_COUNT_W-1:0] wb_cmd_adj_count_pe0_i
  , input  logic [gbp_pkg::ADJ_COUNT_W-1:0] wb_cmd_adj_count_pe1_i
  , input  logic [gbp_pkg::ADJ_COUNT_W-1:0] wb_cmd_adj_count_pe2_i
  , input  logic [gbp_pkg::ADJ_COUNT_W-1:0] wb_cmd_adj_count_pe3_i

  , input  logic [gbp_pkg::STATE_WORDS_W-1:0] wb_cmd_state_words_pe0_i
  , input  logic [gbp_pkg::STATE_WORDS_W-1:0] wb_cmd_state_words_pe1_i
  , input  logic [gbp_pkg::STATE_WORDS_W-1:0] wb_cmd_state_words_pe2_i
  , input  logic [gbp_pkg::STATE_WORDS_W-1:0] wb_cmd_state_words_pe3_i

  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0] wb_cmd_adj_is_local_pe0_i
  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0] wb_cmd_adj_is_local_pe1_i
  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0] wb_cmd_adj_is_local_pe2_i
  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0] wb_cmd_adj_is_local_pe3_i

  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::X_CORD_W-1:0] wb_cmd_adj_neighbor_xs_pe0_i
  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::X_CORD_W-1:0] wb_cmd_adj_neighbor_xs_pe1_i
  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::X_CORD_W-1:0] wb_cmd_adj_neighbor_xs_pe2_i
  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::X_CORD_W-1:0] wb_cmd_adj_neighbor_xs_pe3_i

  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::Y_CORD_W-1:0] wb_cmd_adj_neighbor_ys_pe0_i
  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::Y_CORD_W-1:0] wb_cmd_adj_neighbor_ys_pe1_i
  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::Y_CORD_W-1:0] wb_cmd_adj_neighbor_ys_pe2_i
  , input  logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::Y_CORD_W-1:0] wb_cmd_adj_neighbor_ys_pe3_i

  // ---- Fetch Response injection (testbench simulates pull_server) ----
  , input  logic [3:0] wb_inject_fetch_resp_valid_i
  , input  logic [3:0] wb_inject_fetch_resp_data_valid_i
  , input  logic [3:0][31:0] wb_inject_fetch_resp_data_i
  , input  logic [3:0] wb_inject_fetch_resp_last_i
  , input  logic [3:0] wb_inject_fetch_resp_done_valid_i
  , input  logic [3:0][gbp_pkg::TXN_ID_W-1:0] wb_inject_fetch_resp_txn_id_i
  , input  logic [3:0][gbp_pkg::NODE_ID_W-1:0] wb_inject_fetch_resp_node_id_i
  , input  logic [3:0][gbp_pkg::NODE_ID_W-1:0] wb_inject_fetch_resp_consumer_node_id_i

  // ---- NoC link observation (one per PE) ----
  , output logic [link_sif_width_lp-1:0] pe0_link_sif_o
  , output logic [link_sif_width_lp-1:0] pe1_link_sif_o
  , output logic [link_sif_width_lp-1:0] pe2_link_sif_o
  , output logic [link_sif_width_lp-1:0] pe3_link_sif_o

  // ---- Debug: router->PE ready ----
  , output logic pe0_router_ready
);

  import bsg_manycore_pkg::*;
  import bsg_noc_pkg::*;
  import gbp_pkg::*;

  // --------------------------------------------------------------------------
  // Parameters
  // --------------------------------------------------------------------------
  localparam int x_cord_width_p = gbp_pkg::X_CORD_W;
  localparam int y_cord_width_p = gbp_pkg::Y_CORD_W;
  localparam int data_width_p   = 32;
  localparam int addr_width_p   = 16;
  localparam int dmem_size_p    = 1024;
  localparam int debug_p        = 0;
  localparam int num_tiles_x_p  = 2;
  localparam int num_tiles_y_p  = 2;
  localparam int pod_x_cord_width_p = 1;
  localparam int pod_y_cord_width_p = 1;
  localparam int fwd_fifo_els_p = 4;
  localparam int rev_fifo_els_p = 4;
  localparam int barrier_dirs_p = 4;
  localparam int ipoly_hashing_p = 0;

  localparam int x_subcord_width_lp = (num_tiles_x_p <= 1) ? 1 : $clog2(num_tiles_x_p);
  localparam int y_subcord_width_lp = (num_tiles_y_p <= 1) ? 1 : $clog2(num_tiles_y_p);
  localparam int link_sif_width_lp =
    `bsg_manycore_link_sif_width(addr_width_p, data_width_p, x_cord_width_p, y_cord_width_p);
  localparam int barrier_lg_dirs_lp = $clog2(barrier_dirs_p + 1);

  // --------------------------------------------------------------------------
  // Declare link_sif struct types
  // --------------------------------------------------------------------------
  `declare_bsg_manycore_link_sif_s(addr_width_p, data_width_p, x_cord_width_p, y_cord_width_p);

  // --------------------------------------------------------------------------
  // Reset
  // --------------------------------------------------------------------------
  logic reset_i;
  assign reset_i = ~rst_n;

  // --------------------------------------------------------------------------
  // PE instances
  // --------------------------------------------------------------------------
  bsg_manycore_link_sif_s [1:0][1:0] pe_link_li, pe_link_lo;

  logic [1:0][1:0] pe_barrier_data_i, pe_barrier_data_o;
  logic [1:0][1:0][barrier_dirs_p-1:0] pe_barrier_src_r_o;
  logic [1:0][1:0][barrier_lg_dirs_lp-1:0] pe_barrier_dest_r_o;

  // Tie off barrier
  assign pe_barrier_data_i = '0;

  // Flattened port helpers
  logic [3:0][gbp_pkg::NODE_ID_W-1:0] wb_cmd_node_id_i;
  logic [3:0][gbp_pkg::DOF_W-1:0]     wb_cmd_dof_i;
  logic [3:0][gbp_pkg::ADJ_COUNT_W-1:0]   wb_cmd_adj_count_i;
  logic [3:0][gbp_pkg::STATE_WORDS_W-1:0] wb_cmd_state_words_i;
  logic [3:0][gbp_pkg::MAX_ADJ_COUNT-1:0] wb_cmd_adj_is_local_i;

  assign wb_cmd_node_id_i[0] = wb_cmd_node_id_pe0_i;
  assign wb_cmd_node_id_i[1] = wb_cmd_node_id_pe1_i;
  assign wb_cmd_node_id_i[2] = wb_cmd_node_id_pe2_i;
  assign wb_cmd_node_id_i[3] = wb_cmd_node_id_pe3_i;

  assign wb_cmd_dof_i[0] = wb_cmd_dof_pe0_i;
  assign wb_cmd_dof_i[1] = wb_cmd_dof_pe1_i;
  assign wb_cmd_dof_i[2] = wb_cmd_dof_pe2_i;
  assign wb_cmd_dof_i[3] = wb_cmd_dof_pe3_i;

  assign wb_cmd_adj_count_i[0] = wb_cmd_adj_count_pe0_i;
  assign wb_cmd_adj_count_i[1] = wb_cmd_adj_count_pe1_i;
  assign wb_cmd_adj_count_i[2] = wb_cmd_adj_count_pe2_i;
  assign wb_cmd_adj_count_i[3] = wb_cmd_adj_count_pe3_i;

  assign wb_cmd_state_words_i[0] = wb_cmd_state_words_pe0_i;
  assign wb_cmd_state_words_i[1] = wb_cmd_state_words_pe1_i;
  assign wb_cmd_state_words_i[2] = wb_cmd_state_words_pe2_i;
  assign wb_cmd_state_words_i[3] = wb_cmd_state_words_pe3_i;

  assign wb_cmd_adj_is_local_i[0] = wb_cmd_adj_is_local_pe0_i;
  assign wb_cmd_adj_is_local_i[1] = wb_cmd_adj_is_local_pe1_i;
  assign wb_cmd_adj_is_local_i[2] = wb_cmd_adj_is_local_pe2_i;
  assign wb_cmd_adj_is_local_i[3] = wb_cmd_adj_is_local_pe3_i;

  logic [3:0][gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::X_CORD_W-1:0] wb_cmd_adj_neighbor_xs_i;
  logic [3:0][gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::Y_CORD_W-1:0] wb_cmd_adj_neighbor_ys_i;

  assign wb_cmd_adj_neighbor_xs_i[0] = wb_cmd_adj_neighbor_xs_pe0_i;
  assign wb_cmd_adj_neighbor_xs_i[1] = wb_cmd_adj_neighbor_xs_pe1_i;
  assign wb_cmd_adj_neighbor_xs_i[2] = wb_cmd_adj_neighbor_xs_pe2_i;
  assign wb_cmd_adj_neighbor_xs_i[3] = wb_cmd_adj_neighbor_xs_pe3_i;

  assign wb_cmd_adj_neighbor_ys_i[0] = wb_cmd_adj_neighbor_ys_pe0_i;
  assign wb_cmd_adj_neighbor_ys_i[1] = wb_cmd_adj_neighbor_ys_pe1_i;
  assign wb_cmd_adj_neighbor_ys_i[2] = wb_cmd_adj_neighbor_ys_pe2_i;
  assign wb_cmd_adj_neighbor_ys_i[3] = wb_cmd_adj_neighbor_ys_pe3_i;

  // Flattened RX notification payload outputs
  logic [3:0][gbp_pkg::NODE_ID_W-1:0] rx_notif_source_node_id_o;
  logic [3:0]                         rx_notif_is_factor_o;

  assign rx_notif_source_node_id_pe0_o = rx_notif_source_node_id_o[0];
  assign rx_notif_source_node_id_pe1_o = rx_notif_source_node_id_o[1];
  assign rx_notif_source_node_id_pe2_o = rx_notif_source_node_id_o[2];
  assign rx_notif_source_node_id_pe3_o = rx_notif_source_node_id_o[3];

  assign rx_notif_is_factor_pe0_o = rx_notif_is_factor_o[0];
  assign rx_notif_is_factor_pe1_o = rx_notif_is_factor_o[1];
  assign rx_notif_is_factor_pe2_o = rx_notif_is_factor_o[2];
  assign rx_notif_is_factor_pe3_o = rx_notif_is_factor_o[3];

  for (genvar r = 0; r < 2; r++) begin : g_pe_r
    for (genvar c = 0; c < 2; c++) begin : g_pe_c
      localparam int pe_idx = r * 2 + c;

      gbp_pe #(
        .x_cord_width_p(x_cord_width_p)
        ,.y_cord_width_p(y_cord_width_p)
        ,.data_width_p(data_width_p)
        ,.addr_width_p(addr_width_p)
        ,.dmem_size_p(dmem_size_p)
        ,.debug_p(debug_p)
        ,.num_tiles_x_p(num_tiles_x_p)
        ,.num_tiles_y_p(num_tiles_y_p)
        ,.pod_x_cord_width_p(pod_x_cord_width_p)
        ,.pod_y_cord_width_p(pod_y_cord_width_p)
        ,.fwd_fifo_els_p(fwd_fifo_els_p)
        ,.rev_fifo_els_p(rev_fifo_els_p)
        ,.barrier_dirs_p(barrier_dirs_p)
        ,.ipoly_hashing_p(ipoly_hashing_p)
      ) pe (
        .clk_i(clk)
        ,.reset_i(reset_i)
        ,.link_sif_i(pe_link_li[r][c])
        ,.link_sif_o(pe_link_lo[r][c])
        ,.barrier_data_i(pe_barrier_data_i[r][c])
        ,.barrier_data_o(pe_barrier_data_o[r][c])
        ,.barrier_src_r_o(pe_barrier_src_r_o[r][c])
        ,.barrier_dest_r_o(pe_barrier_dest_r_o[r][c])
        ,.my_x_i(x_subcord_width_lp'(c))
        ,.my_y_i(y_subcord_width_lp'(r))
        ,.pod_x_i(pod_x_cord_width_p'(0))
        ,.pod_y_i(pod_y_cord_width_p'(0))
        ,.wb_cmd_valid_i(wb_cmd_valid_i[pe_idx])
        ,.wb_cmd_node_id_i(wb_cmd_node_id_i[pe_idx])
        ,.wb_cmd_is_factor_i(wb_cmd_is_factor_i[pe_idx])
        ,.wb_cmd_dof_i(wb_cmd_dof_i[pe_idx])
        ,.wb_cmd_adj_count_i(wb_cmd_adj_count_i[pe_idx])
        ,.wb_cmd_state_words_i(wb_cmd_state_words_i[pe_idx])
        ,.wb_cmd_adj_is_local_i(wb_cmd_adj_is_local_i[pe_idx])
        ,.wb_cmd_adj_neighbor_xs_i(wb_cmd_adj_neighbor_xs_i[pe_idx])
        ,.wb_cmd_adj_neighbor_ys_i(wb_cmd_adj_neighbor_ys_i[pe_idx])
        ,.wb_force_done_valid_i(wb_force_done_valid_i[pe_idx])
        ,.wb_cmd_ready_o(wb_cmd_ready_o[pe_idx])
        ,.wb_done_valid_o(wb_done_valid_o[pe_idx])
        ,.tx_notif_valid_o(tx_notif_valid_o[pe_idx])
        ,.reset_valid_o(reset_valid_o[pe_idx])
        ,.rx_notif_valid_o(rx_notif_valid_o[pe_idx])
        ,.rx_notif_source_node_id_o(rx_notif_source_node_id_o[pe_idx])
        ,.rx_notif_is_factor_o(rx_notif_is_factor_o[pe_idx])
        ,.rx_fetch_req_valid_o(rx_fetch_req_valid_o[pe_idx])
        ,.rx_fetch_resp_valid_o(rx_fetch_resp_valid_o[pe_idx])
        ,.tx_fetch_req_valid_o(tx_fetch_req_valid_o[pe_idx])
        ,.tx_fetch_resp_valid_o(tx_fetch_resp_valid_o[pe_idx])
        ,.wb_inject_fetch_resp_valid_i(wb_inject_fetch_resp_valid_i[pe_idx])
        ,.wb_inject_fetch_resp_data_valid_i(wb_inject_fetch_resp_data_valid_i[pe_idx])
        ,.wb_inject_fetch_resp_data_i(wb_inject_fetch_resp_data_i[pe_idx])
        ,.wb_inject_fetch_resp_last_i(wb_inject_fetch_resp_last_i[pe_idx])
        ,.wb_inject_fetch_resp_done_valid_i(wb_inject_fetch_resp_done_valid_i[pe_idx])
        ,.wb_inject_fetch_resp_txn_id_i(wb_inject_fetch_resp_txn_id_i[pe_idx])
        ,.wb_inject_fetch_resp_node_id_i(wb_inject_fetch_resp_node_id_i[pe_idx])
        ,.wb_inject_fetch_resp_consumer_node_id_i(wb_inject_fetch_resp_consumer_node_id_i[pe_idx])
      );
    end
  end

  // Flatten link_sif outputs
  assign pe0_link_sif_o = link_sif_width_lp'(pe_link_lo[0][0]);
  assign pe1_link_sif_o = link_sif_width_lp'(pe_link_lo[0][1]);
  assign pe2_link_sif_o = link_sif_width_lp'(pe_link_lo[1][0]);
  assign pe3_link_sif_o = link_sif_width_lp'(pe_link_lo[1][1]);

  // Debug: router->PE ready
  assign pe0_router_ready = pe_link_li[0][0].fwd.ready_and_rev;

  // --------------------------------------------------------------------------
  // Router instances (bsg_manycore_mesh_node)
  // --------------------------------------------------------------------------
  // links_sif indices: 0=W, 1=E, 2=N, 3=S
  bsg_manycore_link_sif_s [1:0][1:0][3:0] router_link_li, router_link_lo;

  for (genvar r = 0; r < 2; r++) begin : g_rtr_r
    for (genvar c = 0; c < 2; c++) begin : g_rtr_c
      bsg_manycore_mesh_node #(
        .x_cord_width_p(x_cord_width_p)
        ,.y_cord_width_p(y_cord_width_p)
        ,.data_width_p(data_width_p)
        ,.addr_width_p(addr_width_p)
        ,.dims_p(2)
        ,.stub_p(4'b0000)
        ,.repeater_output_p(4'b0000)
        ,.fwd_use_credits_p({5{1'b0}})
        ,.rev_use_credits_p({5{1'b0}})
        ,.fwd_fifo_els_p('{2,2,2,2,2})
        ,.rev_fifo_els_p('{2,2,2,2,2})
        ,.debug_p(0)
      ) router (
        .clk_i(clk)
        ,.reset_i(reset_i)
        ,.links_sif_i(router_link_li[r][c])
        ,.links_sif_o(router_link_lo[r][c])
        ,.proc_link_sif_i(pe_link_lo[r][c])
        ,.proc_link_sif_o(pe_link_li[r][c])
        ,.global_x_i(x_cord_width_p'(c))
        ,.global_y_i(y_cord_width_p'(r))
      );
    end
  end

  // --------------------------------------------------------------------------
  // Router-to-router connections
  // --------------------------------------------------------------------------
  // Horizontal (East-West) 0=W, 1=E, 2=N, 3=S
  assign router_link_li[0][1][0] = router_link_lo[0][0][1];
  assign router_link_li[0][0][1] = router_link_lo[0][1][0];
  assign router_link_li[1][1][0] = router_link_lo[1][0][1];
  assign router_link_li[1][0][1] = router_link_lo[1][1][0];

  // Vertical (North-South)
  // Per bsg_manycore_pod_mesh_array: pod[y].S → pod[y+1].N
  assign router_link_li[1][0][2] = router_link_lo[0][0][3];  // router[1][0].N = router[0][0].S
  assign router_link_li[0][0][3] = router_link_lo[1][0][2];  // router[0][0].S = router[1][0].N
  assign router_link_li[1][1][2] = router_link_lo[0][1][3];  // router[1][1].N = router[0][1].S
  assign router_link_li[0][1][3] = router_link_lo[1][1][2];  // router[0][1].S = router[1][1].N

  // --------------------------------------------------------------------------
  // Boundary tie-offs
  // --------------------------------------------------------------------------
  bsg_manycore_link_sif_s [1:0] tie_west_li,  tie_west_lo;
  bsg_manycore_link_sif_s [1:0] tie_east_li,  tie_east_lo;
  bsg_manycore_link_sif_s [1:0] tie_north_li, tie_north_lo;
  bsg_manycore_link_sif_s [1:0] tie_south_li, tie_south_lo;

  assign router_link_li[0][0][0] = tie_west_lo[0];
  assign tie_west_li[0] = router_link_lo[0][0][0];
  assign router_link_li[1][0][0] = tie_west_lo[1];
  assign tie_west_li[1] = router_link_lo[1][0][0];

  assign router_link_li[0][1][1] = tie_east_lo[0];
  assign tie_east_li[0] = router_link_lo[0][1][1];
  assign router_link_li[1][1][1] = tie_east_lo[1];
  assign tie_east_li[1] = router_link_lo[1][1][1];

  // North/South are fully connected between rows; no boundary tie-offs needed for row 0/1

  for (genvar i = 0; i < 2; i++) begin : g_tieoff
    bsg_manycore_link_sif_tieoff #(
      .addr_width_p(addr_width_p)
      ,.data_width_p(data_width_p)
      ,.x_cord_width_p(x_cord_width_p)
      ,.y_cord_width_p(y_cord_width_p)
    ) tie_west (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(tie_west_li[i])
      ,.link_sif_o(tie_west_lo[i])
    );
    bsg_manycore_link_sif_tieoff #(
      .addr_width_p(addr_width_p)
      ,.data_width_p(data_width_p)
      ,.x_cord_width_p(x_cord_width_p)
      ,.y_cord_width_p(y_cord_width_p)
    ) tie_east (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(tie_east_li[i])
      ,.link_sif_o(tie_east_lo[i])
    );
    bsg_manycore_link_sif_tieoff #(
      .addr_width_p(addr_width_p)
      ,.data_width_p(data_width_p)
      ,.x_cord_width_p(x_cord_width_p)
      ,.y_cord_width_p(y_cord_width_p)
    ) tie_north (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(tie_north_li[i])
      ,.link_sif_o(tie_north_lo[i])
    );
    bsg_manycore_link_sif_tieoff #(
      .addr_width_p(addr_width_p)
      ,.data_width_p(data_width_p)
      ,.x_cord_width_p(x_cord_width_p)
      ,.y_cord_width_p(y_cord_width_p)
    ) tie_south (
      .clk_i(clk)
      ,.reset_i(reset_i)
      ,.link_sif_i(tie_south_li[i])
      ,.link_sif_o(tie_south_lo[i])
    );
  end

endmodule
