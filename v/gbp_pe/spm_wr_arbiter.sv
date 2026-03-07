module spm_wr_arbiter
  import gbp_pkg::*;
(
    input logic clk_i,
    input logic reset_i,

    input logic [1:0] req_i,
    input logic [1:0][ROW_ADDR_W-1:0] row_i,
    input logic [1:0][BEAT_BITS-1:0] data_i,
    input logic [1:0][WSTRB_W-1:0] wstrb_i,

    output logic [1:0] ready_o,

    output logic bank_wr_en_o,
    output logic [ROW_ADDR_W-1:0] bank_wr_row_o,
    output logic [BEAT_BITS-1:0] bank_wr_data_o,
    output logic [WSTRB_W-1:0] bank_wr_wstrb_o
);

  logic rr_sel_r;
  logic grant_src;

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

  assign bank_wr_en_o = req_i[0] | req_i[1];
  assign bank_wr_row_o = grant_src ? row_i[1] : row_i[0];
  assign bank_wr_data_o = grant_src ? data_i[1] : data_i[0];
  assign bank_wr_wstrb_o = grant_src ? wstrb_i[1] : wstrb_i[0];

  assign ready_o[0] = bank_wr_en_o & ~grant_src;
  assign ready_o[1] = bank_wr_en_o & grant_src;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      rr_sel_r <= 1'b0;
    end else if (req_i[0] & req_i[1]) begin
      rr_sel_r <= ~rr_sel_r;
    end
  end

endmodule
