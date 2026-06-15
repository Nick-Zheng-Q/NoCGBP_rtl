// cavity_builder_top.sv
// Unit test wrapper for cavity_builder

module cavity_builder_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  input  logic     start_valid_i,
  output logic     start_ready_o,
  input  gbp_dim_e dim_o_i,

  input  logic                 beat_valid_i,
  output logic                 beat_ready_o,
  input  operand_stream_kind_e beat_kind_i,
  input  logic [OPERAND_STREAM_WIDTH*32-1:0] beat_data_flat_i,
  input  logic                 beat_last_i,

  output logic     cav_valid_o,
  input  logic     cav_ready_i,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    cav_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] cav_L_flat_o,

  output logic     stream_error_o
);

  cavity_builder u_cb (
    .clk_i            (clk_i),
    .reset_i          (reset_i),
    .start_valid_i    (start_valid_i),
    .start_ready_o    (start_ready_o),
    .dim_o_i          (dim_o_i),
    .beat_valid_i     (beat_valid_i),
    .beat_ready_o     (beat_ready_o),
    .beat_kind_i      (beat_kind_i),
    .beat_data_flat_i (beat_data_flat_i),
    .beat_last_i      (beat_last_i),
    .cav_valid_o      (cav_valid_o),
    .cav_ready_i      (cav_ready_i),
    .cav_eta_flat_o   (cav_eta_flat_o),
    .cav_L_flat_o     (cav_L_flat_o),
    .stream_error_o   (stream_error_o)
  );

endmodule
