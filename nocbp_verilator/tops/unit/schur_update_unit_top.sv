// schur_update_unit_top.sv
// Auto-generated unit-test wrapper for schur_update_unit

module schur_update_unit_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // schur_update_unit
  input  logic     sch_valid_i,
  output logic     sch_ready_o,
  input  gbp_dim_e sch_dim_i_i,
  input  gbp_dim_e sch_dim_o_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]                 sch_factor_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0]              sch_factor_L_ii_flat_i,
  input  logic [GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM*32-1:0] sch_L_io_dense_flat_i,
  input  logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0]    sch_solve_X_flat_i,

  output logic     sch_valid_o,
  input  logic     sch_ready_i,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    sch_msg_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] sch_msg_L_flat_o

);

  schur_update_unit u_sch (
    .clk_i             (clk_i),
    .reset_i           (reset_i),
    .valid_i           (sch_valid_i),
    .ready_o           (sch_ready_o),
    .dim_i_i           (sch_dim_i_i),
    .dim_o_i           (sch_dim_o_i),
    .factor_eta_flat_i (sch_factor_eta_flat_i),
    .factor_L_ii_flat_i(sch_factor_L_ii_flat_i),
    .L_io_dense_flat_i (sch_L_io_dense_flat_i),
    .solve_X_flat_i    (sch_solve_X_flat_i),
    .valid_o           (sch_valid_o),
    .ready_i           (sch_ready_i),
    .msg_eta_raw_flat_o (sch_msg_eta_flat_o),
    .msg_L_raw_flat_o   (sch_msg_L_flat_o)
  );


endmodule
