// spm_bank_array_top.sv
// Unit-test wrapper for spm_bank_array
// Flattened per-bank ports for Verilator C++ compatibility

module spm_bank_array_top
  import gbp_pkg::*;
#(
    parameter int NB        = gbp_pkg::NUM_BANKS,
    parameter int ROW_ADDR_W = gbp_pkg::ROW_ADDR_W,
    parameter int BEAT_BITS  = gbp_pkg::BEAT_BITS,
    parameter int WSTRB_W    = gbp_pkg::WSTRB_W
)(
    input logic clk,
    input logic reset_i,

    // Bank 0
    input  logic                  bank0_rd_en,
    input  logic [ROW_ADDR_W-1:0] bank0_rd_addr,
    output logic [ BEAT_BITS-1:0] bank0_rd_data,
    input  logic                  bank0_wr_en,
    input  logic [ROW_ADDR_W-1:0] bank0_wr_addr,
    input  logic [ BEAT_BITS-1:0] bank0_wr_data,
    input  logic [   WSTRB_W-1:0] bank0_wr_wstrb,

    // Bank 1
    input  logic                  bank1_rd_en,
    input  logic [ROW_ADDR_W-1:0] bank1_rd_addr,
    output logic [ BEAT_BITS-1:0] bank1_rd_data,
    input  logic                  bank1_wr_en,
    input  logic [ROW_ADDR_W-1:0] bank1_wr_addr,
    input  logic [ BEAT_BITS-1:0] bank1_wr_data,
    input  logic [   WSTRB_W-1:0] bank1_wr_wstrb,

    // Bank 2
    input  logic                  bank2_rd_en,
    input  logic [ROW_ADDR_W-1:0] bank2_rd_addr,
    output logic [ BEAT_BITS-1:0] bank2_rd_data,
    input  logic                  bank2_wr_en,
    input  logic [ROW_ADDR_W-1:0] bank2_wr_addr,
    input  logic [ BEAT_BITS-1:0] bank2_wr_data,
    input  logic [   WSTRB_W-1:0] bank2_wr_wstrb,

    // Bank 3
    input  logic                  bank3_rd_en,
    input  logic [ROW_ADDR_W-1:0] bank3_rd_addr,
    output logic [ BEAT_BITS-1:0] bank3_rd_data,
    input  logic                  bank3_wr_en,
    input  logic [ROW_ADDR_W-1:0] bank3_wr_addr,
    input  logic [ BEAT_BITS-1:0] bank3_wr_data,
    input  logic [   WSTRB_W-1:0] bank3_wr_wstrb,

    // Bank 4
    input  logic                  bank4_rd_en,
    input  logic [ROW_ADDR_W-1:0] bank4_rd_addr,
    output logic [ BEAT_BITS-1:0] bank4_rd_data,
    input  logic                  bank4_wr_en,
    input  logic [ROW_ADDR_W-1:0] bank4_wr_addr,
    input  logic [ BEAT_BITS-1:0] bank4_wr_data,
    input  logic [   WSTRB_W-1:0] bank4_wr_wstrb,

    // Bank 5
    input  logic                  bank5_rd_en,
    input  logic [ROW_ADDR_W-1:0] bank5_rd_addr,
    output logic [ BEAT_BITS-1:0] bank5_rd_data,
    input  logic                  bank5_wr_en,
    input  logic [ROW_ADDR_W-1:0] bank5_wr_addr,
    input  logic [ BEAT_BITS-1:0] bank5_wr_data,
    input  logic [   WSTRB_W-1:0] bank5_wr_wstrb,

    // Bank 6
    input  logic                  bank6_rd_en,
    input  logic [ROW_ADDR_W-1:0] bank6_rd_addr,
    output logic [ BEAT_BITS-1:0] bank6_rd_data,
    input  logic                  bank6_wr_en,
    input  logic [ROW_ADDR_W-1:0] bank6_wr_addr,
    input  logic [ BEAT_BITS-1:0] bank6_wr_data,
    input  logic [   WSTRB_W-1:0] bank6_wr_wstrb,

    // Bank 7
    input  logic                  bank7_rd_en,
    input  logic [ROW_ADDR_W-1:0] bank7_rd_addr,
    output logic [ BEAT_BITS-1:0] bank7_rd_data,
    input  logic                  bank7_wr_en,
    input  logic [ROW_ADDR_W-1:0] bank7_wr_addr,
    input  logic [ BEAT_BITS-1:0] bank7_wr_data,
    input  logic [   WSTRB_W-1:0] bank7_wr_wstrb
);

  logic [NB-1:0]                bank_rd_en;
  logic [NB-1:0][ROW_ADDR_W-1:0] bank_rd_addr;
  logic [NB-1:0][ BEAT_BITS-1:0] bank_rd_data;
  logic [NB-1:0]                bank_wr_en;
  logic [NB-1:0][ROW_ADDR_W-1:0] bank_wr_addr;
  logic [NB-1:0][ BEAT_BITS-1:0] bank_wr_data;
  logic [NB-1:0][   WSTRB_W-1:0] bank_wr_wstrb;

  // Bank 0
  assign bank_rd_en[0]   = bank0_rd_en;
  assign bank_rd_addr[0] = bank0_rd_addr;
  assign bank0_rd_data   = bank_rd_data[0];
  assign bank_wr_en[0]   = bank0_wr_en;
  assign bank_wr_addr[0] = bank0_wr_addr;
  assign bank_wr_data[0] = bank0_wr_data;
  assign bank_wr_wstrb[0] = bank0_wr_wstrb;

  // Bank 1
  assign bank_rd_en[1]   = bank1_rd_en;
  assign bank_rd_addr[1] = bank1_rd_addr;
  assign bank1_rd_data   = bank_rd_data[1];
  assign bank_wr_en[1]   = bank1_wr_en;
  assign bank_wr_addr[1] = bank1_wr_addr;
  assign bank_wr_data[1] = bank1_wr_data;
  assign bank_wr_wstrb[1] = bank1_wr_wstrb;

  // Bank 2
  assign bank_rd_en[2]   = bank2_rd_en;
  assign bank_rd_addr[2] = bank2_rd_addr;
  assign bank2_rd_data   = bank_rd_data[2];
  assign bank_wr_en[2]   = bank2_wr_en;
  assign bank_wr_addr[2] = bank2_wr_addr;
  assign bank_wr_data[2] = bank2_wr_data;
  assign bank_wr_wstrb[2] = bank2_wr_wstrb;

  // Bank 3
  assign bank_rd_en[3]   = bank3_rd_en;
  assign bank_rd_addr[3] = bank3_rd_addr;
  assign bank3_rd_data   = bank_rd_data[3];
  assign bank_wr_en[3]   = bank3_wr_en;
  assign bank_wr_addr[3] = bank3_wr_addr;
  assign bank_wr_data[3] = bank3_wr_data;
  assign bank_wr_wstrb[3] = bank3_wr_wstrb;

  // Bank 4
  assign bank_rd_en[4]   = bank4_rd_en;
  assign bank_rd_addr[4] = bank4_rd_addr;
  assign bank4_rd_data   = bank_rd_data[4];
  assign bank_wr_en[4]   = bank4_wr_en;
  assign bank_wr_addr[4] = bank4_wr_addr;
  assign bank_wr_data[4] = bank4_wr_data;
  assign bank_wr_wstrb[4] = bank4_wr_wstrb;

  // Bank 5
  assign bank_rd_en[5]   = bank5_rd_en;
  assign bank_rd_addr[5] = bank5_rd_addr;
  assign bank5_rd_data   = bank_rd_data[5];
  assign bank_wr_en[5]   = bank5_wr_en;
  assign bank_wr_addr[5] = bank5_wr_addr;
  assign bank_wr_data[5] = bank5_wr_data;
  assign bank_wr_wstrb[5] = bank5_wr_wstrb;

  // Bank 6
  assign bank_rd_en[6]   = bank6_rd_en;
  assign bank_rd_addr[6] = bank6_rd_addr;
  assign bank6_rd_data   = bank_rd_data[6];
  assign bank_wr_en[6]   = bank6_wr_en;
  assign bank_wr_addr[6] = bank6_wr_addr;
  assign bank_wr_data[6] = bank6_wr_data;
  assign bank_wr_wstrb[6] = bank6_wr_wstrb;

  // Bank 7
  assign bank_rd_en[7]   = bank7_rd_en;
  assign bank_rd_addr[7] = bank7_rd_addr;
  assign bank7_rd_data   = bank_rd_data[7];
  assign bank_wr_en[7]   = bank7_wr_en;
  assign bank_wr_addr[7] = bank7_wr_addr;
  assign bank_wr_data[7] = bank7_wr_data;
  assign bank_wr_wstrb[7] = bank7_wr_wstrb;

  spm_bank_array #(
    .NB(NB),
    .ROW_ADDR_W(ROW_ADDR_W),
    .BEAT_BITS(BEAT_BITS),
    .WSTRB_W(WSTRB_W)
  ) u_dut (
    .clk_i       (clk),
    .reset_i     (reset_i),
    .bank_rd_en  (bank_rd_en),
    .bank_rd_addr(bank_rd_addr),
    .bank_rd_data(bank_rd_data),
    .bank_wr_en  (bank_wr_en),
    .bank_wr_addr(bank_wr_addr),
    .bank_wr_data(bank_wr_data),
    .bank_wr_wstrb(bank_wr_wstrb)
  );

endmodule
