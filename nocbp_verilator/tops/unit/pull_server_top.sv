// pull_server_top.sv
// Unit test wrapper for pull_server

module pull_server_top (
    input  logic        clk
    , input  logic        rst_n

    // Request
    , input  logic        req_valid_i
    , input  logic [9:0] req_target_node_id_i
    , input  logic [9:0] req_consumer_node_id_i
    , input  logic        req_is_factor_i
    , input  logic [5:0]  req_fetch_src_x_i
    , input  logic [4:0]  req_fetch_src_y_i
    , input  logic [5:0]  req_txn_id_i
    , output logic        req_ready_o

    // TX response
    , output logic        tx_valid_o
    , input  logic        tx_ready_i
    , output logic [9:0] tx_node_id_o
    , output logic [9:0] tx_consumer_node_id_o
    , output logic        tx_is_factor_o
    , output logic [3:0]  tx_state_words_o
    , output logic [31:0] tx_data_o
    , output logic        tx_data_valid_o
    , output logic        tx_last_o
    , output logic [5:0]  tx_txn_id_o
);

  // SPM read port
  logic         spm_rd_valid;
  logic [17:0]  spm_rd_addr;
  logic         spm_rd_ready;
  logic [63:0] spm_rd_data;

  // Combinational SPM with multiple test nodes
  assign spm_rd_ready = spm_rd_valid;
  always_comb begin
    spm_rd_data = '0;
    case (spm_rd_addr)
      // Node 0x10 header: 2 state words at state_base=0x100
      18'h010: begin
        spm_rd_data[9:0]    = 10'h010;
        spm_rd_data[13:10]  = 4'd3;
        spm_rd_data[17:14]  = 4'd2;
        spm_rd_data[35:18]  = 18'd4;
        spm_rd_data[53:36]  = 18'h100;
        spm_rd_data[59:54]  = 6'd2;
      end
      // Node 0x11 header: 0 state words
      18'h011: begin
        spm_rd_data[9:0]    = 10'h011;
        spm_rd_data[13:10]  = 4'd3;
        spm_rd_data[17:14]  = 4'd2;
        spm_rd_data[35:18]  = 18'd4;
        spm_rd_data[53:36]  = 18'h000;
        spm_rd_data[59:54]  = 6'd0;
      end
      // Node 0x12 header: 4 state words at state_base=0x200
      18'h012: begin
        spm_rd_data[9:0]    = 10'h012;
        spm_rd_data[13:10]  = 4'd3;
        spm_rd_data[17:14]  = 4'd2;
        spm_rd_data[35:18]  = 18'd4;
        spm_rd_data[53:36]  = 18'h200;
        spm_rd_data[59:54]  = 6'd4;
      end
      // State words for node 0x10
      18'h100: spm_rd_data[31:0] = 32'hDEAD0000;
      18'h101: spm_rd_data[31:0] = 32'hBEEF0001;
      // State words for node 0x12
      18'h200: spm_rd_data[31:0] = 32'hA0000000;
      18'h201: spm_rd_data[31:0] = 32'hA0000001;
      18'h202: spm_rd_data[31:0] = 32'hA0000002;
      18'h203: spm_rd_data[31:0] = 32'hA0000003;
      default: spm_rd_data = '0;
    endcase
  end

  pull_server #(
    .NODE_ID_W(10)
    ,.STATE_WORDS_W(6)
    ,.SPM_ADDR_W(18)
    ,.BEAT_BITS(gbp_pkg::BEAT_BITS)
    ,.X_CORD_W(6)
    ,.Y_CORD_W(5)
    ,.TXN_ID_W(6)
    ,.DATA_WIDTH(32)
  ) dut (
    .clk_i(clk)
    ,.rst_i(~rst_n)
    ,.req_valid_i(req_valid_i)
    ,.req_ready_o(req_ready_o)
    ,.req_target_node_id_i(req_target_node_id_i)
    ,.req_consumer_node_id_i(req_consumer_node_id_i)
    ,.req_is_factor_i(req_is_factor_i)
    ,.req_fetch_src_x_i(req_fetch_src_x_i)
    ,.req_fetch_src_y_i(req_fetch_src_y_i)
    ,.req_txn_id_i(req_txn_id_i)
    ,.spm_rd_valid_o(spm_rd_valid)
    ,.spm_rd_addr_o(spm_rd_addr)
    ,.spm_rd_ready_i(spm_rd_ready)
    ,.spm_rd_data_i(spm_rd_data)
    ,.tx_valid_o(tx_valid_o)
    ,.tx_ready_i(tx_ready_i)
    ,.tx_node_id_o(tx_node_id_o)
    ,.tx_consumer_node_id_o(tx_consumer_node_id_o)
    ,.tx_is_factor_o(tx_is_factor_o)
    ,.tx_state_words_o(tx_state_words_o)
    ,.tx_data_o(tx_data_o)
    ,.tx_data_valid_o(tx_data_valid_o)
    ,.tx_last_o(tx_last_o)
    ,.tx_txn_id_o(tx_txn_id_o)
  );

endmodule
