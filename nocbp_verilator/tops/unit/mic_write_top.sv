module mic_write_top
  import gbp_pkg::*;
(
    input logic clk,
    input logic rst_n,

    input logic i_addr_valid,
    input logic [SPM_ADDR_W-1:0] i_addr_data,
    output logic o_addr_unqueue,

    input logic i_data_valid,
    input logic [BEAT_BITS-1:0] i_data_data,
    output logic o_data_unqueue,

    input logic i_spm_wr_req_ready,
    output logic o_spm_wr_req_valid,
    output logic [SPM_ADDR_W-1:0] o_spm_wr_req_addr,
    output logic [BEAT_BITS-1:0] o_spm_wr_req_data,
    output logic [WSTRB_W-1:0] o_spm_wr_req_wstrb
);

  logic reset_i;
  assign reset_i = ~rst_n;

  mic_spm_arbiter_wr_if mic_to_spm_arbiter();

  assign mic_to_spm_arbiter.spm_wr_req_ready = i_spm_wr_req_ready;
  assign o_spm_wr_req_valid = mic_to_spm_arbiter.spm_wr_req_valid;
  assign o_spm_wr_req_addr = mic_to_spm_arbiter.spm_wr_req_addr;
  assign o_spm_wr_req_data = mic_to_spm_arbiter.spm_wr_req_data;
  assign o_spm_wr_req_wstrb = mic_to_spm_arbiter.spm_wr_req_wstrb;

  mic_write dut (
      .clk_i(clk),
      .reset_i(reset_i),
      .addr_valid_i(i_addr_valid),
      .addr_data_i(i_addr_data),
      .addr_unqueue_o(o_addr_unqueue),
      .data_valid_i(i_data_valid),
      .data_data_i(i_data_data),
      .data_unqueue_o(o_data_unqueue),
      .mic_to_spm_arbiter(mic_to_spm_arbiter)
  );

endmodule
