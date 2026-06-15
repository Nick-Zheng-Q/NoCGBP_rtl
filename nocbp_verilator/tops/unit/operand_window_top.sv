// operand_window_top.sv
// Auto-generated unit-test wrapper for operand_window

module operand_window_top
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // operand_window
  input  gbp_dim_e ow_dim_i_i,
  input  gbp_dim_e ow_dim_o_i,
  input  logic     ow_start_i,
  input  logic     ow_load_valid_i,
  output logic     ow_load_ready_o,
  input  operand_stream_kind_e ow_load_kind_i,
  input  logic [OPERAND_STREAM_WIDTH*32-1:0] ow_load_data_flat_i,
  input  logic     ow_load_last_i,
  output gbp_dim_e ow_msg_dim_i_o,
  output gbp_dim_e ow_msg_dim_o_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]                 ow_msg_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0]              ow_msg_L_ii_flat_o,
  output logic [GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM*32-1:0] ow_msg_L_io_flat_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]                 ow_msg_old_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0]              ow_msg_old_L_flat_o,
  output logic     ow_msg_static_valid_o,
  input  logic     ow_clear_i
);

  operand_window u_ow (
    .clk_i              (clk_i),
    .rst_n_i            (~reset_i),
    .dim_i_i            (ow_dim_i_i),
    .dim_o_i            (ow_dim_o_i),
    .start_i            (ow_start_i),
    .load_valid_i       (ow_load_valid_i),
    .load_ready_o       (ow_load_ready_o),
    .load_kind_i        (ow_load_kind_i),
    .load_data_flat_i   (ow_load_data_flat_i),
    .load_last_i        (ow_load_last_i),
    .msg_dim_i_o        (ow_msg_dim_i_o),
    .msg_dim_o_o        (ow_msg_dim_o_o),
    .msg_eta_flat_o     (ow_msg_eta_flat_o),
    .msg_L_ii_flat_o    (ow_msg_L_ii_flat_o),
    .msg_L_io_flat_o    (ow_msg_L_io_flat_o),
    .msg_old_eta_flat_o (ow_msg_old_eta_flat_o),
    .msg_old_L_flat_o   (ow_msg_old_L_flat_o),
    .msg_static_valid_o (ow_msg_static_valid_o),
    .clear_i            (ow_clear_i)
  );


endmodule
