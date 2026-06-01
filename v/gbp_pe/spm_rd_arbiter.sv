import gbp_pkg::*;
module spm_rd_arbiter
(
    input logic clk_i,
    input logic reset_i,

    input logic [1:0] req_i,
    input logic [1:0][ROW_ADDR_W-1:0] row_i,

    output logic [1:0] ready_o,

    output logic bank_rd_en_o,
    output logic [ROW_ADDR_W-1:0] bank_rd_row_o,
    input logic [BEAT_BITS-1:0] bank_rd_data_i,

    output logic [1:0] rsp_valid_o,
    output logic [1:0][BEAT_BITS-1:0] rsp_data_o
);

  logic rr_sel_r;
  logic grant_src;
  logic grant_src_r;
  logic rd_pending_r;

  always_comb begin
    if (req_i[0] & ~req_i[1]) begin
      grant_src = 1'b0;
    end else if (~req_i[0] & req_i[1]) begin
      grant_src = 1'b1;
    end else if (req_i[0] & req_i[1]) begin
      grant_src = rr_sel_r;
    end else begin
      grant_src = 1'b0;
    end
  end

  assign bank_rd_en_o = req_i[0] | req_i[1];
  assign bank_rd_row_o = grant_src ? row_i[1] : row_i[0];

  assign ready_o[0] = bank_rd_en_o & ~grant_src;
  assign ready_o[1] = bank_rd_en_o & grant_src;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      rr_sel_r <= 1'b0;
      grant_src_r <= 1'b0;
      rd_pending_r <= 1'b0;
      rsp_valid_o <= '0;
      rsp_data_o <= '{default: '0};
    end else begin
      if (req_i[0] & req_i[1]) begin
        rr_sel_r <= ~rr_sel_r;
      end

      grant_src_r <= grant_src;
      rd_pending_r <= bank_rd_en_o;

      rsp_valid_o <= '0;
      if (rd_pending_r) begin
        rsp_valid_o[grant_src_r] <= 1'b1;
        rsp_data_o[grant_src_r] <= bank_rd_data_i;
      end
    end
  end

endmodule
