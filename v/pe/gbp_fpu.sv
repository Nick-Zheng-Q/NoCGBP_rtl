`include "bsg_defines.sv"
`include "bsg_fpu_defines.svh"
`include "HardFloat_consts.vi"
`include "HardFloat_specialize.vi"

module gbp_fpu
  import bsg_manycore_pkg::*;
#(
    parameter int GBP_CORE_PER_PE = 16
    , parameter int FP_EXP_WIDTH_P = 8
    , parameter int FP_MANT_WIDTH_P = 23
    , parameter int DIV_BITS_PER_ITER_P = 1
) (
    input logic clk_i,
    input logic reset_i,
    input logic [GBP_CORE_PER_PE-1:0][31:0] data_a_i,
    input logic [GBP_CORE_PER_PE-1:0][31:0] data_b_i,
    input logic [1:0] op_i,
    input logic valid_i,
    output logic [GBP_CORE_PER_PE-1:0][31:0] data_o
    output logic valid_o
);

  localparam logic [1:0] OP_ADD = 2'b00;
  localparam logic [1:0] OP_SUB = 2'b01;
  localparam logic [1:0] OP_MUL = 2'b10;
  localparam logic [1:0] OP_DIV = 2'b11;

  logic addsub_v[0:GBP_CORE_PER_PE-1];
  logic mul_v[0:GBP_CORE_PER_PE-1];
  logic div_v[0:GBP_CORE_PER_PE-1];

  logic [31:0] addsub_z[0:GBP_CORE_PER_PE-1];
  logic [31:0] mul_z[0:GBP_CORE_PER_PE-1];
  logic [31:0] div_z[0:GBP_CORE_PER_PE-1];

  for (genvar i = 0; i < GBP_CORE_PER_PE; i++) begin : lanes
    logic addsub_ready_lo;
    logic mul_ready_lo;
    logic div_ready_lo;
    logic [4:0] addsub_fflags_lo;
    logic [4:0] mul_fflags_lo;
    logic [4:0] div_fflags_lo;

    bsg_fpu_add_sub #(
          .e_p(FP_EXP_WIDTH_P)
        , .m_p(FP_MANT_WIDTH_P)
    ) addsub (
        .clk_i(clk_i)
        , .reset_i(reset_i)
        , .en_i(1'b1)

        , .v_i(valid_i & ((op_i == OP_ADD) | (op_i == OP_SUB)))
        , .a_i(data_a_i[i])
        , .b_i(data_b_i[i])
        , .sub_i(op_i == OP_SUB)
        , .ready_and_o(addsub_ready_lo)

        , .v_o(addsub_v[i])
        , .z_o(addsub_z[i])
        , .unimplemented_o(addsub_fflags_lo[4])
        , .invalid_o(addsub_fflags_lo[3])
        , .overflow_o(addsub_fflags_lo[2])
        , .underflow_o(addsub_fflags_lo[1])
        , .yumi_i(1'b1)
    );

    bsg_fpu_mul #(
          .e_p(FP_EXP_WIDTH_P)
        , .m_p(FP_MANT_WIDTH_P)
    ) mul (
        .clk_i(clk_i)
        , .reset_i(reset_i)
        , .en_i(1'b1)

        , .v_i(valid_i & (op_i == OP_MUL))
        , .a_i(data_a_i[i])
        , .b_i(data_b_i[i])
        , .ready_and_o(mul_ready_lo)

        , .v_o(mul_v[i])
        , .z_o(mul_z[i])
        , .unimplemented_o(mul_fflags_lo[4])
        , .invalid_o(mul_fflags_lo[3])
        , .overflow_o(mul_fflags_lo[2])
        , .underflow_o(mul_fflags_lo[1])
        , .yumi_i(1'b1)
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
        , .inValid(valid_i & (op_i == OP_DIV))
        , .sqrtOp(1'b0)
        , .a(data_a_i[i])
        , .b(data_b_i[i])
        , .roundingMode(3'b000)
        , .outValid(div_v[i])
        , .sqrtOpOut()
        , .out(div_z[i])
        , .exceptionFlags(div_fflags_lo)
    );

    always_ff @(posedge clk_i) begin
      if (reset_i) begin
        data_o[i] <= '0;
      end else begin
        if (div_v[i]) begin
          data_o[i] <= div_z[i];
        end else if (mul_v[i]) begin
          data_o[i] <= mul_z[i];
        end else if (addsub_v[i]) begin
          data_o[i] <= addsub_z[i];
        end
      end
    end

    assign valid_o = |div_v || |mul_v || |addsub_v;

  end

endmodule
