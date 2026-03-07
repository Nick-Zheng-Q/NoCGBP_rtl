module pe_top_integration (
  input logic clk,
  input logic rst_n,
  output logic rd_req_valid,
  output logic [19:0] rd_req_addr,
  output logic wr_req_valid,
  output logic [19:0] wr_req_addr,
  output logic [31:0] wr_req_data,
  output logic compute_start,
  output logic compute_done,
  output logic [7:0] wr_txn_id,
  output logic [7:0] cmd_txn_id
);

  logic reset_i;
  assign reset_i = ~rst_n;

  pe_top dut (
    .clk_i(clk),
    .reset_i(reset_i),
    .rd_req_valid_o(rd_req_valid),
    .rd_req_addr_o(rd_req_addr),
    .wr_req_valid_o(wr_req_valid),
    .wr_req_addr_o(wr_req_addr),
    .wr_req_data_low_o(wr_req_data),
    .compute_start_o(compute_start),
    .compute_done_o(compute_done),
    .wr_txn_id_o(wr_txn_id),
    .cmd_txn_id_o(cmd_txn_id)
  );

endmodule
