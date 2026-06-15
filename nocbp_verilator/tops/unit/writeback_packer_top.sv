// writeback_packer_top.sv
// Auto-generated unit-test wrapper for writeback_packer

module writeback_packer_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // writeback_packer
  input  logic          wbp_valid_i,
  output logic          wbp_ready_o,
  input  gbp_op_e       wbp_rsp_op_i,
  input  logic [31:0]   wbp_rsp_dst_addr_i,
  input  gbp_dim_e      wbp_msg_dim_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    wbp_msg_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] wbp_msg_L_flat_i,
  input  logic          wbp_msg_fail_i,
  input  gbp_dim_e      wbp_bel_dim_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    wbp_bel_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] wbp_bel_L_flat_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    wbp_bel_mu_flat_i,
  input  logic [31:0]   wbp_bel_residual_i,
  input  logic          wbp_bel_fail_i,
  output logic          wbp_wb_valid_o,
  input  logic          wbp_wb_ready_i,
  output logic [31:0]   wbp_wb_addr_o,
  output logic [15:0]   wbp_wb_nwords_o,
  output wb_kind_e      wbp_wb_kind_o,
  output logic [GBP_MAX_WB_SCALARS*32-1:0] wbp_wb_payload_flat_o,
  output logic          wbp_wb_fail_o
);

  writeback_packer u_wbp (
    .clk_i              (clk_i),
    .reset_i            (reset_i),
    .valid_i            (wbp_valid_i),
    .ready_o            (wbp_ready_o),
    .rsp_op_i           (wbp_rsp_op_i),
    .rsp_dst_addr_i     (wbp_rsp_dst_addr_i),
    .rsp_node_id_i      ('0),
    .rsp_factor_id_i    ('0),
    .msg_dim_i          (wbp_msg_dim_i),
    .msg_eta_flat_i     (wbp_msg_eta_flat_i),
    .msg_L_flat_i       (wbp_msg_L_flat_i),
    .msg_fail_i         (wbp_msg_fail_i),
    .msg_regularized_i  (1'b0),
    .msg_nan_guard_i    (1'b0),
    .msg_min_pivot_i    ('0),
    .bel_dim_i          (wbp_bel_dim_i),
    .bel_eta_flat_i     (wbp_bel_eta_flat_i),
    .bel_L_flat_i       (wbp_bel_L_flat_i),
    .bel_mu_flat_i      (wbp_bel_mu_flat_i),
    .bel_residual_i     (wbp_bel_residual_i),
    .bel_fail_i         (wbp_bel_fail_i),
    .bel_regularized_i  (1'b0),
    .bel_nan_guard_i    (1'b0),
    .bel_degree_mismatch_i(1'b0),
    .bel_min_pivot_i    ('0),
    .wb_valid_o         (wbp_wb_valid_o),
    .wb_ready_i         (wbp_wb_ready_i),
    .wb_addr_o          (wbp_wb_addr_o),
    .wb_nwords_o        (wbp_wb_nwords_o),
    .wb_kind_o          (wbp_wb_kind_o),
    .wb_payload_flat_o  (wbp_wb_payload_flat_o),
    .wb_fail_o          (wbp_wb_fail_o),
    .wb_regularized_o   (),
    .wb_nan_guard_o     ()
  );


endmodule
