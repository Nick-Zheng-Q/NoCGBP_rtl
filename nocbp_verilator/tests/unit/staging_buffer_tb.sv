// staging_buffer_tb.sv
// Unit test for staging_buffer module

`timescale 1ns/1ps

module staging_buffer_tb;

  // Parameters
  localparam int DEPTH = 64;
  localparam int LANES = 16;
  localparam int ADDR_W = $clog2(DEPTH);
  localparam int TEST_ROUNDS = 10;

  // Clock and reset
  logic clk = 0;
  logic reset_i = 1;
  
  always #5 clk = ~clk;
  
  // DUT signals
  logic               stream_wr_valid;
  logic [255:0]       stream_wr_data;
  logic [ADDR_W-1:0]  stream_wr_addr;
  logic               stream_wr_ready;
  
  logic               stream_rd_valid;
  logic [ADDR_W-1:0]  stream_wr_addr_check;
  logic [255:0]       stream_rd_data;
  logic               stream_rd_ready;
  
  logic [LANES-1:0][ADDR_W-1:0] simd_rd_addr_a;
  logic [LANES-1:0][ADDR_W-1:0] simd_rd_addr_b;
  logic [LANES-1:0][31:0]       simd_rd_data_a;
  logic [LANES-1:0][31:0]       simd_rd_data_b;
  
  logic [LANES-1:0]             simd_wr_valid;
  logic [LANES-1:0][ADDR_W-1:0] simd_wr_addr;
  logic [LANES-1:0][31:0]       simd_wr_data;
  
  logic [ADDR_W:0]              occupancy_o;

  // DUT instantiation
  staging_buffer #(
    .DEPTH(DEPTH),
    .LANES(LANES)
  ) dut (
    .clk_i(clk),
    .reset_i(reset_i),
    .stream_wr_valid(stream_wr_valid),
    .stream_wr_data(stream_wr_data),
    .stream_wr_addr(stream_wr_addr),
    .stream_wr_ready(stream_wr_ready),
    .stream_rd_valid(stream_rd_valid),
    .stream_rd_addr(stream_wr_addr_check),
    .stream_rd_data(stream_rd_data),
    .stream_rd_ready(stream_rd_ready),
    .simd_rd_addr_a(simd_rd_addr_a),
    .simd_rd_addr_b(simd_rd_addr_b),
    .simd_rd_data_a(simd_rd_data_a),
    .simd_rd_data_b(simd_rd_data_b),
    .simd_wr_valid(simd_wr_valid),
    .simd_wr_addr(simd_wr_addr),
    .simd_wr_data(simd_wr_data),
    .occupancy_o(occupancy_o)
  );

  // Test variables
  int error_count = 0;
  int test_count = 0;
  
  // Helper: Convert 8 floats to 256-bit
  function automatic [255:0] pack_floats(
    input real f0, f1, f2, f3, f4, f5, f6, f7
  );
    logic [255:0] result;
    result[31:0]   = $shortrealtobits(f0);
    result[63:32]  = $shortrealtobits(f1);
    result[95:64]  = $shortrealtobits(f2);
    result[127:96] = $shortrealtobits(f3);
    result[159:128]= $shortrealtobits(f4);
    result[191:160]= $shortrealtobits(f5);
    result[223:192]= $shortrealtobits(f6);
    result[255:224]= $shortrealtobits(f7);
    return result;
  endfunction
  
  // Helper: Unpack 256-bit to 8 floats
  function automatic void unpack_floats(
    input [255:0] data,
    output real f0, f1, f2, f3, f4, f5, f6, f7
  );
    f0 = $bitstoshortreal(data[31:0]);
    f1 = $bitstoshortreal(data[63:32]);
    f2 = $bitstoshortreal(data[95:64]);
    f3 = $bitstoshortreal(data[127:96]);
    f4 = $bitstoshortreal(data[159:128]);
    f5 = $bitstoshortreal(data[191:160]);
    f6 = $bitstoshortreal(data[223:192]);
    f7 = $bitstoshortreal(data[255:224]);
  endfunction

  // Test 1: Stream write and read
  task test_stream_write_read();
    real write_vals[8];
    real read_vals[8];
    logic [255:0] write_data;
    int addr;
    
    $display("[Test 1] Stream Write/Read Test");
    
    for (int round = 0; round < TEST_ROUNDS; round++) begin
      // Generate random data
      for (int i = 0; i < 8; i++) begin
        write_vals[i] = $random() % 1000 / 10.0;
      end
      addr = ($random() % (DEPTH / 8)) * 8;  // 8-aligned address
      write_data = pack_floats(write_vals[0], write_vals[1], write_vals[2], write_vals[3],
                               write_vals[4], write_vals[5], write_vals[6], write_vals[7]);
      
      // Write
      @(posedge clk);
      stream_wr_valid = 1;
      stream_wr_addr = addr[ADDR_W-1:0];
      stream_wr_data = write_data;
      @(posedge clk);
      stream_wr_valid = 0;
      
      // Small delay
      repeat(2) @(posedge clk);
      
      // Read back
      stream_rd_valid = 1;
      stream_wr_addr_check = addr[ADDR_W-1:0];
      @(posedge clk);
      stream_rd_valid = 0;
      
      // Check result
      unpack_floats(stream_rd_data, read_vals[0], read_vals[1], read_vals[2], read_vals[3],
                    read_vals[4], read_vals[5], read_vals[6], read_vals[7]);
      
      for (int i = 0; i < 8; i++) begin
        test_count++;
        if (write_vals[i] != read_vals[i]) begin
          $display("  FAIL: round=%0d, word=%0d, expected=%f, got=%f", 
                   round, i, write_vals[i], read_vals[i]);
          error_count++;
        end
      end
    end
    
    $display("  Test 1 completed: %0d tests, %0d errors", test_count, error_count);
  endtask

  // Test 2: SIMD parallel read
  task test_simd_parallel_read();
    logic [31:0] test_data [DEPTH];
    logic [ADDR_W-1:0] test_addrs [LANES];
    
    $display("[Test 2] SIMD Parallel Read Test");
    
    // Initialize test data
    for (int i = 0; i < DEPTH; i++) begin
      test_data[i] = $random();
    end
    
    // Write test data via stream
    for (int addr = 0; addr < DEPTH; addr += 8) begin
      @(posedge clk);
      stream_wr_valid = 1;
      stream_wr_addr = addr[ADDR_W-1:0];
      stream_wr_data = {test_data[addr+7], test_data[addr+6], test_data[addr+5], test_data[addr+4],
                        test_data[addr+3], test_data[addr+2], test_data[addr+1], test_data[addr+0]};
      @(posedge clk);
    end
    stream_wr_valid = 0;
    
    // Test parallel SIMD reads
    for (int round = 0; round < TEST_ROUNDS; round++) begin
      // Generate random addresses for each lane
      for (int i = 0; i < LANES; i++) begin
        test_addrs[i] = $random() % DEPTH;
        simd_rd_addr_a[i] = test_addrs[i];
        simd_rd_addr_b[i] = (test_addrs[i] + 1) % DEPTH;
      end
      
      @(posedge clk);
      
      // Check results (async read, so check immediately)
      for (int i = 0; i < LANES; i++) begin
        test_count += 2;
        if (simd_rd_data_a[i] !== test_data[test_addrs[i]]) begin
          $display("  FAIL: round=%0d, lane=%0d, addr_a=%0d, expected=0x%08x, got=0x%08x",
                   round, i, test_addrs[i], test_data[test_addrs[i]], simd_rd_data_a[i]);
          error_count++;
        end
        if (simd_rd_data_b[i] !== test_data[(test_addrs[i] + 1) % DEPTH]) begin
          $display("  FAIL: round=%0d, lane=%0d, addr_b=%0d, expected=0x%08x, got=0x%08x",
                   round, i, (test_addrs[i] + 1) % DEPTH, 
                   test_data[(test_addrs[i] + 1) % DEPTH], simd_rd_data_b[i]);
          error_count++;
        end
      end
    end
    
    $display("  Test 2 completed: %0d tests, %0d errors", test_count, error_count);
  endtask

  // Test 3: SIMD parallel write
  task test_simd_parallel_write();
    logic [31:0] write_data [LANES];
    logic [ADDR_W-1:0] write_addrs [LANES];
    
    $display("[Test 3] SIMD Parallel Write Test");
    
    for (int round = 0; round < TEST_ROUNDS; round++) begin
      // Generate random data and addresses
      for (int i = 0; i < LANES; i++) begin
        write_data[i] = $random();
        write_addrs[i] = i;  // Each lane writes to different address
        simd_wr_valid[i] = (i % 2 == 0);  // Even lanes write
        simd_wr_addr[i] = write_addrs[i];
        simd_wr_data[i] = write_data[i];
      end
      
      @(posedge clk);
      
      // Clear valid
      for (int i = 0; i < LANES; i++) begin
        simd_wr_valid[i] = 0;
      end
      
      @(posedge clk);
      
      // Read back and verify
      for (int i = 0; i < LANES; i += 2) begin  // Check even lanes
        simd_rd_addr_a[i] = write_addrs[i];
        test_count++;
      end
      
      @(posedge clk);
      
      for (int i = 0; i < LANES; i += 2) begin
        if (simd_rd_data_a[i] !== write_data[i]) begin
          $display("  FAIL: round=%0d, lane=%0d, expected=0x%08x, got=0x%08x",
                   round, i, write_data[i], simd_rd_data_a[i]);
          error_count++;
        end
      end
    end
    
    $display("  Test 3 completed: %0d tests, %0d errors", test_count, error_count);
  endtask

  // Main test sequence
  initial begin
    $display("========================================");
    $display("Staging Buffer Unit Test");
    $display("DEPTH=%0d, LANES=%0d", DEPTH, LANES);
    $display("========================================");
    
    // Initialize
    stream_wr_valid = 0;
    stream_wr_data = '0;
    stream_wr_addr = '0;
    stream_rd_valid = 0;
    stream_wr_addr_check = '0;
    
    for (int i = 0; i < LANES; i++) begin
      simd_rd_addr_a[i] = '0;
      simd_rd_addr_b[i] = '0;
      simd_wr_valid[i] = 0;
      simd_wr_addr[i] = '0;
      simd_wr_data[i] = '0;
    end
    
    // Reset
    @(posedge clk);
    reset_i = 0;
    repeat(5) @(posedge clk);
    reset_i = 1;
    repeat(5) @(posedge clk);
    
    // Run tests
    test_stream_write_read();
    repeat(10) @(posedge clk);
    
    test_simd_parallel_read();
    repeat(10) @(posedge clk);
    
    test_simd_parallel_write();
    repeat(10) @(posedge clk);
    
    // Final report
    $display("========================================");
    $display("Test Summary: %0d tests, %0d errors", test_count, error_count);
    if (error_count == 0) begin
      $display("ALL TESTS PASSED!");
    end else begin
      $display("SOME TESTS FAILED!");
    end
    $display("========================================");
    
    $finish;
  end

endmodule
