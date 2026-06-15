// wb_to_wse_adapter_top.sv
// Unit-test wrapper for wb_to_wse_adapter.

module wb_to_wse_adapter_top
  import gbp_pkg::*;
  import gbp_op_pkg::*;
(
  input  logic clk_i,
  input  logic reset_i,

  // Writeback record input (flattened)
  input  logic               wb_valid_i,
  output logic               wb_ready_o,
  input  logic [31:0]        wb_addr_i,
  input  logic [15:0]        wb_nwords_i,
  input  logic [3:0]         wb_kind_i,
  input  logic [GBP_MAX_WB_SCALARS*32-1:0] wb_payload_flat_i,
  input  logic               wb_fail_i,
  input  logic               wb_regularized_i,
  input  logic               wb_nan_guard_i,

  // Descriptor output
  output logic                desc_valid_o,
  input  logic                desc_ready_i,
  output logic [SPM_ADDR_W-1:0] desc_base_addr_o,
  output logic [15:0]         desc_word_count_o,

  // Word stream output
  output logic                word_valid_o,
  input  logic                word_ready_i,
  output logic [FP32_W-1:0]   word_data_o
);

  writeback_record_t wb_i;

  always_comb begin
    wb_i.addr        = wb_addr_i;
    wb_i.nwords      = wb_nwords_i;
    wb_i.kind        = wb_kind_e'(wb_kind_i);
    wb_i.fail        = wb_fail_i;
    wb_i.regularized = wb_regularized_i;
    wb_i.nan_guard   = wb_nan_guard_i;
    for (int i = 0; i < GBP_MAX_WB_SCALARS; i++)
      wb_i.payload[i] = wb_payload_flat_i[i*32 +: 32];
  end

  wb_to_wse_adapter u_dut (
    .clk_i,
    .rst_n_i (~reset_i),
    .wb_valid_i,
    .wb_ready_o,
    .wb_i,
    .desc_valid_o,
    .desc_ready_i,
    .desc_base_addr_o,
    .desc_word_count_o,
    .word_valid_o,
    .word_ready_i,
    .word_data_o
  );

endmodule
