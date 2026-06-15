// ldlt_solve_core_top.sv
// Auto-generated unit-test wrapper for ldlt_solve_core

module ldlt_solve_core_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // ldlt_solve_core
  input  logic     ldlt_req_valid_i,
  output logic     ldlt_req_ready_o,
  input  gbp_dim_e ldlt_req_dim_i,
  input  logic [2:0] ldlt_req_nrhs_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0]            ldlt_req_A_flat_i,
  input  logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0]   ldlt_req_B_flat_i,
  input  logic [31:0] ldlt_req_diag_lambda_i,
  input  logic [31:0] ldlt_req_pivot_eps_i,
  input  logic        ldlt_req_regularize_en_i,

  output logic     ldlt_rsp_valid_o,
  input  logic     ldlt_rsp_ready_i,
  output gbp_dim_e ldlt_rsp_dim_o,
  output logic [2:0] ldlt_rsp_nrhs_o,
  output logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] ldlt_rsp_X_flat_o,
  output logic     ldlt_rsp_fail_o,
  output logic     ldlt_rsp_regularized_o,
  output logic     ldlt_rsp_nan_guard_o,
  output logic [31:0] ldlt_rsp_min_pivot_o
);

  ldlt_solve_core u_ldlt (
    .clk_i                (clk_i),
    .reset_i              (reset_i),
    .req_valid_i          (ldlt_req_valid_i),
    .req_ready_o          (ldlt_req_ready_o),
    .req_dim_i            (ldlt_req_dim_i),
    .req_nrhs_i           (ldlt_req_nrhs_i),
    .req_A_flat_i         (ldlt_req_A_flat_i),
    .req_B_flat_i         (ldlt_req_B_flat_i),
    .req_diag_lambda_i    (ldlt_req_diag_lambda_i),
    .req_pivot_eps_i      (ldlt_req_pivot_eps_i),
    .req_regularize_en_i  (ldlt_req_regularize_en_i),
    .rsp_valid_o          (ldlt_rsp_valid_o),
    .rsp_ready_i          (ldlt_rsp_ready_i),
    .rsp_dim_o            (ldlt_rsp_dim_o),
    .rsp_nrhs_o           (ldlt_rsp_nrhs_o),
    .rsp_X_flat_o         (ldlt_rsp_X_flat_o),
    .rsp_fail_o           (ldlt_rsp_fail_o),
    .rsp_regularized_o    (ldlt_rsp_regularized_o),
    .rsp_nan_guard_o      (ldlt_rsp_nan_guard_o),
    .rsp_min_pivot_o      (ldlt_rsp_min_pivot_o)
  );


endmodule
