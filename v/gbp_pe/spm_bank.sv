// spm_bank.sv
// Synthesizable SPM (Scratchpad Memory) Bank
// Simple dual-port memory with byte-enable write

import gbp_pkg::*;
module spm_bank
(
    input logic clk_i,
    input logic reset_i,

    // Read port
    input  logic                  bank_rd_en,
    input  logic [ROW_ADDR_W-1:0] bank_rd_addr,
    output logic [ BEAT_BITS-1:0] bank_rd_data,

    // Write port
    input logic                  bank_wr_en,
    input logic [ROW_ADDR_W-1:0] bank_wr_addr,
    input logic [ BEAT_BITS-1:0] bank_wr_data,
    input logic [   WSTRB_W-1:0] bank_wr_wstrb
);

  // Memory array
  logic [BEAT_BITS-1:0] mem_r [(1<<ROW_ADDR_W)-1:0];

  // Read logic
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      bank_rd_data <= '0;
    end else begin
      if (bank_rd_en) begin
        bank_rd_data <= mem_r[bank_rd_addr];
      end
    end
  end

  // Write logic with byte-enable
  always_ff @(posedge clk_i) begin
    if (bank_wr_en) begin
      for (int i = 0; i < WSTRB_W; i++) begin
        if (bank_wr_wstrb[i]) begin
          mem_r[bank_wr_addr][8*i +: 8] <= bank_wr_data[8*i +: 8];
        end
      end
    end
  end

endmodule