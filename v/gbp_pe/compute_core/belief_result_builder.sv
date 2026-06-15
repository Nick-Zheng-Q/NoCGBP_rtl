// belief_result_builder.sv
// GBP Compute Core v0.6 — belief result builder
// Uses hardfloat for residual computation.

module belief_result_builder
  import gbp_op_pkg::*;
(
  input  logic       clk_i,
  input  logic       reset_i,

  input  logic       valid_i,
  output logic       ready_o,
  input  gbp_dim_e   dim_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    acc_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] acc_L_flat_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    mu_old_flat_i,
  input  gbp_dim_e   solve_dim_i,
  input  logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] solve_X_flat_i,
  input  logic       solve_fail_i,
  input  logic       solve_regularized_i,
  input  logic       solve_nan_guard_i,
  input  logic [31:0] solve_min_pivot_i,

  output logic       valid_o,
  input  logic       ready_i,
  output gbp_dim_e   result_dim_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    result_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] result_L_flat_o,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    result_mu_flat_o,
  output logic [31:0] result_residual_o,
  output logic        result_fail_o,
  output logic        result_regularized_o,
  output logic        result_nan_guard_o,
  output logic        result_degree_mismatch_o,
  output logic [31:0] result_min_pivot_o
);

  localparam int FP_E = 8;
  localparam int FP_M = 23;

  logic [2:0] d_i;
  assign d_i = dim_to_val(dim_i);

  // Unpack inputs
  logic [31:0] acc_eta [GBP_MAX_VAR_DIM];
  logic [31:0] acc_L   [GBP_MAX_PACKED_VAR];
  logic [31:0] mu_old  [GBP_MAX_VAR_DIM];
  logic [31:0] solve_X [GBP_MAX_VAR_DIM][GBP_MAX_RHS];

  always_comb begin
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++)
      acc_eta[i] = acc_eta_flat_i[i*32 +: 32];
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++)
      acc_L[i] = acc_L_flat_i[i*32 +: 32];
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++)
      mu_old[i] = mu_old_flat_i[i*32 +: 32];
    for (int r = 0; r < GBP_MAX_VAR_DIM; r++)
      for (int c = 0; c < GBP_MAX_RHS; c++)
        solve_X[r][c] = solve_X_flat_i[(r * GBP_MAX_RHS + c) * 32 +: 32];
  end

  // Output registers
  logic       valid_r;
  gbp_dim_e   dim_r;
  logic [31:0] eta_r [GBP_MAX_VAR_DIM];
  logic [31:0] L_r   [GBP_MAX_PACKED_VAR];
  logic [31:0] mu_r  [GBP_MAX_VAR_DIM];
  logic [31:0] residual_r;
  logic        fail_r, reg_r, nan_r, mismatch_r;
  logic [31:0] min_pivot_r;

  // ----------------------------------------------------------
  // Residual computation with hardfloat
  // ----------------------------------------------------------
  // SUB: diff = mu_new - mu_old
  logic sub_v_i, sub_ready, sub_v_o, sub_yumi;
  logic [31:0] sub_a_i, sub_b_i, sub_z_o;

  bsg_fpu_add_sub #(.e_p(FP_E), .m_p(FP_M)) u_sub (
    .clk_i(clk_i), .reset_i(reset_i), .en_i(1'b1),
    .v_i(sub_v_i), .a_i(sub_a_i), .b_i(sub_b_i), .sub_i(1'b1),
    .ready_and_o(sub_ready),
    .v_o(sub_v_o), .z_o(sub_z_o),
    .unimplemented_o(), .invalid_o(), .overflow_o(), .underflow_o(),
    .yumi_i(sub_yumi)
  );

  // MUL: sq = diff * diff
  logic mul_v_i, mul_ready, mul_v_o, mul_yumi;
  logic [31:0] mul_a_i, mul_b_i, mul_z_o;

  bsg_fpu_mul #(.e_p(FP_E), .m_p(FP_M)) u_mul (
    .clk_i(clk_i), .reset_i(reset_i), .en_i(1'b1),
    .v_i(mul_v_i), .a_i(mul_a_i), .b_i(mul_b_i),
    .ready_and_o(mul_ready),
    .v_o(mul_v_o), .z_o(mul_z_o),
    .unimplemented_o(), .invalid_o(), .overflow_o(), .underflow_o(),
    .yumi_i(mul_yumi)
  );

  // ADD: acc += sq
  logic add_v_i, add_ready, add_v_o, add_yumi;
  logic [31:0] add_a_i, add_b_i, add_z_o;

  bsg_fpu_add_sub #(.e_p(FP_E), .m_p(FP_M)) u_add (
    .clk_i(clk_i), .reset_i(reset_i), .en_i(1'b1),
    .v_i(add_v_i), .a_i(add_a_i), .b_i(add_b_i), .sub_i(1'b0),
    .ready_and_o(add_ready),
    .v_o(add_v_o), .z_o(add_z_o),
    .unimplemented_o(), .invalid_o(), .overflow_o(), .underflow_o(),
    .yumi_i(add_yumi)
  );

  // ----------------------------------------------------------
  // FSM
  // ----------------------------------------------------------
  typedef enum logic [3:0] {
    ST_IDLE,
    ST_ISSUE_SUB,
    WAIT_SUB,
    ISSUE_MUL,
    WAIT_MUL,
    ISSUE_ADD,
    WAIT_ADD,
    ST_STORE,
    ST_DONE
  } state_e;

  state_e state_r;
  logic [2:0] idx_r;
  logic [31:0] diff_hold_r;
  logic [31:0] acc_r;

  // Input muxing
  always_comb begin
    sub_v_i = 1'b0; sub_a_i = '0; sub_b_i = '0; sub_yumi = 1'b0;
    mul_v_i = 1'b0; mul_a_i = '0; mul_b_i = '0; mul_yumi = 1'b0;
    add_v_i = 1'b0; add_a_i = '0; add_b_i = '0; add_yumi = 1'b0;

    case (state_r)
      ST_ISSUE_SUB: begin
        sub_v_i = 1'b1;
        sub_a_i = solve_X[idx_r][0];  // mu_new
        sub_b_i = mu_old[idx_r];       // mu_old
      end
      ISSUE_MUL: begin
        mul_v_i = 1'b1;
        mul_a_i = sub_z_o;  // diff
        mul_b_i = sub_z_o;  // diff
        sub_yumi = 1'b1;
      end
      ISSUE_ADD: begin
        add_v_i = 1'b1;
        add_a_i = acc_r;
        add_b_i = mul_z_o;  // sq
        mul_yumi = 1'b1;
      end
      WAIT_ADD: begin
        if (add_v_o) add_yumi = 1'b1;
      end
      default: ;
    endcase
  end

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= ST_IDLE;
      valid_r <= 1'b0;
    end else begin
      case (state_r)
        ST_IDLE: begin
          if (valid_i && ready_o) begin
            // Latch non-residual fields immediately
            dim_r    <= dim_i;
            fail_r   <= solve_fail_i;
            reg_r    <= solve_regularized_i;
            nan_r    <= solve_nan_guard_i;
            mismatch_r <= 1'b0;
            min_pivot_r <= solve_min_pivot_i;

            for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
              eta_r[i] <= acc_eta[i];
              mu_r[i]  <= solve_X[i][0];
            end
            for (int i = 0; i < GBP_MAX_PACKED_VAR; i++)
              L_r[i] <= acc_L[i];

            // Start residual computation
            acc_r    <= '0;  // FP32 zero
            idx_r    <= '0;
            valid_r  <= 1'b0;
            state_r  <= ST_ISSUE_SUB;
          end
        end

        ST_ISSUE_SUB: begin
          if (sub_ready) state_r <= WAIT_SUB;
        end

        WAIT_SUB: begin
          if (sub_v_o) state_r <= ISSUE_MUL;
        end

        ISSUE_MUL: begin
          if (mul_ready) state_r <= WAIT_MUL;
        end

        WAIT_MUL: begin
          if (mul_v_o) begin
            diff_hold_r <= mul_z_o;
            state_r <= ISSUE_ADD;
          end
        end

        ISSUE_ADD: begin
          if (add_ready) state_r <= WAIT_ADD;
        end

        WAIT_ADD: begin
          if (add_v_o) begin
            acc_r <= add_z_o;

            if (idx_r + 1 >= d_i) begin
              state_r <= ST_STORE;
            end else begin
              idx_r   <= idx_r + 1;
              state_r <= ST_ISSUE_SUB;
            end
          end
        end

        ST_STORE: begin
          residual_r <= acc_r;
          valid_r    <= 1'b1;
          state_r    <= ST_DONE;
        end

        ST_DONE: begin
          if (valid_r && ready_i) begin
            valid_r <= 1'b0;
            state_r <= ST_IDLE;
          end
        end

        default: begin
          state_r <= ST_IDLE;
        end
      endcase
    end
  end

  // Output
  assign valid_o   = valid_r;
  assign ready_o   = (state_r == ST_IDLE);
  assign result_dim_o = dim_r;
  assign result_residual_o = residual_r;
  assign result_fail_o = fail_r;
  assign result_regularized_o = reg_r;
  assign result_nan_guard_o = nan_r;
  assign result_degree_mismatch_o = mismatch_r;
  assign result_min_pivot_o = min_pivot_r;

  always_comb begin
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
      result_eta_flat_o[i*32 +: 32] = eta_r[i];
      result_mu_flat_o[i*32 +: 32]  = mu_r[i];
    end
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++)
      result_L_flat_o[i*32 +: 32] = L_r[i];
  end

endmodule
