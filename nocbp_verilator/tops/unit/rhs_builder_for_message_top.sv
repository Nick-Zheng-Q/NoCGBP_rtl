// rhs_builder_for_message_top.sv
// Auto-generated unit-test wrapper for rhs_builder_for_message

module rhs_builder_for_message_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // rhs_builder
  input  gbp_dim_e  rhs_dim_i_i,
  input  gbp_dim_e  rhs_dim_o_i,
  input  logic [GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM*32-1:0] rhs_L_io_flat_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]                 rhs_cav_eta_flat_i,
  output logic [2:0] rhs_nrhs_o,
  output logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] rhs_B_flat_o
);

  rhs_builder_for_message u_rhs (
    .dim_i_i          (rhs_dim_i_i),
    .dim_o_i          (rhs_dim_o_i),
    .L_io_dense_flat_i(rhs_L_io_flat_i),
    .cav_eta_flat_i   (rhs_cav_eta_flat_i),
    .nrhs_o           (rhs_nrhs_o),
    .B_flat_o         (rhs_B_flat_o)
  );


endmodule
