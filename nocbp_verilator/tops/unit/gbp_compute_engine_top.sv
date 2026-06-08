// gbp_compute_engine_top.sv
// Unit test top-level for gbp_compute_engine

`include "bsg_defines.svh"

module gbp_compute_engine_top;

  import gbp_pkg::*;

  // Clock and reset
  logic clk = 0;
  logic rst_n = 0;
  
  always #5 clk = ~clk;
  
  // Command interface
  logic               cmd_valid;
  logic               cmd_is_factor;
  logic [7:0]         cmd_node_idx;
  logic [2:0]         cmd_dofs;
  logic [3:0]         cmd_adj_count;
  logic               cmd_ready;
  logic               compute_done;
  logic               rsp_done;
  
  // Stream interfaces
  logic               stream_in_ready;
  logic               stream_in_valid;
  logic [255:0]       stream_in_data;
  
  logic               stream_out_ready;
  logic               stream_out_valid;
  logic [31:0]        stream_out_data;
  
  // Damping configuration
  logic [31:0]        damping_factor;
  
  // DUT instantiation
  gbp_compute_engine #(
    .LANES(16),
    .MAX_DOFS(6),
    .MAX_ADJACENT(8),
    .STAGING_DEPTH(128)
  ) dut (
    .clk_i(clk),
    .rst_n_i(rst_n),
    
    .cmd_valid_i(cmd_valid),
    .cmd_is_factor_i(cmd_is_factor),
    .cmd_node_idx_i(cmd_node_idx),
    .cmd_dofs_i(cmd_dofs),
    .cmd_adj_count_i(cmd_adj_count),
    .cmd_ready_o(cmd_ready),
    .compute_done_o(compute_done),
    .rsp_done_o(rsp_done),
    
    .stream_in_ready(stream_in_ready),
    .stream_in_valid(stream_in_valid),
    .stream_in_data(stream_in_data),
    
    .stream_out_ready(stream_out_ready),
    .stream_out_valid(stream_out_valid),
    .stream_out_data(stream_out_data),
    
    .damping_factor_i(damping_factor)
  );
  
  // Testbench signals
  initial begin
    // Initialize
    cmd_valid = 0;
    cmd_is_factor = 0;
    cmd_node_idx = 0;
    cmd_dofs = 3'd2;  // dofs=2
    cmd_adj_count = 4'd2;  // 2 adjacent factors
    stream_in_valid = 0;
    stream_in_data = '0;
    stream_out_ready = 1;
    damping_factor = 32'h3E99999A;  // 0.3 in FP32
    
    // Reset
    rst_n = 0;
    repeat(10) @(posedge clk);
    rst_n = 1;
    repeat(5) @(posedge clk);
    
    // Test 1: Variable node update with dofs=2
    $display("Starting Test 1: Variable Node Update (dofs=2)");
    
    // Send command
    @(posedge clk);
    cmd_valid = 1;
    @(posedge clk);
    while (!cmd_ready) @(posedge clk);
    cmd_valid = 0;
    
    // Feed input data (prior + messages)
    // Format: 8 floats per 256-bit beat
    
    // Beat 0: prior_eta (2 floats) + prior_lam (3 floats, upper triangular) + padding
    // prior_eta = [1.0, 2.0], prior_lam = [3.0, 4.0, 5.0] (representing [[3,4],[4,5]])
    @(posedge clk);
    stream_in_valid = 1;
    stream_in_data = {
      32'h00000000,  // padding
      32'h40A00000,  // 5.0 (lam[2])
      32'h40800000,  // 4.0 (lam[1])
      32'h40400000,  // 3.0 (lam[0])
      32'h40000000,  // 2.0 (eta[1])
      32'h3F800000,  // 1.0 (eta[0])
      32'h00000000,  // padding
      32'h00000000   // padding
    };
    @(posedge clk);
    while (!stream_in_ready) @(posedge clk);
    
    // Beat 1: message 0 (5 floats)
    // msg0_eta = [0.1, 0.2], msg0_lam = [0.3, 0.4, 0.5]
    stream_in_data = {
      32'h00000000,
      32'h3F000000,  // 0.5
      32'h3ECCCCCD,  // 0.4
      32'h3E99999A,  // 0.3
      32'h3E4CCCCD,  // 0.2
      32'h3DCCCCCD,  // 0.1
      32'h00000000,
      32'h00000000
    };
    @(posedge clk);
    
    // Beat 2: message 1 (5 floats)
    // msg1_eta = [0.2, 0.3], msg1_lam = [0.4, 0.5, 0.6]
    stream_in_data = {
      32'h00000000,
      32'h3F19999A,  // 0.6
      32'h3F000000,  // 0.5
      32'h3ECCCCCD,  // 0.4
      32'h3E99999A,  // 0.3
      32'h3E4CCCCD,  // 0.2
      32'h00000000,
      32'h00000000
    };
    @(posedge clk);
    stream_in_valid = 0;
    
    // Wait for computation to complete
    $display("Waiting for computation...");
    wait(rsp_done);
    $display("Computation done!");
    
    repeat(10) @(posedge clk);
    
    // Check output
    if (stream_out_valid) begin
      $display("Output result: 0x%08x (%f)", stream_out_data, $bitstoreal(stream_out_data));
    end
    
    repeat(10) @(posedge clk);
    
    // Test 2: Matrix Add
    $display("Starting Test 2: Matrix Add");
    
    // TODO: Add more comprehensive tests
    
    $display("All tests completed!");
    $finish;
  end
  
  // Monitor
  always @(posedge clk) begin
    if (rst_n) begin
      if (compute_done) $display("[%0t] Compute done pulse", $time);
      if (rsp_done) $display("[%0t] Response done", $time);
    end
  end

endmodule
