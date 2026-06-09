// writeback_controller.sv
// After compute, sends NOTIFICATION to all remote consuming neighbors.
// Triggers scoreboard reset for the completed node.

module writeback_controller
  import gbp_pkg::*;
#(
    parameter int NODE_ID_W   = gbp_pkg::NODE_ID_W
    , parameter int ADJ_COUNT_W = gbp_pkg::ADJ_COUNT_W
    , parameter int MAX_ADJ_COUNT = gbp_pkg::MAX_ADJ_COUNT
    , parameter int X_CORD_W  = gbp_pkg::X_CORD_W
    , parameter int Y_CORD_W  = gbp_pkg::Y_CORD_W
) (
    input  logic clk_i
    , input  logic rst_n_i

    // Compute completion (from Compute Unit)
    , input  logic                 done_valid_i
    , input  logic [NODE_ID_W-1:0] done_node_id_i
    , input  logic                 done_is_factor_i

    // Adjacency info (from Metadata Scanner, latched at compute start)
    , input  logic [ADJ_COUNT_W-1:0] adj_count_i
    , input  logic [MAX_ADJ_COUNT-1:0][NODE_ID_W-1:0] adj_neighbor_ids_i
    , input  logic [MAX_ADJ_COUNT-1:0][X_CORD_W-1:0]  adj_neighbor_xs_i
    , input  logic [MAX_ADJ_COUNT-1:0][Y_CORD_W-1:0]  adj_neighbor_ys_i
    , input  logic [MAX_ADJ_COUNT-1:0]                 adj_is_local_i

    // To NoC Adapter (NOTIFICATION TX)
    , output logic                 tx_notif_valid_o
    , input  logic                 tx_notif_ready_i
    , output logic [NODE_ID_W-1:0] tx_notif_source_node_id_o
    , output logic [NODE_ID_W-1:0] tx_notif_target_node_id_o
    , output logic                 tx_notif_is_factor_o
    , output logic [X_CORD_W-1:0]  tx_notif_target_x_o
    , output logic [Y_CORD_W-1:0]  tx_notif_target_y_o

    // Scoreboard reset trigger (to ScoreboardPrefetcher)
    , output logic                 reset_valid_o
    , output logic [NODE_ID_W-1:0] reset_node_id_o
    , output logic                 reset_is_factor_o

    // Done signal (to Phase Controller)
    , output logic                 wb_done_o
);

  // FSM states
  localparam S_IDLE = 2'd0;
  localparam S_SEND = 2'd1;  // sending notifications
  localparam S_DONE = 2'd2;

  logic rst_i;
  assign rst_i = ~rst_n_i;

  logic [1:0] state_r;
  logic [1:0] state_next;

  // Latched node info
  logic [NODE_ID_W-1:0] node_id_r;
  logic                  is_factor_r;

  // Scan counter
  logic [ADJ_COUNT_W-1:0] adj_idx_r;
  logic [ADJ_COUNT_W-1:0] adj_idx_next;

  // Output assignments
  assign tx_notif_valid_o = (state_r == S_SEND);
  assign tx_notif_source_node_id_o = node_id_r;
  assign tx_notif_target_node_id_o = adj_neighbor_ids_i[adj_idx_r];
  assign tx_notif_is_factor_o = is_factor_r;
  assign tx_notif_target_x_o = adj_neighbor_xs_i[adj_idx_r];
  assign tx_notif_target_y_o = adj_neighbor_ys_i[adj_idx_r];

  assign reset_valid_o = done_valid_i && (state_r == S_IDLE);
  assign reset_node_id_o = done_node_id_i;
  assign reset_is_factor_o = done_is_factor_i;

  assign wb_done_o = (state_r == S_DONE);

  // Sequential: adj_idx_r
  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      adj_idx_r <= '0;
    end else begin
      adj_idx_r <= adj_idx_next;
    end
  end

  // Combinational: adj_idx_next
  always_comb begin
    adj_idx_next = adj_idx_r;
    case (state_r)
      S_IDLE: begin
        if (done_valid_i) begin
          if (adj_count_i == 0) begin
            adj_idx_next = '0;
          end else if (adj_is_local_i[0]) begin
            if (adj_count_i == 1) begin
              adj_idx_next = '0;
            end else begin
              adj_idx_next = 1;
            end
          end else begin
            adj_idx_next = '0;
          end
        end
      end
      S_SEND: begin
        if (adj_is_local_i[adj_idx_r]) begin
          if (adj_idx_r != adj_count_i - 1) begin
            adj_idx_next = adj_idx_r + 1;
          end
        end else begin
          if (tx_notif_ready_i) begin
            if (adj_idx_r != adj_count_i - 1) begin
              adj_idx_next = adj_idx_r + 1;
            end
          end
        end
      end
      default: adj_idx_next = adj_idx_r;
    endcase
  end

  // Sequential: state_r
  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      state_r <= S_IDLE;
      node_id_r <= '0;
      is_factor_r <= 1'b0;
    end else begin
      state_r <= state_next;
      if (done_valid_i && (state_r == S_IDLE)) begin
        node_id_r <= done_node_id_i;
        is_factor_r <= done_is_factor_i;
      end
    end
  end

  // Combinational: state_next
  always_comb begin
    state_next = state_r;
    case (state_r)
      S_IDLE: begin
        if (done_valid_i) begin
          if (adj_count_i == 0) begin
            state_next = S_DONE;
          end else if (adj_is_local_i[0]) begin
            if (adj_count_i == 1) begin
              state_next = S_DONE;
            end else begin
              state_next = S_SEND;
            end
          end else begin
            state_next = S_SEND;
          end
        end
      end
      S_SEND: begin
        if (adj_is_local_i[adj_idx_r]) begin
          if (adj_idx_r == adj_count_i - 1) begin
            state_next = S_DONE;
          end else begin
            state_next = S_SEND;
          end
        end else begin
          if (tx_notif_ready_i) begin
            if (adj_idx_r == adj_count_i - 1) begin
              state_next = S_DONE;
            end else begin
              state_next = S_SEND;
            end
          end else begin
            state_next = S_SEND;
          end
        end
      end
      S_DONE: begin
        state_next = S_IDLE;
      end
      default: state_next = S_IDLE;
    endcase
  end

endmodule
