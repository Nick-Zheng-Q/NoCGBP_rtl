module write_stream_engine_top
  import gbp_pkg::*;
(
    input  logic clk,
    input  logic rst_n,

    // Descriptor in
    input  logic                 i_desc_valid,
    input  logic [SPM_ADDR_W-1:0] i_desc_base_addr,
    input  logic [15:0]          i_desc_word_count,
    output logic                 o_desc_ready,

    // Word in
    input  logic                 i_word_valid,
    input  logic [FP32_W-1:0]    i_word_data,
    output logic                 o_word_ready,

    // SPM write
    output logic                 o_spm_wr_valid,
    output logic [SPM_ADDR_W-1:0] o_spm_wr_addr,
    output logic [BEAT_BITS-1:0] o_spm_wr_data,
    output logic [BEAT_BYTES-1:0] o_spm_wr_wstrb,
    input  logic                 i_spm_wr_ready
);

  write_stream_engine dut (
    .clk_i            (clk),
    .rst_n_i          (rst_n),
    .desc_valid_i     (i_desc_valid),
    .desc_ready_o     (o_desc_ready),
    .desc_base_addr_i (i_desc_base_addr),
    .desc_word_count_i(i_desc_word_count),
    .word_valid_i     (i_word_valid),
    .word_ready_o     (o_word_ready),
    .word_data_i      (i_word_data),
    .spm_wr_valid_o   (o_spm_wr_valid),
    .spm_wr_ready_i   (i_spm_wr_ready),
    .spm_wr_addr_o    (o_spm_wr_addr),
    .spm_wr_data_o    (o_spm_wr_data),
    .spm_wr_wstrb_o   (o_spm_wr_wstrb)
  );

endmodule
