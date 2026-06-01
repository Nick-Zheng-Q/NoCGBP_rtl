module compute_unit_top (
    input logic clk,
    input logic rst_n,
    input logic i_cmd_valid,
    input logic i_cmd_kind,
    output logic o_cmd_ready,
    output logic o_rsp_done,
    input logic i_valid,
    input logic [1:0] i_op,
    input logic i_write_ready,
    input logic [31:0] i_a0,
    input logic [31:0] i_a1,
    input logic [31:0] i_a2,
    input logic [31:0] i_a3,
    input logic [31:0] i_b0,
    input logic [31:0] i_b1,
    input logic [31:0] i_b2,
    input logic [31:0] i_b3,
    output logic o_valid,
    output logic o_wr_valid,
    output logic [31:0] o_wr_data,
    output logic [31:0] o_y0,
    output logic [31:0] o_y1,
    output logic [31:0] o_y2,
    output logic [31:0] o_y3
);

  localparam int lanes_lp = 4;

  logic reset_i;
  logic [lanes_lp-1:0][31:0] data_a;
  logic [lanes_lp-1:0][31:0] data_b;
  logic [lanes_lp-1:0][31:0] data_o;
  logic valid_o;

  read_stream_if if_read_stream();
  write_stream_if if_write_stream();

  assign reset_i = ~rst_n;

  assign data_a[0] = i_a0;
  assign data_a[1] = i_a1;
  assign data_a[2] = i_a2;
  assign data_a[3] = i_a3;

  assign data_b[0] = i_b0;
  assign data_b[1] = i_b1;
  assign data_b[2] = i_b2;
  assign data_b[3] = i_b3;

  assign if_read_stream.valid = 1'b0;
  assign if_read_stream.data = '0;

  assign if_write_stream.ready = i_write_ready;

  assign o_valid = valid_o;
  assign o_wr_valid = if_write_stream.valid;
  assign o_wr_data = if_write_stream.data;

  assign o_y0 = data_o[0];
  assign o_y1 = data_o[1];
  assign o_y2 = data_o[2];
  assign o_y3 = data_o[3];

  compute_unit #(
      .GBP_CORE_PER_PE(lanes_lp)
  ) dut (
      .clk_i(clk),
      .reset_i(reset_i),
      .cmd_valid_i(i_cmd_valid),
      .cmd_kind_i(i_cmd_kind),
      .cmd_ready_o(o_cmd_ready),
      .compute_done_o(),
      .rsp_done_o(o_rsp_done),
      .force_persistence_stall_i(1'b0),
      .if_stream_if_stream(if_read_stream),
      .if_write_stream_if_stream(if_write_stream),
      .data_a_i(data_a),
      .data_b_i(data_b),
      .op_i(i_op),
      .valid_i(i_valid),
      .data_o(data_o),
      .valid_o(valid_o)
  );

endmodule
