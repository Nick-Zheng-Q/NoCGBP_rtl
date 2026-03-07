module agu_top
  import gbp_pkg::*;
(
    input logic clk,
    input logic rst_n,
    input logic i_start,
    input logic [15:0] i_xfer_bytes,
    input logic [19:0] i_base_addr,
    input logic [7:0] i_addr_step_bytes,
    input logic i_ready,
    output logic [19:0] o_agu_addr,
    output logic o_valid,
    output logic o_next_desc
);

  // logic  [ 1:0] op;
  logic        [ 7:0] txn_id;
  logic        [ 3:0] operand_id;
  // logic  [ 1:0] wstrb_mode;
  // logic  [ 1:0] bank_mode;
  logic        [ 7:0] bank_mask;
  logic               dim;
  logic [11:0] y_count;
  logic [15:0] y_stride_bytes;
  logic               addr_src;
  op_e                op;  // 2
  wstrb_mode_e        wstrb_mode;  // 2
  bank_mode_e         bank_mode;  // 2

  // desc_unpacked_t desc_unpacked;
  desc_t              desc;

  assign desc.xfer_bytes      = i_xfer_bytes;
  assign desc.base_addr       = i_base_addr;
  assign desc.addr_step_bytes = i_addr_step_bytes;

  logic reset_i;
  assign reset_i = ~rst_n;
  always_comb begin : unpack
    desc.op             = op;
    desc.txn_id         = txn_id;
    desc.start          = i_start;
    desc.operand_id     = operand_id;
    desc.wstrb_mode     = wstrb_mode;
    desc.bank_mode      = bank_mode;
    desc.bank_mask      = bank_mask;
    desc.dim            = dim;
    desc.y_count        = y_count;
    desc.y_stride_bytes = y_stride_bytes;
    desc.addr_src       = addr_src;
  end

  agu dut (
      .clk_i(clk)
      , .reset_i(reset_i)
      , .descriptor_i(desc)
      , .ready_i(i_ready)
      , .agu_addr(o_agu_addr)
      , .valid_o(o_valid)
      , .next_desc_o(o_next_desc)
  );

endmodule
