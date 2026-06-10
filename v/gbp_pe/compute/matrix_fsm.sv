// matrix_fsm.sv
// Matrix operation state machine for GBP compute engine
// Orchestrates SIMD array to perform matrix operations

`include "bsg_defines.sv"

module matrix_fsm #(
    parameter int LANES = 16,
    parameter int MAX_DOFS = 6,
    parameter int ADDR_W = 6
)(
    input  logic clk_i,
    input  logic rst_n_i,
    
    // Command interface
    input  logic               cmd_valid,
    input  logic [2:0]         cmd_op,         // 0: MatAdd, 1: MatSub, 2: MatMul, 3: MatInv, 4: MatVecMul
    input  logic [ADDR_W-1:0]  cmd_base_a,     // Base address for operand A
    input  logic [ADDR_W-1:0]  cmd_base_b,     // Base address for operand B
    input  logic [ADDR_W-1:0]  cmd_base_dest,  // Base address for result
    input  logic [5:0]         cmd_m,          // Rows of A / result
    input  logic [5:0]         cmd_n,          // Cols of B / result (for MatMul)
    input  logic [5:0]         cmd_k,          // Inner dimension (for MatMul)
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
    output logic [LANES-1:0]             simd_op_mac,
    output logic [LANES-1:0][1:0]        simd_src_a,
    output logic [LANES-1:0][1:0]        simd_src_b,
    output logic [LANES-1:0][31:0]       simd_const,
    input  logic [LANES-1:0]             simd_valid,
    input  logic [LANES-1:0][31:0]       simd_result
);

  logic reset_i;
  assign reset_i = ~rst_n_i;

  // Operation encoding
  localparam logic [2:0] OP_MAT_ADD    = 3'd0;
  localparam logic [2:0] OP_MAT_SUB    = 3'd1;
  localparam logic [2:0] OP_MAT_MUL    = 3'd2;
  localparam logic [2:0] OP_MAT_INV    = 3'd3;
  localparam logic [2:0] OP_MAT_VEC_MUL = 3'd4;
  
  // State machine states
  typedef enum logic [4:0] {
    S_IDLE,
    S_MATADD_SETUP,
    S_MATADD_EXEC,
    S_MATADD_WAIT,
    S_MATADD_WRITE,
    S_MATSUB_SETUP,
    S_MATSUB_EXEC,
    S_MATSUB_WAIT,
    S_MATSUB_WRITE,
    S_MATMUL_SETUP,
    S_MATMUL_EXEC,      // Execute dot product computation
    S_MATMUL_WRITE,     // Write result and advance counters
    S_MATVECMUL_SETUP,
    S_MATVECMUL_EXEC,
    S_MATVECMUL_ACCUM,
    S_MATVECMUL_WRITE,
    S_MATINV_SETUP,
    S_MATINV_EXEC,
    S_DONE
  } state_e;
  
  state_e state_r, state_n;
  
  // Operation registers
  logic [2:0] op_r;
  logic [ADDR_W-1:0] base_a_r, base_b_r, base_dest_r;
  logic [5:0] m_r, n_r, k_r;
  
  // Iteration counters
  logic [ADDR_W-1:0] iter_r;
  logic [ADDR_W:0] total_elements;
  
  // MatMul loop indices
  logic [3:0] row_r, col_r, dot_r;  // C[row,col] = dot(A[row,:], B[:,col])
  
  // Accumulator for MAC operations
  logic [LANES-1:0] acc_clear, acc_load;
  
  // Command valid sampling register (for Verilator scheduling)
  logic cmd_valid_r;
  
  // SIMD data source constants
  localparam logic [1:0] SRC_BUFFER_A = 2'b00;
  localparam logic [1:0] SRC_BUFFER_B = 2'b01;
  localparam logic [1:0] SRC_ACC      = 2'b10;
  localparam logic [1:0] SRC_CONST    = 2'b11;
  
  // Debug (disabled)
  // always_ff @(posedge clk_i) begin
  //   if (state_r == S_DONE || state_n == S_DONE)
  //     $display("MATRIX_DBG: state=%s done_o=%b", state_r.name(), done_o);
  // end

  // ============================================================
  // State machine
  // ============================================================
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= S_IDLE;
      op_r <= '0;
      base_a_r <= '0;
      base_b_r <= '0;
      base_dest_r <= '0;
      m_r <= '0;
      n_r <= '0;
      k_r <= '0;
      iter_r <= '0;
      row_r <= '0;
      col_r <= '0;
      dot_r <= '0;
      cmd_valid_r <= 1'b0;
    end else begin
      state_r <= state_n;
      cmd_valid_r <= cmd_valid;  // Sample cmd_valid for next cycle
      
      if (cmd_valid_r && cmd_ready) begin
        op_r <= cmd_op;
        base_a_r <= cmd_base_a;
        base_b_r <= cmd_base_b;
        base_dest_r <= cmd_base_dest;
        m_r <= cmd_m;
        n_r <= cmd_n;
        k_r <= cmd_k;
        iter_r <= '0;
        row_r <= '0;
        col_r <= '0;
        dot_r <= '0;
      end else begin
        // Update iteration counter based on state
        case (state_r)
          S_MATADD_EXEC, S_MATSUB_EXEC: begin
            if (iter_r < total_elements[ADDR_W-1:0]) begin
              iter_r <= iter_r + LANES[ADDR_W-1:0];
            end
          end
          S_MATVECMUL_EXEC: begin
            if (dot_r < k_r - 1) begin
              dot_r <= dot_r + 1'b1;
            end
          end
          S_MATVECMUL_WRITE: begin
            dot_r <= '0;
            if (row_r < m_r - 1) begin
              row_r <= row_r + 1'b1;
            end
          end
          S_MATMUL_EXEC: begin
            // Advance dot product index
            if (dot_r < k_r - 1) begin
              dot_r <= dot_r + 1'b1;
            end
          end
          S_MATMUL_WRITE: begin
            // Advance counters after writing result
            dot_r <= '0;  // Reset dot_r for next element
            if (col_r < n_r - 1) begin
              // Move to next column in same row
              col_r <= col_r + 1'b1;
            end else if (row_r < m_r - 1) begin
              // Move to next row, reset column
              col_r <= '0;
              row_r <= row_r + 1'b1;
            end
          end
          default: begin
            iter_r <= '0;
          end
        endcase
      end
    end
  end
  
  // ============================================================
  // Next state logic
  // ============================================================
  always_comb begin
    state_n = state_r;
    
    case (state_r)
      S_IDLE: begin
        if (cmd_valid_r) begin  // Use sampled cmd_valid
          case (cmd_op)
            OP_MAT_ADD:     state_n = S_MATADD_SETUP;
            OP_MAT_SUB:     state_n = S_MATSUB_SETUP;
            OP_MAT_MUL:     state_n = S_MATMUL_SETUP;
            OP_MAT_INV:     state_n = S_MATINV_SETUP;
            OP_MAT_VEC_MUL: state_n = S_MATVECMUL_SETUP;
            default:        state_n = S_IDLE;
          endcase
        end
      end
      
      S_MATADD_SETUP:  state_n = S_MATADD_EXEC;
      S_MATADD_EXEC:   state_n = (iter_r >= total_elements) ? S_MATADD_WAIT : S_MATADD_EXEC;
      S_MATADD_WAIT:   state_n = (&simd_valid) ? S_MATADD_WRITE : S_MATADD_WAIT;
      S_MATADD_WRITE:  state_n = S_DONE;
      
      S_MATSUB_SETUP:  state_n = S_MATSUB_EXEC;
      S_MATSUB_EXEC:   state_n = (iter_r >= total_elements) ? S_MATSUB_WAIT : S_MATSUB_EXEC;
      S_MATSUB_WAIT:   state_n = (&simd_valid) ? S_MATSUB_WRITE : S_MATSUB_WAIT;
      S_MATSUB_WRITE:  state_n = S_DONE;
      
      S_MATMUL_SETUP:  state_n = S_MATMUL_EXEC;
      S_MATMUL_EXEC:   state_n = (dot_r >= k_r - 1) ? S_MATMUL_WRITE : S_MATMUL_EXEC;
      S_MATMUL_WRITE:  state_n = ((col_r >= n_r - 1) && (row_r >= m_r - 1)) ? S_DONE : S_MATMUL_EXEC;
      
      S_MATINV_SETUP:  state_n = S_MATINV_EXEC;
      S_MATINV_EXEC:   state_n = mat_inv_done ? S_DONE : S_MATINV_EXEC;
      
      S_MATVECMUL_SETUP: state_n = S_MATVECMUL_EXEC;
      S_MATVECMUL_EXEC:  state_n = (dot_r >= k_r - 1) ? S_MATVECMUL_WRITE : S_MATVECMUL_EXEC;
      S_MATVECMUL_WRITE: state_n = (row_r >= m_r - 1) ? S_DONE : S_MATVECMUL_EXEC;
      
      S_DONE: state_n = S_IDLE;
      
      default: state_n = S_IDLE;
    endcase
  end
  
  // ============================================================
  // Output logic
  // ============================================================
  
  // Command ready
  assign cmd_ready = (state_r == S_IDLE);
  assign done_o = (state_r == S_DONE);
  
  // Total elements calculation
  always_comb begin
    case (op_r)
      OP_MAT_ADD, OP_MAT_SUB: total_elements = m_r * n_r;
      OP_MAT_VEC_MUL:         total_elements = m_r;
      default:                total_elements = '0;
    endcase
  end
  
  // MatInv command control
  always_comb begin
    if (state_r == S_MATINV_SETUP) begin
      mat_inv_cmd_valid = 1'b1;
      mat_inv_base_a = base_a_r;
      mat_inv_base_dest = base_dest_r;
      mat_inv_n = m_r[2:0];
    end else begin
      mat_inv_cmd_valid = 1'b0;
      mat_inv_base_a = '0;
      mat_inv_base_dest = '0;
      mat_inv_n = '0;
    end
  end

  // SIMD control signals
  always_comb begin
    // Defaults
    simd_op_add = '0;
    simd_op_sub = '0;
    simd_op_mul = '0;
    simd_op_mac = '0;
    simd_src_a = {LANES{SRC_BUFFER_A}};
    simd_src_b = {LANES{SRC_BUFFER_B}};
    simd_const = '0;
    acc_clear = '0;
    acc_load = '0;
    buf_wr_valid = '0;
    buf_wr_addr = {LANES{ADDR_W'(0)}};
    buf_wr_data = {LANES{32'h0}};
    
    case (state_r)
      S_MATADD_EXEC: begin
        simd_op_add = {LANES{1'b1}};
        // Read addresses: sequential from base_a and base_b
        for (int i = 0; i < LANES; i++) begin
          if (iter_r + i < total_elements) begin
            buf_rd_addr_a[i] = base_a_r + iter_r + i;
            buf_rd_addr_b[i] = base_b_r + iter_r + i;
          end else begin
            buf_rd_addr_a[i] = '0;
            buf_rd_addr_b[i] = '0;
          end
        end
        $display("MAT_DBG: ADD_EXEC iter=%d total=%d base_a=%d base_b=%d addr_a[0]=%d addr_b[0]=%d",
                 iter_r, total_elements, base_a_r, base_b_r, buf_rd_addr_a[0], buf_rd_addr_b[0]);
      end
      
      S_MATADD_WAIT, S_MATADD_WRITE: begin
        // Keep op enabled during WAIT to maintain valid signal
        simd_op_add = {LANES{1'b1}};
        // Preserve read addresses so simd_result doesn't change
        for (int i = 0; i < LANES; i++) begin
          if (iter_r + i < total_elements + LANES) begin
            buf_rd_addr_a[i] = base_a_r + iter_r - LANES + i;
            buf_rd_addr_b[i] = base_b_r + iter_r - LANES + i;
          end else begin
            buf_rd_addr_a[i] = '0;
            buf_rd_addr_b[i] = '0;
          end
        end
        // Write results back
        buf_wr_valid = simd_valid;
        for (int i = 0; i < LANES; i++) begin
          buf_wr_addr[i] = base_dest_r + iter_r - LANES + i;
          buf_wr_data[i] = simd_result[i];
        end
        $display("MAT_DBG: ADD_WRITE iter=%d base_dest=%d wr_addr[0]=%d wr_data[0]=%h (%f)",
                 iter_r, base_dest_r, buf_wr_addr[0], buf_wr_data[0], $bitstoreal(buf_wr_data[0]));
      end
      
      S_MATSUB_EXEC: begin
        simd_op_sub = {LANES{1'b1}};
        for (int i = 0; i < LANES; i++) begin
          if (iter_r + i < total_elements) begin
            buf_rd_addr_a[i] = base_a_r + iter_r + i;
            buf_rd_addr_b[i] = base_b_r + iter_r + i;
          end else begin
            buf_rd_addr_a[i] = '0;
            buf_rd_addr_b[i] = '0;
          end
        end
      end
      
      S_MATSUB_WAIT, S_MATSUB_WRITE: begin
        // Keep op enabled during WAIT to maintain valid signal
        simd_op_sub = {LANES{1'b1}};
        // Preserve read addresses so simd_result doesn't change
        for (int i = 0; i < LANES; i++) begin
          if (iter_r + i < total_elements + LANES) begin
            buf_rd_addr_a[i] = base_a_r + iter_r - LANES + i;
            buf_rd_addr_b[i] = base_b_r + iter_r - LANES + i;
          end else begin
            buf_rd_addr_a[i] = '0;
            buf_rd_addr_b[i] = '0;
          end
        end
        buf_wr_valid = simd_valid;
        for (int i = 0; i < LANES; i++) begin
          buf_wr_addr[i] = base_dest_r + iter_r - LANES + i;
          buf_wr_data[i] = simd_result[i];
        end
      end
      
      S_MATMUL_EXEC: begin
        // Compute dot product: C[row,col] += A[row,dot] * B[dot,col]
        simd_op_mul = {LANES{1'b1}};
        simd_op_mac = {LANES{1'b1}};  // MAC for accumulation
        
        // Only use lane 0 for scalar multiplication
        // A[row, dot] - row-major: base + row*k + dot
        buf_rd_addr_a[0] = base_a_r + row_r * k_r + dot_r;
        // B[dot, col] - row-major: base + dot*n + col
        buf_rd_addr_b[0] = base_b_r + dot_r * n_r + col_r;
        
        // Clear accumulator at start of each dot product
        if (dot_r == 0) acc_clear[0] = 1'b1;
      end
      
      S_MATMUL_WRITE: begin
        // Write final result
        buf_wr_valid[0] = 1'b1;
        buf_wr_addr[0] = base_dest_r + row_r * n_r + col_r;
        buf_wr_data[0] = simd_result[0];
      end
      
      S_MATINV_EXEC: begin
        // Delegate control to mat_inv_gauss_jordan
        buf_rd_addr_a = mat_inv_rd_addr_a;
        buf_rd_addr_b = mat_inv_rd_addr_b;
        buf_wr_valid = mat_inv_wr_valid;
        buf_wr_addr = mat_inv_wr_addr;
        buf_wr_data = mat_inv_wr_data;
        
        simd_op_add = mat_inv_simd_op_add;
        simd_op_sub = mat_inv_simd_op_sub;
        simd_op_mul = mat_inv_simd_op_mul;
        simd_op_mac = mat_inv_simd_op_div;  // Reuse mac for div control
        simd_src_a = mat_inv_simd_src_a;
        simd_src_b = mat_inv_simd_src_b;
        simd_const = mat_inv_simd_const;
      end
      
      S_MATVECMUL_EXEC: begin
        // Matrix-vector: y[row] = sum_dot(A[row,dot] * x[dot])
        // 当前先按 lane0 串行闭合 3x3 路径，避免错误的多行循环。
        simd_op_mul[0] = 1'b1;
        simd_op_mac[0] = 1'b1;
        buf_rd_addr_a[0] = base_a_r + row_r * k_r + dot_r;
        buf_rd_addr_b[0] = base_b_r + dot_r;
        if (dot_r == 0) acc_clear[0] = 1'b1;
      end
      
      S_MATVECMUL_WRITE: begin
        // Write accumulated results - one element at a time using lane 0
        buf_wr_valid = '0;
        buf_wr_valid[0] = 1'b1;
        buf_wr_addr[0] = base_dest_r + row_r;
        buf_wr_data[0] = simd_result[0];
      end
      
      default: begin
        buf_rd_addr_a = {LANES{ADDR_W'(0)}};
        buf_rd_addr_b = {LANES{ADDR_W'(0)}};
        buf_wr_valid = '0;
        buf_wr_addr = {LANES{ADDR_W'(0)}};
        buf_wr_data = {LANES{32'h0}};
      end
    endcase
  end
  
  // Unused outputs
  logic [LANES-1:0][ADDR_W-1:0] unused_rd_addr_a, unused_rd_addr_b;
  assign unused_rd_addr_a = buf_rd_addr_a;
  assign unused_rd_addr_b = buf_rd_addr_b;
  
  // ============================================================
  // MatInv sub-module instance
  // ============================================================
  logic              mat_inv_cmd_valid;
  logic [ADDR_W-1:0] mat_inv_base_a;
  logic [ADDR_W-1:0] mat_inv_base_dest;
  logic [2:0]        mat_inv_n;
  logic              mat_inv_ready;
  logic              mat_inv_done;
  
  logic [LANES-1:0][ADDR_W-1:0] mat_inv_rd_addr_a;
  logic [LANES-1:0][ADDR_W-1:0] mat_inv_rd_addr_b;
  logic [LANES-1:0]             mat_inv_wr_valid;
  logic [LANES-1:0][ADDR_W-1:0] mat_inv_wr_addr;
  logic [LANES-1:0][31:0]       mat_inv_wr_data;
  
  logic [LANES-1:0]       mat_inv_simd_op_add;
  logic [LANES-1:0]       mat_inv_simd_op_sub;
  logic [LANES-1:0]       mat_inv_simd_op_mul;
  logic [LANES-1:0]       mat_inv_simd_op_div;
  logic [LANES-1:0][1:0]  mat_inv_simd_src_a;
  logic [LANES-1:0][1:0]  mat_inv_simd_src_b;
  logic [LANES-1:0][31:0] mat_inv_simd_const;
  
  mat_inv_gauss_jordan #(
    .MAX_N(MAX_DOFS),
    .LANES(LANES),
    .ADDR_W(ADDR_W)
  ) u_mat_inv (
    .clk_i(clk_i),
    .rst_n_i(rst_n_i),
    .cmd_valid(mat_inv_cmd_valid),
    .cmd_base_a(mat_inv_base_a),
    .cmd_base_dest(mat_inv_base_dest),
    .cmd_n(mat_inv_n),
    .cmd_ready(mat_inv_ready),
    .done_o(mat_inv_done),
    .buf_rd_addr_a(mat_inv_rd_addr_a),
    .buf_rd_addr_b(mat_inv_rd_addr_b),
    .buf_rd_data_a(buf_rd_data_a),
    .buf_rd_data_b(buf_rd_data_b),
    .buf_wr_valid(mat_inv_wr_valid),
    .buf_wr_addr(mat_inv_wr_addr),
    .buf_wr_data(mat_inv_wr_data),
    .simd_op_add(mat_inv_simd_op_add),
    .simd_op_sub(mat_inv_simd_op_sub),
    .simd_op_mul(mat_inv_simd_op_mul),
    .simd_op_div(mat_inv_simd_op_div),
    .simd_src_a(mat_inv_simd_src_a),
    .simd_src_b(mat_inv_simd_src_b),
    .simd_const(mat_inv_simd_const),
    .simd_valid(simd_valid),
    .simd_result(simd_result)
  );

endmodule
