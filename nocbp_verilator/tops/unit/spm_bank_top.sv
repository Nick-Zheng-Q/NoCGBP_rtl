// spm_bank_top.sv
// Unit-test wrapper for spm_bank

module spm_bank_top
  import gbp_pkg::*;
#(
    parameter int BANK_ID = 0,
    parameter int ROW_ADDR_W = gbp_pkg::ROW_ADDR_W,
    parameter int BEAT_BITS   = gbp_pkg::BEAT_BITS,
    parameter int WSTRB_W     = gbp_pkg::WSTRB_W
)(
    input  logic clk,
    input  logic reset_i,

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

  spm_bank #(
    .BANK_ID(BANK_ID)
  ) u_dut (
    .clk_i(clk),
    .reset_i(reset_i),
    .bank_rd_en(bank_rd_en),
    .bank_rd_addr(bank_rd_addr),
    .bank_rd_data(bank_rd_data),
    .bank_wr_en(bank_wr_en),
    .bank_wr_addr(bank_wr_addr),
    .bank_wr_data(bank_wr_data),
    .bank_wr_wstrb(bank_wr_wstrb)
  );

endmodule
