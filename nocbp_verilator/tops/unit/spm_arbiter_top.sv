// spm_arbiter_top.sv
// Unit test wrapper for spm_arbiter
// Flattened array ports for Verilator C++ compatibility

module spm_arbiter_top #(
    parameter int NUM_BANKS   = gbp_pkg::NUM_BANKS,
    parameter int NUM_CLIENTS = 7,
    parameter int SPM_ADDR_W  = gbp_pkg::SPM_ADDR_W,
    parameter int BEAT_BITS   = gbp_pkg::BEAT_BITS,
    parameter int ROW_ADDR_W  = gbp_pkg::ROW_ADDR_W,
    parameter int WSTRB_W     = gbp_pkg::WSTRB_W
) (
    input  logic clk,
    input  logic rst_n,

    input  logic [NUM_CLIENTS-1:0] rd_valid,
    output logic [NUM_CLIENTS-1:0] rd_ready,
    input  logic [SPM_ADDR_W-1:0] rd_addr_0,
    input  logic [SPM_ADDR_W-1:0] rd_addr_1,
    input  logic [SPM_ADDR_W-1:0] rd_addr_2,
    input  logic [SPM_ADDR_W-1:0] rd_addr_3,
    input  logic [SPM_ADDR_W-1:0] rd_addr_4,
    input  logic [SPM_ADDR_W-1:0] rd_addr_5,
    input  logic [SPM_ADDR_W-1:0] rd_addr_6,
    output logic [BEAT_BITS-1:0] rd_data_0,
    output logic [BEAT_BITS-1:0] rd_data_1,
    output logic [BEAT_BITS-1:0] rd_data_2,
    output logic [BEAT_BITS-1:0] rd_data_3,
    output logic [BEAT_BITS-1:0] rd_data_4,
    output logic [BEAT_BITS-1:0] rd_data_5,
    output logic [BEAT_BITS-1:0] rd_data_6,

    input  logic [NUM_CLIENTS-1:0] wr_valid,
    output logic [NUM_CLIENTS-1:0] wr_ready,
    input  logic [SPM_ADDR_W-1:0] wr_addr_0,
    input  logic [SPM_ADDR_W-1:0] wr_addr_1,
    input  logic [SPM_ADDR_W-1:0] wr_addr_2,
    input  logic [SPM_ADDR_W-1:0] wr_addr_3,
    input  logic [SPM_ADDR_W-1:0] wr_addr_4,
    input  logic [SPM_ADDR_W-1:0] wr_addr_5,
    input  logic [SPM_ADDR_W-1:0] wr_addr_6,
    input  logic [BEAT_BITS-1:0] wr_data_0,
    input  logic [BEAT_BITS-1:0] wr_data_1,
    input  logic [BEAT_BITS-1:0] wr_data_2,
    input  logic [BEAT_BITS-1:0] wr_data_3,
    input  logic [BEAT_BITS-1:0] wr_data_4,
    input  logic [BEAT_BITS-1:0] wr_data_5,
    input  logic [BEAT_BITS-1:0] wr_data_6,
    input  logic [WSTRB_W-1:0] wr_wstrb_0,
    input  logic [WSTRB_W-1:0] wr_wstrb_1,
    input  logic [WSTRB_W-1:0] wr_wstrb_2,
    input  logic [WSTRB_W-1:0] wr_wstrb_3,
    input  logic [WSTRB_W-1:0] wr_wstrb_4,
    input  logic [WSTRB_W-1:0] wr_wstrb_5,
    input  logic [WSTRB_W-1:0] wr_wstrb_6,

    input  logic [NUM_BANKS-1:0][BEAT_BITS-1:0] bank_rd_data,
    output logic [NUM_BANKS-1:0] bank_rd_en,
    output logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0] bank_rd_addr,
    output logic [NUM_BANKS-1:0] bank_wr_en,
    output logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0] bank_wr_addr,
    output logic [NUM_BANKS-1:0][BEAT_BITS-1:0] bank_wr_data,
    output logic [NUM_BANKS-1:0][WSTRB_W-1:0] bank_wr_wstrb
);

  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] rd_addr;
  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] wr_addr;
  logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0] rd_data;
  logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0] wr_data;
  logic [NUM_CLIENTS-1:0][WSTRB_W-1:0] wr_wstrb;

  assign rd_addr[0] = rd_addr_0;
  assign rd_addr[1] = rd_addr_1;
  assign rd_addr[2] = rd_addr_2;
  assign rd_addr[3] = rd_addr_3;
  assign rd_addr[4] = rd_addr_4;
  assign rd_addr[5] = rd_addr_5;
  assign rd_addr[6] = rd_addr_6;

  assign wr_addr[0] = wr_addr_0;
  assign wr_addr[1] = wr_addr_1;
  assign wr_addr[2] = wr_addr_2;
  assign wr_addr[3] = wr_addr_3;
  assign wr_addr[4] = wr_addr_4;
  assign wr_addr[5] = wr_addr_5;
  assign wr_addr[6] = wr_addr_6;

  assign wr_data[0] = wr_data_0;
  assign wr_data[1] = wr_data_1;
  assign wr_data[2] = wr_data_2;
  assign wr_data[3] = wr_data_3;
  assign wr_data[4] = wr_data_4;
  assign wr_data[5] = wr_data_5;
  assign wr_data[6] = wr_data_6;

  assign wr_wstrb[0] = wr_wstrb_0;
  assign wr_wstrb[1] = wr_wstrb_1;
  assign wr_wstrb[2] = wr_wstrb_2;
  assign wr_wstrb[3] = wr_wstrb_3;
  assign wr_wstrb[4] = wr_wstrb_4;
  assign wr_wstrb[5] = wr_wstrb_5;
  assign wr_wstrb[6] = wr_wstrb_6;

  assign rd_data_0 = rd_data[0];
  assign rd_data_1 = rd_data[1];
  assign rd_data_2 = rd_data[2];
  assign rd_data_3 = rd_data[3];
  assign rd_data_4 = rd_data[4];
  assign rd_data_5 = rd_data[5];
  assign rd_data_6 = rd_data[6];

  spm_arbiter #(
    .NUM_BANKS(NUM_BANKS),
    .NUM_CLIENTS(NUM_CLIENTS),
    .SPM_ADDR_W(SPM_ADDR_W),
    .BEAT_BITS(BEAT_BITS),
    .ROW_ADDR_W(ROW_ADDR_W),
    .WSTRB_W(WSTRB_W)
  ) dut (
    .clk(clk),
    .rst_n(rst_n),
    .rd_valid(rd_valid),
    .rd_ready(rd_ready),
    .rd_addr(rd_addr),
    .rd_data(rd_data),
    .wr_valid(wr_valid),
    .wr_ready(wr_ready),
    .wr_addr(wr_addr),
    .wr_data(wr_data),
    .wr_wstrb(wr_wstrb),
    .bank_rd_en(bank_rd_en),
    .bank_rd_addr(bank_rd_addr),
    .bank_rd_data(bank_rd_data),
    .bank_wr_en(bank_wr_en),
    .bank_wr_addr(bank_wr_addr),
    .bank_wr_data(bank_wr_data),
    .bank_wr_wstrb(bank_wr_wstrb)
  );

endmodule
