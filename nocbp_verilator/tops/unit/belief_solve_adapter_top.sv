// belief_solve_adapter_top.sv
// Auto-generated unit-test wrapper for belief_solve_adapter

module belief_solve_adapter_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // belief_solve_adapter
  input  logic      bsa_valid_i,
  output logic      bsa_ready_o,
  input  gbp_dim_e  bsa_dim_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    bsa_acc_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] bsa_acc_L_flat_i,
  input  logic [31:0] bsa_diag_lambda_i,
  input  logic [31:0] bsa_pivot_eps_i,
  input  logic        bsa_regularize_en_i,
  output logic       bsa_req_valid_o,
  input  logic       bsa_req_ready_i,
  output gbp_dim_e   bsa_req_dim_o,
  output logic [2:0] bsa_req_nrhs_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0]          bsa_req_A_flat_o,
  output logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] bsa_req_B_flat_o,
  output logic [31:0] bsa_req_diag_lambda_o,
  output logic [31:0] bsa_req_pivot_eps_o,
  output logic        bsa_req_regularize_en_o
);

  belief_solve_adapter u_bsa (
    .clk_i                   (clk_i),
    .reset_i                 (reset_i),
    .valid_i                 (bsa_valid_i),
    .ready_o                 (bsa_ready_o),
    .dim_i                   (bsa_dim_i),
    .acc_eta_flat_i          (bsa_acc_eta_flat_i),
    .acc_L_flat_i            (bsa_acc_L_flat_i),
    .diag_lambda_i           (bsa_diag_lambda_i),
    .pivot_eps_i             (bsa_pivot_eps_i),
    .regularize_en_i         (bsa_regularize_en_i),
    .solve_req_valid_o       (bsa_req_valid_o),
    .solve_req_ready_i       (bsa_req_ready_i),
    .solve_req_dim_o         (bsa_req_dim_o),
    .solve_req_nrhs_o        (bsa_req_nrhs_o),
    .solve_req_A_flat_o      (bsa_req_A_flat_o),
    .solve_req_B_flat_o      (bsa_req_B_flat_o),
    .solve_req_diag_lambda_o (bsa_req_diag_lambda_o),
    .solve_req_pivot_eps_o   (bsa_req_pivot_eps_o),
    .solve_req_regularize_en_o(bsa_req_regularize_en_o)
  );


endmodule
