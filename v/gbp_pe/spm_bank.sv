module spm_bank
  import gbp_pkg::*;
(
    input logic clk_i,
    input logic reset_i,

    input logic                  bank_rd_en,
    input logic [ROW_ADDR_W-1:0] bank_rd_addr,
    output logic [BEAT_BITS-1:0] bank_rd_data,

    input logic                  bank_wr_en,
    input logic [ROW_ADDR_W-1:0] bank_wr_addr,
    input logic [BEAT_BITS-1:0]  bank_wr_data,
    input logic [WSTRB_W-1:0]    bank_wr_wstrb
);

  logic [BEAT_BITS-1:0] mem_r [(1<<ROW_ADDR_W)-1:0];
  integer i;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      bank_rd_data <= '0;
    end else begin
      if (bank_rd_en) begin
        bank_rd_data <= mem_r[bank_rd_addr];
      end

      if (bank_wr_en) begin
        for (i = 0; i < WSTRB_W; i = i + 1) begin
          if (bank_wr_wstrb[i]) begin
            mem_r[bank_wr_addr][8*i +: 8] <= bank_wr_data[8*i +: 8];
          end
        end
      end
    end
  end

endmodule
