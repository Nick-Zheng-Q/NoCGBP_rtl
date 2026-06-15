// rhs_builder_for_message.sv
// GBP Compute Core v0.6 — RHS builder for Schur complement (purely combinational)

module rhs_builder_for_message
  import gbp_op_pkg::*;
(
  input  gbp_dim_e dim_i_i,
  input  gbp_dim_e dim_o_i,

  // L_io_dense: dim_i × dim_o, row-major, flattened
  input  logic [GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM*32-1:0] L_io_dense_flat_i,

  // cav_eta: dim_o entries, flattened
  input  logic [GBP_MAX_VAR_DIM*32-1:0] cav_eta_flat_i,

  output logic [2:0] nrhs_o,

  // B: dim_o × (dim_i+1), flattened
  output logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] B_flat_o
);

  logic [2:0] d_i, d_o;
  assign d_i = dim_to_val(dim_i_i);
  assign d_o = dim_to_val(dim_o_i);
  assign nrhs_o = d_i + 3'd1;

  // Unpack inputs
  logic [31:0] L_io [GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM];
  logic [31:0] cav_eta [GBP_MAX_VAR_DIM];
  logic [31:0] B [GBP_MAX_VAR_DIM][GBP_MAX_RHS];

  always_comb begin
    // Unpack L_io
    for (int i = 0; i < GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM; i++) begin
      L_io[i] = L_io_dense_flat_i[i*32 +: 32];
    end
    // Unpack cav_eta
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
      cav_eta[i] = cav_eta_flat_i[i*32 +: 32];
    end

    // Zero B
    for (int r = 0; r < GBP_MAX_VAR_DIM; r++) begin
      for (int c = 0; c < GBP_MAX_RHS; c++) begin
        B[r][c] = '0;
      end
    end

    // B[:, 0:dim_i-1] = transpose(L_io)
    for (int r = 0; r < GBP_MAX_VAR_DIM; r++) begin
      for (int c = 0; c < GBP_MAX_VAR_DIM; c++) begin
        if (r < d_o && c < d_i) begin
          B[r][c] = L_io[c * GBP_MAX_VAR_DIM + r];  // transpose
        end
      end
    end

    // B[:, dim_i] = cav_eta
    for (int r = 0; r < GBP_MAX_VAR_DIM; r++) begin
      if (r < d_o) begin
        B[r][d_i] = cav_eta[r];
      end
    end

    // Pack B output
    for (int r = 0; r < GBP_MAX_VAR_DIM; r++) begin
      for (int c = 0; c < GBP_MAX_RHS; c++) begin
        B_flat_o[(r * GBP_MAX_RHS + c) * 32 +: 32] = B[r][c];
      end
    end
  end

endmodule
