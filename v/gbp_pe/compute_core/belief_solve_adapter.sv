// belief_solve_adapter.sv
// GBP Compute Core v0.6 — belief solve adapter

module belief_solve_adapter
  import gbp_op_pkg::*;
(
  input  logic      clk_i,
  input  logic      reset_i,

  // Input
  input  logic      valid_i,
  output logic      ready_o,
  input  gbp_dim_e  dim_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    acc_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] acc_L_flat_i,
  input  logic [31:0] diag_lambda_i,
  input  logic [31:0] pivot_eps_i,
  input  logic        regularize_en_i,

  // Output
  output logic       solve_req_valid_o,
  input  logic       solve_req_ready_i,
  output gbp_dim_e   solve_req_dim_o,
  output logic [2:0] solve_req_nrhs_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0]            solve_req_A_flat_o,
  output logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0]   solve_req_B_flat_o,
  output logic [31:0] solve_req_diag_lambda_o,
  output logic [31:0] solve_req_pivot_eps_o,
  output logic        solve_req_regularize_en_o
);

  logic [2:0] d_i;
  assign d_i = dim_to_val(dim_i);

  logic valid_r;
  gbp_dim_e dim_r;
  logic [31:0] diag_lambda_r, pivot_eps_r;
  logic regularize_en_r;
  logic [31:0] A_r [GBP_MAX_PACKED_VAR];
  logic [31:0] B_r [GBP_MAX_VAR_DIM][GBP_MAX_RHS];

  // Load registers directly from the flat input buses.  This avoids a
  // simulator scheduling issue where intermediate unpacked arrays would not
  // yet reflect new flat-bus values at the latch edge.
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      valid_r <= 1'b0;
    end else if (valid_i && ready_o) begin
      valid_r        <= 1'b1;
      dim_r          <= dim_i;
      diag_lambda_r  <= diag_lambda_i;
      pivot_eps_r    <= pivot_eps_i;
      regularize_en_r <= regularize_en_i;
      for (int i = 0; i < GBP_MAX_PACKED_VAR; i++)
        A_r[i] <= acc_L_flat_i[i*32 +: 32];
      // Load all rows unconditionally.  Rows >= d_i see zero from the
      // accumulator's output, so this is dimension-safe.
      for (int r = 0; r < GBP_MAX_VAR_DIM; r++)
        for (int c = 0; c < GBP_MAX_RHS; c++)
          B_r[r][c] <= (c == 0) ? acc_eta_flat_i[r*32 +: 32] : '0;
    end else if (valid_r && solve_req_ready_i) begin
      valid_r <= 1'b0;
    end
  end

  assign solve_req_valid_o       = valid_r;
  assign solve_req_dim_o         = dim_r;
  assign solve_req_nrhs_o        = 3'd1;
  assign solve_req_diag_lambda_o = diag_lambda_r;
  assign solve_req_pivot_eps_o   = pivot_eps_r;
  assign solve_req_regularize_en_o = regularize_en_r;
  assign ready_o                 = ~valid_r || solve_req_ready_i;

  always_comb begin
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++)
      solve_req_A_flat_o[i*32 +: 32] = A_r[i];
    for (int r = 0; r < GBP_MAX_VAR_DIM; r++)
      for (int c = 0; c < GBP_MAX_RHS; c++)
        solve_req_B_flat_o[(r * GBP_MAX_RHS + c) * 32 +: 32] = B_r[r][c];
  end

endmodule
