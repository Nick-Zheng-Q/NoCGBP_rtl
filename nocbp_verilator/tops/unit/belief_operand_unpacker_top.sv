// belief_operand_unpacker_top.sv
// Test wrapper for belief_operand_unpacker

module belief_operand_unpacker_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  input  gbp_dim_e    dim_i,
  input  logic [15:0] degree_i,
  input  logic [31:0] op_id_i,

  input  logic                 beat_valid_i,
  output logic                 beat_ready_o,
  input  operand_stream_kind_e beat_kind_i,
  input  logic [OPERAND_STREAM_WIDTH*32-1:0] beat_data_flat_i,
  input  logic [31:0]          beat_op_id_i,
  input  logic [15:0]          beat_beat_idx_i,
  input  logic                 beat_last_i,

  output logic     prior_valid_o,
  input  logic     prior_ready_i,
  output gbp_dim_e prior_dim_o,
  output logic [15:0] prior_degree_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    prior_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] prior_L_flat_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    prior_mu_old_flat_o,

  output logic     msg_valid_o,
  input  logic     msg_ready_i,
  output gbp_dim_e msg_dim_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    msg_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] msg_L_flat_o,
  output logic     msg_last_o,

  output logic     stream_error_o
);

  belief_operand_unpacker u_unpack (
    .clk_i,
    .reset_i,
    .dim_i,
    .degree_i,
    .op_id_i,
    .beat_valid_i,
    .beat_ready_o,
    .beat_kind_i,
    .beat_data_flat_i,
    .beat_op_id_i,
    .beat_beat_idx_i,
    .beat_last_i,
    .prior_valid_o,
    .prior_ready_i,
    .prior_dim_o,
    .prior_degree_o,
    .prior_eta_flat_o,
    .prior_L_flat_o,
    .prior_mu_old_flat_o,
    .msg_valid_o,
    .msg_ready_i,
    .msg_dim_o,
    .msg_eta_flat_o,
    .msg_L_flat_o,
    .msg_last_o,
    .stream_error_o
  );

endmodule
