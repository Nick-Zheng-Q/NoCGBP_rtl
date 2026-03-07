module stream_dispatcher_top
  import gbp_pkg::*;
(
    input logic clk,
    input logic rst_n,

    input logic i_valid,
    input logic [1:0] i_mode,
    input logic [SPM_ADDR_W-1:0] i_node_address,
    input logic [XFER_BYTES_W-1:0] i_xfer_bytes,
    input logic [STEP_BYTES_W-1:0] i_addr_step_bytes,

    input logic i_read_ready,
    input logic i_write_ready,

    output logic o_ready,
    output logic o_read_valid,
    output logic o_write_valid,
    output logic [SPM_ADDR_W-1:0] o_read_base_addr,
    output logic [XFER_BYTES_W-1:0] o_read_xfer_bytes,
    output logic [STEP_BYTES_W-1:0] o_read_addr_step_bytes,
    output logic o_read_op,
    output logic [3:0] o_read_operand_id
);

  logic reset_i;
  assign reset_i = ~rst_n;

  control_dispatch_if control_if();
  stream_dispatcher_if read_if();
  stream_dispatcher_if write_if();

  assign control_if.valid = i_valid;
  assign control_if.mode = stream_type_e'(i_mode);
  assign control_if.node_address = i_node_address;
  assign control_if.xfer_bytes = i_xfer_bytes;
  assign control_if.addr_step_bytes = i_addr_step_bytes;
  assign o_ready = control_if.ready;

  assign read_if.ready = i_read_ready;
  assign write_if.ready = i_write_ready;

  assign o_read_valid = read_if.valid;
  assign o_write_valid = write_if.valid;
  assign o_read_base_addr = read_if.data.base_addr;
  assign o_read_xfer_bytes = read_if.data.xfer_bytes;
  assign o_read_addr_step_bytes = read_if.data.addr_step_bytes;
  assign o_read_op = read_if.data.op;
  assign o_read_operand_id = read_if.data.operand_id;

  stream_dispatcher dut (
      .clk_i(clk),
      .reset_i(reset_i),
      .control_dispatch_if(control_if),
      .stream_dispatcher_if_read(read_if),
      .stream_dispatcher_if_write(write_if)
  );

endmodule
