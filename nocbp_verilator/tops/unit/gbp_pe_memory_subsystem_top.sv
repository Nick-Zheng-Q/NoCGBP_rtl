// gbp_pe_memory_subsystem_top.sv
// Unit-test wrapper for memory subsystem.
// Exposes 7 client read/write ports for bank conflict testing.

module gbp_pe_memory_subsystem_top (
    input logic clk
    , input logic rst_n

    // Client 0 read/write
    , input  logic        c0_rd_valid_i
    , output logic        c0_rd_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c0_rd_addr_i
    , output logic [gbp_pkg::BEAT_BITS-1:0]  c0_rd_data_o
    , input  logic        c0_wr_valid_i
    , output logic        c0_wr_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c0_wr_addr_i
    , input  logic [gbp_pkg::BEAT_BITS-1:0]  c0_wr_data_i
    , input  logic [gbp_pkg::WSTRB_W-1:0]   c0_wr_wstrb_i

    // Client 1 read/write
    , input  logic        c1_rd_valid_i
    , output logic        c1_rd_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c1_rd_addr_i
    , output logic [gbp_pkg::BEAT_BITS-1:0]  c1_rd_data_o
    , input  logic        c1_wr_valid_i
    , output logic        c1_wr_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c1_wr_addr_i
    , input  logic [gbp_pkg::BEAT_BITS-1:0]  c1_wr_data_i
    , input  logic [gbp_pkg::WSTRB_W-1:0]   c1_wr_wstrb_i

    // Client 2 read/write
    , input  logic        c2_rd_valid_i
    , output logic        c2_rd_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c2_rd_addr_i
    , output logic [gbp_pkg::BEAT_BITS-1:0]  c2_rd_data_o
    , input  logic        c2_wr_valid_i
    , output logic        c2_wr_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c2_wr_addr_i
    , input  logic [gbp_pkg::BEAT_BITS-1:0]  c2_wr_data_i
    , input  logic [gbp_pkg::WSTRB_W-1:0]   c2_wr_wstrb_i

    // Client 3 read/write
    , input  logic        c3_rd_valid_i
    , output logic        c3_rd_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c3_rd_addr_i
    , output logic [gbp_pkg::BEAT_BITS-1:0]  c3_rd_data_o
    , input  logic        c3_wr_valid_i
    , output logic        c3_wr_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c3_wr_addr_i
    , input  logic [gbp_pkg::BEAT_BITS-1:0]  c3_wr_data_i
    , input  logic [gbp_pkg::WSTRB_W-1:0]   c3_wr_wstrb_i

    // Client 4 read/write
    , input  logic        c4_rd_valid_i
    , output logic        c4_rd_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c4_rd_addr_i
    , output logic [gbp_pkg::BEAT_BITS-1:0]  c4_rd_data_o
    , input  logic        c4_wr_valid_i
    , output logic        c4_wr_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c4_wr_addr_i
    , input  logic [gbp_pkg::BEAT_BITS-1:0]  c4_wr_data_i
    , input  logic [gbp_pkg::WSTRB_W-1:0]   c4_wr_wstrb_i

    // Client 5 read/write
    , input  logic        c5_rd_valid_i
    , output logic        c5_rd_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c5_rd_addr_i
    , output logic [gbp_pkg::BEAT_BITS-1:0]  c5_rd_data_o
    , input  logic        c5_wr_valid_i
    , output logic        c5_wr_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c5_wr_addr_i
    , input  logic [gbp_pkg::BEAT_BITS-1:0]  c5_wr_data_i
    , input  logic [gbp_pkg::WSTRB_W-1:0]   c5_wr_wstrb_i

    // Client 6 read/write
    , input  logic        c6_rd_valid_i
    , output logic        c6_rd_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c6_rd_addr_i
    , output logic [gbp_pkg::BEAT_BITS-1:0]  c6_rd_data_o
    , input  logic        c6_wr_valid_i
    , output logic        c6_wr_ready_o
    , input  logic [gbp_pkg::SPM_ADDR_W-1:0] c6_wr_addr_i
    , input  logic [gbp_pkg::BEAT_BITS-1:0]  c6_wr_data_i
    , input  logic [gbp_pkg::WSTRB_W-1:0]   c6_wr_wstrb_i

    // Bank enable observation (for verification)
    , output logic [gbp_pkg::NUM_BANKS-1:0] bank_rd_en_o
    , output logic [gbp_pkg::NUM_BANKS-1:0] bank_wr_en_o
);

  localparam int MEM_CLIENTS = 7;
  logic [MEM_CLIENTS-1:0]                 rd_valid, rd_ready;
  logic [MEM_CLIENTS-1:0][gbp_pkg::SPM_ADDR_W-1:0] rd_addr;
  logic [MEM_CLIENTS-1:0][gbp_pkg::BEAT_BITS-1:0]  rd_data;
  logic [MEM_CLIENTS-1:0]                 wr_valid, wr_ready;
  logic [MEM_CLIENTS-1:0][gbp_pkg::SPM_ADDR_W-1:0] wr_addr;
  logic [MEM_CLIENTS-1:0][gbp_pkg::BEAT_BITS-1:0]  wr_data;
  logic [MEM_CLIENTS-1:0][gbp_pkg::WSTRB_W-1:0]   wr_wstrb;

  assign rd_valid[0] = c0_rd_valid_i;
  assign c0_rd_ready_o = rd_ready[0];
  assign rd_addr[0]  = c0_rd_addr_i;
  assign c0_rd_data_o  = rd_data[0];
  assign wr_valid[0] = c0_wr_valid_i;
  assign c0_wr_ready_o = wr_ready[0];
  assign wr_addr[0]  = c0_wr_addr_i;
  assign wr_data[0]  = c0_wr_data_i;
  assign wr_wstrb[0] = c0_wr_wstrb_i;

  assign rd_valid[1] = c1_rd_valid_i;
  assign c1_rd_ready_o = rd_ready[1];
  assign rd_addr[1]  = c1_rd_addr_i;
  assign c1_rd_data_o  = rd_data[1];
  assign wr_valid[1] = c1_wr_valid_i;
  assign c1_wr_ready_o = wr_ready[1];
  assign wr_addr[1]  = c1_wr_addr_i;
  assign wr_data[1]  = c1_wr_data_i;
  assign wr_wstrb[1] = c1_wr_wstrb_i;

  assign rd_valid[2] = c2_rd_valid_i;
  assign c2_rd_ready_o = rd_ready[2];
  assign rd_addr[2]  = c2_rd_addr_i;
  assign c2_rd_data_o  = rd_data[2];
  assign wr_valid[2] = c2_wr_valid_i;
  assign c2_wr_ready_o = wr_ready[2];
  assign wr_addr[2]  = c2_wr_addr_i;
  assign wr_data[2]  = c2_wr_data_i;
  assign wr_wstrb[2] = c2_wr_wstrb_i;

  assign rd_valid[3] = c3_rd_valid_i;
  assign c3_rd_ready_o = rd_ready[3];
  assign rd_addr[3]  = c3_rd_addr_i;
  assign c3_rd_data_o  = rd_data[3];
  assign wr_valid[3] = c3_wr_valid_i;
  assign c3_wr_ready_o = wr_ready[3];
  assign wr_addr[3]  = c3_wr_addr_i;
  assign wr_data[3]  = c3_wr_data_i;
  assign wr_wstrb[3] = c3_wr_wstrb_i;

  assign rd_valid[4] = c4_rd_valid_i;
  assign c4_rd_ready_o = rd_ready[4];
  assign rd_addr[4]  = c4_rd_addr_i;
  assign c4_rd_data_o  = rd_data[4];
  assign wr_valid[4] = c4_wr_valid_i;
  assign c4_wr_ready_o = wr_ready[4];
  assign wr_addr[4]  = c4_wr_addr_i;
  assign wr_data[4]  = c4_wr_data_i;
  assign wr_wstrb[4] = c4_wr_wstrb_i;

  assign rd_valid[5] = c5_rd_valid_i;
  assign c5_rd_ready_o = rd_ready[5];
  assign rd_addr[5]  = c5_rd_addr_i;
  assign c5_rd_data_o  = rd_data[5];
  assign wr_valid[5] = c5_wr_valid_i;
  assign c5_wr_ready_o = wr_ready[5];
  assign wr_addr[5]  = c5_wr_addr_i;
  assign wr_data[5]  = c5_wr_data_i;
  assign wr_wstrb[5] = c5_wr_wstrb_i;

  assign rd_valid[6] = c6_rd_valid_i;
  assign c6_rd_ready_o = rd_ready[6];
  assign rd_addr[6]  = c6_rd_addr_i;
  assign c6_rd_data_o  = rd_data[6];
  assign wr_valid[6] = c6_wr_valid_i;
  assign c6_wr_ready_o = wr_ready[6];
  assign wr_addr[6]  = c6_wr_addr_i;
  assign wr_data[6]  = c6_wr_data_i;
  assign wr_wstrb[6] = c6_wr_wstrb_i;

  gbp_pe_memory_subsystem u_dut (
    .clk(clk)
    ,.rst_n(rst_n)
    ,.rd_valid_i(rd_valid)
    ,.rd_ready_o(rd_ready)
    ,.rd_addr_i(rd_addr)
    ,.rd_data_o(rd_data)
    ,.wr_valid_i(wr_valid)
    ,.wr_ready_o(wr_ready)
    ,.wr_addr_i(wr_addr)
    ,.wr_data_i(wr_data)
    ,.wr_wstrb_i(wr_wstrb)
  );

  // Expose internal bank enables for test observation
  assign bank_rd_en_o = u_dut.u_spm_arbiter.bank_rd_en;
  assign bank_wr_en_o = u_dut.u_spm_arbiter.bank_wr_en;

endmodule
