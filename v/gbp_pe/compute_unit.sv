`include "bsg_defines.sv"
`include "bsg_fpu_defines.svh"
`include "HardFloat_consts.vi"
`include "HardFloat_specialize.vi"

module compute_unit
  import bsg_manycore_pkg::*;
  import gbp_pkg::*;
#(
    parameter int GBP_CORE_PER_PE = 16
    , parameter int FP_EXP_WIDTH_P = 8
    , parameter int FP_MANT_WIDTH_P = 23
    , parameter int DIV_BITS_PER_ITER_P = 1
) (
    input logic clk_i,
    input logic reset_i,
    input logic cmd_valid_i,
    input logic cmd_kind_i,
    output logic cmd_ready_o,
    output logic compute_done_o,
    output logic rsp_done_o,
    input logic force_persistence_stall_i,
    read_stream_if.master if_stream_if_stream,
    write_stream_if.slave if_write_stream_if_stream,
    input logic [GBP_CORE_PER_PE-1:0][31:0] data_a_i,
    input logic [GBP_CORE_PER_PE-1:0][31:0] data_b_i,
    input logic [1:0] op_i,
    input logic valid_i,
    output logic [GBP_CORE_PER_PE-1:0][31:0] data_o,
    output logic valid_o
);

  localparam logic [1:0] OP_ADD = 2'b00;
  localparam logic [1:0] OP_SUB = 2'b01;
  localparam logic [1:0] OP_MUL = 2'b10;
  localparam logic [1:0] OP_DIV = 2'b11;

  localparam int REC_W = FP_EXP_WIDTH_P + FP_MANT_WIDTH_P + 2;

  logic [GBP_CORE_PER_PE-1:0][REC_W-1:0] rec_a;
  logic [GBP_CORE_PER_PE-1:0][REC_W-1:0] rec_b;
  logic [GBP_CORE_PER_PE-1:0][REC_W-1:0] add_rec_z;
  logic [GBP_CORE_PER_PE-1:0][REC_W-1:0] sub_rec_z;
  logic [GBP_CORE_PER_PE-1:0][REC_W-1:0] mul_rec_z;

  logic [GBP_CORE_PER_PE-1:0][31:0] add_z;
  logic [GBP_CORE_PER_PE-1:0][31:0] sub_z;
  logic [GBP_CORE_PER_PE-1:0][31:0] mul_z;
  logic [GBP_CORE_PER_PE-1:0][31:0] div_z;

  logic [GBP_CORE_PER_PE-1:0] div_v;
  logic [GBP_CORE_PER_PE-1:0] div_done_r;
  logic [GBP_CORE_PER_PE-1:0][31:0] div_result_r;

  logic wr_pending_r;
  logic [BEAT_BITS-1:0] wr_data_r;
  logic [31:0] lane0_result_r;
  logic busy_r;
  logic done_pulse_r;
  logic valid_exec_i;
  logic valid_r;
  logic div_active_r;
  logic write_ready_lo;

  assign cmd_ready_o = ~busy_r;
  assign valid_exec_i = valid_i & busy_r;
  assign write_ready_lo = if_write_stream_if_stream.ready & ~force_persistence_stall_i;

  assign if_stream_if_stream.ready = 1'b1;

  for (genvar i = 0; i < GBP_CORE_PER_PE; i++) begin : lanes
    logic add_invalid_lo;
    logic sub_invalid_lo;
    logic mul_invalid_lo;
    logic [4:0] add_fflags_lo;
    logic [4:0] sub_fflags_lo;
    logic [4:0] mul_fflags_lo;
    logic div_ready_lo;
    logic [4:0] div_fflags_lo;

    fNToRecFN #(
        .expWidth(FP_EXP_WIDTH_P)
        , .sigWidth(FP_MANT_WIDTH_P + 1)
    ) a_to_rec (
        .in(data_a_i[i])
        , .out(rec_a[i])
    );

    fNToRecFN #(
        .expWidth(FP_EXP_WIDTH_P)
        , .sigWidth(FP_MANT_WIDTH_P + 1)
    ) b_to_rec (
        .in(data_b_i[i])
        , .out(rec_b[i])
    );

    addRecFN #(
        .expWidth(FP_EXP_WIDTH_P)
        , .sigWidth(FP_MANT_WIDTH_P + 1)
    ) add_op (
        .control(`flControl_default)
        , .subOp(1'b0)
        , .a(rec_a[i])
        , .b(rec_b[i])
        , .roundingMode(3'b000)
        , .out(add_rec_z[i])
        , .exceptionFlags(add_fflags_lo)
    );

    addRecFN #(
        .expWidth(FP_EXP_WIDTH_P)
        , .sigWidth(FP_MANT_WIDTH_P + 1)
    ) sub_op (
        .control(`flControl_default)
        , .subOp(1'b1)
        , .a(rec_a[i])
        , .b(rec_b[i])
        , .roundingMode(3'b000)
        , .out(sub_rec_z[i])
        , .exceptionFlags(sub_fflags_lo)
    );

    mulRecFN #(
        .expWidth(FP_EXP_WIDTH_P)
        , .sigWidth(FP_MANT_WIDTH_P + 1)
    ) mul_op (
        .control(`flControl_default)
        , .a(rec_a[i])
        , .b(rec_b[i])
        , .roundingMode(3'b000)
        , .out(mul_rec_z[i])
        , .exceptionFlags(mul_fflags_lo)
    );

    recFNToFN #(
        .expWidth(FP_EXP_WIDTH_P)
        , .sigWidth(FP_MANT_WIDTH_P + 1)
    ) add_to_fn (
        .in(add_rec_z[i])
        , .out(add_z[i])
    );

    recFNToFN #(
        .expWidth(FP_EXP_WIDTH_P)
        , .sigWidth(FP_MANT_WIDTH_P + 1)
    ) sub_to_fn (
        .in(sub_rec_z[i])
        , .out(sub_z[i])
    );

    recFNToFN #(
        .expWidth(FP_EXP_WIDTH_P)
        , .sigWidth(FP_MANT_WIDTH_P + 1)
    ) mul_to_fn (
        .in(mul_rec_z[i])
        , .out(mul_z[i])
    );

    divSqrtFN #(
        .expWidth(FP_EXP_WIDTH_P)
        , .sigWidth(FP_MANT_WIDTH_P + 1)
        , .bits_per_iter_p(DIV_BITS_PER_ITER_P)
    ) div (
        .nReset(~reset_i)
        , .clock(clk_i)
        , .control(`flControl_default)
        , .inReady(div_ready_lo)
        , .inValid(valid_exec_i & ~div_active_r & (op_i == OP_DIV))
        , .sqrtOp(1'b0)
        , .a(data_a_i[i])
        , .b(data_b_i[i])
        , .roundingMode(3'b000)
        , .outValid(div_v[i])
        , .sqrtOpOut()
        , .out(div_z[i])
        , .exceptionFlags(div_fflags_lo)
    );

    assign add_invalid_lo = add_fflags_lo[3];
    assign sub_invalid_lo = sub_fflags_lo[3];
    assign mul_invalid_lo = mul_fflags_lo[3];
  end

  assign valid_o = valid_r;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      wr_pending_r <= 1'b0;
      wr_data_r <= '0;
      lane0_result_r <= '0;
      busy_r <= 1'b0;
      done_pulse_r <= 1'b0;
      valid_r <= 1'b0;
      div_active_r <= 1'b0;
      div_done_r <= '0;
      div_result_r <= '0;
      data_o <= '0;
    end else begin
      done_pulse_r <= 1'b0;
      valid_r <= 1'b0;

      if (cmd_valid_i && cmd_ready_o) begin
        busy_r <= 1'b1;
      end

      if (valid_exec_i && ~div_active_r && (op_i != OP_DIV)) begin
        for (int i = 0; i < GBP_CORE_PER_PE; i++) begin
          unique case (op_i)
            OP_SUB: data_o[i] <= sub_z[i];
            OP_MUL: data_o[i] <= mul_z[i];
            default: data_o[i] <= add_z[i];
          endcase
        end
        unique case (op_i)
          OP_SUB: lane0_result_r <= sub_z[0];
          OP_MUL: lane0_result_r <= mul_z[0];
          default: lane0_result_r <= add_z[0];
        endcase
        valid_r <= 1'b1;
      end

      if (valid_exec_i && ~div_active_r && (op_i == OP_DIV)) begin
        div_active_r <= 1'b1;
        div_done_r <= '0;
      end

      if (div_active_r) begin
        for (int i = 0; i < GBP_CORE_PER_PE; i++) begin
          if (div_v[i]) begin
            div_done_r[i] <= 1'b1;
            div_result_r[i] <= div_z[i];
          end
        end

        if (&(div_done_r | div_v)) begin
          for (int i = 0; i < GBP_CORE_PER_PE; i++) begin
            data_o[i] <= div_v[i] ? div_z[i] : div_result_r[i];
          end
          lane0_result_r <= div_v[0] ? div_z[0] : div_result_r[0];
          valid_r <= 1'b1;
          div_active_r <= 1'b0;
          div_done_r <= '0;
        end
      end

      if (wr_pending_r && write_ready_lo) begin
        wr_pending_r <= 1'b0;
        busy_r <= 1'b0;
        done_pulse_r <= 1'b1;
      end

      if (!wr_pending_r && valid_o) begin
        wr_pending_r <= 1'b1;
        wr_data_r <= {{(BEAT_BITS-32){1'b0}}, lane0_result_r};
      end
    end
  end

  assign if_write_stream_if_stream.valid = wr_pending_r & ~force_persistence_stall_i;
  assign if_write_stream_if_stream.data = wr_data_r;
  assign compute_done_o = valid_r;
  assign rsp_done_o = done_pulse_r;

endmodule
