module read_stream_engine_top
  import gbp_pkg::*;
(
    input logic clk,
    input logic rst_n,

    input logic i_desc_valid,
    input logic i_desc_start,
    input logic [SPM_ADDR_W-1:0] i_base_addr,
    input logic [XFER_BYTES_W-1:0] i_xfer_bytes,
    input logic [STEP_BYTES_W-1:0] i_addr_step_bytes,
    output logic o_desc_ready,

    output logic o_spm_rd_req_valid,
    output logic [SPM_ADDR_W-1:0] o_spm_rd_req_addr,
    output logic o_spm_rd_req_bytes,
    input logic i_spm_rd_rsp_valid,
    input logic [BEAT_BITS-1:0] i_spm_rd_rsp_data,

    output logic o_stream_valid,
    output logic [31:0] o_stream_data,
    input logic i_stream_ready,

    output logic [7:0] o_occ,
    output logic o_afull
);

  logic reset_i;
  assign reset_i = ~rst_n;

  stream_dispatcher_if disp_if();
  read_stream_if stream_if0();
  stream_control_if ctrl_if();
  mic_spm_arbiter_if mic_if();

  assign disp_if.valid = i_desc_valid;
  assign disp_if.data.op = OP_READ;
  assign disp_if.data.txn_id = '0;
  assign disp_if.data.start = i_desc_start;
  assign disp_if.data.base_addr = i_base_addr;
  assign disp_if.data.xfer_bytes = i_xfer_bytes;
  assign disp_if.data.addr_step_bytes = i_addr_step_bytes;
  assign disp_if.data.operand_id = '0;
  assign disp_if.data.wstrb_mode = WSTRB_FULL;
  assign disp_if.data.dim = 1'b0;
  assign disp_if.data.y_count = '0;
  assign disp_if.data.y_stride_bytes = '0;
  assign disp_if.data.addr_src = 1'b0;

  assign o_desc_ready = disp_if.ready;

  assign mic_if.spm_rd_rsp_valid = i_spm_rd_rsp_valid;
  assign mic_if.spm_rd_rsp_data = i_spm_rd_rsp_data;
  assign o_spm_rd_req_valid = mic_if.spm_rd_req_valid;
  assign o_spm_rd_req_addr = mic_if.spm_rd_req_addr;
  assign o_spm_rd_req_bytes = mic_if.spm_rd_req_bytes;

  assign o_stream_valid = stream_if0.valid;
  assign o_stream_data = stream_if0.data;
  assign stream_if0.ready = i_stream_ready;

  assign o_occ = ctrl_if.occ;
  assign o_afull = ctrl_if.afull;

  read_stream_engine dut (
      .clk_i(clk),
      .reset_i(reset_i),
      .if_stream_dispatcher_if_stream(disp_if),
      .if_stream_if_stream(stream_if0),
      .if_stream_control_if_stream(ctrl_if),
      .mic_to_spm_arbiter(mic_if)
  );

endmodule
