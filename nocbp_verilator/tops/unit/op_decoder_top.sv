// op_decoder_top.sv
// Auto-generated unit-test wrapper for op_decoder

module op_decoder_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // op_decoder
  input  gbp_op_e          od_cmd_op_i,
  input  gbp_factor_type_e od_cmd_factor_type_i,
  input  gbp_dim_e         od_cmd_dim_i_i,
  input  gbp_dim_e         od_cmd_dim_o_i,
  input  logic             od_cmd_direction_i,
  output logic          od_is_msg_o,
  output logic          od_is_belief_o,
  output logic          od_legal_o,
  output logic          od_illegal_dim_o,
  output logic          od_illegal_factor_o,
  output logic          od_illegal_op_o,
  output logic [2:0]    od_dim_i_val_o,
  output logic [2:0]    od_dim_o_val_o,
  output logic [4:0]    od_e_i_o,
  output logic [4:0]    od_e_o_o,
  output logic [2:0]    od_nrhs_o
);

  op_decoder u_od (
    .cmd_op_i          (od_cmd_op_i),
    .cmd_factor_type_i (od_cmd_factor_type_i),
    .cmd_dim_i_i       (od_cmd_dim_i_i),
    .cmd_dim_o_i       (od_cmd_dim_o_i),
    .cmd_direction_i   (od_cmd_direction_i),
    .is_msg_o          (od_is_msg_o),
    .is_belief_o       (od_is_belief_o),
    .is_relin_o        (),
    .is_robust_o       (),
    .dim_i_val_o       (od_dim_i_val_o),
    .dim_o_val_o       (od_dim_o_val_o),
    .e_i_o             (od_e_i_o),
    .e_o_o             (od_e_o_o),
    .p_i_o             (),
    .p_o_o             (),
    .nrhs_o            (od_nrhs_o),
    .legal_o           (od_legal_o),
    .illegal_dim_o     (od_illegal_dim_o),
    .illegal_factor_o  (od_illegal_factor_o),
    .illegal_op_o      (od_illegal_op_o)
  );


endmodule
