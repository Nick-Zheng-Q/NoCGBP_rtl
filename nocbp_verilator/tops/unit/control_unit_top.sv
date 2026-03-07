module control_unit_top
  import gbp_pkg::*;
(
    input logic clk,
    input logic rst_n,

    input logic i_dispatch_ready,
    output logic o_dispatch_valid,
    output logic [1:0] o_dispatch_mode,
    output logic [SPM_ADDR_W-1:0] o_dispatch_node_address,
    output logic [XFER_BYTES_W-1:0] o_dispatch_xfer_bytes,
    output logic [STEP_BYTES_W-1:0] o_dispatch_addr_step_bytes,

    input logic i_compute_done,
    input logic i_compute_cmd_ready,
    input logic i_compute_rsp_done,
    output logic o_compute_start,
    output logic o_compute_mode,
    output logic o_compute_cmd_valid,
    output logic o_compute_cmd_kind,
    output logic [NODE_ID_W-1:0] o_compute_cmd_node_idx,
    output logic o_compute_cmd_iter0,
    output logic [TXN_ID_W-1:0] o_compute_cmd_txn_id,

    input logic [7:0] i_read_occ,
    input logic i_read_afull,
    input logic i_read_meta_valid,
    input logic [BEAT_BITS-1:0] i_read_meta_data,
    input logic [7:0] i_write_occ,
    input logic i_write_afull
);

  logic reset_i;
  assign reset_i = ~rst_n;

  control_dispatch_if control_dispatch_if0();
  control_compute_if control_compute_if0();
  stream_control_if stream_control_if_read0();
  stream_control_if stream_control_if_write0();

  assign control_dispatch_if0.ready = i_dispatch_ready;
  assign o_dispatch_valid = control_dispatch_if0.valid;
  assign o_dispatch_mode = control_dispatch_if0.mode;
  assign o_dispatch_node_address = control_dispatch_if0.node_address;
  assign o_dispatch_xfer_bytes = control_dispatch_if0.xfer_bytes;
  assign o_dispatch_addr_step_bytes = control_dispatch_if0.addr_step_bytes;

  assign control_compute_if0.done = i_compute_done;
  assign control_compute_if0.cmd_ready = i_compute_cmd_ready;
  assign control_compute_if0.rsp_done = i_compute_rsp_done;
  assign o_compute_start = control_compute_if0.start;
  assign o_compute_mode = control_compute_if0.mode;
  assign o_compute_cmd_valid = control_compute_if0.cmd_valid;
  assign o_compute_cmd_kind = control_compute_if0.cmd_kind;
  assign o_compute_cmd_node_idx = control_compute_if0.cmd_node_idx;
  assign o_compute_cmd_iter0 = control_compute_if0.cmd_iter0;
  assign o_compute_cmd_txn_id = control_compute_if0.cmd_txn_id;

  assign stream_control_if_read0.occ = i_read_occ;
  assign stream_control_if_read0.afull = i_read_afull;
  assign stream_control_if_read0.meta_valid = i_read_meta_valid;
  assign stream_control_if_read0.meta_data = i_read_meta_data;
  assign stream_control_if_write0.occ = i_write_occ;
  assign stream_control_if_write0.afull = i_write_afull;
  assign stream_control_if_write0.meta_valid = 1'b0;
  assign stream_control_if_write0.meta_data = '0;

  control_unit dut (
      .clk_i(clk),
      .reset_i(reset_i),
      .control_dispatch_if(control_dispatch_if0),
      .control_compute_if(control_compute_if0),
      .stream_control_if_read(stream_control_if_read0),
      .stream_control_if_write(stream_control_if_write0)
  );

endmodule
