// gbp_control_fsm.sv
// GBP node computation control FSM
// Orchestrates Variable Node update_belief and Factor Node compute_messages

`include "bsg_defines.sv"
`include "gbp_compute_defines.svh"

module gbp_control_fsm #(
    parameter int LANES = 16,
    parameter int MAX_DOFS = 6,
    parameter int MAX_ADJACENT = 8,
    parameter int STAGING_DEPTH = 1024,
    parameter int ADDR_W = $clog2(STAGING_DEPTH)
) (
    input logic clk_i,
    input logic rst_n_i,

    // External command interface
    input  logic                         cmd_valid,
    input  logic                         cmd_is_factor,      // 0: variable, 1: factor
    input  logic [             7:0]      cmd_node_idx,
    input  logic [             2:0]      cmd_dofs,           // Node dimension (1, 2, 3, or 6)
    input  logic [             3:0]      cmd_adj_count,      // Number of adjacent nodes
    input  logic [             3:0]      cmd_msg_count,      // Messages to consume this round
    input  logic [MAX_ADJACENT-1:0][2:0] cmd_neighbor_dofs,  // Per-edge DOF for factor nodes
    input  logic [             9:0]      cmd_state_words,    // STATE words count for load/store
    output logic                         cmd_ready,
    output logic                         done_o,

    // Stream control (to load/store data from SPM)
    output logic        stream_req_state,
    output logic        stream_req_messages,
    output logic [15:0] stream_xfer_bytes,
    input  logic        stream_grant,
    input  logic        stream_done,

    // Stream data interface
    output logic         stream_in_ready,
    input  logic         stream_in_valid,
    input  logic [255:0] stream_in_data,

    output logic         stream_out_valid,
    input  logic         stream_out_ready,
    output logic [255:0] stream_out_data,

    // Matrix FSM interface
    output logic              mat_cmd_valid,
    output logic [       2:0] mat_cmd_op,
    output logic [ADDR_W-1:0] mat_cmd_base_a,
    output logic [ADDR_W-1:0] mat_cmd_base_b,
    output logic [ADDR_W-1:0] mat_cmd_base_dest,
    output logic [       5:0] mat_cmd_m,
    output logic [       5:0] mat_cmd_n,
    output logic [       5:0] mat_cmd_k,
    input  logic              mat_cmd_ready,
    input  logic              mat_done,

    // Staging buffer direct access (for data movement)
    output logic [ADDR_W-1:0] buf_wr_addr,
    output logic [     255:0] buf_wr_data,
    output logic              buf_wr_valid,

    // Message base address for ns data routing (variable accumulate path)
    output logic [ADDR_W-1:0] msg_base_addr_o,

    // Staging buffer internal read (for unpack/pack)
    output logic              internal_rd_valid,
    output logic [ADDR_W-1:0] internal_rd_addr,
    input  logic [     255:0] internal_rd_data,

    // Configuration
    input logic [31:0] damping_factor  // FP32 damping (e.g., 0.4)
);

  import gbp_pkg::*;

  logic reset_i;
  assign reset_i = ~rst_n_i;

  // Strict compact form lookup (words = dof + dof*(dof+1)/2)
  localparam int COMPACT_PAYLOAD_WORDS[0:7] = '{0, 2, 5, 9, 14, 20, 27, 0};
  localparam int SQUARE_WORDS[0:7] = '{0, 1, 4, 9, 16, 25, 36, 0};

  // Cumulative message offset for factor nodes (supports mixed DOF)
  function automatic int message_offset_words(input logic [3:0] msg_index,
                                              input logic [MAX_ADJACENT-1:0][2:0] neighbor_dofs);
    int offset;
    int j;
    begin
      offset = 0;
      for (j = 0; j < MAX_ADJACENT; j = j + 1) begin
        if (j < msg_index) begin
          offset = offset + COMPACT_PAYLOAD_WORDS[neighbor_dofs[j]];
        end
      end
      message_offset_words = offset;
    end
  endfunction

  // Compute total words for all per-edge messages
  function automatic int total_message_words(input logic [3:0] msg_count,
                                             input logic [MAX_ADJACENT-1:0][2:0] neighbor_dofs);
    int total;
    int j;
    begin
      total = 0;
      for (j = 0; j < MAX_ADJACENT; j = j + 1) begin
        if (j < msg_count) begin
          total = total + COMPACT_PAYLOAD_WORDS[neighbor_dofs[j]];
        end
      end
      total_message_words = total;
    end
  endfunction

  // ============================================================
  // Address allocation for factor node (binary, uniform DOF)
  // ============================================================
  function automatic int fac_msg_compact_base(input int idx, input int d);
    fac_msg_compact_base = idx * (d + (d * (d + 1)) / 2);
  endfunction

  function automatic int fac_msg_dense_base(input int idx, input int d);
    int c = d + (d * (d + 1)) / 2;
    fac_msg_dense_base = 2 * c + idx * (d + d * d);
  endfunction

  function automatic int fac_out_dense_base(input int d);
    int c = d + (d * (d + 1)) / 2;
    fac_out_dense_base = 2 * c + 2 * (2 * d) + (2 * d) * (2 * d) + (2 * d) + d + d * d;
  endfunction

  function automatic int fac_out_compact_base(input int d);
    int c = d + (d * (d + 1)) / 2;
    fac_out_compact_base = fac_out_dense_base(d) + d + d * d;
  endfunction

  // J, z, J^T, Lambda_f, eta_f bases (measurement_dim = d assumed)
  function automatic int fac_j_base(input int d);
    int c = d + (d * (d + 1)) / 2;
    fac_j_base = 2 * c;
  endfunction
  function automatic int fac_z_base(input int d);
    fac_z_base = fac_j_base(d) + d * (2 * d);
  endfunction
  function automatic int fac_jt_base(input int d);
    fac_jt_base = fac_z_base(d) + d;
  endfunction
  function automatic int fac_lf_base(input int d);
    fac_lf_base = fac_jt_base(d) + (2 * d) * d;
  endfunction

  function automatic int fac_lf_00(input int d);
    fac_lf_00 = fac_lf_base(d);
  endfunction
  function automatic int fac_lf_01(input int d);
    fac_lf_01 = fac_lf_base(d) + d * d;
  endfunction
  function automatic int fac_lf_10(input int d);
    fac_lf_10 = fac_lf_base(d) + 2 * d * d;
  endfunction
  function automatic int fac_lf_11(input int d);
    fac_lf_11 = fac_lf_base(d) + 3 * d * d;
  endfunction

  function automatic int fac_ef_base(input int d);
    fac_ef_base = fac_lf_base(d) + 4 * d * d;
  endfunction
  function automatic int fac_ef_0(input int d);
    fac_ef_0 = fac_ef_base(d);
  endfunction
  function automatic int fac_ef_1(input int d);
    fac_ef_1 = fac_ef_base(d) + d;
  endfunction

  function automatic int fac_tmp1_base(input int d);
    fac_tmp1_base = fac_ef_base(d) + 2 * d;
  endfunction
  function automatic int fac_tmp2_base(input int d);
    fac_tmp2_base = fac_tmp1_base(d) + d * d;
  endfunction
  function automatic int fac_tmp3_base(input int d);
    fac_tmp3_base = fac_tmp2_base(d) + d * d;
  endfunction
  function automatic int fac_tmp4_base(input int d);
    fac_tmp4_base = fac_tmp3_base(d) + d;
  endfunction

  // Compact Lambda index: given (row, col) in dense matrix, return index in compact form
  function automatic int compact_lambda_idx(input int i, input int j, input int d);
    int offset, k;
    int row, col;
    begin
      row = (j < i) ? j : i;
      col = (j < i) ? i : j;
      offset = d;  // skip eta
      for (k = 0; k < row; k = k + 1) offset = offset + (d - k);
      compact_lambda_idx = offset + (col - row);
    end
  endfunction

  // ============================================================
  // State definitions (from gbp_pkg)
  // ============================================================

  gbp_fsm_state_e state_r, state_n;

  // Unpack/Pack sub-states
  typedef enum logic [2:0] {
    FMT_IDLE,
    FMT_RD_COMPACT,
    FMT_WAIT_COMPACT,
    FMT_RD_DENSE,
    FMT_WAIT_DENSE,
    FMT_WR,
    FMT_DONE
  } fmt_state_e;

  // Node parameters
  logic is_factor_r;
  logic [2:0] dofs_r;
  logic [3:0] adj_count_r;
  logic [3:0] msg_count_r;
  logic [3:0] current_adj_r;
  logic [MAX_ADJACENT-1:0][2:0] neighbor_dofs_r;

  // Computed values
  logic [ADDR_W-1:0] prior_eta_addr;
  logic [ADDR_W-1:0] prior_lam_addr;
  logic [ADDR_W-1:0] msg_base_addr;
  logic [ADDR_W-1:0] result_addr;

  int payload_size_int;
  int msg_size_int;
  int lam_size_int;

  always_comb begin
    payload_size_int = COMPACT_PAYLOAD_WORDS[dofs_r];
    msg_size_int = payload_size_int;
    lam_size_int = SQUARE_WORDS[dofs_r];
  end

  assign prior_eta_addr = ADDR_W'(0);
  assign prior_lam_addr = ADDR_W'(dofs_r);
  assign msg_base_addr = ADDR_W'(payload_size_int);
  assign msg_base_addr_o = msg_base_addr;
  assign result_addr = ADDR_W'(STAGING_DEPTH - 64);

  // Accumulation counter
  logic [3:0] accum_count_r;

  // Matrix command valid register
  logic mat_cmd_valid_r;
  logic mat_cmd_valid_next;
  logic mat_done_r;

  // Build J^T registers
  logic [7:0][31:0] jt_row_buffer_r;
  logic [3:0] jt_row_r;
  logic [2:0] jt_col_r;
  logic [2:0] jt_word_in_beat_r;
  logic [ADDR_W-1:0] jt_rd_addr_r;
  logic [31:0] jt_val_r;
  logic jt_done_r;

  // Format (unpack/pack) registers
  fmt_state_e fmt_substate_r, fmt_substate_n;
  logic [5:0] fmt_elem_r, fmt_elem_n;
  logic [2:0] fmt_row_r, fmt_row_n;
  logic [2:0] fmt_col_r, fmt_col_n;
  logic [255:0] fmt_beat_r, fmt_beat_n;
  logic [31:0] fmt_val_r, fmt_val_n;
  logic [ADDR_W-1:0] fmt_src_base_r, fmt_src_base_n;
  logic [ADDR_W-1:0] fmt_dst_base_r, fmt_dst_base_n;
  logic [5:0] fmt_total_elems_r, fmt_total_elems_n;
  logic fmt_is_pack_r, fmt_is_pack_n;

  // Modified beat for write
  logic [255:0] modified_beat;
  always_comb begin
    modified_beat = fmt_beat_r;
    if (fmt_substate_r == FMT_WR) modified_beat[{fmt_elem_r[2:0], 5'b0}+:32] = fmt_val_r;
  end

  // Debug
  always_ff @(posedge clk_i) begin
    if (state_r == GFSM_VAR_DONE || state_r == GFSM_FAC_DONE)
      $display("GBP_FSM_DBG: DONE state=%b done_o=%b reset_i=%b", state_r, done_o, reset_i);
  end

  always_ff @(posedge clk_i) begin
    if (!reset_i && state_r != state_n) begin
      $display("GBP_FSM_DBG: state %s -> %s @ time=%0t", state_r.name(), state_n.name(), $time);
    end
  end

  // ============================================================
  // State machine sequential logic
  // ============================================================
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= GFSM_IDLE;
      is_factor_r <= 1'b0;
      dofs_r <= '0;
      adj_count_r <= '0;
      msg_count_r <= '0;
      current_adj_r <= '0;
      accum_count_r <= '0;
      neighbor_dofs_r <= '0;
      mat_cmd_valid_r <= 1'b0;
      mat_done_r <= 1'b0;
      jt_row_buffer_r <= '0;
      jt_row_r <= '0;
      jt_col_r <= '0;
      jt_word_in_beat_r <= '0;
      jt_rd_addr_r <= '0;
      jt_val_r <= '0;
      jt_done_r <= 1'b0;
      fmt_substate_r <= FMT_IDLE;
      fmt_elem_r <= '0;
      fmt_row_r <= '0;
      fmt_col_r <= '0;
      fmt_beat_r <= '0;
      fmt_val_r <= '0;
      fmt_src_base_r <= '0;
      fmt_dst_base_r <= '0;
      fmt_total_elems_r <= '0;
      fmt_is_pack_r <= 1'b0;
    end else begin
      state_r <= state_n;
      // Clear mat_cmd_valid_r when matrix operation completes to prevent
      // stale value from triggering a new operation in the next state
      if (mat_done_r) mat_cmd_valid_r <= 1'b0;
      else mat_cmd_valid_r <= mat_cmd_valid_next;
      mat_done_r <= mat_done;
      fmt_substate_r <= fmt_substate_n;
      fmt_elem_r <= fmt_elem_n;
      fmt_row_r <= fmt_row_n;
      fmt_col_r <= fmt_col_n;
      fmt_beat_r <= fmt_beat_n;
      fmt_val_r <= fmt_val_n;
      fmt_src_base_r <= fmt_src_base_n;
      fmt_dst_base_r <= fmt_dst_base_n;
      fmt_total_elems_r <= fmt_total_elems_n;
      fmt_is_pack_r <= fmt_is_pack_n;

      if (cmd_valid && cmd_ready) begin
        is_factor_r <= cmd_is_factor;
        dofs_r <= cmd_dofs;
        adj_count_r <= cmd_adj_count;
        msg_count_r <= cmd_msg_count;
        neighbor_dofs_r <= cmd_neighbor_dofs;
        current_adj_r <= '0;
        accum_count_r <= '0;
      end else begin
        case (state_r)
          GFSM_VAR_ACCUMULATE: begin
            if (mat_done_r) begin
              if (msg_count_r != 4'd0 && accum_count_r >= msg_count_r - 1'b1) begin
                accum_count_r <= '0;
              end else begin
                accum_count_r <= accum_count_r + 1'b1;
              end
            end
          end

          GFSM_FAC_BUILD_JT: begin
            // Build J^T from J using internal read/write
            case (jt_col_r)
              3'd0: begin
                jt_rd_addr_r <= ADDR_W'((fac_j_base(
                    dofs_r
                ) + jt_col_r * (2 * dofs_r) + jt_row_r) & (~7));
                jt_word_in_beat_r <= 3'((fac_j_base(
                    dofs_r
                ) + jt_col_r * (2 * dofs_r) + jt_row_r) & 7);
              end
              default: ;
            endcase
            if (jt_done_r) begin
              jt_row_r  <= '0;
              jt_col_r  <= '0;
              jt_done_r <= 1'b0;
            end else if (internal_rd_valid) begin
              jt_val_r <= internal_rd_data[{jt_word_in_beat_r, 5'b0}+:32];
              jt_row_buffer_r[jt_col_r] <= internal_rd_data[{jt_word_in_beat_r, 5'b0}+:32];
              if (jt_col_r + 1 >= dofs_r) begin
                // Write J^T row
                jt_done_r <= 1'b1;
              end else begin
                jt_col_r <= jt_col_r + 1;
              end
            end
          end

          GFSM_FAC_NEXT_ADJACENT: begin
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
    fmt_substate_n = fmt_substate_r;
    fmt_elem_n = fmt_elem_r;
    fmt_row_n = fmt_row_r;
    fmt_col_n = fmt_col_r;
    fmt_beat_n = fmt_beat_r;
    fmt_val_n = fmt_val_r;
    fmt_src_base_n = fmt_src_base_r;
    fmt_dst_base_n = fmt_dst_base_r;
    fmt_total_elems_n = fmt_total_elems_r;
    fmt_is_pack_n = fmt_is_pack_r;

    case (state_r)
      GFSM_IDLE: begin
        if (cmd_valid) begin
          if (cmd_is_factor) state_n = GFSM_FAC_LOAD_DATA;
          else state_n = GFSM_VAR_LOAD_DATA;
        end
      end

      // Variable Node path
      GFSM_VAR_LOAD_DATA: begin
        if (stream_done) begin
          if (msg_count_r == 4'd0) state_n = GFSM_VAR_INVERT_LAM;
          else state_n = GFSM_VAR_ACCUMULATE;
        end
      end

      GFSM_VAR_ACCUMULATE: begin
        if (mat_done_r && (msg_count_r != 4'd0) && (accum_count_r >= msg_count_r - 1'b1)) begin
          state_n = GFSM_VAR_INVERT_LAM;
        end
      end

      GFSM_VAR_INVERT_LAM: begin
        if (mat_done_r && mat_cmd_valid_r) state_n = GFSM_VAR_MVMUL;
      end

      GFSM_VAR_MVMUL: begin
        if (mat_done_r && mat_cmd_valid_r) state_n = GFSM_VAR_STORE_RESULT;
      end

      GFSM_VAR_STORE_RESULT: begin
        if (stream_done) state_n = GFSM_VAR_DONE;
      end

      GFSM_VAR_DONE: state_n = GFSM_IDLE;

      // Factor Node path
      GFSM_FAC_LOAD_DATA: begin
        if (stream_done) state_n = GFSM_FAC_BUILD_JT;
      end

      GFSM_FAC_BUILD_JT: begin
        if (jt_done_r) state_n = GFSM_FAC_COMPUTE_ETAF;
      end

      GFSM_FAC_COMPUTE_ETAF: begin
        if (mat_done_r) state_n = GFSM_FAC_COMPUTE_LF;
      end

      GFSM_FAC_COMPUTE_LF: begin
        if (mat_done_r) state_n = GFSM_FAC_UNPACK_MSG0;
      end

      GFSM_FAC_UNPACK_MSG0: begin
        if (fmt_substate_r == FMT_IDLE) begin
          fmt_src_base_n = ADDR_W'(fac_msg_compact_base(0, dofs_r));
          fmt_dst_base_n = ADDR_W'(fac_msg_dense_base(0, dofs_r));
          fmt_total_elems_n = 6'(dofs_r + dofs_r * dofs_r);
          fmt_is_pack_n = 1'b0;
          fmt_elem_n = '0;
          fmt_row_n = '0;
          fmt_col_n = '0;
          fmt_substate_n = FMT_RD_COMPACT;
        end else if (fmt_substate_r == FMT_DONE) begin
          state_n = GFSM_FAC_UNPACK_MSG1;
        end
      end

      GFSM_FAC_UNPACK_MSG1: begin
        if (fmt_substate_r == FMT_IDLE) begin
          fmt_src_base_n = ADDR_W'(fac_msg_compact_base(1, dofs_r));
          fmt_dst_base_n = ADDR_W'(fac_msg_dense_base(1, dofs_r));
          fmt_total_elems_n = 6'(dofs_r + dofs_r * dofs_r);
          fmt_is_pack_n = 1'b0;
          fmt_elem_n = '0;
          fmt_row_n = '0;
          fmt_col_n = '0;
          fmt_substate_n = FMT_RD_COMPACT;
        end else if (fmt_substate_r == FMT_DONE) begin
          state_n = GFSM_FAC_ADD_BLOCK;
        end
      end

      GFSM_FAC_ADD_BLOCK: begin
        if (mat_done_r) state_n = GFSM_FAC_INVERT_BLOCK;
      end

      GFSM_FAC_INVERT_BLOCK: begin
        if (mat_done_r) state_n = GFSM_FAC_MUL_BLOCK1;
      end

      GFSM_FAC_MUL_BLOCK1: begin
        if (mat_done_r) state_n = GFSM_FAC_MUL_BLOCK2;
      end

      GFSM_FAC_MUL_BLOCK2: begin
        if (mat_done_r) state_n = GFSM_FAC_SUB_LAMBDA;
      end

      GFSM_FAC_SUB_LAMBDA: begin
        if (mat_done_r) state_n = GFSM_FAC_ADD_ETA;
      end

      GFSM_FAC_ADD_ETA: begin
        if (mat_done_r) state_n = GFSM_FAC_MVMUL_ETA;
      end

      GFSM_FAC_MVMUL_ETA: begin
        if (mat_done_r) state_n = GFSM_FAC_SUB_ETA;
      end

      GFSM_FAC_SUB_ETA: begin
        if (mat_done_r) state_n = GFSM_FAC_PACK_MSG;
      end

      GFSM_FAC_PACK_MSG: begin
        if (fmt_substate_r == FMT_IDLE) begin
          fmt_src_base_n = ADDR_W'(fac_out_dense_base(dofs_r));
          fmt_dst_base_n = ADDR_W'(fac_out_compact_base(dofs_r));
          fmt_total_elems_n = 6'(dofs_r + dofs_r * dofs_r);
          fmt_is_pack_n = 1'b1;
          fmt_elem_n = '0;
          fmt_row_n = '0;
          fmt_col_n = '0;
          fmt_substate_n = FMT_RD_COMPACT;
        end else if (fmt_substate_r == FMT_DONE) begin
          state_n = GFSM_FAC_STORE_MESSAGE;
        end
      end

      GFSM_FAC_STORE_MESSAGE: begin
        if (stream_done) state_n = GFSM_FAC_NEXT_ADJACENT;
      end

      GFSM_FAC_NEXT_ADJACENT: begin
        if (msg_count_r == 4'd0) begin
          state_n = GFSM_FAC_DONE;
        end else if (current_adj_r >= msg_count_r - 1) begin
          state_n = GFSM_FAC_DONE;
        end else begin
          state_n = GFSM_FAC_ADD_BLOCK;
        end
      end

      GFSM_FAC_DONE: state_n = GFSM_IDLE;

      default: state_n = GFSM_IDLE;
    endcase

    // Format sub-state machine (shared for unpack/pack)
    case (fmt_substate_r)
      FMT_IDLE: begin
        // Initiated by entering S_FAC_UNPACK_MSGx or GFSM_FAC_PACK_MSG
      end

      FMT_RD_COMPACT: begin
        fmt_substate_n = FMT_WAIT_COMPACT;
      end

      FMT_WAIT_COMPACT: begin
        int cidx, word_in_beat;
        if (!fmt_is_pack_r) begin
          if (fmt_elem_r < dofs_r) cidx = fmt_elem_r;
          else
            cidx = compact_lambda_idx(
              (fmt_elem_r - dofs_r) / dofs_r, (fmt_elem_r - dofs_r) % dofs_r, dofs_r
            );
        end else begin
          if (fmt_elem_r < dofs_r) cidx = fmt_elem_r;
          else
            cidx = compact_lambda_idx(
              (fmt_elem_r - dofs_r) / dofs_r, (fmt_elem_r - dofs_r) % dofs_r, dofs_r
            );
        end
        word_in_beat = cidx & 7;
        fmt_val_n = internal_rd_data[{3'(word_in_beat), 5'b0}+:32];
        fmt_substate_n = FMT_RD_DENSE;
      end

      FMT_RD_DENSE: begin
        fmt_substate_n = FMT_WAIT_DENSE;
      end

      FMT_WAIT_DENSE: begin
        fmt_beat_n = internal_rd_data;
        fmt_substate_n = FMT_WR;
      end

      FMT_WR: begin
        // Advance to next element
        fmt_elem_n = fmt_elem_r + 1;
        if (fmt_elem_r + 1 >= fmt_total_elems_r) begin
          fmt_substate_n = FMT_DONE;
        end else begin
          fmt_substate_n = FMT_RD_COMPACT;
        end
      end

      FMT_DONE: begin
        fmt_substate_n = FMT_IDLE;
      end

      default: fmt_substate_n = FMT_IDLE;
    endcase
  end

  // ============================================================
  // Output logic
  // ============================================================

  assign cmd_ready = (state_r == GFSM_IDLE);

  // Stream control
  always_comb begin
    stream_req_state = 1'b0;
    stream_req_messages = 1'b0;
    stream_xfer_bytes = '0;
    stream_in_ready = 1'b0;
    stream_out_valid = 1'b0;
    stream_out_data = '0;

    case (state_r)
      GFSM_VAR_LOAD_DATA: begin
        stream_req_state  = 1'b1;
        stream_xfer_bytes = 16'(payload_size_int * 4);
        stream_in_ready   = 1'b1;
      end

      GFSM_VAR_ACCUMULATE: begin
        // Messages come from accumulator via stream_in
        stream_in_ready = 1'b1;
      end

      GFSM_VAR_STORE_RESULT: begin
        stream_out_valid = 1'b1;
        // Output mu, stored at result_addr by MVMUL
        stream_out_data[ADDR_W-1:0] = result_addr;
      end

      GFSM_FAC_LOAD_DATA: begin
        stream_req_state = 1'b1;
        stream_req_messages = 1'b1;
        // Load STATE: use cmd_state_words provided by control unit
        stream_xfer_bytes = 16'(cmd_state_words * 4);
        stream_in_ready = 1'b1;
      end

      GFSM_FAC_STORE_MESSAGE: begin
        stream_out_valid = 1'b1;
        stream_out_data[ADDR_W-1:0] = ADDR_W'(message_offset_words(current_adj_r, neighbor_dofs_r));
      end

      default: ;
    endcase
  end

  // Buffer write and internal read
  always_comb begin
    buf_wr_valid = 1'b0;
    buf_wr_addr = ADDR_W'(0);
    buf_wr_data = '0;
    internal_rd_valid = 1'b0;
    internal_rd_addr = ADDR_W'(0);

    // Build J^T outputs
    if (state_r == GFSM_FAC_BUILD_JT) begin
      if (!jt_done_r) begin
        internal_rd_valid = 1'b1;
        internal_rd_addr =
            ADDR_W'((fac_j_base(dofs_r) + jt_col_r * (2 * dofs_r) + jt_row_r) & (~ADDR_W'(7)));
      end else begin
        // Write J^T row
        buf_wr_valid = 1'b1;
        buf_wr_addr = ADDR_W'(fac_jt_base(dofs_r) + jt_row_r * dofs_r);
        buf_wr_data = {
          128'h0, jt_row_buffer_r[3], jt_row_buffer_r[2], jt_row_buffer_r[1], jt_row_buffer_r[0]
        };
        case (dofs_r)
          3'd6:
          buf_wr_data = {
            jt_row_buffer_r[5],
            jt_row_buffer_r[4],
            jt_row_buffer_r[3],
            jt_row_buffer_r[2],
            jt_row_buffer_r[1],
            jt_row_buffer_r[0],
            64'h0
          };
          3'd3: buf_wr_data = {jt_row_buffer_r[2], jt_row_buffer_r[1], jt_row_buffer_r[0], 160'h0};
          3'd1: buf_wr_data = {jt_row_buffer_r[0], 224'h0};
          default: ;
        endcase
      end
    end

    // Format sub-state outputs
    case (fmt_substate_r)
      FMT_RD_COMPACT: begin
        int cidx, beat_addr;
        if (!fmt_is_pack_r) begin
          // UNPACK: read compact element
          if (fmt_elem_r < dofs_r) begin
            // eta: direct mapping
            cidx = fmt_elem_r;
          end else begin
            int di = fmt_elem_r - dofs_r;
            int row = di / dofs_r;
            int col = di % dofs_r;
            cidx = compact_lambda_idx(row, col, dofs_r);
          end
        end else begin
          // PACK: read dense element, compute compact index
          if (fmt_elem_r < dofs_r) begin
            cidx = fmt_elem_r;
          end else begin
            int ci = fmt_elem_r - dofs_r;
            int row = ci / dofs_r;
            int col = ci % dofs_r;
            cidx = compact_lambda_idx(row, col, dofs_r);
          end
        end
        beat_addr = cidx >> 3;
        internal_rd_valid = 1'b1;
        internal_rd_addr = fmt_src_base_r + ADDR_W'(beat_addr << 3);
      end

      FMT_RD_DENSE: begin
        int didx, beat_addr;
        if (!fmt_is_pack_r) begin
          // UNPACK: read dense beat for RMW
          didx = fmt_elem_r;
        end else begin
          // PACK: read dense beat
          didx = fmt_elem_r;
        end
        beat_addr = didx >> 3;
        internal_rd_valid = 1'b1;
        internal_rd_addr = fmt_dst_base_r + ADDR_W'(beat_addr << 3);
      end

      FMT_WR: begin
        int didx, word_in_beat;
        if (!fmt_is_pack_r) begin
          // UNPACK: write modified dense beat
          didx = fmt_elem_r;
        end else begin
          // PACK: write modified compact beat
          didx = fmt_elem_r;
        end
        word_in_beat = didx & 7;
        buf_wr_valid = 1'b1;
        buf_wr_addr  = fmt_dst_base_r + ADDR_W'((didx >> 3) << 3);
        buf_wr_data  = modified_beat;
      end

      default: ;
    endcase
  end

  // Matrix FSM commands
  always_comb begin
    mat_cmd_valid_next = 1'b0;
    mat_cmd_op = `GBP_OP_MAT_ADD;
    mat_cmd_base_a = ADDR_W'(0);
    mat_cmd_base_b = ADDR_W'(0);
    mat_cmd_base_dest = ADDR_W'(0);
    mat_cmd_m = 6'd1;
    mat_cmd_n = 6'd1;
    mat_cmd_k = 6'd1;

    case (state_r)
      GFSM_VAR_ACCUMULATE: begin
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_ADD;
        mat_cmd_base_a = prior_eta_addr;
        mat_cmd_base_b = msg_base_addr + ADDR_W'(message_offset_words(accum_count_r, dofs_r));
        mat_cmd_base_dest = prior_eta_addr;
        mat_cmd_m = 6'd1;
        mat_cmd_n = 6'(payload_size_int);
      end

      GFSM_VAR_INVERT_LAM: begin
        // mat_cmd_valid_r: 0 = no command issued yet, 1 = command issued
        // mat_done_r: 0 = not done, 1 = done (or stale from previous op)
        //
        // Sequence:
        //   Cycle 0: mat_cmd_valid_r=0, mat_done_r may be stale(1) → issue command
        //   Cycle 1: mat_cmd_valid_r=1, matrix processes → wait
        //   Cycle 2: mat_done_r=1 (new) → transition
        //
        // Key: always issue command on first cycle, then wait for fresh mat_done_r
        if (!mat_cmd_valid_r) begin
          mat_cmd_valid_next = mat_cmd_ready;
        end
        mat_cmd_op = `GBP_OP_MAT_INV;
        mat_cmd_base_a = prior_lam_addr;
        mat_cmd_base_dest = result_addr;
        mat_cmd_m = {3'b0, dofs_r};
        mat_cmd_n = {3'b0, dofs_r};
      end

      GFSM_VAR_MVMUL: begin
        mat_cmd_valid_next = mat_cmd_ready;
        mat_cmd_op = `GBP_OP_MAT_VEC_MUL;
        mat_cmd_base_a = result_addr;
        mat_cmd_base_b = prior_eta_addr;
        mat_cmd_base_dest = result_addr;  // mu overwrites inv_lam (no longer needed)
        mat_cmd_m = {3'b0, dofs_r};
        mat_cmd_n = 6'd1;
        mat_cmd_k = {3'b0, dofs_r};
      end

      GFSM_FAC_COMPUTE_ETAF: begin
        // eta_f = J^T * z
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_VEC_MUL;
        mat_cmd_base_a = ADDR_W'(fac_jt_base(dofs_r));
        mat_cmd_base_b = ADDR_W'(fac_z_base(dofs_r));
        mat_cmd_base_dest = ADDR_W'(fac_ef_base(dofs_r));
        mat_cmd_m = 6'(2 * dofs_r);
        mat_cmd_n = 6'd1;
        mat_cmd_k = {3'b0, dofs_r};
      end

      GFSM_FAC_COMPUTE_LF: begin
        // Lambda_f = J^T * J
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_MUL;
        mat_cmd_base_a = ADDR_W'(fac_jt_base(dofs_r));
        mat_cmd_base_b = ADDR_W'(fac_j_base(dofs_r));
        mat_cmd_base_dest = ADDR_W'(fac_lf_base(dofs_r));
        mat_cmd_m = 6'(2 * dofs_r);
        mat_cmd_n = 6'(2 * dofs_r);
        mat_cmd_k = {3'b0, dofs_r};
      end

      GFSM_FAC_ADD_BLOCK: begin
        // Lambda_jj_plus = Lambda_f[j,j] + Lambda_msg_j
        int lf_jj, msg_j_lam, d;
        d = dofs_r;
        if (current_adj_r == 0) begin
          lf_jj = fac_lf_11(d);
          msg_j_lam = fac_msg_dense_base(1, d) + d;
        end else begin
          lf_jj = fac_lf_00(d);
          msg_j_lam = fac_msg_dense_base(0, d) + d;
        end
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_ADD;
        mat_cmd_base_a = ADDR_W'(lf_jj);
        mat_cmd_base_b = ADDR_W'(msg_j_lam);
        mat_cmd_base_dest = ADDR_W'(fac_tmp1_base(d));
        mat_cmd_m = {3'b0, dofs_r};
        mat_cmd_n = {3'b0, dofs_r};
      end

      GFSM_FAC_INVERT_BLOCK: begin
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_INV;
        mat_cmd_base_a = ADDR_W'(fac_tmp1_base(dofs_r));
        mat_cmd_base_dest = ADDR_W'(fac_tmp1_base(dofs_r));
        mat_cmd_m = {3'b0, dofs_r};
        mat_cmd_n = {3'b0, dofs_r};
      end

      GFSM_FAC_MUL_BLOCK1: begin
        // tmp = Lambda_f[i,j] * Lambda_jj_inv
        int lf_ij, d;
        d = dofs_r;
        if (current_adj_r == 0) lf_ij = fac_lf_01(d);
        else lf_ij = fac_lf_10(d);
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_MUL;
        mat_cmd_base_a = ADDR_W'(lf_ij);
        mat_cmd_base_b = ADDR_W'(fac_tmp1_base(d));
        mat_cmd_base_dest = ADDR_W'(fac_tmp2_base(d));
        mat_cmd_m = {3'b0, dofs_r};
        mat_cmd_n = {3'b0, dofs_r};
        mat_cmd_k = {3'b0, dofs_r};
      end

      GFSM_FAC_MUL_BLOCK2: begin
        // tmp2 = tmp * Lambda_f[j,i]
        int lf_ji, d;
        d = dofs_r;
        if (current_adj_r == 0) lf_ji = fac_lf_10(d);
        else lf_ji = fac_lf_01(d);
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_MUL;
        mat_cmd_base_a = ADDR_W'(fac_tmp2_base(d));
        mat_cmd_base_b = ADDR_W'(lf_ji);
        mat_cmd_base_dest = ADDR_W'(fac_tmp4_base(d));
        mat_cmd_m = {3'b0, dofs_r};
        mat_cmd_n = {3'b0, dofs_r};
        mat_cmd_k = {3'b0, dofs_r};
      end

      GFSM_FAC_SUB_LAMBDA: begin
        // Lambda_out = Lambda_f[i,i] - tmp2
        int lf_ii, d;
        d = dofs_r;
        if (current_adj_r == 0) lf_ii = fac_lf_00(d);
        else lf_ii = fac_lf_11(d);
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_SUB;
        mat_cmd_base_a = ADDR_W'(lf_ii);
        mat_cmd_base_b = ADDR_W'(fac_tmp4_base(d));
        mat_cmd_base_dest = ADDR_W'(fac_out_dense_base(d) + d);
        mat_cmd_m = {3'b0, dofs_r};
        mat_cmd_n = {3'b0, dofs_r};
      end

      GFSM_FAC_ADD_ETA: begin
        // eta_j_plus = eta_f[j] + eta_msg_j
        int ef_j, msg_j_eta, d;
        d = dofs_r;
        if (current_adj_r == 0) begin
          ef_j = fac_ef_1(d);
          msg_j_eta = fac_msg_dense_base(1, d);
        end else begin
          ef_j = fac_ef_0(d);
          msg_j_eta = fac_msg_dense_base(0, d);
        end
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_ADD;
        mat_cmd_base_a = ADDR_W'(ef_j);
        mat_cmd_base_b = ADDR_W'(msg_j_eta);
        mat_cmd_base_dest = ADDR_W'(fac_tmp3_base(d));
        mat_cmd_m = 6'd1;
        mat_cmd_n = {3'b0, dofs_r};
      end

      GFSM_FAC_MVMUL_ETA: begin
        // tmp_eta = tmp * eta_j_plus
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_VEC_MUL;
        mat_cmd_base_a = ADDR_W'(fac_tmp2_base(dofs_r));
        mat_cmd_base_b = ADDR_W'(fac_tmp3_base(dofs_r));
        mat_cmd_base_dest = ADDR_W'(fac_tmp4_base(dofs_r));
        mat_cmd_m = {3'b0, dofs_r};
        mat_cmd_n = 6'd1;
        mat_cmd_k = {3'b0, dofs_r};
      end

      GFSM_FAC_SUB_ETA: begin
        // eta_out = eta_f[i] - tmp_eta
        int ef_i, d;
        d = dofs_r;
        if (current_adj_r == 0) ef_i = fac_ef_0(d);
        else ef_i = fac_ef_1(d);
        mat_cmd_valid_next = mat_cmd_ready && !mat_done_r;
        mat_cmd_op = `GBP_OP_MAT_SUB;
        mat_cmd_base_a = ADDR_W'(ef_i);
        mat_cmd_base_b = ADDR_W'(fac_tmp4_base(d));
        mat_cmd_base_dest = ADDR_W'(fac_out_dense_base(d));
        mat_cmd_m = 6'd1;
        mat_cmd_n = {3'b0, dofs_r};
      end

      default: ;
    endcase
  end

  // done_o driven by state
  assign done_o = (state_r == GFSM_VAR_DONE) || (state_r == GFSM_FAC_DONE);
  assign mat_cmd_valid = mat_cmd_valid_r;

endmodule
