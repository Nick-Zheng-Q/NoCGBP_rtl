module scratchpad_to_fpu #(
    parameter int SPM_DATA_WIDTH_P = 128,
    parameter int LANES = 16,
    localparam int FPU_DATA_WIDTH_LP = LANES * 32,
    localparam int BEATS_LP = (FPU_DATA_WIDTH_LP / SPM_DATA_WIDTH_P)
) (
    input logic clk_i,
    input logic reset_i,

    input logic spm_v_i,
    input logic [SPM_DATA_WIDTH_P-1:0] spm_data_i,

    output logic [LANES-1:0][31:0] fpu_data_o,
    output logic fpu_v_o
);

  logic [FPU_DATA_WIDTH_LP-1:0] buffer_r;
  logic [$clog2(BEATS_LP+1)-1:0] beat_cnt_r;

  for (genvar i = 0; i < LANES; i++) begin : unpack
    assign fpu_data_o[i] = buffer_r[i * 32 +: 32];
  end

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      buffer_r <= '0;
      beat_cnt_r <= '0;
      fpu_v_o <= 1'b0;
    end else begin
      fpu_v_o <= 1'b0;
      if (spm_v_i) begin
        buffer_r[beat_cnt_r * SPM_DATA_WIDTH_P +: SPM_DATA_WIDTH_P] <= spm_data_i;

        if (beat_cnt_r == (BEATS_LP-1)) begin
          beat_cnt_r <= '0;
          fpu_v_o <= 1'b1;
        end else begin
          beat_cnt_r <= beat_cnt_r + 1'b1;
        end
      end
    end
  end

endmodule
