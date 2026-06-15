// packed_accumulator_top.sv
// Test wrapper for packed_accumulator

module packed_accumulator_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  input  logic     start_valid_i,
  output logic     start_ready_o,
  input  gbp_dim_e dim_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    prior_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] prior_L_flat_i,
  input  logic [15:0] degree_i,

  input  logic     msg_valid_i,
  output logic     msg_ready_o,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    msg_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] msg_L_flat_i,
  input  logic     msg_last_i,

  output logic     acc_valid_o,
  input  logic     acc_ready_i,
  output gbp_dim_e dim_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    acc_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] acc_L_flat_o,
  output logic [15:0] msg_count_o,
  output logic     degree_mismatch_o
);

  packed_accumulator u_acc (
    .clk_i,
    .reset_i,
    .start_valid_i,
    .start_ready_o,
    .dim_i,
    .prior_eta_flat_i,
    .prior_L_flat_i,
    .degree_i,
    .msg_valid_i,
    .msg_ready_o,
    .msg_eta_flat_i,
    .msg_L_flat_i,
    .msg_last_i,
    .acc_valid_o,
    .acc_ready_i,
    .dim_o,
    .acc_eta_flat_o,
    .acc_L_flat_o,
    .msg_count_o,
    .degree_mismatch_o
  );

endmodule
