// staging_buffer.sv
// Data staging buffer for GBP compute engine
// Supports parallel SIMD access and stream I/O

`include "bsg_defines.sv"

module staging_buffer #(
    parameter int DEPTH = 64,           // Number of floats (32-bit)
    parameter int LANES = 16,           // SIMD width
    parameter int ADDR_W = $clog2(DEPTH)
)(
    input  logic clk_i,
    input  logic reset_i,
    
    // Stream write interface (256-bit = 8 floats per beat)
    input  logic               stream_wr_valid,
    input  logic [255:0]       stream_wr_data,     // 8 x 32-bit floats
    input  logic [ADDR_W-1:0]  stream_wr_addr,     // Base address (0-7 alignment)
    output logic               stream_wr_ready,
    
    // Stream read interface (256-bit = 8 floats per beat)
    input  logic               stream_rd_valid,
    input  logic [ADDR_W-1:0]  stream_rd_addr,
    output logic [255:0]       stream_rd_data,
    output logic               stream_rd_ready,
    
    // SIMD read interface (per-lane independent address)
    input  logic [LANES-1:0][ADDR_W-1:0] simd_rd_addr_a,
    input  logic [LANES-1:0][ADDR_W-1:0] simd_rd_addr_b,
    output logic [LANES-1:0][31:0]       simd_rd_data_a,
    output logic [LANES-1:0][31:0]       simd_rd_data_b,
    
    // SIMD write interface (per-lane with individual valid)
    input  logic [LANES-1:0]             simd_wr_valid,
    input  logic [LANES-1:0][ADDR_W-1:0] simd_wr_addr,
    input  logic [LANES-1:0][31:0]       simd_wr_data,
    
    // Status
    output logic [ADDR_W:0]              occupancy_o
);

  // ============================================================
  // Memory array: multi-ported register file
  // Each entry is 32-bit float
  // Supports:
  //   - 1 stream write (8 words)
  //   - 1 stream read (8 words)
  //   - 2*LANES SIMD reads
  //   - LANES SIMD writes
  // ============================================================
  
  logic [31:0] mem [DEPTH];
  
  // Stream write: 8 consecutive words
  always_ff @(posedge clk_i) begin
    if (stream_wr_valid && stream_wr_ready) begin
      mem[stream_wr_addr + 0] <= stream_wr_data[31:0];
      mem[stream_wr_addr + 1] <= stream_wr_data[63:32];
      mem[stream_wr_addr + 2] <= stream_wr_data[95:64];
      mem[stream_wr_addr + 3] <= stream_wr_data[127:96];
      mem[stream_wr_addr + 4] <= stream_wr_data[159:128];
      mem[stream_wr_addr + 5] <= stream_wr_data[191:160];
      mem[stream_wr_addr + 6] <= stream_wr_data[223:192];
      mem[stream_wr_addr + 7] <= stream_wr_data[255:224];
    end
  end
  
  // SIMD writes: each lane can write independently
  always_ff @(posedge clk_i) begin
    for (int i = 0; i < LANES; i++) begin
      if (simd_wr_valid[i]) begin
        mem[simd_wr_addr[i]] <= simd_wr_data[i];
      end
    end
  end
  
  // Stream read: 8 consecutive words (async read)
  assign stream_rd_data = {mem[stream_rd_addr + 7],
                           mem[stream_rd_addr + 6],
                           mem[stream_rd_addr + 5],
                           mem[stream_rd_addr + 4],
                           mem[stream_rd_addr + 3],
                           mem[stream_rd_addr + 2],
                           mem[stream_rd_addr + 1],
                           mem[stream_rd_addr + 0]};
  
  // SIMD reads: each lane has independent async read
  for (genvar i = 0; i < LANES; i++) begin : g_simd_rd
    assign simd_rd_data_a[i] = mem[simd_rd_addr_a[i]];
    assign simd_rd_data_b[i] = mem[simd_rd_addr_b[i]];
  end
  
  // Ready signals (always ready for now, can add backpressure later)
  assign stream_wr_ready = 1'b1;
  assign stream_rd_ready = 1'b1;
  
  // Occupancy tracking (simplified, just for debugging)
  logic [ADDR_W:0] occupancy_r;
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      occupancy_r <= '0;
    end else begin
      // Simplified tracking - actual implementation would track valid entries
      // This is a placeholder for more sophisticated management
      occupancy_r <= occupancy_r;  // Placeholder
    end
  end
  assign occupancy_o = occupancy_r;

endmodule
