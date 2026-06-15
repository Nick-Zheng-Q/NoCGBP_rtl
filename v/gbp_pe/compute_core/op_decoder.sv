// op_decoder.sv
// GBP Compute Core v0.6 — operation decoder (purely combinational)

module op_decoder
  import gbp_op_pkg::*;
(
  // Command input
  input  gbp_op_e          cmd_op_i,
  input  gbp_factor_type_e cmd_factor_type_i,
  input  gbp_dim_e         cmd_dim_i_i,
  input  gbp_dim_e         cmd_dim_o_i,
  input  logic             cmd_direction_i,

  // Op decode
  output logic          is_msg_o,
  output logic          is_belief_o,
  output logic          is_relin_o,
  output logic          is_robust_o,

  // Dimension info
  output logic [2:0]    dim_i_val_o,
  output logic [2:0]    dim_o_val_o,
  output logic [4:0]    e_i_o,
  output logic [4:0]    e_o_o,
  output logic [4:0]    p_i_o,
  output logic [4:0]    p_o_o,
  output logic [2:0]    nrhs_o,

  // Validation
  output logic          legal_o,
  output logic          illegal_dim_o,
  output logic          illegal_factor_o,
  output logic          illegal_op_o
);

  // Op decode
  assign is_msg_o    = (cmd_op_i == OP_MSG_F2V);
  assign is_belief_o = (cmd_op_i == OP_BELIEF);
  assign is_relin_o  = (cmd_op_i == OP_RELIN_CHECK);
  assign is_robust_o = (cmd_op_i == OP_ROBUST_SCALE);

  // Dimension decode
  logic [2:0] d_i, d_o;
  assign d_i = dim_to_val(cmd_dim_i_i);
  assign d_o = dim_to_val(cmd_dim_o_i);

  assign dim_i_val_o = d_i;
  assign dim_o_val_o = d_o;
  assign e_i_o       = E(d_i);
  assign e_o_o       = E(d_o);
  assign p_i_o       = P(d_i);
  assign p_o_o       = P(d_o);
  assign nrhs_o      = d_i + 3'd1;

  // Validation
  logic dim_ok, factor_ok, op_ok;

  always_comb begin
    dim_ok    = 1'b1;
    factor_ok = 1'b1;
    op_ok     = 1'b1;

    // Only validate factor type for OP_MSG_F2V
    if (cmd_op_i == OP_MSG_F2V) begin
      case (cmd_factor_type_i)
        FACTOR_SCALAR: begin
          if (d_i != 1 || d_o != 1) begin factor_ok = 1'b0; dim_ok = 1'b0; end
        end
        FACTOR_SE2: begin
          if (d_i != 3 || d_o != 3) begin factor_ok = 1'b0; dim_ok = 1'b0; end
        end
        FACTOR_BA: begin
          if (cmd_direction_i == 0) begin
            if (d_i != 6 || d_o != 3) begin factor_ok = 1'b0; dim_ok = 1'b0; end
          end else begin
            if (d_i != 3 || d_o != 6) begin factor_ok = 1'b0; dim_ok = 1'b0; end
          end
        end
        FACTOR_SE3: begin
          if (d_i != 6 || d_o != 6) begin factor_ok = 1'b0; dim_ok = 1'b0; end
        end
        default: factor_ok = 1'b0;
      endcase
    end

    case (cmd_op_i)
      OP_MSG_F2V, OP_BELIEF: op_ok = 1'b1;
      OP_RELIN_CHECK:  op_ok = ENABLE_RELIN_P;
      OP_ROBUST_SCALE: op_ok = ENABLE_ROBUST_P;
      default: op_ok = 1'b0;
    endcase
  end

  assign illegal_dim_o    = ~dim_ok;
  assign illegal_factor_o = ~factor_ok;
  assign illegal_op_o     = ~op_ok;
  assign legal_o          = dim_ok & factor_ok & op_ok;

endmodule
