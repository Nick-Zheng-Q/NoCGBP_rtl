// gbp_control_fsm.sv
// GBP node computation control FSM
// Orchestrates Variable Node update_belief and Factor Node compute_messages

`include "bsg_defines.sv"
`include "gbp_compute_defines.svh"

module gbp_control_fsm #(
    parameter int LANES = 16,
    parameter int MAX_DOFS = 6,
    parameter int MAX_ADJACENT = 8,
    parameter int STAGING_DEPTH = 128,
    parameter int ADDR_W = $clog2(STAGING_DEPTH)
)(
    input  logic clk_i,
    input  logic reset_i,
    
    // External command interface
    input  logic               cmd_valid,
    input  logic               cmd_is_factor,     // 0: variable, 1: factor
    input  logic [7:0]         cmd_node_idx,
    input  logic [2:0]         cmd_dofs,          // Node dimension (2, 3, or 6)
    input  logic [3:0]         cmd_adj_count,     // Number of adjacent nodes
    input  logic [3:0]         cmd_msg_count,     // 本轮需要消费的 message 条数
    output logic               cmd_ready,
    output logic               done_o,
    
    // Stream control (to load/store data from SPM)
    output logic               stream_req_state,
    output logic               stream_req_messages,
    output logic [15:0]        stream_xfer_bytes,
    input  logic               stream_grant,
    input  logic               stream_done,
    
    // Stream data interface
    output logic               stream_in_ready,
    input  logic               stream_in_valid,
    input  logic [255:0]       stream_in_data,
    
    output logic               stream_out_valid,
    input  logic               stream_out_ready,
    output logic [255:0]       stream_out_data,
    
    // Matrix FSM interface
    output logic               mat_cmd_valid,
    output logic [2:0]         mat_cmd_op,
    output logic [ADDR_W-1:0]  mat_cmd_base_a,
    output logic [ADDR_W-1:0]  mat_cmd_base_b,
    output logic [ADDR_W-1:0]  mat_cmd_base_dest,
    output logic [3:0]         mat_cmd_m,
    output logic [3:0]         mat_cmd_n,
    output logic [3:0]         mat_cmd_k,
    input  logic               mat_cmd_ready,
    input  logic               mat_done,
    
    // Staging buffer direct access (for data movement)
    output logic [ADDR_W-1:0]  buf_wr_addr,
    output logic [255:0]       buf_wr_data,
    output logic               buf_wr_valid,
    
    // Configuration
    input  logic [31:0]        damping_factor     // FP32 damping (e.g., 0.4)
);

  localparam int VAR_PAYLOAD_CHUNK_WORDS = 15;

  function automatic int compact_payload_words(input logic [2:0] dofs);
    begin
      unique case (dofs)
        3'd1: compact_payload_words = 2;
        3'd2: compact_payload_words = 5;
        3'd3: compact_payload_words = 9;
        3'd4: compact_payload_words = 14;
        3'd5: compact_payload_words = 20;
        3'd6: compact_payload_words = 27;
        default: compact_payload_words = 0;
      endcase
    end
  endfunction

  function automatic int square_words(input logic [2:0] dofs);
    begin
      unique case (dofs)
        3'd1: square_words = 1;
        3'd2: square_words = 4;
        3'd3: square_words = 9;
        3'd4: square_words = 16;
        3'd5: square_words = 25;
        3'd6: square_words = 36;
        default: square_words = 0;
      endcase
    end
  endfunction

  function automatic int compact_payload_rows(input logic [2:0] dofs);
    begin
      unique case (dofs)
        3'd1,
        3'd2: compact_payload_rows = 1;
        3'd3,
        3'd4: compact_payload_rows = 2;
        3'd5: compact_payload_rows = 3;
        3'd6: compact_payload_rows = 4;
        default: compact_payload_rows = 0;
      endcase
    end
  endfunction

  function automatic int payload_chunk_count(input logic [2:0] dofs);
    begin
      unique case (dofs)
        3'd6: payload_chunk_count = 2;
        3'd1,
        3'd2,
        3'd3,
        3'd4,
        3'd5: payload_chunk_count = 1;
        default: payload_chunk_count = 0;
      endcase
    end
  endfunction

  function automatic int chunk_offset_words(input logic [1:0] chunk_idx);
    begin
      unique case (chunk_idx)
        2'd0: chunk_offset_words = 0;
        2'd1: chunk_offset_words = 15;
        2'd2: chunk_offset_words = 30;
        2'd3: chunk_offset_words = 45;
        default: chunk_offset_words = 0;
      endcase
    end
  endfunction

  function automatic int message_offset_words(input logic [3:0] msg_index,
                                              input logic [2:0] dofs);
    int stride_words;
    int offset_words;
    int idx;
    begin
      stride_words = compact_payload_rows(dofs) << 3;
      offset_words = 0;
      for (idx = 0; idx < msg_index; idx = idx + 1) begin
        offset_words = offset_words + stride_words;
      end
      message_offset_words = offset_words;
    end
  endfunction

  // ============================================================
  // State definitions
  // ============================================================
  
  // Top-level states
  typedef enum logic [3:0] {
    S_IDLE,
    S_VAR_LOAD_DATA,
    S_VAR_ACCUMULATE,
    S_VAR_INVERT_LAM,
    S_VAR_MVMUL,
    S_VAR_STORE_RESULT,
    S_VAR_DONE,
    S_FAC_LOAD_DATA,
    S_FAC_LOOP_INIT,
    S_FAC_CAVITY_ACCUM,
    S_FAC_EXTRACT_BLOCKS,
    S_FAC_INVERT_LNONO,
    S_FAC_COMPUTE_MESSAGE,
    S_FAC_STORE_MESSAGE,
    S_FAC_NEXT_ADJACENT,
    S_FAC_DONE
  } top_state_e;
  
  top_state_e state_r, state_n;
  
  // Node parameters
  logic is_factor_r;
  logic [2:0] dofs_r;
  logic [3:0] adj_count_r;
  logic [3:0] msg_count_r;
  logic [3:0] current_adj_r;    // Current adjacent node being processed
  
  // Computed values
  logic [ADDR_W-1:0] prior_eta_addr;
  logic [ADDR_W-1:0] prior_lam_addr;
  logic [ADDR_W-1:0] msg_base_addr;
  logic [ADDR_W-1:0] result_addr;
  logic [1:0]        payload_chunk_r;
  
  int payload_size_int;
  int payload_rows_int;
  int payload_stride_words_int;
  int msg_size_int;
  int lam_size_int;
  int payload_chunk_count_int;
  int chunk_offset_int;
  int chunk_words_int;
  int payload_remaining_int;
  
  // whitebox variable/state message 都使用紧凑 payload：
  // eta 后接上三角 lam。
  always_comb begin
    payload_size_int = compact_payload_words(dofs_r);
    payload_rows_int = compact_payload_rows(dofs_r);
    payload_stride_words_int = payload_rows_int << 3;
    msg_size_int = payload_stride_words_int;
    lam_size_int = square_words(dofs_r);  // Full matrix size for computation
    payload_chunk_count_int = payload_chunk_count(dofs_r);
    chunk_offset_int = chunk_offset_words(payload_chunk_r);
    payload_remaining_int = payload_size_int - chunk_offset_int;
    if (payload_remaining_int > VAR_PAYLOAD_CHUNK_WORDS) begin
      chunk_words_int = VAR_PAYLOAD_CHUNK_WORDS;
    end else if (payload_remaining_int > 0) begin
      chunk_words_int = payload_remaining_int;
    end else begin
      chunk_words_int = 1;
    end
  end
  
  // Address calculation
  assign prior_eta_addr = ADDR_W'(0);
  assign prior_lam_addr = ADDR_W'(dofs_r);  // After eta
  assign msg_base_addr = ADDR_W'(payload_stride_words_int);  // 消息起点按 beat 对齐
  assign result_addr = ADDR_W'(STAGING_DEPTH - 64);  // Reserve space at end
  
  // Accumulation counter
  logic [3:0] accum_count_r;
  
  // Matrix command valid register (for Verilator scheduling)
  logic mat_cmd_valid_r;
  logic mat_cmd_valid_next;
  
  // mat_done sampling register (for Verilator scheduling)
  logic mat_done_r;
  
  // Debug
  always_ff @(posedge clk_i) begin
    if (state_r == S_VAR_DONE || state_r == S_FAC_DONE)
      $display("GBP_FSM_DBG: DONE state=%b done_o=%b reset_i=%b", state_r, done_o, reset_i);
  end
  
  // ============================================================
  // State machine sequential logic
  // ============================================================
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= S_IDLE;
      is_factor_r <= 1'b0;
      dofs_r <= '0;
      adj_count_r <= '0;
      msg_count_r <= '0;
      current_adj_r <= '0;
      accum_count_r <= '0;
      payload_chunk_r <= '0;
      mat_cmd_valid_r <= 1'b0;
      mat_done_r <= 1'b0;
    end else begin
      // Register updates
      mat_cmd_valid_r <= mat_cmd_valid_next;
      mat_done_r <= mat_done;
      // done_o is now driven by assign statement below
      state_r <= state_n;
      
      if (cmd_valid && cmd_ready) begin
        is_factor_r <= cmd_is_factor;
        dofs_r <= cmd_dofs;
        adj_count_r <= cmd_adj_count;
        msg_count_r <= cmd_msg_count;
        current_adj_r <= '0;
        accum_count_r <= '0;
        payload_chunk_r <= '0;
      end else begin
        // State-specific updates
        case (state_r)
          S_VAR_ACCUMULATE: begin
            if (mat_done_r) begin
              if (msg_count_r != 4'd0 && accum_count_r >= msg_count_r - 1'b1) begin
                accum_count_r <= '0;
                if (payload_chunk_r + 1'b1 < payload_chunk_count_int) begin
                  payload_chunk_r <= payload_chunk_r + 1'b1;
                end else begin
                  payload_chunk_r <= '0;
                end
              end else begin
                accum_count_r <= accum_count_r + 1'b1;
              end
            end
          end
          
          S_FAC_CAVITY_ACCUM: begin
            if (mat_done_r) accum_count_r <= accum_count_r + 1'b1;
          end
          
          S_FAC_NEXT_ADJACENT: begin
            current_adj_r <= current_adj_r + 1'b1;
            accum_count_r <= '0;
          end
          
          default: ;
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
        if (cmd_valid) begin
          if (cmd_is_factor) state_n = S_FAC_LOAD_DATA;
          else               state_n = S_VAR_LOAD_DATA;
        end
      end
      
      // Variable Node path
      S_VAR_LOAD_DATA: begin
        // 读取完成后直接在 compact payload 上累加 message。
        if (stream_done) begin
          if (msg_count_r == 4'd0) state_n = S_VAR_STORE_RESULT;
          else                     state_n = S_VAR_ACCUMULATE;
        end
      end
      
      S_VAR_ACCUMULATE: begin
        // 对 compact payload 分块累加完所有 message 后直接回写 belief。
        if (mat_done_r
            && (msg_count_r != 4'd0)
            && (accum_count_r >= msg_count_r - 1'b1)
            && (payload_chunk_r + 1'b1 >= payload_chunk_count_int)) begin
          state_n = S_VAR_STORE_RESULT;
        end
      end
      
      S_VAR_INVERT_LAM: begin
        if (mat_done_r) state_n = S_VAR_MVMUL;
      end
      
      S_VAR_MVMUL: begin
        if (mat_done_r) state_n = S_VAR_STORE_RESULT;
      end
      
      S_VAR_STORE_RESULT: begin
        if (stream_done) state_n = S_VAR_DONE;
      end
      
      S_VAR_DONE: state_n = S_IDLE;
      
      // Factor Node path (simplified - binary factor assumed)
      S_FAC_LOAD_DATA: begin
        if (stream_done) state_n = S_FAC_LOOP_INIT;
      end
      
      S_FAC_LOOP_INIT: begin
        state_n = S_FAC_CAVITY_ACCUM;
      end
      
      S_FAC_CAVITY_ACCUM: begin
        // Accumulate cavity for current adjacent variable
        // Skip the current adjacent variable
        if ((msg_count_r <= 4'd1 && mat_done)
            || (msg_count_r > 4'd1 && accum_count_r >= msg_count_r - 2 && mat_done)) begin
          state_n = S_FAC_EXTRACT_BLOCKS;
        end
      end
      
      S_FAC_EXTRACT_BLOCKS: begin
        state_n = S_FAC_INVERT_LNONO;
      end
      
      S_FAC_INVERT_LNONO: begin
        if (mat_done) state_n = S_FAC_COMPUTE_MESSAGE;
      end
      
      S_FAC_COMPUTE_MESSAGE: begin
        if (mat_done) state_n = S_FAC_STORE_MESSAGE;
      end
      
      S_FAC_STORE_MESSAGE: begin
        if (stream_done) state_n = S_FAC_NEXT_ADJACENT;
      end
      
      S_FAC_NEXT_ADJACENT: begin
        if (msg_count_r == 4'd0) begin
          state_n = S_FAC_DONE;
        end else if (current_adj_r >= msg_count_r - 1) begin
          state_n = S_FAC_DONE;
        end else begin
          state_n = S_FAC_LOOP_INIT;
        end
      end
      
      S_FAC_DONE: state_n = S_IDLE;
      
      default: state_n = S_IDLE;
    endcase
  end
  
  // ============================================================
  // Output logic
  // ============================================================
  
  assign cmd_ready = (state_r == S_IDLE);
  
  // Stream control
  always_comb begin
    stream_req_state = 1'b0;
    stream_req_messages = 1'b0;
    stream_xfer_bytes = '0;
    stream_in_ready = 1'b0;
    stream_out_valid = 1'b0;
    stream_out_data = '0;
    
    case (state_r)
      S_VAR_LOAD_DATA: begin
        stream_req_state = 1'b1;
        stream_xfer_bytes = 16'd256;  // Load prior + messages
        stream_in_ready = 1'b1;
      end

      S_VAR_ACCUMULATE: begin
        // 输入流在 LOAD_DATA 已经完成，这里只保留计算。
      end
      
      S_VAR_STORE_RESULT: begin
        stream_out_valid = 1'b1;
        // variable belief 直接在 addr0 的 compact payload 上原地更新并回写。
        stream_out_data[ADDR_W-1:0] = prior_eta_addr;
      end
      
      S_FAC_LOAD_DATA: begin
        stream_req_state = 1'b1;
        stream_req_messages = 1'b1;
        stream_xfer_bytes = 16'd512;
        stream_in_ready = 1'b1;
      end
      
      S_FAC_STORE_MESSAGE: begin
        stream_out_valid = 1'b1;
        stream_out_data[ADDR_W-1:0] = result_addr;
      end
      
      default: ;
    endcase
  end
  
  // Buffer write (for stream data input)
  always_comb begin
    buf_wr_valid = stream_in_valid && stream_in_ready;
    buf_wr_addr = ADDR_W'(0);  // Sequential write
    buf_wr_data = stream_in_data;
  end
  
  // Matrix command valid output (registered for Verilator scheduling)
  assign mat_cmd_valid = mat_cmd_valid_r;
  
  // done_o is driven by assign statement
  assign done_o = (state_r == S_VAR_DONE) || (state_r == S_FAC_DONE);
  
  // Matrix FSM commands
  always_comb begin
    // Defaults
    mat_cmd_valid_next = 1'b0;
    mat_cmd_op = `GBP_OP_MAT_ADD;
    mat_cmd_base_a = ADDR_W'(0);
    mat_cmd_base_b = ADDR_W'(0);
    mat_cmd_base_dest = ADDR_W'(0);
    mat_cmd_m = 4'd1;
    mat_cmd_n = 4'd1;
    mat_cmd_k = 4'd1;
    
    case (state_r)
      S_VAR_ACCUMULATE: begin
        // Accumulate one compact payload chunk from message[accum_count] into prior belief.
        mat_cmd_valid_next = mat_cmd_ready;
        mat_cmd_op = `GBP_OP_MAT_ADD;
        mat_cmd_base_a = prior_eta_addr + ADDR_W'(chunk_offset_int);
        mat_cmd_base_b =
            msg_base_addr
            + ADDR_W'(message_offset_words(accum_count_r, dofs_r) + chunk_offset_int);
        mat_cmd_base_dest = prior_eta_addr + ADDR_W'(chunk_offset_int);
        mat_cmd_m = 4'd1;
        mat_cmd_n = 4'(chunk_words_int);
      end
      
      S_VAR_INVERT_LAM: begin
        // Inverse of belief_lam
        mat_cmd_valid_next = mat_cmd_ready;
        mat_cmd_op = `GBP_OP_MAT_INV;
        mat_cmd_base_a = prior_lam_addr;
        mat_cmd_base_dest = result_addr;  // Store sigma
        mat_cmd_m = {1'b0, dofs_r};
        mat_cmd_n = {1'b0, dofs_r};
      end
      
      S_VAR_MVMUL: begin
        // mu = sigma * eta
        mat_cmd_valid_next = mat_cmd_ready;
        mat_cmd_op = `GBP_OP_MAT_VEC_MUL;
        mat_cmd_base_a = result_addr;  // sigma
        mat_cmd_base_b = prior_eta_addr;  // eta
        mat_cmd_base_dest = result_addr + ADDR_W'(lam_size_int);  // mu
        mat_cmd_m = {1'b0, dofs_r};
        mat_cmd_n = 4'd1;
        mat_cmd_k = {1'b0, dofs_r};
      end
      
      S_FAC_CAVITY_ACCUM: begin
        // Cavity accumulation (simplified)
        mat_cmd_valid_next = mat_cmd_ready;
        mat_cmd_op = `GBP_OP_MAT_ADD;
        // Add beliefs from other variables, subtract old messages
        // This is a placeholder for full cavity computation
      end
      
      S_FAC_INVERT_LNONO: begin
        // Inverse of lnono block
        mat_cmd_valid_next = mat_cmd_ready;
        mat_cmd_op = `GBP_OP_MAT_INV;
      end
      
      S_FAC_COMPUTE_MESSAGE: begin
        // Schur complement message computation
        mat_cmd_valid_next = mat_cmd_ready;
        mat_cmd_op = `GBP_OP_MAT_MUL;
      end
      
      default: ;
    endcase
  end

endmodule
