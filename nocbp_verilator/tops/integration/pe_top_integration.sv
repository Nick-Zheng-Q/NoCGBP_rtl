module pe_top_integration (
  input logic clk,
  input logic rst_n,
  input logic cmd_valid,
  input logic [1:0] cmd_kind,
  input logic [7:0] cmd_txn,
  output logic cmd_ready,
  output logic cmd_rsp_done,
  output logic cmd_rsp_error,
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
    .cmd_valid_i(cmd_valid),
    .cmd_kind_i(cmd_kind),
    .cmd_txn_id_i(cmd_txn),
    .force_persistence_stall_i(1'b0),
    .cmd_ready_o(cmd_ready),
    .rsp_done_o(cmd_rsp_done),
    .rsp_error_o(cmd_rsp_error),
    .ingress_wr_valid_i(1'b0),
    .ingress_wr_addr_i('0),
    .ingress_wr_data_i('0),
    .ingress_wr_ready_o(),
    .rd_req_valid_o(rd_req_valid),
    .rd_req_addr_o(rd_req_addr),
    .wr_req_valid_o(wr_req_valid),
    .wr_req_addr_o(wr_req_addr),
    .wr_req_data_low_o(wr_req_data),
    .ingress_wr_req_valid_o(),
    .ingress_wr_req_addr_o(),
    .ingress_wr_req_data_low_o(),
    .compute_start_o(compute_start),
    .compute_done_o(compute_done),
    .wr_txn_id_o(wr_txn_id),
    .cmd_txn_id_o(cmd_txn_id)
  );

endmodule
