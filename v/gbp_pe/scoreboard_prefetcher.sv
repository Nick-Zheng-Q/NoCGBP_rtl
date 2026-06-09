// scoreboard_prefetcher.sv
// Tracks per-edge fetch state: IDLE → NOTIFIED → IN_FLIGHT → READY

module scoreboard_prefetcher
  import gbp_pkg::*;
#(
    parameter int NODE_ID_W     = gbp_pkg::NODE_ID_W
    , parameter int X_CORD_W    = gbp_pkg::X_CORD_W
    , parameter int Y_CORD_W    = gbp_pkg::Y_CORD_W
    , parameter int ADJ_COUNT_W = gbp_pkg::ADJ_COUNT_W
    , parameter int TXN_ID_W    = gbp_pkg::TXN_ID_W
    , parameter int STATE_WORDS_W = gbp_pkg::STATE_WORDS_W
    , parameter int SCOREBOARD_DEPTH = gbp_pkg::SCOREBOARD_DEPTH
    , parameter int NUM_NODES   = gbp_pkg::NUM_NODES_PER_PE
) (
    input  logic clk_i
    , input  logic rst_n_i

    , input  logic                 rx_notif_valid_i
    , output logic                 rx_notif_ready_o
    , input  logic [NODE_ID_W-1:0] rx_notif_source_node_id_i
    , input  logic                 rx_notif_is_factor_i
    , input  logic [X_CORD_W-1:0]  rx_notif_source_x_i
    , input  logic [Y_CORD_W-1:0]  rx_notif_source_y_i

    , output logic                 fetch_req_valid_o
    , input  logic                 fetch_req_ready_i
    , output logic [NODE_ID_W-1:0] fetch_req_target_node_id_o
    , output logic [NODE_ID_W-1:0] fetch_req_consumer_node_id_o
    , output logic                 fetch_req_is_factor_o
    , output logic [X_CORD_W-1:0]  fetch_req_target_x_o
    , output logic [Y_CORD_W-1:0]  fetch_req_target_y_o
    , output logic [TXN_ID_W-1:0]  fetch_req_txn_id_o

    , input  logic                 complete_valid_i
    , input  logic [TXN_ID_W-1:0]  complete_txn_id_i
    , input  logic [NODE_ID_W-1:0] complete_node_id_i
    , input  logic [NODE_ID_W-1:0] complete_consumer_node_id_i

    , output logic                     staging_reserve_valid_o
    , output logic [STATE_WORDS_W-1:0] staging_reserve_words_o
    , input  logic                     staging_reserve_ready_i
    , input  logic                     staging_batch_closed_i

    , input  logic                 adj_valid_i
    , output logic                 adj_ready_o
    , input  logic [NODE_ID_W-1:0] adj_neighbor_id_i
    , input  logic [X_CORD_W-1:0]  adj_neighbor_x_i
    , input  logic [Y_CORD_W-1:0]  adj_neighbor_y_i
    , input  logic                 adj_is_local_i
    , input  logic                 adj_last_i
    , input  logic [ADJ_COUNT_W-1:0] adj_edge_idx_i
    , input  logic [NODE_ID_W-1:0] adj_current_node_id_i

    , input  logic                 reset_valid_i
    , input  logic [NODE_ID_W-1:0] reset_node_id_i
    , input  logic                 reset_is_factor_i

    , output logic [NUM_NODES-1:0] node_ready_o

    , output logic [$clog2(SCOREBOARD_DEPTH):0] scoreboard_occupancy_o
    , output logic scoreboard_full_o
);

  logic rst_i;
  assign rst_i = ~rst_n_i;

  localparam EDGE_IDLE      = 2'b00;
  localparam EDGE_NOTIFIED  = 2'b01;
  localparam EDGE_IN_FLIGHT = 2'b10;
  localparam EDGE_READY     = 2'b11;

  localparam int PENDING_W = NUM_NODES * ADJ_COUNT_W;  // 256 bits

  // Edge table (packed struct array — this works in Verilator)
  typedef struct packed {
    logic [1:0]            state;
    logic [NODE_ID_W-1:0]  consumer_node;
    logic [NODE_ID_W-1:0]  source_node;
    logic [X_CORD_W-1:0]   source_x;
    logic [Y_CORD_W-1:0]   source_y;
    logic                   is_factor;
  } edge_entry_t;

  edge_entry_t edges [SCOREBOARD_DEPTH];

  // Per-node state: packed bit vectors (NOT unpacked arrays)
  logic [NUM_NODES-1:0] node_has_edge_r;
  logic [PENDING_W-1:0] node_pending_r;

  logic [$clog2(SCOREBOARD_DEPTH):0] sb_count_r;
  logic [TXN_ID_W-1:0] free_ptr_r;
  logic [TXN_ID_W-1:0] scan_ptr_r;

  logic                 fetch_pending_r;
  logic [NODE_ID_W-1:0] fetch_target_r;
  logic [NODE_ID_W-1:0] fetch_consumer_r;
  logic                 fetch_is_factor_r;
  logic [X_CORD_W-1:0]  fetch_target_x_r;
  logic [Y_CORD_W-1:0]  fetch_target_y_r;
  logic [TXN_ID_W-1:0]  fetch_txn_id_r;

  assign rx_notif_ready_o = 1'b1;
  assign adj_ready_o = !staging_batch_closed_i && !scoreboard_full_o;
  assign scoreboard_occupancy_o = sb_count_r;
  assign scoreboard_full_o = (sb_count_r >= SCOREBOARD_DEPTH);
  assign staging_reserve_valid_o = fetch_pending_r && fetch_req_ready_i;
  assign staging_reserve_words_o = STATE_WORDS_W'(8);
  assign fetch_req_valid_o = fetch_pending_r && !staging_batch_closed_i;
  assign fetch_req_target_node_id_o = fetch_target_r;
  assign fetch_req_consumer_node_id_o = fetch_consumer_r;
  assign fetch_req_is_factor_o = fetch_is_factor_r;
  assign fetch_req_target_x_o = fetch_target_x_r;
  assign fetch_req_target_y_o = fetch_target_y_r;
  assign fetch_req_txn_id_o = fetch_txn_id_r;

  // Node readiness: has_edge && pending == 0
  generate
    for (genvar gi = 0; gi < NUM_NODES; gi++) begin : g_nr
      assign node_ready_o[gi] = node_has_edge_r[gi]
              && (node_pending_r[gi*ADJ_COUNT_W +: ADJ_COUNT_W] == '0);
    end
  endgenerate

  // Guard: do not re-register edges for a node that is already ready.
  // This prevents duplicate registrations on the second SCAN.
  logic adj_should_register;
  assign adj_should_register = adj_valid_i && !node_ready_o[adj_current_node_id_i];

  // Helper functions for pending count manipulation
  function automatic [PENDING_W-1:0] pending_incr(
    input [PENDING_W-1:0] cur,
    input [NODE_ID_W-1:0] node_id
  );
    pending_incr = cur;
    pending_incr[node_id*ADJ_COUNT_W +: ADJ_COUNT_W] =
      cur[node_id*ADJ_COUNT_W +: ADJ_COUNT_W] + 1;
  endfunction

  function automatic [PENDING_W-1:0] pending_decr(
    input [PENDING_W-1:0] cur,
    input [NODE_ID_W-1:0] node_id
  );
    pending_decr = cur;
    pending_decr[node_id*ADJ_COUNT_W +: ADJ_COUNT_W] =
      cur[node_id*ADJ_COUNT_W +: ADJ_COUNT_W] - 1;
  endfunction

  function automatic [PENDING_W-1:0] pending_clear_node(
    input [PENDING_W-1:0] cur,
    input [NODE_ID_W-1:0] node_id
  );
    pending_clear_node = cur;
    pending_clear_node[node_id*ADJ_COUNT_W +: ADJ_COUNT_W] = '0;
  endfunction

  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      sb_count_r      <= '0;
      free_ptr_r      <= '0;
      scan_ptr_r      <= '0;
      fetch_pending_r <= 1'b0;
      node_has_edge_r <= '0;
      node_pending_r  <= '0;
      for (int i = 0; i < SCOREBOARD_DEPTH; i++) begin
        edges[i] <= '0;
      end
    end else begin

      // 1. Register new edge (guarded: skip if node already ready)
      if (adj_should_register) begin
        edges[free_ptr_r].source_node   <= adj_neighbor_id_i;
        edges[free_ptr_r].source_x      <= adj_neighbor_x_i;
        edges[free_ptr_r].source_y      <= adj_neighbor_y_i;
        edges[free_ptr_r].consumer_node <= adj_current_node_id_i;
        edges[free_ptr_r].is_factor     <= 1'b0;
        if (adj_is_local_i) begin
          edges[free_ptr_r].state <= EDGE_READY;
        end else begin
          edges[free_ptr_r].state <= EDGE_NOTIFIED;
        end
        free_ptr_r <= free_ptr_r + 1;
      end

      // 2. Notification: only marks edges that were registered before adj_valid.
      //    Most edges are already NOTIFIED by adj_valid; this handles race cases.
      if (rx_notif_valid_i) begin
        for (int i = 0; i < SCOREBOARD_DEPTH; i++) begin
          if (edges[i].consumer_node != NODE_ID_W'(0)
              && edges[i].state == EDGE_IDLE
              && edges[i].source_node == rx_notif_source_node_id_i) begin
            edges[i].state     <= EDGE_NOTIFIED;
            edges[i].is_factor <= rx_notif_is_factor_i;
          end
        end
      end

      // 3. Fetch issue
      if (fetch_pending_r && fetch_req_ready_i && !staging_batch_closed_i) begin
        edges[fetch_txn_id_r].state <= EDGE_IN_FLIGHT;
        fetch_pending_r <= 1'b0;
        sb_count_r <= sb_count_r + 1;
      end

      // 4. Fetch scan
      if (!fetch_pending_r && sb_count_r < SCOREBOARD_DEPTH) begin
        for (int i = 0; i < SCOREBOARD_DEPTH; i++) begin
          logic [TXN_ID_W-1:0] idx;
          idx = scan_ptr_r + TXN_ID_W'(i);
          if (edges[idx].consumer_node != NODE_ID_W'(0)
              && edges[idx].state == EDGE_NOTIFIED
              && !staging_batch_closed_i) begin
            fetch_pending_r   <= 1'b1;
            fetch_target_r    <= edges[idx].source_node;
            fetch_consumer_r  <= edges[idx].consumer_node;
            fetch_is_factor_r <= edges[idx].is_factor;
            fetch_target_x_r  <= edges[idx].source_x;
            fetch_target_y_r  <= edges[idx].source_y;
            fetch_txn_id_r    <= idx;
            scan_ptr_r        <= idx + 1;
            break;
          end
        end
      end

      // 5. Completion
      if (complete_valid_i) begin
        edges[complete_txn_id_i].state <= EDGE_READY;
        if (sb_count_r > 0) sb_count_r <= sb_count_r - 1;
      end

      // 6. Reset
      if (reset_valid_i) begin
        int reset_inflight_count = 0;
        for (int i = 0; i < SCOREBOARD_DEPTH; i++) begin
          if (edges[i].consumer_node == reset_node_id_i) begin
            if (edges[i].state == EDGE_IN_FLIGHT) reset_inflight_count++;
            edges[i] <= '0;
          end
        end
        sb_count_r <= sb_count_r - reset_inflight_count;
      end
    end
  end

  // Per-node tracking: use registered signals for Verilator compatibility
  logic                 adj_v_r;
  logic [NODE_ID_W-1:0] adj_node_r;
  logic                 adj_local_r;
  logic                 comp_v_r;
  logic [NODE_ID_W-1:0] comp_node_r;
  logic                 rst_v_r;
  logic [NODE_ID_W-1:0] rst_node_r;

  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      adj_v_r   <= 1'b0;
      comp_v_r  <= 1'b0;
      rst_v_r   <= 1'b0;
      node_has_edge_r <= '0;
      node_pending_r  <= '0;
    end else begin
      adj_v_r   <= adj_should_register;
      adj_node_r  <= adj_current_node_id_i;
      adj_local_r <= adj_is_local_i;
      comp_v_r  <= complete_valid_i;
      comp_node_r <= complete_consumer_node_id_i;
      rst_v_r   <= reset_valid_i;
      rst_node_r  <= reset_node_id_i;

      if (adj_v_r) begin
        node_has_edge_r <= node_has_edge_r | (NUM_NODES'(1) << adj_node_r);
        if (!adj_local_r) begin
          node_pending_r <= pending_incr(node_pending_r, adj_node_r);
        end
      end
      if (comp_v_r) begin
        node_pending_r <= pending_decr(node_pending_r, comp_node_r);
      end
      if (rst_v_r) begin
        node_pending_r <= pending_clear_node(node_pending_r, rst_node_r);
        node_has_edge_r <= node_has_edge_r & ~(NUM_NODES'(1) << rst_node_r);
      end
    end
  end

endmodule
