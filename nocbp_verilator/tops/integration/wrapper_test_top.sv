// wrapper_test_top.sv
// Test top module for compute_unit_wrapper - converts interface ports to regular signals

`include "bsg_defines.sv"

module wrapper_test_top
#(
    parameter int GBP_CORE_PER_PE = 16
    , parameter int FP_EXP_WIDTH_P = 8
    , parameter int FP_MANT_WIDTH_P = 23
    , parameter int DIV_BITS_PER_ITER_P = 1
) (
    input logic clk_i,
    input logic reset_i,
    
    // Command interface
    input logic cmd_valid_i,
    input logic cmd_kind_i,
    input logic [2:0] cmd_dofs_i,
    input logic [3:0] cmd_adj_count_i,
    input logic [3:0] cmd_msg_count_i,
    output logic cmd_ready_o,
    output logic compute_done_o,
    output logic rsp_done_o,
    input logic force_persistence_stall_i,
    
    // Stream input (read_stream_if signals)
    output logic stream_in_ready_o,
    input logic stream_in_valid_i,
    input logic [255:0] stream_in_data_i,
    
    // Stream output (write_stream_if signals)
    input logic stream_out_ready_i,
    output logic stream_out_valid_o,
    output logic [255:0] stream_out_data_o,
    
    // Direct data interface
    input logic [GBP_CORE_PER_PE-1:0][31:0] data_a_i,
    input logic [GBP_CORE_PER_PE-1:0][31:0] data_b_i,
    input logic [1:0] op_i,
    input logic valid_i,
    output logic [GBP_CORE_PER_PE-1:0][31:0] data_o,
    output logic valid_o
);

  // Internal interface instances
  read_stream_if read_stream_if_inst();
  write_stream_if write_stream_if_inst();
  
  // Connect internal interfaces to ports
  assign stream_in_ready_o = read_stream_if_inst.ready;
  assign read_stream_if_inst.valid = stream_in_valid_i;
  assign read_stream_if_inst.data = stream_in_data_i;
  
  assign stream_out_valid_o = write_stream_if_inst.valid;
  assign stream_out_data_o = write_stream_if_inst.data;
  assign write_stream_if_inst.ready = stream_out_ready_i;

  // Instantiate the wrapper
  compute_unit_wrapper #(
    .GBP_CORE_PER_PE(GBP_CORE_PER_PE),
    .FP_EXP_WIDTH_P(FP_EXP_WIDTH_P),
    .FP_MANT_WIDTH_P(FP_MANT_WIDTH_P),
    .DIV_BITS_PER_ITER_P(DIV_BITS_PER_ITER_P)
  ) u_wrapper (
    .clk_i(clk_i),
    .reset_i(reset_i),
    .cmd_valid_i(cmd_valid_i),
    .cmd_kind_i(cmd_kind_i),
    .cmd_dofs_i(cmd_dofs_i),
    .cmd_adj_count_i(cmd_adj_count_i),
    .cmd_msg_count_i(cmd_msg_count_i),
    .cmd_wr_xfer_bytes_i(16'd32),
    .cmd_ready_o(cmd_ready_o),
    .compute_done_o(compute_done_o),
    .rsp_done_o(rsp_done_o),
    .force_persistence_stall_i(force_persistence_stall_i),
    .if_stream_if_stream(read_stream_if_inst),
    .if_write_stream_if_stream(write_stream_if_inst),
    .data_a_i(data_a_i),
    .data_b_i(data_b_i),
    .op_i(op_i),
    .valid_i(valid_i),
    .data_o(data_o),
    .valid_o(valid_o)
  );

endmodule
