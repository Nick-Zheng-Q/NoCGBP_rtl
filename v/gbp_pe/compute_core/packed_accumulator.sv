// packed_accumulator.sv
// GBP Compute Core v0.7 — packed accumulator for belief update
// Accumulates prior + incoming factor-to-variable messages.
// Source: docs/gbp_pe/08_NEW_COMPUTE_UNIT.md §16

module packed_accumulator
  import gbp_op_pkg::*;
(
  input  logic     clk_i,
  input  logic     reset_i,

  // Start (prior)
  input  logic     start_valid_i,
  output logic     start_ready_o,
  input  gbp_dim_e dim_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    prior_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] prior_L_flat_i,
  input  logic [15:0] degree_i,

  // Message stream
  input  logic     msg_valid_i,
  output logic     msg_ready_o,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    msg_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] msg_L_flat_i,
  input  logic     msg_last_i,

  // Accumulated output
  output logic     acc_valid_o,
  input  logic     acc_ready_i,
  output gbp_dim_e dim_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    acc_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] acc_L_flat_o,
  output logic [15:0] msg_count_o,
  output logic     degree_mismatch_o
);

  localparam int FP_E = 8;
  localparam int FP_M = 23;

  // Helper: bsg_fpu_add_sub returns a tiny normalized number instead of zero
  // for 0+0, so flush any denormal/subnormal result back to true zero.
  function automatic logic is_fp_zero(input logic [31:0] x);
    is_fp_zero = (x[30:23] == 8'h00) && (x[22:0] == 23'h0);
  endfunction

  // ----------------------------------------------------------
  // Dimension
  // ----------------------------------------------------------
  logic [2:0] d_i;
  logic [4:0] e_i, p_i;
  assign d_i = dim_to_val(dim_i);
  assign e_i = 5'(E(d_i));
  assign p_i = 5'(P(d_i));

  // ----------------------------------------------------------
  // Accumulator: fixed layout acc[0..GBP_MAX_VAR_DIM-1]=eta,
  // acc[GBP_MAX_VAR_DIM..GBP_MAX_VAR_DIM+GBP_MAX_PACKED_VAR-1]=L_packed.
  // This avoids combinational d_i/p_i races on the output ports.
  // ----------------------------------------------------------
  logic [31:0] acc [GBP_MAX_MSG_SCALAR];

  // Latched message buffer (same fixed layout)
  logic [31:0] msg_buf [GBP_MAX_MSG_SCALAR];

  localparam int ETA_OFF = 0;
  localparam int L_OFF   = GBP_MAX_VAR_DIM;

  // Index mapping for the fixed-offset storage
  logic [4:0] acc_rd_addr, buf_rd_addr;
  always_comb begin
    if (add_idx_r < d_i) begin
      acc_rd_addr = ETA_OFF + 5'(add_idx_r);
      buf_rd_addr = ETA_OFF + 5'(add_idx_r);
    end else begin
      acc_rd_addr = L_OFF + 5'(add_idx_r - d_i);
      buf_rd_addr = L_OFF + 5'(add_idx_r - d_i);
    end
  end

  // ----------------------------------------------------------
  // FP add unit
  // ----------------------------------------------------------
  logic fp_v_i, fp_ready, fp_v_o, fp_yumi;
  logic [31:0] fp_a_i, fp_b_i, fp_z_o;

  bsg_fpu_add_sub #(.e_p(FP_E), .m_p(FP_M)) u_fp (
    .clk_i(clk_i), .reset_i(reset_i), .en_i(1'b1),
    .v_i(fp_v_i), .a_i(fp_a_i), .b_i(fp_b_i), .sub_i(1'b0),
    .ready_and_o(fp_ready),
    .v_o(fp_v_o), .z_o(fp_z_o),
    .unimplemented_o(), .invalid_o(), .overflow_o(), .underflow_o(),
    .yumi_i(fp_yumi)
  );

  // ----------------------------------------------------------
  // FSM
  // ----------------------------------------------------------
  typedef enum logic [2:0] {
    ST_IDLE,
    ST_WAIT_MSG,
    ST_ADD,
    ST_DONE
  } state_e;

  state_e state_r;

  logic [4:0]  add_idx_r;      // scalar index being added
  logic [15:0] msg_count_r;
  logic        fp_started_r;
  logic        degree_mismatch_r;
  logic        msg_last_r;     // latched msg_last_i for current message
  // Track the operands issued to the adder so we can correct the
  // bsg_fpu_add_sub 0+0 -> smallest-normalized-number artifact.
  logic [31:0] fp_a_r, fp_b_r;

  // ----------------------------------------------------------
  // Sequential logic
  // ----------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r           <= ST_IDLE;
      add_idx_r         <= '0;
      msg_count_r       <= '0;
      fp_started_r      <= 1'b0;
      degree_mismatch_r <= 1'b0;
      msg_last_r        <= 1'b0;
      fp_a_r            <= '0;
      fp_b_r            <= '0;
      for (int i = 0; i < GBP_MAX_MSG_SCALAR; i++) begin
        acc[i]     <= '0;
        msg_buf[i] <= '0;
      end
    end else begin
      case (state_r)
        // --------------------------------------------------
        ST_IDLE: begin
          degree_mismatch_r <= 1'b0;
          fp_started_r      <= 1'b0;
          if (start_valid_i && start_ready_o) begin
            state_r     <= ST_WAIT_MSG;
            msg_count_r <= '0;
            add_idx_r   <= '0;
            // Latch prior into acc at fixed offsets
            for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
              if (i < d_i)
                acc[ETA_OFF + i] <= prior_eta_flat_i[i*32 +: 32];
              else
                acc[ETA_OFF + i] <= '0;
            end
            for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
              if (i < p_i)
                acc[L_OFF + i] <= prior_L_flat_i[i*32 +: 32];
              else
                acc[L_OFF + i] <= '0;
            end
          end
        end

        // --------------------------------------------------
        ST_WAIT_MSG: begin
          fp_started_r <= 1'b0;
          if (msg_valid_i && msg_ready_o) begin
            // Latch msg_last for mismatch check at message completion
            msg_last_r <= msg_last_i;

            // Latch message into buffer at fixed offsets
            for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
              if (i < d_i)
                msg_buf[ETA_OFF + i] <= msg_eta_flat_i[i*32 +: 32];
              else
                msg_buf[ETA_OFF + i] <= '0;
            end
            for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
              if (i < p_i)
                msg_buf[L_OFF + i] <= msg_L_flat_i[i*32 +: 32];
              else
                msg_buf[L_OFF + i] <= '0;
            end

            state_r   <= ST_ADD;
            add_idx_r <= '0;
          end else if (msg_count_r == degree_i) begin
            state_r <= ST_DONE;
          end
        end

        // --------------------------------------------------
        ST_ADD: begin
          if (fp_v_o) begin
            // Correct bsg_fpu_add_sub artifact where 0+0 returns the
            // smallest normalized number instead of zero.
            if (is_fp_zero(fp_a_r) && is_fp_zero(fp_b_r))
              acc[acc_rd_addr] <= 32'h0;
            else
              acc[acc_rd_addr] <= fp_z_o;
            fp_started_r   <= 1'b0;

            if (add_idx_r + 1 >= e_i) begin
              msg_count_r <= msg_count_r + 16'd1;
              // Degree mismatch check at message completion
              if ((msg_last_r && (msg_count_r + 16'd1 != degree_i)) ||
                  (!msg_last_r && (msg_count_r + 16'd1 == degree_i))) begin
                degree_mismatch_r <= 1'b1;
                state_r <= ST_DONE;
              end else if (msg_count_r + 16'd1 >= degree_i) begin
                state_r <= ST_DONE;
              end else begin
                state_r <= ST_WAIT_MSG;
              end
            end else begin
              add_idx_r <= add_idx_r + 5'd1;
            end
          end else if (!fp_started_r && fp_ready) begin
            fp_started_r <= 1'b1;
            // Use the fixed-offset mapped addresses so the 0+0 flush check
            // corresponds to the actual operands being added.
            fp_a_r       <= acc[acc_rd_addr];
            fp_b_r       <= msg_buf[buf_rd_addr];
          end
        end

        // --------------------------------------------------
        ST_DONE: begin
          if (acc_ready_i) begin
            state_r <= ST_IDLE;
          end
        end

        default: begin
          state_r <= ST_IDLE;
        end
      endcase
    end
  end

  // ----------------------------------------------------------
  // FP input muxing
  // ----------------------------------------------------------
  always_comb begin
    fp_v_i  = 1'b0;
    fp_a_i  = '0;
    fp_b_i  = '0;
    fp_yumi = 1'b0;

    if (state_r == ST_ADD) begin
      if (fp_v_o) begin
        fp_yumi = 1'b1;
      end else if (!fp_started_r && fp_ready) begin
        fp_v_i = 1'b1;
        fp_a_i = acc[acc_rd_addr];
        fp_b_i = msg_buf[buf_rd_addr];
      end
    end
  end

  // ----------------------------------------------------------
  // Handshake / output
  // ----------------------------------------------------------
  assign start_ready_o = (state_r == ST_IDLE);
  assign msg_ready_o   = (state_r == ST_WAIT_MSG) && (msg_count_r < degree_i);
  assign acc_valid_o   = (state_r == ST_DONE);

  assign dim_o              = dim_i;
  assign msg_count_o        = msg_count_r;
  assign degree_mismatch_o  = degree_mismatch_r;

  // Output from fixed offsets; unused entries were zeroed during load.
  always_comb begin
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++)
      acc_eta_flat_o[i*32 +: 32] = acc[ETA_OFF + i];
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++)
      acc_L_flat_o[i*32 +: 32] = acc[L_OFF + i];
  end

endmodule
