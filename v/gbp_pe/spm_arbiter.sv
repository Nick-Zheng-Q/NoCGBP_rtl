import gbp_pkg::*;
module spm_arbiter
(
    input logic clk_i,
    input logic reset_i,

    mic_spm_arbiter_if.slave rd_if,
    mic_spm_arbiter_wr_if.slave wr_if,
    spm_bank_if.master bank_if
);

  localparam int unsigned ROW_BYTES_LG = BYTE_OFF_W + WORD_OFF_W;
  logic rd_pending_r;

  logic rd_req_bytes_unused;

  assign rd_req_bytes_unused = rd_if.spm_rd_req_bytes;

  assign wr_if.spm_wr_req_ready = 1'b1;

  assign bank_if.bank_rd_en = rd_if.spm_rd_req_valid;
  assign bank_if.bank_rd_bank = rd_if.spm_rd_req_addr[ROW_BYTES_LG +: BANK_ID_W];
  assign bank_if.bank_rd_addr = rd_if.spm_rd_req_addr[(ROW_BYTES_LG + BANK_ID_W) +: ROW_ADDR_W];

  assign bank_if.bank_wr_en = wr_if.spm_wr_req_valid;
  assign bank_if.bank_wr_bank = wr_if.spm_wr_req_addr[ROW_BYTES_LG +: BANK_ID_W];
  assign bank_if.bank_wr_addr = wr_if.spm_wr_req_addr[(ROW_BYTES_LG + BANK_ID_W) +: ROW_ADDR_W];
  assign bank_if.bank_wr_data = wr_if.spm_wr_req_data;
  assign bank_if.bank_wr_wstrb = wr_if.spm_wr_req_wstrb;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      rd_pending_r <= 1'b0;
      rd_if.spm_rd_rsp_valid <= 1'b0;
      rd_if.spm_rd_rsp_data <= '0;
    end else begin
      rd_pending_r <= rd_if.spm_rd_req_valid;
      rd_if.spm_rd_rsp_valid <= rd_pending_r;
      if (rd_pending_r) begin
        rd_if.spm_rd_rsp_data <= bank_if.bank_rd_data;
      end
    end
  end

endmodule
