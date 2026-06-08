// pull_client.sv
// Sends FETCH_REQUEST to producer PE via NoC Adapter.
// Sends 3 stores: {is_factor, consumer_node_id}, {target_node_id}, {txn_id}

module pull_client
  import gbp_pkg::*;
#(
    parameter int NODE_ID_W = gbp_pkg::NODE_ID_W
    , parameter int X_CORD_W = gbp_pkg::X_CORD_W
    , parameter int Y_CORD_W = gbp_pkg::Y_CORD_W
    , parameter int TXN_ID_W = gbp_pkg::TXN_ID_W
) (
    input  logic clk_i
    , input  logic rst_n_i

    // Request from ScoreboardPrefetcher
    , input  logic                 req_valid_i
    , output logic                 req_ready_o
    , input  logic [NODE_ID_W-1:0] req_target_node_id_i
    , input  logic [NODE_ID_W-1:0] req_consumer_node_id_i
    , input  logic                 req_is_factor_i
    , input  logic [X_CORD_W-1:0]  req_target_x_i
    , input  logic [Y_CORD_W-1:0]  req_target_y_i
    , input  logic [TXN_ID_W-1:0]  req_txn_id_i

    // To NoC Adapter (FETCH_REQUEST TX)
    , output logic                 tx_fetch_req_valid_o
    , input  logic                 tx_fetch_req_ready_i
    , output logic [NODE_ID_W-1:0] tx_fetch_req_target_node_id_o
    , output logic [NODE_ID_W-1:0] tx_fetch_req_consumer_node_id_o
    , output logic                 tx_fetch_req_is_factor_o
    , output logic [X_CORD_W-1:0]  tx_fetch_req_target_x_o
    , output logic [Y_CORD_W-1:0]  tx_fetch_req_target_y_o
    , output logic [TXN_ID_W-1:0]  tx_fetch_req_txn_id_o
    , output logic [1:0]           tx_fetch_req_store_idx_o  // 0,1,2 = which store
);

  // FSM states
  localparam S_IDLE = 2'd0;
  localparam S_S0   = 2'd1;  // send store 0
  localparam S_S1   = 2'd2;  // send store 1
  localparam S_S2   = 2'd3;  // send store 2

  logic rst_i;
  assign rst_i = ~rst_n_i;

  logic [1:0] state_r;

  // Latched request
  logic [NODE_ID_W-1:0] target_node_id_r;
  logic [NODE_ID_W-1:0] consumer_node_id_r;
  logic                  is_factor_r;
  logic [X_CORD_W-1:0]  target_x_r;
  logic [Y_CORD_W-1:0]  target_y_r;
  logic [TXN_ID_W-1:0]  txn_id_r;

  // Output assignments
  assign req_ready_o = (state_r == S_IDLE);

  assign tx_fetch_req_valid_o = (state_r != S_IDLE);
  assign tx_fetch_req_target_node_id_o = target_node_id_r;
  assign tx_fetch_req_consumer_node_id_o = consumer_node_id_r;
  assign tx_fetch_req_is_factor_o = is_factor_r;
  assign tx_fetch_req_target_x_o = target_x_r;
  assign tx_fetch_req_target_y_o = target_y_r;
  assign tx_fetch_req_txn_id_o = txn_id_r;
  assign tx_fetch_req_store_idx_o = state_r - 2'd1;  // 0,1,2

  // FSM
  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      state_r <= S_IDLE;
      target_node_id_r <= '0;
      consumer_node_id_r <= '0;
      is_factor_r <= 1'b0;
      target_x_r <= '0;
      target_y_r <= '0;
      txn_id_r <= '0;
    end else begin
      case (state_r)
        S_IDLE: begin
          if (req_valid_i) begin
            target_node_id_r <= req_target_node_id_i;
            consumer_node_id_r <= req_consumer_node_id_i;
            is_factor_r <= req_is_factor_i;
            target_x_r <= req_target_x_i;
            target_y_r <= req_target_y_i;
            txn_id_r <= req_txn_id_i;
            state_r <= S_S0;
          end
        end

        S_S0: begin
          if (tx_fetch_req_ready_i) begin
            state_r <= S_S1;
          end
        end

        S_S1: begin
          if (tx_fetch_req_ready_i) begin
            state_r <= S_S2;
          end
        end

        S_S2: begin
          if (tx_fetch_req_ready_i) begin
            state_r <= S_IDLE;
          end
        end

        default: state_r <= S_IDLE;
      endcase
    end
  end

endmodule
