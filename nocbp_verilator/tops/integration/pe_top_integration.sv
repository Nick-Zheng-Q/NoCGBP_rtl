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

  // pe_top integration test uses whitebox bypass mode to directly verify
  // the compute+memory datapath (compute_unit + stream engines + spm_arbiter + banks).
  // The control pipeline (phase_controller/node_scheduler/metadata_scanner) is verified
  // separately in control_subsystem tests.
  pe_top dut (
    .clk_i(clk),
    .reset_i(reset_i),
    .wb_bypass_control_i(1'b1),
    .wb_cmd_valid_i(cmd_valid),
    .wb_cmd_node_id_i(cmd_txn[3:0]),
    .wb_cmd_is_factor_i(cmd_kind[0]),
    .wb_cmd_dof_i(4'd1),
    .wb_cmd_adj_count_i(4'd0),
    .wb_cmd_state_words_i(6'd2),
    .wb_cmd_ready_o(cmd_ready),
    .rsp_done_o(cmd_rsp_done),
    .rsp_error_o(cmd_rsp_error),
    .compute_start_o(compute_start),
    .compute_done_o(compute_done),
    .tx_notif_valid_o(),
    .tx_notif_ready_i(1'b0),
    .tx_notif_source_node_id_o(),
    .tx_notif_is_factor_o(),
    .tx_notif_target_x_o(),
    .tx_notif_target_y_o(),
    .rx_notif_valid_i(1'b0),
    .rx_notif_ready_o(),
    .rx_notif_source_node_id_i('0),
    .rx_notif_is_factor_i(1'b0),
    .rx_notif_source_x_i('0),
    .rx_notif_source_y_i('0)
  );

  // Legacy probe signals — new architecture does not expose per-request SPM
  // handshakes at pe_top boundary.  Tie them off to keep the testbench port
  // list compatible; the test now monitors compute_start / compute_done.
  assign rd_req_valid = 1'b0;
  assign rd_req_addr  = '0;
  assign wr_req_valid = 1'b0;
  assign wr_req_addr  = '0;
  assign wr_req_data  = '0;
  assign wr_txn_id    = cmd_txn;
  assign cmd_txn_id   = cmd_txn;

endmodule
