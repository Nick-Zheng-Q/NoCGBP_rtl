// gbp_compute_engine.sv
// Top-level GBP compute engine
// Integrates SIMD array, staging buffer, matrix FSM, and GBP control FSM
// Replaces existing compute_unit with full GBP computation support

`include "bsg_defines.sv"
`include "gbp_compute_defines.svh"

module gbp_compute_engine
  import gbp_pkg::*;
#(
    parameter int LANES = 16,
    parameter int MAX_DOFS = 6,
    parameter int MAX_ADJACENT = 8,
    parameter int STAGING_DEPTH = 1024,
    parameter int FP_EXP_WIDTH_P = 8,
    parameter int FP_MANT_WIDTH_P = 23,
    parameter int DIV_BITS_PER_ITER_P = 1
)(
    input  logic clk_i,
    input  logic rst_n_i,
    
    // Command interface (from control_unit)
    input  logic               cmd_valid_i,
    input  logic               cmd_is_factor_i,   // 0: variable node, 1: factor node
    input  logic [7:0]         cmd_node_idx_i,
    input  logic [2:0]         cmd_dofs_i,        // Node dimension (2, 3, or 6)
    input  logic [3:0]         cmd_adj_count_i,   // Number of adjacent nodes
    input  logic [3:0]         cmd_msg_count_i,   // 本轮需要消费的 message 条数
    input  logic [MAX_ADJACENT-1:0][2:0] cmd_neighbor_dofs_i, // Per-edge DOF for factor nodes
    input  logic [9:0]         cmd_state_words_i, // STATE words count for load/store
    input  logic [15:0]        cmd_wr_xfer_bytes_i,
    output logic               cmd_ready_o,
    output logic               compute_done_o,
    output logic               rsp_done_o,
    
    // Stream interfaces (connect to read_stream_if and write_stream_if)
    // Input stream (from read_stream_engine)
    output logic               stream_in_ready,
    input  logic               stream_in_valid,
    input  logic [255:0]       stream_in_data,    // 8 floats per beat
    
    // Output stream (to write_stream_engine)
    input  logic               stream_out_ready,
    output logic               stream_out_valid,
    output logic [BEAT_BITS-1:0] stream_out_data,
    
    // Configuration
    input  logic [31:0]        damping_factor_i   // FP32 damping (e.g., 0.4)
);

  logic reset_i;
  assign reset_i = ~rst_n_i;

  localparam int ADDR_W = $clog2(STAGING_DEPTH);

  // Strict compact form: words = dof + dof*(dof+1)/2
  // Beats = ceil(words / 8)
  function automatic int compact_words(input logic [2:0] dofs);
    begin
      compact_words = int'(dofs) + (int'(dofs) * (int'(dofs) + 1)) / 2;
    end
  endfunction

  function automatic logic [15:0] compact_payload_beats(
      input logic [2:0] dofs
  );
    int words;
    begin
      words = compact_words(dofs);
      compact_payload_beats = 16'((words + 7) / 8);
    end
  endfunction

  function automatic logic [15:0] bytes_to_beats(
      input logic [15:0] xfer_bytes
  );
    begin
      bytes_to_beats = (xfer_bytes + 16'd31) >> 5;
    end
  endfunction

  // Accumulate beats for messages with potentially different sizes (per-edge DOF)
  function automatic logic [15:0] accumulate_message_beats(
      input logic [3:0] msg_count,
      input logic [MAX_ADJACENT-1:0][2:0] neighbor_dofs
  );
    logic [15:0] total;
    int unsigned idx;
    begin
      total = 16'd0;
      for (idx = 0; idx < MAX_ADJACENT; idx = idx + 1) begin
        if (idx < msg_count) begin
          total = total + compact_payload_beats(neighbor_dofs[idx]);
        end
      end
      accumulate_message_beats = total;
    end
  endfunction
  
  // Sample mat_done for output to gbp_control_fsm
  always_ff @(posedge clk_i) begin
    mat_done_r <= mat_done;
  end
  
  // ============================================================
  // Internal signals
  // ============================================================
  
  // Staging buffer signals
  logic               buf_stream_wr_valid;
  logic [255:0]       buf_stream_wr_data;
  logic [ADDR_W-1:0]  buf_stream_wr_addr;
  logic               buf_stream_wr_ready;
  
  logic               buf_stream_rd_valid;
  logic [ADDR_W-1:0]  buf_stream_rd_addr;
  logic [255:0]       buf_stream_rd_data;
  logic               buf_stream_rd_ready;
  
  logic [LANES-1:0][ADDR_W-1:0] buf_simd_rd_addr_a;
  logic [LANES-1:0][ADDR_W-1:0] buf_simd_rd_addr_b;
  logic [LANES-1:0][31:0]       buf_simd_rd_data_a;
  logic [LANES-1:0][31:0]       buf_simd_rd_data_b;
  
  logic [LANES-1:0]             buf_simd_wr_valid;
  logic [LANES-1:0][ADDR_W-1:0] buf_simd_wr_addr;
  logic [LANES-1:0][31:0]       buf_simd_wr_data;
  
  // Matrix FSM signals
  logic               mat_cmd_valid;
  wire                mat_cmd_valid_sampled = mat_cmd_valid;  // Explicit sampling for Verilator
  
  // Debug (disabled for cleaner output)
  // always_ff @(posedge clk_i) begin
  //   $display("ENG_DBG: mat_cmd_valid=%b mat_cmd_valid_sampled=%b mat_cmd_ready=%b mat_done=%b", 
  //            mat_cmd_valid, mat_cmd_valid_sampled, mat_cmd_ready, mat_done);
  // end
  logic [2:0]         mat_cmd_op;
  logic [ADDR_W-1:0]  mat_cmd_base_a;
  logic [ADDR_W-1:0]  mat_cmd_base_b;
  logic [ADDR_W-1:0]  mat_cmd_base_dest;
  logic [5:0]         mat_cmd_m;
  logic [5:0]         mat_cmd_n;
  logic [5:0]         mat_cmd_k;
  logic               mat_cmd_ready;
  logic               mat_done;
  logic               mat_done_r;  // Sampling register for Verilator
  
  // GBP Control FSM signals
  logic               gbp_stream_req_state;
  logic               gbp_stream_req_messages;
  logic [15:0]        gbp_stream_xfer_bytes;
  logic               gbp_stream_grant;
  logic               gbp_stream_done;
  logic               stream_in_hs;
  logic               stream_out_hs;
  logic [15:0]        stream_target_beats;
  logic [15:0]        state_target_beats;
  logic [15:0]        stream_in_beats_r, stream_in_beats_n;
  logic [15:0]        stream_out_beats_r, stream_out_beats_n;
  logic               stream_dir_out_r, stream_dir_out_n;
  logic               stream_active_r, stream_active_n;
  logic               start_input_stream;
  logic               start_output_stream;
  
  logic               gbp_stream_in_ready;
  logic               gbp_stream_in_valid;
  logic [255:0]       gbp_stream_in_data;
  
  logic               gbp_stream_out_valid;
  logic               gbp_stream_out_ready;
  logic [255:0]       gbp_stream_out_data;
  logic [ADDR_W-1:0]  stream_wr_addr_r, stream_wr_addr_n;
  
  logic [ADDR_W-1:0]  gbp_buf_wr_addr;
  logic [255:0]       gbp_buf_wr_data;
  logic               gbp_buf_wr_valid;
  logic [ADDR_W-1:0]  gbp_msg_base_addr;
  logic               gbp_internal_rd_valid;
  logic [ADDR_W-1:0]  gbp_internal_rd_addr;
  logic [255:0]       gbp_internal_rd_data;
  logic [3:0]         cmd_msg_count_r;
  logic [15:0]        cmd_wr_xfer_bytes_r;
  logic [15:0]        cmd_stream_xfer_bytes_r;
  logic [MAX_ADJACENT-1:0][2:0] cmd_neighbor_dofs_r;
  logic [9:0]         cmd_state_words_r;
  logic               cmd_is_factor_r;
  logic [ADDR_W-1:0]  stream_out_base_addr;
  
  // SIMD array signals
  logic [LANES-1:0]             simd_op_add;
  logic [LANES-1:0]             simd_op_sub;
  logic [LANES-1:0]             simd_op_mul;
  logic [LANES-1:0]             simd_op_mac;
  logic [LANES-1:0][1:0]        simd_src_a;
  logic [LANES-1:0][1:0]        simd_src_b;
  logic [LANES-1:0][31:0]       simd_const;
  logic [LANES-1:0]             simd_valid;
  logic [LANES-1:0][31:0]       simd_result;
  logic                         simd_busy;
  
  // ============================================================
  // Staging Buffer
  // ============================================================
  staging_buffer #(
    .DEPTH(STAGING_DEPTH),
    .LANES(LANES)
  ) u_staging_buffer (
    .clk_i(clk_i),
    .rst_n_i(rst_n_i),
    
    .stream_wr_valid(buf_stream_wr_valid),
    .stream_wr_data(buf_stream_wr_data),
    .stream_wr_addr(buf_stream_wr_addr),
    .stream_wr_ready(buf_stream_wr_ready),
    
    .stream_rd_valid(buf_stream_rd_valid),
    .stream_rd_addr(buf_stream_rd_addr),
    .stream_rd_data(buf_stream_rd_data),
    .stream_rd_ready(buf_stream_rd_ready),
    
    .simd_rd_addr_a(buf_simd_rd_addr_a),
    .simd_rd_addr_b(buf_simd_rd_addr_b),
    .simd_rd_data_a(buf_simd_rd_data_a),
    .simd_rd_data_b(buf_simd_rd_data_b),
    
    .simd_wr_valid(buf_simd_wr_valid),
    .simd_wr_addr(buf_simd_wr_addr),
    .simd_wr_data(buf_simd_wr_data),
    
    .occupancy_o()
  );
  
  // ============================================================
  // SIMD Array
  // ============================================================
  simd_array #(
    .LANES(LANES),
    .FP_EXP_WIDTH_P(FP_EXP_WIDTH_P),
    .FP_MANT_WIDTH_P(FP_MANT_WIDTH_P),
    .DIV_BITS_PER_ITER_P(DIV_BITS_PER_ITER_P)
  ) u_simd_array (
    .clk_i(clk_i),
    .rst_n_i(rst_n_i),
    
    .op_add_en(simd_op_add),
    .op_sub_en(simd_op_sub),
    .op_mul_en(simd_op_mul),
    .op_div_en({LANES{1'b0}}),  // TODO: Connect for division operations
    .op_mac_en(simd_op_mac),
    
    .src_a_sel(simd_src_a),
    .src_b_sel(simd_src_b),
    .const_val(simd_const),
    
    .data_a_i(buf_simd_rd_data_a),
    .data_b_i(buf_simd_rd_data_b),
    
    .result_o(simd_result),
    .valid_o(simd_valid),
    .busy_o(simd_busy),
    
    .acc_clear(),
    .acc_load(),
    .acc_value_o()
  );
  
  // ============================================================
  // Matrix FSM
  // ============================================================
  matrix_fsm #(
    .LANES(LANES),
    .MAX_DOFS(MAX_DOFS),
    .ADDR_W(ADDR_W)
  ) u_matrix_fsm (
    .clk_i(clk_i),
    .rst_n_i(rst_n_i),
    
    .cmd_valid(mat_cmd_valid_sampled),
    .cmd_op(mat_cmd_op),
    .cmd_base_a(mat_cmd_base_a),
    .cmd_base_b(mat_cmd_base_b),
    .cmd_base_dest(mat_cmd_base_dest),
    .cmd_m(mat_cmd_m),
    .cmd_n(mat_cmd_n),
    .cmd_k(mat_cmd_k),
    .cmd_ready(mat_cmd_ready),
    .done_o(mat_done),
    
    .buf_rd_addr_a(buf_simd_rd_addr_a),
    .buf_rd_addr_b(buf_simd_rd_addr_b),
    .buf_rd_data_a(buf_simd_rd_data_a),
    .buf_rd_data_b(buf_simd_rd_data_b),
    
    .buf_wr_valid(buf_simd_wr_valid),
    .buf_wr_addr(buf_simd_wr_addr),
    .buf_wr_data(buf_simd_wr_data),
    
    .simd_op_add(simd_op_add),
    .simd_op_sub(simd_op_sub),
    .simd_op_mul(simd_op_mul),
    .simd_op_mac(simd_op_mac),
    .simd_src_a(simd_src_a),
    .simd_src_b(simd_src_b),
    .simd_const(simd_const),
    .simd_valid(simd_valid),
    .simd_result(simd_result)
  );
  
  // ============================================================
  // GBP Control FSM
  // ============================================================
  gbp_control_fsm #(
    .LANES(LANES),
    .MAX_DOFS(MAX_DOFS),
    .MAX_ADJACENT(MAX_ADJACENT),
    .STAGING_DEPTH(STAGING_DEPTH)
  ) u_gbp_control_fsm (
    .clk_i(clk_i),
    .rst_n_i(rst_n_i),
    
    .cmd_valid(cmd_valid_i),
    .cmd_is_factor(cmd_is_factor_i),
    .cmd_node_idx(cmd_node_idx_i),
    .cmd_dofs(cmd_dofs_i),
    .cmd_adj_count(cmd_adj_count_i),
    .cmd_msg_count(cmd_msg_count_i),
    .cmd_neighbor_dofs(cmd_neighbor_dofs_i),
    .cmd_state_words(cmd_state_words_i),
    .cmd_ready(cmd_ready_o),
    .done_o(rsp_done_o),
    
    .stream_req_state(gbp_stream_req_state),
    .stream_req_messages(gbp_stream_req_messages),
    .stream_xfer_bytes(gbp_stream_xfer_bytes),
    .stream_grant(gbp_stream_grant),
    .stream_done(gbp_stream_done),
    
    .stream_in_ready(gbp_stream_in_ready),
    .stream_in_valid(gbp_stream_in_valid),
    .stream_in_data(gbp_stream_in_data),
    
    .stream_out_valid(gbp_stream_out_valid),
    .stream_out_ready(gbp_stream_out_ready),
    .stream_out_data(gbp_stream_out_data),
    
    .mat_cmd_valid(mat_cmd_valid),
    .mat_cmd_op(mat_cmd_op),
    .mat_cmd_base_a(mat_cmd_base_a),
    .mat_cmd_base_b(mat_cmd_base_b),
    .mat_cmd_base_dest(mat_cmd_base_dest),
    .mat_cmd_m(mat_cmd_m),
    .mat_cmd_n(mat_cmd_n),
    .mat_cmd_k(mat_cmd_k),
    .mat_cmd_ready(mat_cmd_ready),
    .mat_done(mat_done_r),
    
    .buf_wr_addr(gbp_buf_wr_addr),
    .buf_wr_data(gbp_buf_wr_data),
    .buf_wr_valid(gbp_buf_wr_valid),
    .msg_base_addr_o(gbp_msg_base_addr),
    .internal_rd_valid(gbp_internal_rd_valid),
    .internal_rd_addr(gbp_internal_rd_addr),
    .internal_rd_data(gbp_internal_rd_data),

    .damping_factor(damping_factor_i)
  );
  
  // ============================================================
  // Stream interface connections
  // ============================================================
  
  // Input stream: Connect external to GBP control FSM
  assign stream_in_ready = gbp_stream_in_ready;
  assign gbp_stream_in_valid = stream_in_valid;
  assign gbp_stream_in_data = stream_in_data;
  
  // NS data write to staging buffer (variable accumulate path)
  // When ns data arrives during accumulate (stream not active), write to msg region
  logic [ADDR_W-1:0] ns_wr_offset_r;
  logic               ns_write_active;

  assign ns_write_active = !stream_active_r && !start_input_stream
                           && stream_in_valid && stream_in_ready
                           && !gbp_stream_req_state;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      ns_wr_offset_r <= '0;
    end else begin
      if (ns_write_active) begin
        ns_wr_offset_r <= ns_wr_offset_r + ADDR_W'(8);
      end
      // Reset when FSM leaves accumulate (new command or done)
      if (cmd_valid_i && cmd_ready_o) begin
        ns_wr_offset_r <= '0;
      end
    end
  end

  // Buffer write: Connect GBP control FSM to staging buffer
  // Priority: ns_write > stream_write > fsm_write
  logic stream_wr_sel;
  assign stream_wr_sel = stream_active_r || start_input_stream;
  assign buf_stream_wr_valid = ns_write_active ? 1'b1 : (stream_wr_sel ? stream_in_hs : gbp_buf_wr_valid);
  assign buf_stream_wr_data  = ns_write_active ? stream_in_data : (stream_wr_sel ? stream_in_data : gbp_buf_wr_data);
  assign buf_stream_wr_addr  = ns_write_active ? (gbp_msg_base_addr + ns_wr_offset_r) : (stream_wr_sel ? (start_input_stream ? ADDR_W'(0) : stream_wr_addr_r) : gbp_buf_wr_addr);

  // Internal read data for FSM
  assign gbp_internal_rd_data = buf_stream_rd_data;

  // Output stream: store state/message 时从 staging_buffer 逐 beat 读出真实结果。
  assign stream_out_valid = gbp_stream_out_valid;
  assign gbp_stream_out_ready = stream_out_ready;
  assign stream_out_base_addr = gbp_stream_out_data[ADDR_W-1:0];
  // Internal read takes priority over output stream when FSM needs direct buffer access
  assign buf_stream_rd_valid = gbp_internal_rd_valid
                               || ((stream_active_r && stream_dir_out_r) || start_output_stream);
  assign buf_stream_rd_addr = gbp_internal_rd_valid
                              ? gbp_internal_rd_addr
                              : stream_out_base_addr
                                + ADDR_W'((start_output_stream ? 16'd0 : stream_out_beats_r) << 3);
  assign stream_out_data = buf_stream_rd_data;
  
  // Stream grant/done (simplified - always grant for now)
  assign gbp_stream_grant = 1'b1;
  // stream_done 必须对应整条 xfer 完成，不能把单 beat 握手误判成整次传输结束。
  assign state_target_beats = bytes_to_beats(gbp_stream_xfer_bytes);
  // Factor path: state already includes old messages; do NOT add message beats.
  // Variable path: state (prior) + messages are separate streams.
  assign stream_target_beats =
      stream_dir_out_r ? bytes_to_beats(cmd_wr_xfer_bytes_r)
                       : (cmd_is_factor_r
                          ? state_target_beats
                          : ((gbp_stream_req_state && gbp_stream_req_messages)
                             ? (state_target_beats + accumulate_message_beats(cmd_msg_count_r, cmd_neighbor_dofs_r))
                             : (gbp_stream_req_state ? state_target_beats
                                : (gbp_stream_req_messages ? accumulate_message_beats(cmd_msg_count_r, cmd_neighbor_dofs_r)
                                   : 16'd0))));

  assign stream_in_hs =
      ((stream_active_r && !stream_dir_out_r) || start_input_stream)
      && stream_in_valid
      && stream_in_ready;
  assign stream_out_hs =
      ((stream_active_r && stream_dir_out_r) || start_output_stream)
      && gbp_stream_out_valid
      && gbp_stream_out_ready;

  assign gbp_stream_done =
      ((!stream_dir_out_r || start_input_stream)
       && stream_in_hs
       && (((start_input_stream ? 16'd1 : (stream_in_beats_r + 16'd1)) >= stream_target_beats)))
      || ((stream_dir_out_r || start_output_stream)
          && stream_out_hs
          && ((start_output_stream ? 16'd1 : (stream_out_beats_r + 16'd1)) >= stream_target_beats));

  // Compute done (pulse when computation completes)
  logic compute_done_r;
  logic               cmd_accepted_r;  // pulse: command was accepted previous cycle

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      compute_done_r <= 1'b0;
      stream_in_beats_r <= '0;
      stream_out_beats_r <= '0;
      stream_dir_out_r <= 1'b0;
      stream_active_r <= 1'b0;
      stream_wr_addr_r <= '0;
      cmd_msg_count_r <= '0;
      cmd_wr_xfer_bytes_r <= '0;
      cmd_stream_xfer_bytes_r <= '0;
      cmd_neighbor_dofs_r <= '0;
      cmd_state_words_r <= '0;
      cmd_is_factor_r <= 1'b0;
      cmd_accepted_r <= 1'b0;
    end else begin
      compute_done_r <= rsp_done_o;
      stream_in_beats_r <= stream_in_beats_n;
      stream_out_beats_r <= stream_out_beats_n;
      stream_dir_out_r <= stream_dir_out_n;
      stream_active_r <= stream_active_n;
      stream_wr_addr_r <= stream_wr_addr_n;
      cmd_accepted_r <= (cmd_valid_i && cmd_ready_o);
      if (cmd_valid_i && cmd_ready_o) begin
        cmd_msg_count_r <= cmd_msg_count_i;
        cmd_wr_xfer_bytes_r <= cmd_wr_xfer_bytes_i;
        cmd_neighbor_dofs_r <= cmd_neighbor_dofs_i;
        cmd_state_words_r <= cmd_state_words_i;
        cmd_is_factor_r <= cmd_is_factor_i;
      end
      // Capture stream_xfer_bytes one cycle after cmd accepted (FSM now in load state)
      if (cmd_accepted_r) begin
        cmd_stream_xfer_bytes_r <= gbp_stream_xfer_bytes;
      end
    end
  end
  assign compute_done_o = compute_done_r;

  always_comb begin
    stream_in_beats_n = stream_in_beats_r;
    stream_out_beats_n = stream_out_beats_r;
    stream_dir_out_n = stream_dir_out_r;
    stream_active_n = stream_active_r;
    stream_wr_addr_n = stream_wr_addr_r;

    start_input_stream = (!stream_active_r && (gbp_stream_req_state || gbp_stream_req_messages));
    start_output_stream = (!stream_active_r && gbp_stream_out_valid);

    if (start_input_stream) begin
      stream_active_n = 1'b1;
      stream_dir_out_n = 1'b0;
      stream_in_beats_n = 16'd0;
      stream_out_beats_n = 16'd0;
      stream_wr_addr_n = '0;
    end else if (start_output_stream) begin
      stream_active_n = 1'b1;
      stream_dir_out_n = 1'b1;
      stream_in_beats_n = 16'd0;
      stream_out_beats_n = 16'd0;
    end

    if (stream_in_hs) begin
      stream_in_beats_n = stream_in_beats_r + 16'd1;
      stream_wr_addr_n = stream_wr_addr_r + ADDR_W'(8);
    end

    if (stream_out_hs) begin
      stream_out_beats_n = stream_out_beats_r + 16'd1;
    end

    if (gbp_stream_done) begin
      stream_active_n = 1'b0;
      stream_in_beats_n = 16'd0;
      stream_out_beats_n = 16'd0;
    end
  end

endmodule
