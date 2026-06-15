// gbp_compute_core_top.sv
// Unit-test wrapper for gbp_compute_core.
// Flattens unpacked-struct ports so the C++ testbench can drive/read them.

module gbp_compute_core_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // Command channel (flattened)
  input  logic        cmd_valid_i,
  output logic        cmd_ready_o,
  input  logic [3:0]  cmd_op_i,
  input  logic [1:0]  cmd_factor_type_i,
  input  logic [1:0]  cmd_dim_i_i,
  input  logic [1:0]  cmd_dim_o_i,
  input  logic        cmd_direction_i,
  input  logic        cmd_ctx_id_i,
  input  logic [31:0] cmd_op_id_i,
  input  logic [31:0] cmd_node_id_i,
  input  logic [31:0] cmd_factor_id_i,
  input  logic [31:0] cmd_dst_addr_i,
  input  logic [31:0] cmd_aux_addr_i,
  input  logic [31:0] cmd_damping_i,
  input  logic [31:0] cmd_diag_lambda_i,
  input  logic [31:0] cmd_pivot_eps_i,
  input  logic        cmd_regularize_en_i,
  input  logic [15:0] cmd_degree_i,

  // Operand stream channel (flattened)
  input  logic        operand_valid_i,
  output logic        operand_ready_o,
  input  logic [3:0]  operand_kind_i,
  input  logic        operand_ctx_id_i,
  input  logic [31:0] operand_op_id_i,
  input  logic [15:0] operand_beat_idx_i,
  input  logic        operand_last_i,
  input  logic [OPERAND_STREAM_WIDTH*32-1:0] operand_data_flat_i,

  // Response channel (flattened)
  output logic        rsp_valid_o,
  input  logic        rsp_ready_i,
  output logic [3:0]  rsp_op_o,
  output logic        rsp_ctx_id_o,
  output logic [31:0] rsp_op_id_o,
  output logic [31:0] rsp_dst_addr_o,
  output logic [31:0] rsp_aux_addr_o,
  output logic [31:0] rsp_node_id_o,
  output logic [31:0] rsp_factor_id_o,
  output logic        rsp_fail_o,
  output logic        rsp_regularized_o,
  output logic        rsp_nan_guard_o,
  output logic        rsp_degree_mismatch_o,
  output logic        rsp_stream_error_o,
  output logic [31:0] rsp_min_pivot_o,

  // Message result fields
  output logic [1:0]                       rsp_msg_dim_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    rsp_msg_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] rsp_msg_L_flat_o,
  output logic                             rsp_msg_fail_o,
  output logic                             rsp_msg_regularized_o,
  output logic                             rsp_msg_nan_guard_o,
  output logic [31:0]                      rsp_msg_min_pivot_o,

  // Belief result fields
  output logic [1:0]                       rsp_bel_dim_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    rsp_bel_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] rsp_bel_L_flat_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    rsp_bel_mu_flat_o,
  output logic [31:0]                      rsp_bel_residual_o,
  output logic                             rsp_bel_fail_o,
  output logic                             rsp_bel_regularized_o,
  output logic                             rsp_bel_nan_guard_o,
  output logic                             rsp_bel_degree_mismatch_o,
  output logic [31:0]                      rsp_bel_min_pivot_o
);

  // ----------------------------------------------------------
  // Reconstruct command struct from flattened inputs
  // ----------------------------------------------------------
  gbp_core_req_t cmd;
  always_comb begin
    cmd.op            = gbp_op_e'(cmd_op_i);
    cmd.factor_type   = gbp_factor_type_e'(cmd_factor_type_i);
    cmd.dim_i         = gbp_dim_e'(cmd_dim_i_i);
    cmd.dim_o         = gbp_dim_e'(cmd_dim_o_i);
    cmd.direction     = cmd_direction_i;
    cmd.ctx_id        = cmd_ctx_id_i;
    cmd.op_id         = cmd_op_id_i;
    cmd.node_id       = cmd_node_id_i;
    cmd.factor_id     = cmd_factor_id_i;
    cmd.dst_addr      = cmd_dst_addr_i;
    cmd.aux_addr      = cmd_aux_addr_i;
    cmd.damping       = cmd_damping_i;
    cmd.diag_lambda   = cmd_diag_lambda_i;
    cmd.pivot_eps     = cmd_pivot_eps_i;
    cmd.regularize_en = cmd_regularize_en_i;
    cmd.degree        = cmd_degree_i;
  end

  // ----------------------------------------------------------
  // Reconstruct operand stream beat from flattened inputs
  // ----------------------------------------------------------
  operand_stream_beat_t operand;
  always_comb begin
    operand.kind     = operand_stream_kind_e'(operand_kind_i);
    operand.ctx_id   = operand_ctx_id_i;
    operand.op_id    = operand_op_id_i;
    operand.beat_idx = operand_beat_idx_i;
    operand.last     = operand_last_i;
    for (int i = 0; i < OPERAND_STREAM_WIDTH; i++)
      operand.data[i] = operand_data_flat_i[i*32 +: 32];
  end

  // ----------------------------------------------------------
  // DUT
  // ----------------------------------------------------------
  gbp_core_rsp_t rsp;

  gbp_compute_core u_core (
    .clk_i,
    .reset_i,
    .cmd_valid_i,
    .cmd_ready_o,
    .cmd_i(cmd),
    .operand_valid_i,
    .operand_ready_o,
    .operand_i(operand),
    .rsp_valid_o,
    .rsp_ready_i,
    .rsp_o(rsp)
  );

  // ----------------------------------------------------------
  // Flatten response struct to scalar outputs
  // ----------------------------------------------------------
  assign rsp_op_o               = rsp.op;
  assign rsp_ctx_id_o           = rsp.ctx_id;
  assign rsp_op_id_o            = rsp.op_id;
  assign rsp_dst_addr_o         = rsp.dst_addr;
  assign rsp_aux_addr_o         = rsp.aux_addr;
  assign rsp_node_id_o          = rsp.node_id;
  assign rsp_factor_id_o        = rsp.factor_id;
  assign rsp_fail_o             = rsp.fail;
  assign rsp_regularized_o      = rsp.regularized;
  assign rsp_nan_guard_o        = rsp.nan_guard;
  assign rsp_degree_mismatch_o  = rsp.degree_mismatch;
  assign rsp_stream_error_o     = rsp.stream_error;
  assign rsp_min_pivot_o        = rsp.min_pivot;

  assign rsp_msg_dim_o          = rsp.msg_result.dim;
  assign rsp_msg_fail_o         = rsp.msg_result.fail;
  assign rsp_msg_regularized_o  = rsp.msg_result.regularized;
  assign rsp_msg_nan_guard_o    = rsp.msg_result.nan_guard;
  assign rsp_msg_min_pivot_o    = rsp.msg_result.min_pivot;

  assign rsp_bel_dim_o          = rsp.belief_result.dim;
  assign rsp_bel_fail_o         = rsp.belief_result.fail;
  assign rsp_bel_regularized_o  = rsp.belief_result.regularized;
  assign rsp_bel_nan_guard_o    = rsp.belief_result.nan_guard;
  assign rsp_bel_degree_mismatch_o = rsp.belief_result.degree_mismatch;
  assign rsp_bel_min_pivot_o    = rsp.belief_result.min_pivot;
  assign rsp_bel_residual_o     = rsp.belief_result.residual;

  generate
    genvar i;
    for (i = 0; i < GBP_MAX_VAR_DIM; i++) begin : gen_eta
      assign rsp_msg_eta_flat_o[i*32 +: 32] = rsp.msg_result.eta[i];
      assign rsp_bel_eta_flat_o[i*32 +: 32] = rsp.belief_result.eta[i];
      assign rsp_bel_mu_flat_o[i*32 +: 32]  = rsp.belief_result.mu[i];
    end
    for (i = 0; i < GBP_MAX_PACKED_VAR; i++) begin : gen_L
      assign rsp_msg_L_flat_o[i*32 +: 32] = rsp.msg_result.L_packed[i];
      assign rsp_bel_L_flat_o[i*32 +: 32] = rsp.belief_result.L_packed[i];
    end
  endgenerate

endmodule
