module mic_read_top
  import gbp_pkg::*;
(
    input  logic clk,
    input  logic rst_n,

    input  logic        i_addr_valid,
    input  logic [SPM_ADDR_W-1:0] i_addr_data,
    output logic        o_addr_unqueue,

    output logic        o_data_valid,
    output logic [BEAT_BITS-1:0] o_data_data,
    input  logic        i_data_ready,

    output logic        o_spm_rd_req_valid,
    output logic [SPM_ADDR_W-1:0] o_spm_rd_req_addr,
    output logic        o_spm_rd_req_bytes,

    input  logic        i_spm_rd_rsp_valid,
    input  logic [BEAT_BITS-1:0] i_spm_rd_rsp_data
);

  logic reset_i;
  assign reset_i = ~rst_n;

  mic_spm_arbiter_if mic_to_spm_arbiter();

  assign mic_to_spm_arbiter.spm_rd_rsp_valid = i_spm_rd_rsp_valid;
  assign mic_to_spm_arbiter.spm_rd_rsp_data  = i_spm_rd_rsp_data;

  assign o_spm_rd_req_valid = mic_to_spm_arbiter.spm_rd_req_valid;
  assign o_spm_rd_req_addr  = mic_to_spm_arbiter.spm_rd_req_addr;
  assign o_spm_rd_req_bytes = mic_to_spm_arbiter.spm_rd_req_bytes;

  mic_read dut (
      .clk_i(clk),
      .reset_i(reset_i),
      .valid_i(i_addr_valid),
      .data_i(i_addr_data),
      .unqueue_o(o_addr_unqueue),
      .mic_to_spm_arbiter(mic_to_spm_arbiter),
      .ready_i(i_data_ready),
      .valid_o(o_data_valid),
      .data_o(o_data_data)
  );

endmodule
