// Scratchpad testbench for Verilator
// Tests write and read operations for each bank

`include "bsg_defines.sv"

module scratchpad_test_top (
  input  logic clk,
  input  logic rst_n,

  // Write port
  input  logic w_v_i,
  input  logic [3:0] w_addr_i,
  input  logic [31:0] w_data_i,

  // Read port
  input  logic r_v_i,
  input  logic [3:0] r_addr_i,
  output logic [31:0] r_data_o,
  output logic r_v_o
);

  logic reset_i;
  assign reset_i = ~rst_n;

  // Test with 4 banks: addr_width_p=4, num_banks_p=4
  // Each bank has 2 elements (4 - log2(4) = 2)
  scratchpad #(
    .data_width_p(32)
    ,.addr_width_p(4)
    ,.num_banks_p(4)
  ) dut (
    .clk_i(clk)
    ,.reset_i(reset_i)
    ,.w_v_i(w_v_i)
    ,.w_addr_i(w_addr_i)
    ,.w_data_i(w_data_i)
    ,.r_v_i(r_v_i)
    ,.r_addr_i(r_addr_i)
    ,.r_data_o(r_data_o)
    ,.r_v_o(r_v_o)
  );

endmodule
