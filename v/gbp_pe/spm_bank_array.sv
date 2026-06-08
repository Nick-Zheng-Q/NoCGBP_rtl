// spm_bank_array.sv
// Array of SPM banks with flattened ports (no interface).
// Directly instantiates NB spm_bank modules.

module spm_bank_array
  import gbp_pkg::*;
#(
    parameter int NB        = gbp_pkg::NUM_BANKS,
    parameter int ROW_ADDR_W = gbp_pkg::ROW_ADDR_W,
    parameter int BEAT_BITS  = gbp_pkg::BEAT_BITS,
    parameter int WSTRB_W    = gbp_pkg::WSTRB_W
)(
    input logic clk_i,
    input logic rst_n_i,

    // Per-bank read ports
    input  logic [NB-1:0]                bank_rd_en,
    input  logic [NB-1:0][ROW_ADDR_W-1:0] bank_rd_addr,
    output logic [NB-1:0][ BEAT_BITS-1:0] bank_rd_data,

    // Per-bank write ports
    input logic [NB-1:0]                bank_wr_en,
    input logic [NB-1:0][ROW_ADDR_W-1:0] bank_wr_addr,
    input logic [NB-1:0][ BEAT_BITS-1:0] bank_wr_data,
    input logic [NB-1:0][   WSTRB_W-1:0] bank_wr_wstrb
);

  logic reset_i;
  assign reset_i = ~rst_n_i;

  for (genvar b = 0; b < NB; b++) begin : banks
    spm_bank #(.BANK_ID(b)) u_bank (
      .clk_i       (clk_i),
      .rst_n_i     (rst_n_i),
      .bank_rd_en  (bank_rd_en[b]),
      .bank_rd_addr(bank_rd_addr[b]),
      .bank_rd_data(bank_rd_data[b]),
      .bank_wr_en  (bank_wr_en[b]),
      .bank_wr_addr(bank_wr_addr[b]),
      .bank_wr_data(bank_wr_data[b]),
      .bank_wr_wstrb(bank_wr_wstrb[b])
    );
  end

endmodule
