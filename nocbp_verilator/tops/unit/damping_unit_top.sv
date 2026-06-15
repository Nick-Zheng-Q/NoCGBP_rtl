// damping_unit_top.sv
// Auto-generated unit-test wrapper for damping_unit

module damping_unit_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // damping_unit
  input  logic     damp_valid_i,
  output logic     damp_ready_o,
  input  gbp_dim_e damp_dim_i,
  input  logic [31:0] damp_damping_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    damp_eta_raw_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] damp_L_raw_flat_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    damp_old_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] damp_old_L_flat_i,
  output logic     damp_valid_o,
  input  logic     damp_ready_i,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    damp_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] damp_L_flat_o
);

  damping_unit u_damp (
    .clk_i              (clk_i),
    .reset_i            (reset_i),
    .valid_i            (damp_valid_i),
    .ready_o            (damp_ready_o),
    .dim_i_i            (damp_dim_i),
    .damping_i          (damp_damping_i),
    .msg_eta_raw_flat_i (damp_eta_raw_flat_i),
    .msg_L_raw_flat_i   (damp_L_raw_flat_i),
    .old_msg_eta_flat_i (damp_old_eta_flat_i),
    .old_msg_L_flat_i   (damp_old_L_flat_i),
    .valid_o            (damp_valid_o),
    .ready_i            (damp_ready_i),
    .msg_eta_flat_o     (damp_eta_flat_o),
    .msg_L_flat_o       (damp_L_flat_o)
  );


endmodule
