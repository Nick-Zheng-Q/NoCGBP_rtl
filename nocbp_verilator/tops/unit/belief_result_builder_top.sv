// belief_result_builder_top.sv
// Auto-generated unit-test wrapper for belief_result_builder

module belief_result_builder_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // belief_result_builder
  input  logic       brb_valid_i,
  output logic       brb_ready_o,
  input  gbp_dim_e   brb_dim_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    brb_acc_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] brb_acc_L_flat_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    brb_mu_old_flat_i,
  input  gbp_dim_e   brb_solve_dim_i,
  input  logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] brb_solve_X_flat_i,
  input  logic       brb_solve_fail_i,
  input  logic       brb_solve_regularized_i,
  input  logic       brb_solve_nan_guard_i,
  input  logic [31:0] brb_solve_min_pivot_i,
  output logic       brb_valid_o,
  input  logic       brb_ready_i,
  output gbp_dim_e   brb_result_dim_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    brb_result_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] brb_result_L_flat_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    brb_result_mu_flat_o,
  output logic [31:0] brb_result_residual_o,
  output logic        brb_result_fail_o
);

  belief_result_builder u_brb (
    .clk_i                   (clk_i),
    .reset_i                 (reset_i),
    .valid_i                 (brb_valid_i),
    .ready_o                 (brb_ready_o),
    .dim_i                   (brb_dim_i),
    .acc_eta_flat_i          (brb_acc_eta_flat_i),
    .acc_L_flat_i            (brb_acc_L_flat_i),
    .mu_old_flat_i           (brb_mu_old_flat_i),
    .solve_dim_i             (brb_solve_dim_i),
    .solve_X_flat_i          (brb_solve_X_flat_i),
    .solve_fail_i            (brb_solve_fail_i),
    .solve_regularized_i     (brb_solve_regularized_i),
    .solve_nan_guard_i       (brb_solve_nan_guard_i),
    .solve_min_pivot_i       (brb_solve_min_pivot_i),
    .valid_o                 (brb_valid_o),
    .ready_i                 (brb_ready_i),
    .result_dim_o            (brb_result_dim_o),
    .result_eta_flat_o       (brb_result_eta_flat_o),
    .result_L_flat_o         (brb_result_L_flat_o),
    .result_mu_flat_o        (brb_result_mu_flat_o),
    .result_residual_o       (brb_result_residual_o),
    .result_fail_o           (brb_result_fail_o),
    .result_regularized_o    (),
    .result_nan_guard_o      (),
    .result_degree_mismatch_o(),
    .result_min_pivot_o      ()
  );


endmodule
