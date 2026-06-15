// cavity_builder.sv
// GBP Compute Core v0.6 — cavity builder (stream accumulator)
// A_cav = L_oo + belief_L - old_msg_L
// eta_cav = eta_o + belief_eta - old_msg_eta
//
// Three operand streams arrive in strict order:
//   1. OST_CAV_FACTOR_O  — stored directly into accumulator
//   2. OST_CAV_BELIEF_O  — acc += belief (FP add per scalar)
//   3. OST_CAV_OLD_TO_O  — acc -= old_msg (FP sub per scalar)
//
// Each stream carries E(d_o) scalars, packed as:
//   data[0..d_o-1] = eta, data[d_o..E(d_o)-1] = L_packed

module cavity_builder
  import gbp_op_pkg::*;
(
  input  logic     clk_i,
  input  logic     reset_i,

  input  logic     start_valid_i,
  output logic     start_ready_o,
  input  gbp_dim_e dim_o_i,

  input  logic                 beat_valid_i,
  output logic                 beat_ready_o,
  input  operand_stream_kind_e beat_kind_i,
  input  logic [OPERAND_STREAM_WIDTH*32-1:0] beat_data_flat_i,
  input  logic                 beat_last_i,

  output logic     cav_valid_o,
  input  logic     cav_ready_i,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    cav_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] cav_L_flat_o,

  output logic     stream_error_o
);

  localparam int FP_E = 8;
  localparam int FP_M = 23;

  // ----------------------------------------------------------
  // Dimension
  // ----------------------------------------------------------
  logic [2:0] d_o;
  logic [4:0] e_o;
  assign d_o = dim_to_val(dim_o_i);
  assign e_o = E(d_o);

  // ----------------------------------------------------------
  // Accumulator: acc[0..e_o-1], where acc[0..d_o-1]=eta, acc[d_o..e_o-1]=L_packed
  // ----------------------------------------------------------
  logic [31:0] acc [GBP_MAX_MSG_SCALAR];

  // ----------------------------------------------------------
  // FP add/sub unit
  // ----------------------------------------------------------
  logic fp_v_i, fp_ready, fp_v_o, fp_yumi;
  logic [31:0] fp_a_i, fp_b_i, fp_z_o;
  logic fp_sub_i;

  bsg_fpu_add_sub #(.e_p(FP_E), .m_p(FP_M)) u_fp (
    .clk_i(clk_i), .reset_i(reset_i), .en_i(1'b1),
    .v_i(fp_v_i), .a_i(fp_a_i), .b_i(fp_b_i), .sub_i(fp_sub_i),
    .ready_and_o(fp_ready),
    .v_o(fp_v_o), .z_o(fp_z_o),
    .unimplemented_o(), .invalid_o(), .overflow_o(), .underflow_o(),
    .yumi_i(fp_yumi)
  );

  // ----------------------------------------------------------
  // Beat scalar unpacking
  // ----------------------------------------------------------
  logic [31:0] scalars [OPERAND_STREAM_WIDTH];
  always_comb begin
    for (int i = 0; i < OPERAND_STREAM_WIDTH; i++)
      scalars[i] = beat_data_r[i];
  end

  // ----------------------------------------------------------
  // FSM
  // ----------------------------------------------------------
  typedef enum logic [3:0] {
    ST_IDLE,
    ST_FACTOR,        // Receive FACTOR_O beat, store directly
    ST_BELIEF_LATCH,  // Latch a BELIEF_O beat into buffer
    ST_BELIEF_FP,     // Feed one scalar to FP add, wait for result
    ST_OLD_LATCH,     // Latch an OLD_TO_O beat into buffer
    ST_OLD_FP,        // Feed one scalar to FP sub, wait for result
    ST_DONE,
    ST_ERROR
  } state_e;

  state_e state_r;

  // Global scalar counter (0..e_o-1 across all beats)
  logic [4:0] global_idx_r;
  // Index within current beat (0..15)
  logic [3:0] beat_idx_r;
  // Whether current beat is the last in this stream phase
  logic       beat_last_r;
  // Latched beat data for FP processing phases
  logic [31:0] beat_data_r [OPERAND_STREAM_WIDTH];
  // Whether an FP operation has been started for current scalar
  logic        fp_started_r;

  // Absolute index for each scalar in a beat (combinational)
  logic [4:0] abs_idx [OPERAND_STREAM_WIDTH];
  always_comb begin
    for (int i = 0; i < OPERAND_STREAM_WIDTH; i++)
      abs_idx[i] = global_idx_r + 5'(i);
  end

  // Number of valid scalars in the current beat
  logic [4:0] scalars_in_beat;
  assign scalars_in_beat = beat_last_r
                         ? (e_o - 5'(global_idx_r - beat_idx_r))
                         : 5'(OPERAND_STREAM_WIDTH);

  // ----------------------------------------------------------
  // FSM sequential logic
  // ----------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r      <= ST_IDLE;
      global_idx_r <= '0;
      beat_idx_r   <= '0;
      beat_last_r  <= 1'b0;
      fp_started_r <= 1'b0;
      stream_error_o <= 1'b0;
      for (int i = 0; i < GBP_MAX_MSG_SCALAR; i++)
        acc[i] <= '0;
    end else begin
      case (state_r)
        // --------------------------------------------------
        ST_IDLE: begin
          stream_error_o <= 1'b0;
          fp_started_r <= 1'b0;
          if (start_valid_i && start_ready_o) begin
            state_r      <= ST_FACTOR;
            global_idx_r <= '0;
            beat_idx_r   <= '0;
            for (int i = 0; i < GBP_MAX_MSG_SCALAR; i++)
              acc[i] <= '0;
          end
        end

        // --------------------------------------------------
        // Phase 1: Store FACTOR_O directly into acc
        // --------------------------------------------------
        ST_FACTOR: begin
          if (beat_valid_i && beat_ready_o) begin
            if (beat_kind_i != OST_CAV_FACTOR_O) begin
              state_r <= ST_ERROR;
              stream_error_o <= 1'b1;
            end else begin
              for (int i = 0; i < OPERAND_STREAM_WIDTH; i++) begin
                beat_data_r[i] <= beat_data_flat_i[i*32 +: 32];
                if (abs_idx[i] < e_o)
                  acc[abs_idx[i]] <= beat_data_flat_i[i*32 +: 32];
              end
              if (beat_last_i) begin
                state_r      <= ST_BELIEF_LATCH;
                global_idx_r <= '0;
                beat_idx_r   <= '0;
              end else begin
                global_idx_r <= global_idx_r + OPERAND_STREAM_WIDTH;
              end
            end
          end
        end

        // --------------------------------------------------
        // Phase 2: BELIEF_O — acc += belief
        //   Latch a beat, then process scalars one at a time
        // --------------------------------------------------
        ST_BELIEF_LATCH: begin
          if (beat_valid_i && beat_ready_o) begin
            if (beat_kind_i != OST_CAV_BELIEF_O) begin
              state_r <= ST_ERROR;
              stream_error_o <= 1'b1;
            end else begin
              // Latch beat info
              beat_last_r <= beat_last_i;
              beat_idx_r  <= '0;
              for (int i = 0; i < OPERAND_STREAM_WIDTH; i++)
                beat_data_r[i] <= beat_data_flat_i[i*32 +: 32];
              // Move to FP processing
              state_r <= ST_BELIEF_FP;
            end
          end
        end

        ST_BELIEF_FP: begin
          if (fp_v_o) begin
            // Result ready: write back
            acc[global_idx_r] <= fp_z_o;
            fp_started_r <= 1'b0;

            // Advance
            if (global_idx_r + 1 >= e_o) begin
              state_r <= ST_OLD_LATCH;
              global_idx_r <= '0;
              beat_idx_r <= '0;
            end else begin
              global_idx_r <= global_idx_r + 1;
              if (beat_idx_r + 1 >= scalars_in_beat) begin
                beat_idx_r <= '0;
                state_r <= ST_BELIEF_LATCH;
              end else begin
                beat_idx_r <= beat_idx_r + 1;
              end
            end
          end else if (!fp_started_r && fp_ready) begin
            fp_started_r <= 1'b1;
          end
        end

        // --------------------------------------------------
        // Phase 3: OLD_TO_O — acc -= old_msg
        // --------------------------------------------------
        ST_OLD_LATCH: begin
          if (beat_valid_i && beat_ready_o) begin
            if (beat_kind_i != OST_CAV_OLD_TO_O) begin
              state_r <= ST_ERROR;
              stream_error_o <= 1'b1;
            end else begin
              beat_last_r <= beat_last_i;
              beat_idx_r  <= '0;
              for (int i = 0; i < OPERAND_STREAM_WIDTH; i++)
                beat_data_r[i] <= beat_data_flat_i[i*32 +: 32];
              state_r <= ST_OLD_FP;
            end
          end
        end

        ST_OLD_FP: begin
          if (fp_v_o) begin
            acc[global_idx_r] <= fp_z_o;
            fp_started_r <= 1'b0;

            if (global_idx_r + 1 >= e_o) begin
              state_r <= ST_DONE;
              global_idx_r <= '0;
              beat_idx_r <= '0;
            end else begin
              global_idx_r <= global_idx_r + 1;
              if (beat_idx_r + 1 >= scalars_in_beat) begin
                beat_idx_r <= '0;
                state_r <= ST_OLD_LATCH;
              end else begin
                beat_idx_r <= beat_idx_r + 1;
              end
            end
          end else if (!fp_started_r && fp_ready) begin
            fp_started_r <= 1'b1;
          end
        end

        // --------------------------------------------------
        ST_DONE: begin
          if (cav_ready_i) begin
            state_r <= ST_IDLE;
          end
        end

        ST_ERROR: begin
          fp_started_r <= 1'b0;
          if (start_valid_i) begin
            state_r <= ST_IDLE;
            stream_error_o <= 1'b0;
          end
        end

        default: begin
          state_r        <= ST_ERROR;
          stream_error_o <= 1'b1;
        end
      endcase
    end
  end

  // ----------------------------------------------------------
  // FP unit input muxing (combinational)
  // ----------------------------------------------------------
  always_comb begin
    fp_v_i   = 1'b0;
    fp_a_i   = '0;
    fp_b_i   = '0;
    fp_sub_i = 1'b0;
    fp_yumi  = 1'b0;

    case (state_r)
      ST_BELIEF_FP: begin
        // Consume result when ready
        if (fp_v_o) begin
          fp_yumi = 1'b1;
        end else if (!fp_started_r && fp_ready) begin
          // Start one FP add operation for current scalar
          fp_v_i   = 1'b1;
          fp_a_i   = acc[global_idx_r];
          fp_b_i   = scalars[beat_idx_r];
          fp_sub_i = 1'b0;
        end
      end
      ST_OLD_FP: begin
        if (fp_v_o) begin
          fp_yumi = 1'b1;
        end else if (!fp_started_r && fp_ready) begin
          fp_v_i   = 1'b1;
          fp_a_i   = acc[global_idx_r];
          fp_b_i   = scalars[beat_idx_r];
          fp_sub_i = 1'b1;
        end
      end
      default: ;
    endcase
  end

  // ----------------------------------------------------------
  // Output
  // ----------------------------------------------------------
  assign start_ready_o = (state_r == ST_IDLE);
  assign beat_ready_o  = (state_r == ST_FACTOR)
                      || (state_r == ST_BELIEF_LATCH)
                      || (state_r == ST_OLD_LATCH);
  assign cav_valid_o   = (state_r == ST_DONE);

  always_comb begin
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
      if (i < d_o)
        cav_eta_flat_o[i*32 +: 32] = acc[i];
      else
        cav_eta_flat_o[i*32 +: 32] = '0;
    end
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
      if (i < P(d_o))
        cav_L_flat_o[i*32 +: 32] = acc[d_o + i];
      else
        cav_L_flat_o[i*32 +: 32] = '0;
    end
  end

endmodule
