// compute_unit_wrapper.sv
// Wrapper to adapt gbp_compute_engine to existing compute_unit interface
// This allows drop-in replacement of compute_unit with full GBP capabilities

`include "bsg_defines.sv"
`include "gbp_compute_defines.svh"

module compute_unit_wrapper
  import bsg_manycore_pkg::*;
  import gbp_pkg::*;
#(
    parameter int GBP_CORE_PER_PE = 16
    , parameter int FP_EXP_WIDTH_P = 8
    , parameter int FP_MANT_WIDTH_P = 23
    , parameter int DIV_BITS_PER_ITER_P = 1
    , parameter int STAGING_DEPTH = 128
    , parameter int MAX_DOFS = 6
    , parameter int MAX_ADJACENT = 8
) (
    input logic clk_i,
    input logic reset_i,
    
    // Command interface (from control_unit)
    input logic cmd_valid_i,
    input logic cmd_kind_i,           // 0: read/stream mode, 1: write mode (unused in wrapper)
    input logic [2:0] cmd_dofs_i,
    input logic [3:0] cmd_adj_count_i,
    input logic [3:0] cmd_msg_count_i,
    input logic [15:0] cmd_wr_xfer_bytes_i,
    output logic cmd_ready_o,
    output logic compute_done_o,
    output logic rsp_done_o,
    input logic force_persistence_stall_i,
    
    // Stream interfaces (compatible with existing)
    read_stream_if.master if_stream_if_stream,
    write_stream_if.slave if_write_stream_if_stream,
    
    // Direct data interface (legacy, maintained for compatibility)
    input logic [GBP_CORE_PER_PE-1:0][31:0] data_a_i,
    input logic [GBP_CORE_PER_PE-1:0][31:0] data_b_i,
    input logic [1:0] op_i,
    input logic valid_i,
    output logic [GBP_CORE_PER_PE-1:0][31:0] data_o,
    output logic valid_o
);

  // ========================================================================
  // Internal signals
  // ========================================================================
  
  // GBP engine command signals
  logic gbp_cmd_valid;
  logic gbp_cmd_is_factor;
  logic [7:0] gbp_cmd_node_idx;
  logic [2:0] gbp_cmd_dofs;
  logic [3:0] gbp_cmd_adj_count;
  logic [3:0] gbp_cmd_msg_count;
  logic gbp_cmd_ready;
  logic gbp_compute_done;
  logic gbp_rsp_done;
  
  // Stream signals
  logic stream_in_ready;
  logic stream_in_valid;
  logic [255:0] stream_in_data;
  logic stream_out_ready;
  logic stream_out_valid;
  logic [BEAT_BITS-1:0] stream_out_data;
  
  // Damping factor (configurable, default 0.4)
  logic [31:0] damping_factor;
  assign damping_factor = 32'h3ECCCCCD;  // 0.4 in FP32
  
  // ========================================================================
  // Command mapping from simple op to GBP command
  // ========================================================================
  
  always_comb begin
    gbp_cmd_is_factor = cmd_kind_i;
    gbp_cmd_dofs = cmd_dofs_i;
    gbp_cmd_adj_count = cmd_adj_count_i;
    gbp_cmd_msg_count = cmd_msg_count_i;
    gbp_cmd_node_idx = 8'd0;
  end
  
  // ========================================================================
  // Stream interface adaptation
  // ========================================================================
  
  // Input stream: connect directly (256-bit)
  assign stream_in_valid = if_stream_if_stream.valid;
  assign stream_in_data = if_stream_if_stream.data;
  assign if_stream_if_stream.ready = stream_in_ready;
  
  // write_stream_engine 自己已经带数据 FIFO，这里直接透传 32b 结果即可。
  // 之前“攒满 8 个 word 再发”的适配会导致 STORE_RESULT 只有一拍 valid 时永远写不出去。
  assign if_write_stream_if_stream.valid = stream_out_valid & ~force_persistence_stall_i;
  assign if_write_stream_if_stream.data = stream_out_data;
  assign stream_out_ready = if_write_stream_if_stream.ready & ~force_persistence_stall_i;
  
  // ========================================================================
  // GBP Compute Engine instantiation
  // ========================================================================
  
  gbp_compute_engine #(
    .LANES(GBP_CORE_PER_PE),
    .MAX_DOFS(MAX_DOFS),
    .MAX_ADJACENT(MAX_ADJACENT),
    .STAGING_DEPTH(STAGING_DEPTH),
    .FP_EXP_WIDTH_P(FP_EXP_WIDTH_P),
    .FP_MANT_WIDTH_P(FP_MANT_WIDTH_P),
    .DIV_BITS_PER_ITER_P(DIV_BITS_PER_ITER_P)
  ) u_gbp_engine (
    .clk_i(clk_i),
    .reset_i(reset_i),
    
    // Command interface
    .cmd_valid_i(gbp_cmd_valid),
    .cmd_is_factor_i(gbp_cmd_is_factor),
    .cmd_node_idx_i(gbp_cmd_node_idx),
    .cmd_dofs_i(gbp_cmd_dofs),
    .cmd_adj_count_i(gbp_cmd_adj_count),
    .cmd_msg_count_i(gbp_cmd_msg_count),
    .cmd_wr_xfer_bytes_i(cmd_wr_xfer_bytes_i),
    .cmd_ready_o(gbp_cmd_ready),
    .compute_done_o(gbp_compute_done),
    .rsp_done_o(gbp_rsp_done),
    
    // Stream interfaces
    .stream_in_ready(stream_in_ready),
    .stream_in_valid(stream_in_valid),
    .stream_in_data(stream_in_data),
    
    .stream_out_ready(stream_out_ready),
    .stream_out_valid(stream_out_valid),
    .stream_out_data(stream_out_data),
    
    // Configuration
    .damping_factor_i(damping_factor)
  );
  
  // ========================================================================
  // Command state machine
  // ========================================================================
  
  typedef enum logic [2:0] {
    S_IDLE,
    S_CMD_ISSUE,
    S_WAIT_DONE,
    S_RSP_WAIT
  } state_e;
  
  state_e state_r, state_n;
  
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= S_IDLE;
    end else begin
      state_r <= state_n;
    end
  end
  
  // Command handshake fix: gbp_cmd_valid is asserted combinationally based on cmd_valid_i
  // This ensures cmd_valid is visible to gbp_control_fsm in the same cycle
  assign gbp_cmd_valid = (state_r == S_IDLE) ? cmd_valid_i : 1'b0;
  
  always_comb begin
    state_n = state_r;
    cmd_ready_o = 1'b0;
    compute_done_o = 1'b0;
    rsp_done_o = 1'b0;
    
    case (state_r)
      S_IDLE: begin
        // cmd_ready_o is asserted when gbp_engine is ready
        // gbp_cmd_valid is combinationally driven by cmd_valid_i above
        cmd_ready_o = gbp_cmd_ready;
        // When both valid and ready are high, command is accepted in this cycle
        if (cmd_valid_i && gbp_cmd_ready) begin
          state_n = S_WAIT_DONE;  // Skip S_CMD_ISSUE, go directly to wait
        end
      end
      
      S_WAIT_DONE: begin
        compute_done_o = gbp_compute_done;
        if (gbp_compute_done) begin
          // 当前 gbp_compute_engine 的 compute_done_o 已经由 rsp_done_o 延后一拍得到，
          // 这里不应再等待第二次 rsp_done，否则 wrapper 会永久卡在等待态。
          rsp_done_o = 1'b1;
          state_n = S_IDLE;
        end
      end
      
      S_RSP_WAIT: begin
        rsp_done_o = gbp_rsp_done;
        if (gbp_rsp_done) begin
          state_n = S_IDLE;
        end
      end
      
      default: state_n = S_IDLE;
    endcase
  end
  
  // ========================================================================
  // Legacy data interface (for backward compatibility)
  // ========================================================================
  
  // When in SIMPLE mode, provide passthrough to legacy compute behavior
  // This is a placeholder - full implementation would instantiate original compute_unit
  // or provide equivalent simple computation
  
  assign data_o = data_a_i;  // Passthrough for now
  assign valid_o = valid_i;

endmodule
