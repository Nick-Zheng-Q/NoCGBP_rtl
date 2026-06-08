import "DPI-C" context function int pmem_read(input int raddr);
import "DPI-C" context function void pmem_write(
  input int  waddr,
  input int  wdata,
  input byte byte_num
);

module spm_bank
  import gbp_pkg::*;
#(
    parameter int BANK_ID = 0
)
(
    input logic clk_i,
    input logic reset_i,

    input  logic                  bank_rd_en,
    input  logic [ROW_ADDR_W-1:0] bank_rd_addr,
    output logic [ BEAT_BITS-1:0] bank_rd_data,

    input logic                  bank_wr_en,
    input logic [ROW_ADDR_W-1:0] bank_wr_addr,
    input logic [ BEAT_BITS-1:0] bank_wr_data,
    input logic [   WSTRB_W-1:0] bank_wr_wstrb
);

  localparam int unsigned WORDS_PER_ROW = BEAT_BITS / 32;
  integer word_idx;
  integer i;

  // Synchronous read with 1-cycle latency (same as spm_bank.sv)
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      bank_rd_data <= '0;
    end else if (bank_wr_en) begin
      // during write, drive read data with write data to avoid unknowns
      bank_rd_data <= bank_wr_data;
    end else if (bank_rd_en) begin
      for (word_idx = 0; word_idx < WORDS_PER_ROW; word_idx = word_idx + 1) begin
        bank_rd_data[word_idx*32 +: 32] <= pmem_read(
            (int'(bank_rd_addr) * WORDS_PER_ROW) + word_idx);
      end
    end
  end

  // Synchronous write via DPI-C
  always_ff @(posedge clk_i) begin
    if (bank_wr_en) begin
      for (i = 0; i < WSTRB_W; i = i + 1) begin
        if (bank_wr_wstrb[i]) begin
          pmem_write((int'(bank_wr_addr) * WORDS_PER_ROW) + (i >> 2),
                     bank_wr_data[(i >> 2) * 32 +: 32],
                     byte'(i[1:0]));
        end
      end
    end
  end

endmodule
