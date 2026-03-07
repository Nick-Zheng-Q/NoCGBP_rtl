module spm_subsystem_top
  import gbp_pkg::*;
(
    input logic clk,
    input logic rst_n,

    input logic i_wr0_valid,
    input logic [SPM_ADDR_W-1:0] i_wr0_addr,
    input logic [BEAT_BITS-1:0] i_wr0_data,
    input logic [WSTRB_W-1:0] i_wr0_wstrb,
    output logic o_wr0_ready,

    input logic i_wr1_valid,
    input logic [SPM_ADDR_W-1:0] i_wr1_addr,
    input logic [BEAT_BITS-1:0] i_wr1_data,
    input logic [WSTRB_W-1:0] i_wr1_wstrb,
    output logic o_wr1_ready,

    input logic i_rd0_valid,
    input logic [SPM_ADDR_W-1:0] i_rd0_addr,
    input logic i_rd0_bytes,
    output logic o_rd0_rsp_valid,
    output logic [BEAT_BITS-1:0] o_rd0_rsp_data,

    input logic i_rd1_valid,
    input logic [SPM_ADDR_W-1:0] i_rd1_addr,
    input logic i_rd1_bytes,
    output logic o_rd1_rsp_valid,
    output logic [BEAT_BITS-1:0] o_rd1_rsp_data
);

  logic reset_i;
  assign reset_i = ~rst_n;

  mic_spm_arbiter_if rd_if[2*NB]();
  mic_spm_arbiter_wr_if wr_if[2*NB]();

  assign rd_if[0].spm_rd_req_valid = i_rd0_valid;
  assign rd_if[0].spm_rd_req_addr = i_rd0_addr;
  assign rd_if[0].spm_rd_req_bytes = i_rd0_bytes;
  assign wr_if[0].spm_wr_req_valid = i_wr0_valid;
  assign wr_if[0].spm_wr_req_addr = i_wr0_addr;
  assign wr_if[0].spm_wr_req_data = i_wr0_data;
  assign wr_if[0].spm_wr_req_wstrb = i_wr0_wstrb;

  assign rd_if[1].spm_rd_req_valid = i_rd1_valid;
  assign rd_if[1].spm_rd_req_addr = i_rd1_addr;
  assign rd_if[1].spm_rd_req_bytes = i_rd1_bytes;
  assign wr_if[1].spm_wr_req_valid = i_wr1_valid;
  assign wr_if[1].spm_wr_req_addr = i_wr1_addr;
  assign wr_if[1].spm_wr_req_data = i_wr1_data;
  assign wr_if[1].spm_wr_req_wstrb = i_wr1_wstrb;

  for (genvar i = 2; i < 2*NB; i++) begin : zero_srcs
    assign rd_if[i].spm_rd_req_valid = 1'b0;
    assign rd_if[i].spm_rd_req_addr = '0;
    assign rd_if[i].spm_rd_req_bytes = 1'b0;
    assign wr_if[i].spm_wr_req_valid = 1'b0;
    assign wr_if[i].spm_wr_req_addr = '0;
    assign wr_if[i].spm_wr_req_data = '0;
    assign wr_if[i].spm_wr_req_wstrb = '0;
  end

  assign o_wr0_ready = wr_if[0].spm_wr_req_ready;
  assign o_wr1_ready = wr_if[1].spm_wr_req_ready;
  assign o_rd0_rsp_valid = rd_if[0].spm_rd_rsp_valid;
  assign o_rd0_rsp_data = rd_if[0].spm_rd_rsp_data;
  assign o_rd1_rsp_valid = rd_if[1].spm_rd_rsp_valid;
  assign o_rd1_rsp_data = rd_if[1].spm_rd_rsp_data;

  spm_subsystem dut (
      .clk_i(clk),
      .reset_i(reset_i),
      .rd_if(rd_if),
      .wr_if(wr_if)
  );

endmodule
