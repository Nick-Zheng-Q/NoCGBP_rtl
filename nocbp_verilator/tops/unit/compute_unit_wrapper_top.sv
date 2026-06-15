// compute_unit_wrapper_top.sv
// Unit-test wrapper for compute_unit_wrapper.
// compute_unit_wrapper now has flattened scalar/vector ports, so this top
// is just a thin pass-through to keep the existing C++ testbench interface.

module compute_unit_wrapper_top
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
  input  logic [15:0] cmd_degree_i,
  input  logic [31:0] cmd_damping_i,
  input  logic [31:0] cmd_diag_lambda_i,
  input  logic [31:0] cmd_pivot_eps_i,
  input  logic        cmd_regularize_en_i,

  // Operand descriptors (flattened arrays)
  input  logic        cmd_operand_desc_valid_i [8],
  input  logic [3:0]  cmd_operand_desc_kind_i  [8],
  input  logic [31:0] cmd_operand_desc_base_addr_i [8],
  input  logic [15:0] cmd_operand_desc_nbeats_i [8],

  // Read request channel (flattened)
  output logic        rd_req_valid_o,
  input  logic        rd_req_ready_i,
  output logic [31:0] rd_req_op_id_o,
  output logic [3:0]  rd_req_op_o,
  output logic [31:0] rd_req_base_addr_o,
  output logic [15:0] rd_req_nbeats_o,
  output logic [3:0]  rd_req_kind_o,

  // Operand stream channel (flattened)
  input  logic        operand_valid_i,
  output logic        operand_ready_o,
  input  logic [3:0]  operand_kind_i,
  input  logic        operand_ctx_id_i,
  input  logic [31:0] operand_op_id_i,
  input  logic [15:0] operand_beat_idx_i,
  input  logic        operand_last_i,
  input  logic [OPERAND_STREAM_WIDTH*32-1:0] operand_data_flat_i,

  // Writeback channel (flattened)
  output logic        wb_valid_o,
  input  logic        wb_ready_i,
  output logic [31:0] wb_addr_o,
  output logic [15:0] wb_nwords_o,
  output logic [3:0]  wb_kind_o,
  output logic [GBP_MAX_WB_SCALARS*32-1:0] wb_payload_flat_o,
  output logic        wb_fail_o,
  output logic        wb_regularized_o,
  output logic        wb_nan_guard_o,

  // Done channel (flattened)
  output logic        done_valid_o,
  input  logic        done_ready_i,
  output logic [31:0] done_node_id_o,
  output logic [31:0] done_factor_id_o,
  output logic [3:0]  done_op_o,
  output logic        done_ctx_id_o,
  output logic        done_success_o,
  output logic        done_fail_o,
  output logic        done_regularized_o,
  output logic        done_nan_guard_o,
  output logic        done_degree_mismatch_o,
  output logic        done_stream_error_o,
  output logic [31:0] done_residual_o,
  output logic [31:0] done_min_pivot_o
);

  // Convert unpacked descriptor arrays into the vectors expected by the DUT.
  logic [7:0]      desc_valid_v;
  logic [8*4-1:0]  desc_kind_v;
  logic [8*32-1:0] desc_base_addr_v;
  logic [8*16-1:0] desc_nbeats_v;

  always_comb begin
    for (int i = 0; i < 8; i++) begin
      desc_valid_v[i]            = cmd_operand_desc_valid_i[i];
      desc_kind_v[i*4 +: 4]      = cmd_operand_desc_kind_i[i];
      desc_base_addr_v[i*32 +: 32] = cmd_operand_desc_base_addr_i[i];
      desc_nbeats_v[i*16 +: 16]  = cmd_operand_desc_nbeats_i[i];
    end
  end

  compute_unit_wrapper u_wrapper (
    .clk_i,
    .reset_i,

    .cmd_valid_i,
    .cmd_ready_o,
    .cmd_op_i,
    .cmd_factor_type_i,
    .cmd_dim_i_i,
    .cmd_dim_o_i,
    .cmd_direction_i,
    .cmd_ctx_id_i,
    .cmd_op_id_i,
    .cmd_node_id_i,
    .cmd_factor_id_i,
    .cmd_dst_addr_i,
    .cmd_aux_addr_i,
    .cmd_degree_i,
    .cmd_damping_i,
    .cmd_diag_lambda_i,
    .cmd_pivot_eps_i,
    .cmd_regularize_en_i,
    .cmd_operand_desc_valid_i (desc_valid_v),
    .cmd_operand_desc_kind_i  (desc_kind_v),
    .cmd_operand_desc_base_addr_i (desc_base_addr_v),
    .cmd_operand_desc_nbeats_i (desc_nbeats_v),

    .rd_req_valid_o,
    .rd_req_ready_i,
    .rd_req_op_id_o,
    .rd_req_op_o,
    .rd_req_base_addr_o,
    .rd_req_nbeats_o,
    .rd_req_kind_o,

    .operand_valid_i,
    .operand_ready_o,
    .operand_kind_i,
    .operand_ctx_id_i,
    .operand_op_id_i,
    .operand_beat_idx_i,
    .operand_last_i,
    .operand_data_flat_i,

    .wb_valid_o,
    .wb_ready_i,
    .wb_addr_o,
    .wb_nwords_o,
    .wb_kind_o,
    .wb_payload_flat_o,
    .wb_fail_o,
    .wb_regularized_o,
    .wb_nan_guard_o,

    .done_valid_o,
    .done_ready_i,
    .done_node_id_o,
    .done_factor_id_o,
    .done_op_o,
    .done_ctx_id_o,
    .done_success_o,
    .done_fail_o,
    .done_regularized_o,
    .done_nan_guard_o,
    .done_degree_mismatch_o,
    .done_stream_error_o,
    .done_residual_o,
    .done_min_pivot_o
  );

endmodule
