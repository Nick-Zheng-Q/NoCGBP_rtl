// pull_client_top.sv
// Unit test wrapper for pull_client

module pull_client_top (
    input  logic        clk
    , input  logic        rst_n

    // Request
    , input  logic        req_valid_i
    , input  logic [12:0] req_target_node_id_i
    , input  logic [12:0] req_consumer_node_id_i
    , input  logic        req_is_factor_i
    , input  logic [2:0]  req_target_x_i
    , input  logic [2:0]  req_target_y_i
    , input  logic [5:0]  req_txn_id_i
    , output logic        req_ready_o

    // TX
    , output logic        tx_fetch_req_valid_o
    , input  logic        tx_fetch_req_ready_i
    , output logic [12:0] tx_fetch_req_target_node_id_o
    , output logic [12:0] tx_fetch_req_consumer_node_id_o
    , output logic        tx_fetch_req_is_factor_o
    , output logic [2:0]  tx_fetch_req_target_x_o
    , output logic [2:0]  tx_fetch_req_target_y_o
    , output logic [5:0]  tx_fetch_req_txn_id_o
    , output logic [1:0]  tx_fetch_req_store_idx_o
);

  pull_client #(
    .NODE_ID_W(10)
    ,.X_CORD_W(6)
    ,.Y_CORD_W(5)
    ,.TXN_ID_W(6)
  ) dut (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.req_valid_i(req_valid_i)
    ,.req_ready_o(req_ready_o)
    ,.req_target_node_id_i(req_target_node_id_i)
    ,.req_consumer_node_id_i(req_consumer_node_id_i)
    ,.req_is_factor_i(req_is_factor_i)
    ,.req_target_x_i(req_target_x_i)
    ,.req_target_y_i(req_target_y_i)
    ,.req_txn_id_i(req_txn_id_i)
    ,.tx_fetch_req_valid_o(tx_fetch_req_valid_o)
    ,.tx_fetch_req_ready_i(tx_fetch_req_ready_i)
    ,.tx_fetch_req_target_node_id_o(tx_fetch_req_target_node_id_o)
    ,.tx_fetch_req_consumer_node_id_o(tx_fetch_req_consumer_node_id_o)
    ,.tx_fetch_req_is_factor_o(tx_fetch_req_is_factor_o)
    ,.tx_fetch_req_target_x_o(tx_fetch_req_target_x_o)
    ,.tx_fetch_req_target_y_o(tx_fetch_req_target_y_o)
    ,.tx_fetch_req_txn_id_o(tx_fetch_req_txn_id_o)
    ,.tx_fetch_req_store_idx_o(tx_fetch_req_store_idx_o)
  );

endmodule
