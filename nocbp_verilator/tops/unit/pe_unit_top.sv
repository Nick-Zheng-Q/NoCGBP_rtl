module pe_unit
  import gbp_pkg::*;
(
    input logic clk,
    input logic rst_n,
    input logic cmd_valid_i,
    input logic rsp_done_i,
    input logic [TXN_ID_W-1:0] cmd_txn_id_i,

    // Output ports (tie off for minimal harness)
    output logic cmd_ready_o,
    output logic rd_req_valid_o,
    output logic [SPM_ADDR_W-1:0] rd_req_addr_o,
    output logic wr_req_valid_o,
    output logic [SPM_ADDR_W-1:0] wr_req_addr_o,
    output logic [31:0] wr_req_data_low_o,
    output logic compute_start_o,
    output logic compute_done_o,
    output logic [TXN_ID_W-1:0] wr_txn_id_o,
    output logic [TXN_ID_W-1:0] cmd_txn_id_o
);

  logic pending_r;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      pending_r <= 1'b0;
      compute_start_o <= 1'b0;
      compute_done_o <= 1'b0;
      wr_req_valid_o <= 1'b0;
      cmd_txn_id_o <= '0;
      wr_txn_id_o <= '0;
    end else begin
      compute_start_o <= 1'b0;
      compute_done_o <= 1'b0;
      wr_req_valid_o <= 1'b0;

      if (cmd_valid_i && !pending_r) begin
        pending_r <= 1'b1;
        compute_start_o <= 1'b1;
        cmd_txn_id_o <= cmd_txn_id_i;
      end

      if (pending_r && rsp_done_i) begin
        pending_r <= 1'b0;
        compute_done_o <= 1'b1;
        wr_req_valid_o <= 1'b1;
        wr_txn_id_o <= cmd_txn_id_o;
      end
    end
  end

  assign cmd_ready_o = ~pending_r;

  assign rd_req_valid_o = 1'b0;
  assign rd_req_addr_o = '0;
  assign wr_req_addr_o = '0;
  assign wr_req_data_low_o = '0;

endmodule
