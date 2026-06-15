// belief_operand_unpacker.sv
// GBP Compute Core v0.7 — belief operand unpacker
// Converts OST_BELIEF_PRIOR / OST_BELIEF_MSG beats into typed structs.
// Source: docs/gbp_pe/08_NEW_COMPUTE_UNIT.md §17

module belief_operand_unpacker
  import gbp_op_pkg::*;
(
    input logic clk_i,
    input logic reset_i,

    // Configuration (stable before first beat)
    input  gbp_dim_e    dim_i,
    input  logic [15:0] degree_i,
    input  logic [31:0] op_id_i,

    // Operand stream input
    input  logic                                               beat_valid_i,
    output logic                                               beat_ready_o,
    input  operand_stream_kind_e                               beat_kind_i,
    input  logic                 [OPERAND_STREAM_WIDTH*32-1:0] beat_data_flat_i,
    input  logic                 [                       31:0] beat_op_id_i,
    input  logic                 [                       15:0] beat_beat_idx_i,
    input  logic                                               beat_last_i,

    // Prior output
    output logic                                 prior_valid_o,
    input  logic                                 prior_ready_i,
    output gbp_dim_e                             prior_dim_o,
    output logic     [                     15:0] prior_degree_o,
    output logic     [   GBP_MAX_VAR_DIM*32-1:0] prior_eta_flat_o,
    output logic     [GBP_MAX_PACKED_VAR*32-1:0] prior_L_flat_o,
    output logic     [   GBP_MAX_VAR_DIM*32-1:0] prior_mu_old_flat_o,

    // Message output
    output logic                                 msg_valid_o,
    input  logic                                 msg_ready_i,
    output gbp_dim_e                             msg_dim_o,
    output logic     [   GBP_MAX_VAR_DIM*32-1:0] msg_eta_flat_o,
    output logic     [GBP_MAX_PACKED_VAR*32-1:0] msg_L_flat_o,
    output logic                                 msg_last_o,

    // Error
    output logic stream_error_o
);

  localparam int BUF_DEPTH = 64;
  localparam int PTR_W = $clog2(BUF_DEPTH);

  // ----------------------------------------------------------
  // Dimension
  // ----------------------------------------------------------
  logic [2:0] d_i;
  logic [4:0] e_i;
  assign d_i = dim_to_val(dim_i);
  assign e_i = 5'(E(d_i));

  logic [ 5:0] prior_total;  // eta(d) + L_packed(e_i) + mu_old(d) (max 33)
  logic [15:0] msg_total;  // degree_i * e_i
  assign prior_total = 6'(e_i + d_i + d_i);
  assign msg_total   = degree_i * 16'(e_i);

  // ----------------------------------------------------------
  // Scalar buffer
  // ----------------------------------------------------------
  logic [31:0] buf_data[BUF_DEPTH];
  logic [PTR_W-1:0] wr_ptr_r, rd_ptr_r;
  logic [PTR_W:0] buf_cnt_r;

  // Counters for total scalars accepted in each phase
  logic [5:0] prior_cnt_r;
  logic [15:0] msg_in_cnt_r;
  logic [15:0] msg_out_cnt_r;

  // Position within the current message (0..e_i-1).  In V0 each
  // OST_BELIEF_MSG beat carries one complete or partial message; we never
  // cross a message boundary when writing so that beat_last_i unambiguously
  // marks the final message.
  logic [4:0] msg_subcnt_r;

  // Last-beat tracking for msg_last_o propagation
  logic             last_beat_last_r;
  logic [PTR_W-1:0] last_scalar_ptr_r;

  // ----------------------------------------------------------
  // FSM
  // ----------------------------------------------------------
  typedef enum logic [2:0] {
    ST_IDLE,
    ST_PRIOR,
    ST_PRIOR_WAIT,
    ST_MSG,
    ST_ERROR
  } state_e;

  state_e           state_r;

  // ----------------------------------------------------------
  // Helpers
  // ----------------------------------------------------------
  logic   [PTR_W:0] room;
  logic   [    4:0] beat_nwrite;  // up to 16
  logic             beat_room_enough;

  assign room             = BUF_DEPTH - buf_cnt_r;
  assign beat_room_enough = room >= msg_rem;

  // Scalars available in current beat to write (min(16, remaining))
  logic [5:0] prior_rem;
  logic [4:0] msg_rem;
  assign prior_rem = (prior_total > 6'(prior_cnt_r))
                       ? ((prior_total - 6'(prior_cnt_r)) > 6'd16 ? 6'd16 : (prior_total - 6'(prior_cnt_r)))
                       : 6'd0;
  // Do not write past the current message boundary so that beat_last_i marks
  // the last message precisely.
  assign msg_rem   = (msg_total > msg_in_cnt_r)
                     ? ((e_i - msg_subcnt_r) < 16
                        ? 5'(e_i - msg_subcnt_r)
                        : 5'd16)
                     : 5'd0;

  // Whether a complete message is available to emit.
  // If we have already seen the last beat, do not emit past the last valid scalar.
  wire [PTR_W-1:0] msg_last_scalar_ptr = rd_ptr_r + PTR_W'(e_i) - 1'b1;
  wire msg_avail = (buf_cnt_r >= e_i) && (msg_out_cnt_r < degree_i)
                   && (!last_beat_last_r || (msg_last_scalar_ptr <= last_scalar_ptr_r));

  // ----------------------------------------------------------
  // Sequential logic
  // ----------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r        <= ST_IDLE;
      wr_ptr_r       <= '0;
      rd_ptr_r       <= '0;
      buf_cnt_r      <= '0;
      prior_cnt_r    <= '0;
      msg_in_cnt_r   <= '0;
      msg_out_cnt_r  <= '0;
      msg_subcnt_r   <= '0;
      last_beat_last_r <= 1'b0;
      last_scalar_ptr_r <= '0;
      stream_error_o <= 1'b0;
      for (int i = 0; i < BUF_DEPTH; i++) buf_data[i] <= '0;
    end else begin
      case (state_r)
        // --------------------------------------------------
        ST_IDLE: begin
          stream_error_o <= 1'b0;
          if (beat_valid_i && beat_ready_o) begin
            if (beat_op_id_i != op_id_i || beat_kind_i != OST_BELIEF_PRIOR) begin
              state_r <= ST_ERROR;
              stream_error_o <= 1'b1;
            end else begin
              // First prior beat: write into buffer
              wr_ptr_r      <= PTR_W'(prior_rem);
              rd_ptr_r      <= '0;
              buf_cnt_r     <= OPERAND_STREAM_WIDTH'(prior_rem);
              prior_cnt_r   <= 6'(prior_rem);
              msg_in_cnt_r  <= '0;
              msg_out_cnt_r <= '0;
              for (int i = 0; i < OPERAND_STREAM_WIDTH; i++) begin
                if (5'(i) < prior_rem) begin
                  buf_data[i] <= beat_data_flat_i[i*32+:32];
                end
              end
              if (6'(prior_rem) >= prior_total) state_r <= ST_PRIOR_WAIT;
              else state_r <= ST_PRIOR;
            end
          end
        end

        // --------------------------------------------------
        ST_PRIOR: begin
          if (beat_valid_i && beat_ready_o) begin
            if (beat_op_id_i != op_id_i || beat_kind_i != OST_BELIEF_PRIOR) begin
              state_r <= ST_ERROR;
              stream_error_o <= 1'b1;
            end else begin
              // Write scalars into buffer
              for (int i = 0; i < OPERAND_STREAM_WIDTH; i++) begin
                if (5'(i) < prior_rem) begin
                  buf_data[wr_ptr_r+PTR_W'(i)] <= beat_data_flat_i[i*32+:32];
                end
              end
              wr_ptr_r    <= wr_ptr_r + PTR_W'(prior_rem);
              buf_cnt_r   <= buf_cnt_r + OPERAND_STREAM_WIDTH'(prior_rem);
              prior_cnt_r <= prior_cnt_r + 6'(prior_rem);

              if (prior_cnt_r + 6'(prior_rem) >= prior_total) begin
                state_r <= ST_PRIOR_WAIT;
              end
            end
          end
        end

        // --------------------------------------------------
        ST_PRIOR_WAIT: begin
          if (prior_valid_o && prior_ready_i) begin
            // Clear buffer for message phase
            wr_ptr_r          <= '0;
            rd_ptr_r          <= '0;
            buf_cnt_r         <= '0;
            msg_in_cnt_r      <= '0;
            msg_out_cnt_r     <= '0;
            msg_subcnt_r      <= '0;
            last_beat_last_r  <= 1'b0;
            last_scalar_ptr_r <= '0;
            if (degree_i == 16'd0) state_r <= ST_IDLE;
            else state_r <= ST_MSG;
          end
        end

        // --------------------------------------------------
        ST_MSG: begin
          // Emit messages whenever enough scalars are buffered
          if (msg_valid_o && msg_ready_i) begin
            rd_ptr_r      <= rd_ptr_r + PTR_W'(e_i);
            buf_cnt_r     <= buf_cnt_r - OPERAND_STREAM_WIDTH'(e_i);
            msg_out_cnt_r <= msg_out_cnt_r + 16'd1;
            if (msg_out_cnt_r + 16'd1 >= degree_i) begin
              // All messages emitted; return to idle once buffer drained
              state_r <= ST_IDLE;
            end
          end else if (beat_valid_i && beat_ready_o) begin
            if (beat_op_id_i != op_id_i || beat_kind_i != OST_BELIEF_MSG) begin
              state_r <= ST_ERROR;
              stream_error_o <= 1'b1;
            end else begin
              for (int i = 0; i < OPERAND_STREAM_WIDTH; i++) begin
                if (5'(i) < msg_rem) begin
                  buf_data[wr_ptr_r+PTR_W'(i)] <= beat_data_flat_i[i*32+:32];
                end
              end
              wr_ptr_r     <= wr_ptr_r + PTR_W'(msg_rem);
              buf_cnt_r    <= buf_cnt_r + OPERAND_STREAM_WIDTH'(msg_rem);
              msg_in_cnt_r <= msg_in_cnt_r + 16'(msg_rem);
              if (msg_subcnt_r + msg_rem >= e_i)
                msg_subcnt_r <= '0;
              else
                msg_subcnt_r <= msg_subcnt_r + msg_rem;
              if (beat_last_i) begin
                last_beat_last_r  <= 1'b1;
                last_scalar_ptr_r <= wr_ptr_r + PTR_W'(msg_rem) - 1'b1;
              end
            end
          end
        end

        // --------------------------------------------------
        ST_ERROR: begin
          // Hold until reset
        end

        default: begin
          state_r        <= ST_ERROR;
          stream_error_o <= 1'b1;
        end
      endcase
    end
  end

  // ----------------------------------------------------------
  // Output construction
  // ----------------------------------------------------------
  assign prior_valid_o = (state_r == ST_PRIOR_WAIT);
  assign prior_dim_o = dim_i;
  assign prior_degree_o = degree_i;

  always_comb begin
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
      if (i < d_i) prior_eta_flat_o[i*32+:32] = buf_data[i];
      else prior_eta_flat_o[i*32+:32] = '0;
    end
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
      if (i < P(d_i)) prior_L_flat_o[i*32+:32] = buf_data[d_i+i];
      else prior_L_flat_o[i*32+:32] = '0;
    end
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
      if (i < d_i) prior_mu_old_flat_o[i*32+:32] = buf_data[e_i+i];
      else prior_mu_old_flat_o[i*32+:32] = '0;
    end
  end

  assign msg_valid_o = (state_r == ST_MSG) && msg_avail;
  assign msg_dim_o   = dim_i;
  // The last message is the one whose final scalar came from the last beat.
  // For a well-formed stream this coincides with msg_out_cnt_r + 1 == degree_i.
  assign msg_last_o  = (state_r == ST_MSG) && msg_avail && last_beat_last_r
                       && (rd_ptr_r + PTR_W'(e_i) - 1'b1 == last_scalar_ptr_r);

  always_comb begin
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
      if (i < d_i) msg_eta_flat_o[i*32+:32] = buf_data[rd_ptr_r+PTR_W'(i)];
      else msg_eta_flat_o[i*32+:32] = '0;
    end
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
      if (i < P(d_i)) msg_L_flat_o[i*32+:32] = buf_data[rd_ptr_r+PTR_W'(d_i+i)];
      else msg_L_flat_o[i*32+:32] = '0;
    end
  end

  // ----------------------------------------------------------
  // Beat ready
  // ----------------------------------------------------------
  assign beat_ready_o = (state_r == ST_IDLE)
                      || ((state_r == ST_PRIOR) && beat_room_enough)
                      || ((state_r == ST_MSG) && !msg_valid_o && beat_room_enough);

endmodule
