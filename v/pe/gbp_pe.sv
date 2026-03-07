module gbp_pe
  import bsg_manycore_pkg::*;
#(
    parameter int GBP_CORE_PER_PE = 16
    , parameter int FP_EXP_WIDTH_P = 8
    , parameter int FP_MANT_WIDTH_P = 23
    , parameter int DIV_BITS_PER_ITER_P = 1
    , parameter int SPM_ADDR_WIDTH_P = 4
) (
    input logic clk_i,
    input logic reset_i,
    input logic valid_i,
    input logic [1:0] op_i
);

  localparam int spm_data_width_lp = GBP_CORE_PER_PE * 32;
  localparam logic [SPM_ADDR_WIDTH_P-1:0] spm_addr_lp = '0;

  logic [spm_data_width_lp-1:0] spm_wdata;
  logic [spm_data_width_lp-1:0] spm_rdata;
  logic [GBP_CORE_PER_PE-1:0][31:0] spm_data;

  logic [1:0] op_delayed;
  logic fpu_valid_o;
  logic busy;
  logic issue_fire;  // 发起一次op的有效握手
  assign issue_fire = issue_v && !busy;   // issue_v 就是你的输入 valid，例如 length 或 fpu_valid_i

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      op_delayed <= '0;
      busy <= 1'b0;
    end else begin
      if (issue_fire) begin
        op_delayed <= op_i;
        busy <= 1'b1;
      end else if (fpu_valid_o) begin
        busy <= 1'b0;
      end
    end
  end

  gbp_fpu #(
      .GBP_CORE_PER_PE(GBP_CORE_PER_PE),
      .FP_EXP_WIDTH_P(FP_EXP_WIDTH_P),
      .FP_MANT_WIDTH_P(FP_MANT_WIDTH_P),
      .DIV_BITS_PER_ITER_P(DIV_BITS_PER_ITER_P)
  ) u_gbp_fpu (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .data_a_i(spm_a_data),
      .data_b_i(spm_b_data),
      .op_i(op_delayed),
      .valid_i(valid_i),
      .data_o(data_o),
      .valid_o(fpu_valid_o)
  );

  scratchpad #(
        .data_width_p(spm_data_width_lp)
      , .addr_width_p(SPM_ADDR_WIDTH_P)
      , .num_banks_p (1)
  ) u_scratchpad (
        .clk_i  (clk_i)
      , .reset_i(reset_i)

      // write port
      , .w_v_i(fpu_valid_o)
      , .w_addr_i(spm_addr_lp)
      , .w_data_i(spm_wdata)

      , .r_v_i(!busy)
      , .r_addr_i(spm_addr_lp)
      , .r_data_o(spm_rdata)
      , .r_v_o()
  );


endmodule
