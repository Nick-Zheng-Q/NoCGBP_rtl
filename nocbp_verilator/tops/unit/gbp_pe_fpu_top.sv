module gbp_pe_fpu_top (
  input  logic clk,
  input  logic rst_n,
  input  logic valid_i,
  input  logic [1:0] op_i,
  input  logic [31:0] a_i,
  input  logic [31:0] b_i,
  output logic [31:0] y_o
);

  localparam int lanes_lp = 1;

  logic reset_i;
  logic [lanes_lp-1:0][31:0] data_a;
  logic [lanes_lp-1:0][31:0] data_b;
  logic [lanes_lp-1:0][31:0] data_y;

  assign reset_i = ~rst_n;
  assign data_a[0] = a_i;
  assign data_b[0] = b_i;
  assign y_o = data_y[0];

  gbp_pe #(
    .GBP_CORE_PER_PE(lanes_lp)
  ) dut (
    .clk_i(clk)
    ,.reset_i(reset_i)
    ,.data_i(data_a)
    ,.data_b_i(data_b)
    ,.op_i(op_i)
    ,.length(valid_i)
    ,.data_o(data_y)
  );

endmodule
