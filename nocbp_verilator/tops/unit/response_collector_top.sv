// response_collector_top.sv
// Unit test wrapper for response_collector

module response_collector_top (
    input  logic        clk
    , input  logic        rst_n

    // RX response
    , input  logic        rx_valid_i
    , output logic        rx_ready_o
    , input  logic        rx_is_factor_i
    , input  logic [3:0]  rx_state_words_i
    , input  logic [31:0] rx_data_i
    , input  logic        rx_data_valid_i
    , input  logic        rx_last_i
    , input  logic        rx_done_valid_i
    , input  logic [5:0]  rx_txn_id_i
    , input  logic [12:0] rx_node_id_i
    , input  logic [12:0] rx_consumer_node_id_i

    // STAGING write
    , output logic        staging_wr_valid_o
    , input  logic        staging_wr_ready_i
    , output logic [15:0] staging_wr_addr_o
    , output logic [31:0] staging_wr_data_o

    // Remote stream
    , output logic        remote_valid_o
    , input  logic        remote_ready_i
    , output logic [31:0] remote_data_o
    , output logic        remote_last_o

    // STAGING reservation
    , input  logic        staging_reserve_valid_i
    , input  logic [3:0]  staging_reserve_words_i
    , output logic        staging_reserve_ready_o

    // Batch control
    , output logic        staging_batch_closed_o
    , input  logic        staging_batch_done_i

    // Completion
    , output logic        complete_valid_o
    , output logic [5:0]  complete_txn_id_o
    , output logic [12:0] complete_node_id_o
    , output logic [12:0] complete_consumer_node_id_o
);

  response_collector #(
    .NODE_ID_W(10)
    ,.STATE_WORDS_W(6)
    ,.TXN_ID_W(6)
    ,.DATA_WIDTH(32)
    ,.SPM_ADDR_W(18)
  ) dut (
    .clk_i(clk)
    ,.rst_i(~rst_n)
    ,.rx_valid_i(rx_valid_i)
    ,.rx_ready_o(rx_ready_o)
    ,.rx_is_factor_i(rx_is_factor_i)
    ,.rx_state_words_i(rx_state_words_i)
    ,.rx_data_i(rx_data_i)
    ,.rx_data_valid_i(rx_data_valid_i)
    ,.rx_last_i(rx_last_i)
    ,.rx_done_valid_i(rx_done_valid_i)
    ,.rx_txn_id_i(rx_txn_id_i)
    ,.rx_node_id_i(rx_node_id_i)
    ,.rx_consumer_node_id_i(rx_consumer_node_id_i)
    ,.staging_wr_valid_o(staging_wr_valid_o)
    ,.staging_wr_ready_i(staging_wr_ready_i)
    ,.staging_wr_addr_o(staging_wr_addr_o)
    ,.staging_wr_data_o(staging_wr_data_o)
    ,.remote_valid_o(remote_valid_o)
    ,.remote_ready_i(remote_ready_i)
    ,.remote_data_o(remote_data_o)
    ,.remote_last_o(remote_last_o)
    ,.staging_reserve_valid_i(staging_reserve_valid_i)
    ,.staging_reserve_words_i(staging_reserve_words_i)
    ,.staging_reserve_ready_o(staging_reserve_ready_o)
    ,.staging_batch_closed_o(staging_batch_closed_o)
    ,.staging_batch_done_i(staging_batch_done_i)
    ,.complete_valid_o(complete_valid_o)
    ,.complete_txn_id_o(complete_txn_id_o)
    ,.complete_node_id_o(complete_node_id_o)
    ,.complete_consumer_node_id_o(complete_consumer_node_id_o)
  );

endmodule
