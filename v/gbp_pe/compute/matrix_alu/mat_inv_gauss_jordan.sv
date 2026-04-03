// mat_inv_gauss_jordan.sv
// Matrix inversion using Gauss-Jordan elimination
// Optimized for small matrices (up to 6x6) common in GBP

`include "bsg_defines.sv"

module mat_inv_gauss_jordan #(
    parameter int MAX_N = 6,            // Maximum matrix dimension
    parameter int LANES = 16,           // SIMD width
    parameter int ADDR_W = 6
)(
    input  logic clk_i,
    input  logic reset_i,
    
    // Command interface
    input  logic               cmd_valid,
    input  logic [ADDR_W-1:0]  cmd_base_a,      // Input matrix address
    input  logic [ADDR_W-1:0]  cmd_base_dest,   // Output inverse address
    input  logic [2:0]         cmd_n,           // Matrix dimension (2-6)
    output logic               cmd_ready,
    output logic               done_o,
    
    // Staging buffer interface
    output logic [LANES-1:0][ADDR_W-1:0] buf_rd_addr_a,
    output logic [LANES-1:0][ADDR_W-1:0] buf_rd_addr_b,
    input  logic [LANES-1:0][31:0]       buf_rd_data_a,
    input  logic [LANES-1:0][31:0]       buf_rd_data_b,
    
    output logic [LANES-1:0]             buf_wr_valid,
    output logic [LANES-1:0][ADDR_W-1:0] buf_wr_addr,
    output logic [LANES-1:0][31:0]       buf_wr_data,
    
    // SIMD array interface
    output logic [LANES-1:0]             simd_op_add,
    output logic [LANES-1:0]             simd_op_sub,
    output logic [LANES-1:0]             simd_op_mul,
    output logic [LANES-1:0]             simd_op_div,
    output logic [LANES-1:0][1:0]        simd_src_a,
    output logic [LANES-1:0][1:0]        simd_src_b,
    output logic [LANES-1:0][31:0]       simd_const,
    input  logic [LANES-1:0]             simd_valid,
    input  logic [LANES-1:0][31:0]       simd_result
);

  // Gauss-Jordan elimination states
  typedef enum logic [3:0] {
    S_IDLE,
    S_SETUP,              // Setup: prepare to process
    S_LOAD_PIVOT,         // Load pivot element
    S_NORMALIZE,          // Normalize pivot row
    S_ELIMINATE,          // Eliminate column entries
    S_ELIM_WAIT,          // Wait for SIMD operation
    S_NEXT_COL,           // Move to next column
    S_WRITE_RESULT,       // Write final result
    S_DONE
  } state_e;
  
  state_e state_r, state_n;
  
  // Matrix dimension
  logic [2:0] n_r;
  logic [2:0] col_r;      // Current column (pivot)
  logic [2:0] row_r;      // Current row for elimination
  
  // Computation registers
  logic [31:0] pivot_val_r;
  logic [31:0] factor_r;
  
  // Output assignments
  assign cmd_ready = (state_r == S_IDLE);
  assign done_o = (state_r == S_DONE);
  
  // State machine
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= S_IDLE;
      n_r <= '0;
      col_r <= '0;
      row_r <= '0;
      pivot_val_r <= '0;
      factor_r <= '0;
    end else begin
      state_r <= state_n;
      
      if (cmd_valid && cmd_ready) begin
        n_r <= cmd_n;
        col_r <= '0;
        row_r <= '0;
      end else begin
        case (state_r)
          S_LOAD_PIVOT: begin
            pivot_val_r <= buf_rd_data_a[0];
          end
          
          S_ELIM_WAIT: begin
            // Move to next row for elimination
            if (row_r < n_r - 1) begin
              row_r <= row_r + 1'b1;
            end
          end
          
          S_NEXT_COL: begin
            col_r <= col_r + 1'b1;
            row_r <= '0;
          end
          
          default: ;
        endcase
      end
    end
  end
  
  // Next state logic
  always_comb begin
    state_n = state_r;
    
    case (state_r)
      S_IDLE: begin
        if (cmd_valid) state_n = S_SETUP;
      end
      
      S_SETUP: begin
        state_n = S_LOAD_PIVOT;
      end
      
      S_LOAD_PIVOT: begin
        state_n = S_NORMALIZE;
      end
      
      S_NORMALIZE: begin
        // After normalizing pivot row, start elimination
        state_n = S_ELIMINATE;
      end
      
      S_ELIMINATE: begin
        state_n = S_ELIM_WAIT;
      end
      
      S_ELIM_WAIT: begin
        // Check if we've processed all rows
        if (row_r >= n_r - 1) begin
          state_n = S_NEXT_COL;
        end else begin
          state_n = S_ELIMINATE;
        end
      end
      
      S_NEXT_COL: begin
        if (col_r >= n_r - 1) begin
          state_n = S_WRITE_RESULT;
        end else begin
          state_n = S_LOAD_PIVOT;
        end
      end
      
      S_WRITE_RESULT: begin
        state_n = S_DONE;
      end
      
      S_DONE: begin
        state_n = S_IDLE;
      end
      
      default: state_n = S_IDLE;
    endcase
  end
  
  // SIMD control
  always_comb begin
    // Defaults
    simd_op_add = '0;
    simd_op_sub = '0;
    simd_op_mul = '0;
    simd_op_div = '0;
    simd_src_a = {LANES{2'b00}};  // buffer_a
    simd_src_b = {LANES{2'b01}};  // buffer_b
    simd_const = '0;
    
    buf_rd_addr_a = {LANES{ADDR_W'(0)}};
    buf_rd_addr_b = {LANES{ADDR_W'(0)}};
    buf_wr_valid = '0;
    buf_wr_addr = {LANES{ADDR_W'(0)}};
    buf_wr_data = {LANES{32'h0}};
    
    case (state_r)
      S_SETUP: begin
        // Initialize identity matrix part in staging buffer
        // For simplicity, we assume input matrix is in left half,
        // and we initialize right half to identity
        buf_wr_valid = {{15{1'b0}}, 1'b1};
        for (int i = 0; i < LANES; i++) begin
          if (i < n_r * n_r) begin
            int row = i / n_r;
            int col = i % n_r;
            buf_wr_addr[i] = cmd_base_a + n_r * n_r + i;  // Right half
            if (row == col) begin
              buf_wr_data[i] = 32'h3F800000;  // 1.0f
            end else begin
              buf_wr_data[i] = 32'h0;         // 0.0f
            end
          end
        end
      end
      
      S_LOAD_PIVOT: begin
        // Read pivot element A[col][col]
        buf_rd_addr_a[0] = cmd_base_a + col_r * n_r + col_r;
      end
      
      S_NORMALIZE: begin
        // Divide entire row by pivot value
        // For now, simplified: assume pivot is 1 or handle via SIMD
        simd_op_div = {{15{1'b0}}, 1'b1};
        simd_const[0] = pivot_val_r;
        simd_src_b = {LANES{2'b11}};  // Use constant
        
        // Read row elements
        for (int i = 0; i < LANES; i++) begin
          if (i < 2 * n_r) begin  // Row has 2*n elements (A and I parts)
            buf_rd_addr_a[i] = cmd_base_a + col_r * 2 * n_r + i;
          end
        end
        
        // Write normalized row back
        buf_wr_valid = {{15{1'b0}}, 1'b1};
        for (int i = 0; i < LANES; i++) begin
          if (i < 2 * n_r) begin
            buf_wr_addr[i] = cmd_base_a + col_r * 2 * n_r + i;
            buf_wr_data[i] = simd_result[i];
          end
        end
      end
      
      S_ELIMINATE: begin
        // row = row - factor * pivot_row
        // where factor = A[row][col]
        if (row_r != col_r) begin
          simd_op_sub = {{15{1'b0}}, 1'b1};
          simd_op_mul = {{15{1'b0}}, 1'b1};
          
          // Read current row
          for (int i = 0; i < LANES; i++) begin
            if (i < 2 * n_r) begin
              buf_rd_addr_a[i] = cmd_base_a + row_r * 2 * n_r + i;
              buf_rd_addr_b[i] = cmd_base_a + col_r * 2 * n_r + i;  // Pivot row
            end
          end
          
          // Write back
          buf_wr_valid = {{15{1'b0}}, 1'b1};
          for (int i = 0; i < LANES; i++) begin
            if (i < 2 * n_r) begin
              buf_wr_addr[i] = cmd_base_a + row_r * 2 * n_r + i;
              buf_wr_data[i] = simd_result[i];
            end
          end
        end
      end
      
      S_WRITE_RESULT: begin
        // Copy inverse from right half of augmented matrix to destination
        buf_wr_valid = {{15{1'b0}}, 1'b1};
        for (int i = 0; i < LANES; i++) begin
          if (i < n_r * n_r) begin
            buf_rd_addr_a[i] = cmd_base_a + n_r * n_r + i;  // Right half
            buf_wr_addr[i] = cmd_base_dest + i;
            buf_wr_data[i] = buf_rd_data_a[i];
          end
        end
      end
      
      default: ;
    endcase
  end

endmodule
