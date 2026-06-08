// gbp_pe_memory_subsystem.sv
// Encapsulates the SPM arbitration and bank array.
// Exposes a 7-client read/write port vector and instantiates the 8 banks
// inside the wrapper so that memory subsystem tests can exercise the full
// interleaved path without pulling in the rest of the PE.

module gbp_pe_memory_subsystem
  import gbp_pkg::*;
#(
    parameter int NUM_BANKS   = gbp_pkg::NUM_BANKS,
    parameter int NUM_CLIENTS = 7,
    parameter int SPM_ADDR_W  = gbp_pkg::SPM_ADDR_W,
    parameter int BEAT_BITS   = gbp_pkg::BEAT_BITS,
    parameter int ROW_ADDR_W  = gbp_pkg::ROW_ADDR_W,
    parameter int WSTRB_W     = gbp_pkg::WSTRB_W
)(
    input  logic clk,
    input  logic rst_n,

    // Client read ports
    input  logic [NUM_CLIENTS-1:0]              rd_valid_i,
    output logic [NUM_CLIENTS-1:0]              rd_ready_o,
    input  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] rd_addr_i,
    output logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0]   rd_data_o,

    // Client write ports
    input  logic [NUM_CLIENTS-1:0]              wr_valid_i,
    output logic [NUM_CLIENTS-1:0]              wr_ready_o,
    input  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] wr_addr_i,
    input  logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0]   wr_data_i,
    input  logic [NUM_CLIENTS-1:0][WSTRB_W-1:0] wr_wstrb_i
);

  // -------------------------------------------------------------------------
  // SPM Arbiter
  // -------------------------------------------------------------------------
  logic [NUM_BANKS-1:0]                bank_rd_en;
  logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0] bank_rd_addr;
  logic [NUM_BANKS-1:0][BEAT_BITS-1:0]  bank_rd_data;

  logic [NUM_BANKS-1:0]                bank_wr_en;
  logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0] bank_wr_addr;
  logic [NUM_BANKS-1:0][BEAT_BITS-1:0]  bank_wr_data;
  logic [NUM_BANKS-1:0][WSTRB_W-1:0]   bank_wr_wstrb;

  spm_arbiter #(
    .NUM_BANKS(NUM_BANKS)
    ,.NUM_CLIENTS(NUM_CLIENTS)
    ,.SPM_ADDR_W(SPM_ADDR_W)
    ,.BEAT_BITS(BEAT_BITS)
    ,.ROW_ADDR_W(ROW_ADDR_W)
    ,.WSTRB_W(WSTRB_W)
  ) u_spm_arbiter (
    .clk(clk)
    ,.rst_n(rst_n)
    ,.rd_valid(rd_valid_i)
    ,.rd_ready(rd_ready_o)
    ,.rd_addr(rd_addr_i)
    ,.rd_data(rd_data_o)
    ,.wr_valid(wr_valid_i)
    ,.wr_ready(wr_ready_o)
    ,.wr_addr(wr_addr_i)
    ,.wr_data(wr_data_i)
    ,.wr_wstrb(wr_wstrb_i)
    ,.bank_rd_en(bank_rd_en)
    ,.bank_rd_addr(bank_rd_addr)
    ,.bank_rd_data(bank_rd_data)
    ,.bank_wr_en(bank_wr_en)
    ,.bank_wr_addr(bank_wr_addr)
    ,.bank_wr_data(bank_wr_data)
    ,.bank_wr_wstrb(bank_wr_wstrb)
  );

  // -------------------------------------------------------------------------
  // SPM Bank Array
  // -------------------------------------------------------------------------
  for (genvar b = 0; b < NUM_BANKS; b++) begin : banks
    spm_bank #(.BANK_ID(b)) u_bank (
      .clk_i(clk)
      ,.reset_i(~rst_n)
      ,.bank_rd_en(bank_rd_en[b])
      ,.bank_rd_addr(bank_rd_addr[b])
      ,.bank_rd_data(bank_rd_data[b])
      ,.bank_wr_en(bank_wr_en[b])
      ,.bank_wr_addr(bank_wr_addr[b])
      ,.bank_wr_data(bank_wr_data[b])
      ,.bank_wr_wstrb(bank_wr_wstrb[b])
    );
  end

endmodule
