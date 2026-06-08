module read_stream_engine_top
  import gbp_pkg::*;
(
    input  logic clk,
    input  logic rst_n,

    // Descriptor in
    input  logic                 i_desc_valid,
    input  logic [SPM_ADDR_W-1:0] i_desc_base_addr,
    input  logic [15:0]          i_desc_word_count,
    input  logic                 i_desc_is_staging,
    output logic                 o_desc_ready,

    // Beat out
    output logic                 o_beat_valid,
    output logic [BEAT_BITS-1:0] o_beat_data,
    input  logic                 i_beat_ready,

    // SPM read
    output logic                 o_spm_rd_valid,
    output logic [SPM_ADDR_W-1:0] o_spm_rd_addr,
    input  logic                 i_spm_rd_ready,
    input  logic [BEAT_BITS-1:0] i_spm_rd_data
);

  read_stream_engine dut (
    .clk           (clk),
    .rst_n         (rst_n),
    .desc_valid    (i_desc_valid),
    .desc_ready    (o_desc_ready),
    .desc_base_addr(i_desc_base_addr),
    .desc_word_count(i_desc_word_count),
    .desc_is_staging(i_desc_is_staging),
    .beat_valid    (o_beat_valid),
    .beat_ready    (i_beat_ready),
    .beat_data     (o_beat_data),
    .spm_rd_valid  (o_spm_rd_valid),
    .spm_rd_ready  (i_spm_rd_ready),
    .spm_rd_addr   (o_spm_rd_addr),
    .spm_rd_data   (i_spm_rd_data)
  );

endmodule
