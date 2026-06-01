// control_gbp_test_top.sv
// Test top module for control_unit_gbp

module control_gbp_test_top (
    input logic clk_i,
    input logic reset_i,
    
    // control_dispatch_if (flattened)
    input logic disp_ready,
    output logic disp_valid,
    output logic [1:0] disp_mode,
    output logic [19:0] disp_node_address,
    output logic [15:0] disp_xfer_bytes,
    output logic [7:0] disp_addr_step_bytes,
    
    // control_compute_if (flattened)
    input logic comp_done,
    input logic comp_cmd_ready,
    input logic comp_rsp_done,
    output logic comp_start,
    output logic comp_mode,
    output logic comp_cmd_valid,
    output logic comp_cmd_kind,
    output logic [12:0] comp_cmd_node_idx,
    output logic comp_cmd_iter0,
    output logic [7:0] comp_cmd_txn_id,
    
    // stream_control_if_read (flattened)
    input logic [7:0] stream_occ,
    input logic stream_afull,
    input logic stream_meta_valid,
    input logic [255:0] stream_meta_data,
    
    // Debug outputs
    output logic [4:0] debug_state
);

  import gbp_pkg::*;

  // Interface instances
  control_dispatch_if disp_if();
  control_compute_if comp_if();
  stream_control_if stream_if();
  
  // Connect interfaces to ports
  assign disp_if.ready = disp_ready;
  assign disp_valid = disp_if.valid;
  assign disp_mode = disp_if.mode;
  assign disp_node_address = disp_if.node_address;
  assign disp_xfer_bytes = disp_if.xfer_bytes;
  assign disp_addr_step_bytes = disp_if.addr_step_bytes;
  
  assign comp_if.done = comp_done;
  assign comp_if.cmd_ready = comp_cmd_ready;
  assign comp_if.rsp_done = comp_rsp_done;
  assign comp_start = comp_if.start;
  assign comp_mode = comp_if.mode;
  assign comp_cmd_valid = comp_if.cmd_valid;
  assign comp_cmd_kind = comp_if.cmd_kind;
  assign comp_cmd_node_idx = comp_if.cmd_node_idx;
  assign comp_cmd_iter0 = comp_if.cmd_iter0;
  assign comp_cmd_txn_id = comp_if.cmd_txn_id;
  
  assign stream_if.occ = stream_occ;
  assign stream_if.afull = stream_afull;
  assign stream_if.meta_valid = stream_meta_valid;
  assign stream_if.meta_data = stream_meta_data;

  // DUT
  control_unit_gbp u_dut (
    .clk_i(clk_i),
    .reset_i(reset_i),
    .control_dispatch_if(disp_if),
    .control_compute_if(comp_if),
    .stream_control_if_read(stream_if),
    .stream_control_if_write(stream_if)  // Share for write (simplified for test)
  );
  
  // Export state for debugging
  assign debug_state = u_dut.state_r;

endmodule
