module scratchpad_top (
  input logic clk,
  input logic rst_n,

  // single-bank port
  input logic sb_w_v,
  input logic [3:0] sb_w_addr,
  input logic [31:0] sb_w_data,
  input logic sb_r_v,
  input logic [3:0] sb_r_addr,
  output logic [31:0] sb_r_data,
  output logic sb_r_v,

  // multi-bank port
  input logic mb_w_v,
  input logic [3:0] mb_w_addr,
  input logic [31:0] mb_w_data,
  input logic mb_r_v,
  input logic [3:0] mb_r_addr,
  output logic [31:0] mb_r_data,
  output logic mb_r_v
);

  logic reset_i;
  assign reset_i = ~rst_n;

  scratchpad #(
    .data_width_p(32)
    ,.addr_width_p(4)
    ,.num_banks_p(1)
  ) sb (
    .clk_i(clk)
    ,.reset_i(reset_i)
    ,.w_v_i(sb_w_v)
    ,.w_addr_i(sb_w_addr)
    ,.w_data_i(sb_w_data)
    ,.r_v_i(sb_r_v)
    ,.r_addr_i(sb_r_addr)
    ,.r_data_o(sb_r_data)
    ,.r_v_o(sb_r_v)
  );

  scratchpad #(
    .data_width_p(32)
    ,.addr_width_p(4)
    ,.num_banks_p(4)
  ) mb (
    .clk_i(clk)
    ,.reset_i(reset_i)
    ,.w_v_i(mb_w_v)
    ,.w_addr_i(mb_w_addr)
    ,.w_data_i(mb_w_data)
    ,.r_v_i(mb_r_v)
    ,.r_addr_i(mb_r_addr)
    ,.r_data_o(mb_r_data)
    ,.r_v_o(mb_r_v)
  );

endmodule
