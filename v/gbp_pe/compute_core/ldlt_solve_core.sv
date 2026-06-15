// ldlt_solve_core.sv
// GBP Compute Core v0.7 — LDLT solve core
// Solves A * X = B for SPD matrices with optional diagonal loading.
// Source: docs/gbp_pe/08_NEW_COMPUTE_UNIT.md §13
//
// V0 implementation: sequential, one FP op at a time. Correctness first.
// Uses bsg_fpu_add_sub, bsg_fpu_mul, and HardFloat divSqrtFN.

`include "HardFloat_consts.vi"
`include "HardFloat_specialize.vi"

module ldlt_solve_core
  import gbp_op_pkg::*;
(
    input logic clk_i,
    input logic reset_i,

    // Request
    input  logic                                          req_valid_i,
    output logic                                          req_ready_o,
    input  gbp_dim_e                                      req_dim_i,
    input  logic     [                               2:0] req_nrhs_i,
    input  logic     [         GBP_MAX_PACKED_VAR*32-1:0] req_A_flat_i,
    input  logic     [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] req_B_flat_i,
    input  logic     [                              31:0] req_diag_lambda_i,
    input  logic     [                              31:0] req_pivot_eps_i,
    input  logic                                          req_regularize_en_i,

    // Response
    output logic                                          rsp_valid_o,
    input  logic                                          rsp_ready_i,
    output gbp_dim_e                                      rsp_dim_o,
    output logic     [                               2:0] rsp_nrhs_o,
    output logic     [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] rsp_X_flat_o,
    output logic                                          rsp_fail_o,
    output logic                                          rsp_regularized_o,
    output logic                                          rsp_nan_guard_o,
    output logic     [                              31:0] rsp_min_pivot_o
);

  localparam int FP_E = 8;
  localparam int FP_M = 23;

  // ----------------------------------------------------------
  // Dimension decode
  // ----------------------------------------------------------
  logic [2:0] d;
  logic [4:0] p_d;
  assign d   = dim_to_val(req_dim_i);
  assign p_d = P(d);

  // ----------------------------------------------------------
  // Storage
  // A is symmetric dense dxd
  // L is unit lower triangular dxd (diagonal implicitly 1)
  // D is diagonal d
  // B is d x nrhs, overwritten in-place by X
  // ----------------------------------------------------------
  fp32_t A                                         [GBP_MAX_VAR_DIM] [GBP_MAX_VAR_DIM];
  fp32_t L                                         [GBP_MAX_VAR_DIM] [GBP_MAX_VAR_DIM];
  fp32_t D                                         [GBP_MAX_VAR_DIM];
  fp32_t B                                         [GBP_MAX_VAR_DIM] [    GBP_MAX_RHS];

  // Working registers
  fp32_t acc_r;
  fp32_t f0_r;  // latch for intermediate FP result

  // Status
  logic  fail_r;
  logic  regularized_r;
  logic  nan_guard_r;
  fp32_t min_pivot_r;

  // ----------------------------------------------------------
  // FSM
  // ----------------------------------------------------------
  typedef enum logic [5:0] {
    ST_IDLE,
    ST_LOAD_A,
    ST_LOAD_B,
    ST_REGULARIZE,
    ST_REG_WAIT,
    ST_FACTOR_D_INIT,
    ST_FACTOR_D_MUL1,
    ST_FACTOR_D_MUL1_WAIT,
    ST_FACTOR_D_MUL2,
    ST_FACTOR_D_MUL2_WAIT,
    ST_FACTOR_D_SUB,
    ST_FACTOR_D_SUB_WAIT,
    ST_FACTOR_D_FINISH,
    ST_FACTOR_L_INIT,
    ST_FACTOR_L_MUL1,
    ST_FACTOR_L_MUL1_WAIT,
    ST_FACTOR_L_MUL2,
    ST_FACTOR_L_MUL2_WAIT,
    ST_FACTOR_L_SUB,
    ST_FACTOR_L_SUB_WAIT,
    ST_FACTOR_L_DIV,
    ST_FACTOR_L_DIV_WAIT,
    ST_FACTOR_L_FINISH,
    ST_FORWARD_INIT,
    ST_FORWARD_MUL,
    ST_FORWARD_MUL_WAIT,
    ST_FORWARD_SUB,
    ST_FORWARD_SUB_WAIT,
    ST_FORWARD_FINISH,
    ST_DIAG_DIV,
    ST_DIAG_DIV_WAIT,
    ST_DIAG_FINISH,
    ST_BACKWARD_INIT,
    ST_BACKWARD_MUL,
    ST_BACKWARD_MUL_WAIT,
    ST_BACKWARD_SUB,
    ST_BACKWARD_SUB_WAIT,
    ST_BACKWARD_FINISH,
    ST_PACK,
    ST_DONE
  } state_e;

  state_e state_r, next_state;

  // Counters
  logic [4:0] k_r;
  logic [2:0] i_r, j_r, r_r;

  // ----------------------------------------------------------
  // FP add/sub unit
  // ----------------------------------------------------------
  logic add_v_i, add_ready, add_v_o;
  fp32_t add_a_i, add_b_i, add_z_o;
  logic add_sub_i;
  logic add_yumi;

  bsg_fpu_add_sub #(
      .e_p(FP_E),
      .m_p(FP_M)
  ) u_add (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .en_i(1'b1),
      .v_i(add_v_i),
      .a_i(add_a_i),
      .b_i(add_b_i),
      .sub_i(add_sub_i),
      .ready_and_o(add_ready),
      .v_o(add_v_o),
      .z_o(add_z_o),
      .unimplemented_o(),
      .invalid_o(),
      .overflow_o(),
      .underflow_o(),
      .yumi_i(add_yumi)
  );

  // ----------------------------------------------------------
  // FP mul unit
  // ----------------------------------------------------------
  logic mul_v_i, mul_ready, mul_v_o;
  fp32_t mul_a_i, mul_b_i, mul_z_o;
  logic mul_yumi;

  bsg_fpu_mul #(
      .e_p(FP_E),
      .m_p(FP_M)
  ) u_mul (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .en_i(1'b1),
      .v_i(mul_v_i),
      .a_i(mul_a_i),
      .b_i(mul_b_i),
      .ready_and_o(mul_ready),
      .v_o(mul_v_o),
      .z_o(mul_z_o),
      .unimplemented_o(),
      .invalid_o(),
      .overflow_o(),
      .underflow_o(),
      .yumi_i(mul_yumi)
  );

  // ----------------------------------------------------------
  // FP div unit (HardFloat)
  // ----------------------------------------------------------
  logic div_in_ready, div_in_valid, div_out_valid;
  fp32_t div_a_i, div_b_i, div_z_o;

  divSqrtFN #(
      .expWidth(FP_E),
      .sigWidth(FP_M + 1),
      .bits_per_iter_p(2)
  ) u_div (
      .nReset(~reset_i),
      .clock(clk_i),
      .control(`flControl_default),
      .inReady(div_in_ready),
      .inValid(div_in_valid),
      .sqrtOp(1'b0),
      .a(div_a_i),
      .b(div_b_i),
      .roundingMode(3'b000),
      .outValid(div_out_valid),
      .sqrtOpOut(),
      .out(div_z_o),
      .exceptionFlags()
  );

  // ----------------------------------------------------------
  // Helper: is NaN or Inf?
  // ----------------------------------------------------------
  function automatic logic is_nan_or_inf(input fp32_t x);
    is_nan_or_inf = (x[30:23] == 8'hFF);
  endfunction

  // ----------------------------------------------------------
  // Combinational FP control and next-state logic
  // ----------------------------------------------------------
  always_comb begin
    // Defaults
    next_state   = state_r;
    add_v_i      = 1'b0;
    add_a_i      = '0;
    add_b_i      = '0;
    add_sub_i    = 1'b0;
    add_yumi     = 1'b0;
    mul_v_i      = 1'b0;
    mul_a_i      = '0;
    mul_b_i      = '0;
    mul_yumi     = 1'b0;
    div_in_valid = 1'b0;
    div_a_i      = '0;
    div_b_i      = '0;

    case (state_r)
      ST_IDLE: begin
        if (req_valid_i && req_ready_o) next_state = ST_LOAD_A;
      end

      ST_LOAD_A: begin
        if (k_r >= p_d) next_state = ST_LOAD_B;
      end

      ST_LOAD_B: begin
        next_state = ST_REGULARIZE;
      end

      ST_REGULARIZE: begin
        if (k_r >= d) next_state = ST_FACTOR_D_INIT;
        else if (req_regularize_en_i) begin
          if (add_ready) begin
            add_v_i = 1'b1;
            add_a_i = A[k_r][k_r];
            add_b_i = req_diag_lambda_i;
            add_sub_i = 1'b0;
            next_state = ST_REG_WAIT;
          end
        end else begin
          next_state = ST_FACTOR_D_INIT;
        end
      end

      ST_REG_WAIT: begin
        if (add_v_o) begin
          add_yumi   = 1'b1;
          next_state = ST_REGULARIZE;
        end
      end

      // --------------------------------------------------
      // D[k] = A[k][k] - sum_{j<k} L[k][j]^2 * D[j]
      // --------------------------------------------------
      ST_FACTOR_D_INIT: begin
        next_state = ST_FACTOR_D_MUL1;
      end

      ST_FACTOR_D_MUL1: begin
        if (j_r < k_r) begin
          if (mul_ready) begin
            mul_v_i = 1'b1;
            mul_a_i = L[k_r][j_r];
            mul_b_i = L[k_r][j_r];
            next_state = ST_FACTOR_D_MUL1_WAIT;
          end
        end else begin
          next_state = ST_FACTOR_D_FINISH;
        end
      end

      ST_FACTOR_D_MUL1_WAIT: begin
        if (mul_v_o) begin
          mul_yumi   = 1'b1;
          next_state = ST_FACTOR_D_MUL2;
        end
      end

      ST_FACTOR_D_MUL2: begin
        if (mul_ready) begin
          mul_v_i = 1'b1;
          mul_a_i = f0_r;  // L[k][j]^2
          mul_b_i = D[j_r];
          next_state = ST_FACTOR_D_MUL2_WAIT;
        end
      end

      ST_FACTOR_D_MUL2_WAIT: begin
        if (mul_v_o) begin
          mul_yumi   = 1'b1;
          next_state = ST_FACTOR_D_SUB;
        end
      end

      ST_FACTOR_D_SUB: begin
        if (add_ready) begin
          add_v_i = 1'b1;
          add_a_i = acc_r;
          add_b_i = f0_r;  // L[k][j]^2 * D[j]
          add_sub_i = 1'b1;
          next_state = ST_FACTOR_D_SUB_WAIT;
        end
      end

      ST_FACTOR_D_SUB_WAIT: begin
        if (add_v_o) begin
          add_yumi   = 1'b1;
          next_state = ST_FACTOR_D_MUL1;
        end
      end

      ST_FACTOR_D_FINISH: begin
        if (k_r + 1 >= d) next_state = ST_FORWARD_INIT;
        else next_state = ST_FACTOR_L_INIT;
      end

      // --------------------------------------------------
      // L[i][k] = (A[i][k] - sum_{j<k} L[i][j]*L[k][j]*D[j]) / D[k]
      // --------------------------------------------------
      ST_FACTOR_L_INIT: begin
        next_state = ST_FACTOR_L_MUL1;
      end

      ST_FACTOR_L_MUL1: begin
        if (j_r < k_r) begin
          if (mul_ready) begin
            mul_v_i = 1'b1;
            mul_a_i = L[i_r][j_r];
            mul_b_i = L[k_r][j_r];
            next_state = ST_FACTOR_L_MUL1_WAIT;
          end
        end else begin
          next_state = ST_FACTOR_L_DIV;
        end
      end

      ST_FACTOR_L_MUL1_WAIT: begin
        if (mul_v_o) begin
          mul_yumi   = 1'b1;
          next_state = ST_FACTOR_L_MUL2;
        end
      end

      ST_FACTOR_L_MUL2: begin
        if (mul_ready) begin
          mul_v_i = 1'b1;
          mul_a_i = f0_r;
          mul_b_i = D[j_r];
          next_state = ST_FACTOR_L_MUL2_WAIT;
        end
      end

      ST_FACTOR_L_MUL2_WAIT: begin
        if (mul_v_o) begin
          mul_yumi   = 1'b1;
          next_state = ST_FACTOR_L_SUB;
        end
      end

      ST_FACTOR_L_SUB: begin
        if (add_ready) begin
          add_v_i = 1'b1;
          add_a_i = acc_r;
          add_b_i = f0_r;
          add_sub_i = 1'b1;
          next_state = ST_FACTOR_L_SUB_WAIT;
        end
      end

      ST_FACTOR_L_SUB_WAIT: begin
        if (add_v_o) begin
          add_yumi   = 1'b1;
          next_state = ST_FACTOR_L_MUL1;
        end
      end

      ST_FACTOR_L_DIV: begin
        if (div_in_ready) begin
          div_in_valid = 1'b1;
          div_a_i      = acc_r;
          div_b_i      = D[k_r];
          next_state   = ST_FACTOR_L_DIV_WAIT;
        end
      end

      ST_FACTOR_L_DIV_WAIT: begin
        if (div_out_valid) next_state = ST_FACTOR_L_FINISH;
      end

      ST_FACTOR_L_FINISH: begin
        if (i_r + 1 < d) next_state = ST_FACTOR_L_INIT;
        else if (k_r + 1 < d) next_state = ST_FACTOR_D_INIT;
        else next_state = ST_FORWARD_INIT;
      end

      // --------------------------------------------------
      // Forward solve L * y = B (y stored in B)
      // y[i][r] = B[i][r] - sum_{j<i} L[i][j] * y[j][r]
      // --------------------------------------------------
      ST_FORWARD_INIT: begin
        next_state = ST_FORWARD_MUL;
      end

      ST_FORWARD_MUL: begin
        if (j_r < i_r) begin
          if (mul_ready) begin
            mul_v_i = 1'b1;
            mul_a_i = L[i_r][j_r];
            mul_b_i = B[j_r][r_r];
            next_state = ST_FORWARD_MUL_WAIT;
          end
        end else begin
          next_state = ST_FORWARD_FINISH;
        end
      end

      ST_FORWARD_MUL_WAIT: begin
        if (mul_v_o) begin
          mul_yumi   = 1'b1;
          next_state = ST_FORWARD_SUB;
        end
      end

      ST_FORWARD_SUB: begin
        if (add_ready) begin
          add_v_i = 1'b1;
          add_a_i = acc_r;
          add_b_i = f0_r;
          add_sub_i = 1'b1;
          next_state = ST_FORWARD_SUB_WAIT;
        end
      end

      ST_FORWARD_SUB_WAIT: begin
        if (add_v_o) begin
          add_yumi   = 1'b1;
          next_state = ST_FORWARD_MUL;
        end
      end

      ST_FORWARD_FINISH: begin
        if (r_r + 1 < req_nrhs_i) next_state = ST_FORWARD_INIT;
        else if (i_r + 1 < d) next_state = ST_FORWARD_INIT;
        else next_state = ST_DIAG_DIV;
      end

      // --------------------------------------------------
      // Diagonal scale: z = y / D
      // --------------------------------------------------
      ST_DIAG_DIV: begin
        if (div_in_ready) begin
          div_in_valid = 1'b1;
          div_a_i      = B[i_r][r_r];
          div_b_i      = D[i_r];
          next_state   = ST_DIAG_DIV_WAIT;
        end
      end

      ST_DIAG_DIV_WAIT: begin
        if (div_out_valid) next_state = ST_DIAG_FINISH;
      end

      ST_DIAG_FINISH: begin
        if (r_r + 1 < req_nrhs_i) next_state = ST_DIAG_DIV;
        else if (i_r + 1 < d) next_state = ST_DIAG_DIV;
        else next_state = ST_BACKWARD_INIT;
      end

      // --------------------------------------------------
      // Backward solve L^T * x = z (x stored in B)
      // x[i][r] = z[i][r] - sum_{j>i} L[j][i] * x[j][r]
      // --------------------------------------------------
      ST_BACKWARD_INIT: begin
        next_state = ST_BACKWARD_MUL;
      end

      ST_BACKWARD_MUL: begin
        if (j_r > i_r && j_r < d) begin
          if (mul_ready) begin
            mul_v_i = 1'b1;
            mul_a_i = L[j_r][i_r];
            mul_b_i = B[j_r][r_r];
            next_state = ST_BACKWARD_MUL_WAIT;
          end
        end else begin
          next_state = ST_BACKWARD_FINISH;
        end
      end

      ST_BACKWARD_MUL_WAIT: begin
        if (mul_v_o) begin
          mul_yumi   = 1'b1;
          next_state = ST_BACKWARD_SUB;
        end
      end

      ST_BACKWARD_SUB: begin
        if (add_ready) begin
          add_v_i = 1'b1;
          add_a_i = acc_r;
          add_b_i = f0_r;
          add_sub_i = 1'b1;
          next_state = ST_BACKWARD_SUB_WAIT;
        end
      end

      ST_BACKWARD_SUB_WAIT: begin
        if (add_v_o) begin
          add_yumi   = 1'b1;
          next_state = ST_BACKWARD_MUL;
        end
      end

      ST_BACKWARD_FINISH: begin
        if (r_r + 1 < req_nrhs_i) next_state = ST_BACKWARD_INIT;
        else if (i_r != 0) next_state = ST_BACKWARD_INIT;
        else next_state = ST_PACK;
      end

      // --------------------------------------------------
      ST_PACK: begin
        next_state = ST_DONE;
      end

      ST_DONE: begin
        if (rsp_ready_i) next_state = ST_IDLE;
      end

      default: next_state = ST_IDLE;
    endcase
  end

  // ----------------------------------------------------------
  // Sequential logic
  // ----------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r       <= ST_IDLE;
      k_r           <= '0;
      i_r           <= '0;
      j_r           <= '0;
      r_r           <= '0;
      acc_r         <= '0;
      f0_r          <= '0;
      fail_r        <= 1'b0;
      regularized_r <= 1'b0;
      nan_guard_r   <= 1'b0;
      min_pivot_r   <= '0;
      rsp_X_flat_o  <= '0;
    end else begin
      state_r <= next_state;

      case (state_r)
        ST_IDLE: begin
          if (req_valid_i && req_ready_o) begin
            k_r           <= '0;
            i_r           <= '0;
            j_r           <= '0;
            r_r           <= '0;
            fail_r        <= 1'b0;
            regularized_r <= req_regularize_en_i;
            nan_guard_r   <= 1'b0;
            min_pivot_r   <= '0;
          end
        end

        ST_LOAD_A: begin
          if (k_r < p_d) begin
            A[i_r][j_r] <= req_A_flat_i[k_r*32+:32];
            A[j_r][i_r] <= req_A_flat_i[k_r*32+:32];
            if (is_nan_or_inf(req_A_flat_i[k_r*32+:32])) nan_guard_r <= 1'b1;

            if (j_r + 1 < d) begin
              j_r <= j_r + 1;
            end else begin
              i_r <= i_r + 1;
              j_r <= i_r + 1;
            end
            k_r <= k_r + 1;
          end else begin
            k_r <= '0;
            i_r <= '0;
            j_r <= '0;
          end
        end

        ST_LOAD_B: begin
          for (int ii = 0; ii < GBP_MAX_VAR_DIM; ii++) begin
            for (int rr = 0; rr < GBP_MAX_RHS; rr++) begin
              if (ii < d && rr < req_nrhs_i) B[ii][rr] <= req_B_flat_i[(ii*GBP_MAX_RHS+rr)*32+:32];
              else B[ii][rr] <= '0;
            end
          end
        end

        ST_REG_WAIT: begin
          if (add_v_o) begin
            A[k_r][k_r] <= add_z_o;
            if (is_nan_or_inf(add_z_o)) nan_guard_r <= 1'b1;
          end
        end

        ST_FACTOR_D_INIT: begin
          acc_r <= A[k_r][k_r];
          j_r   <= '0;
        end

        ST_FACTOR_D_MUL1_WAIT: begin
          if (mul_v_o) f0_r <= mul_z_o;
        end

        ST_FACTOR_D_MUL2_WAIT: begin
          if (mul_v_o) f0_r <= mul_z_o;
        end

        ST_FACTOR_D_SUB_WAIT: begin
          if (add_v_o) begin
            acc_r <= add_z_o;
            j_r   <= j_r + 1;
          end
        end

        ST_FACTOR_D_FINISH: begin
          D[k_r] <= acc_r;
          if (!(acc_r > req_pivot_eps_i)) fail_r <= 1'b1;
          else if (min_pivot_r == '0 || acc_r < min_pivot_r) min_pivot_r <= acc_r;
          if (is_nan_or_inf(acc_r)) nan_guard_r <= 1'b1;

          j_r <= '0;
          if (k_r + 1 < d) begin
            // Compute L[:,k] next, start at row k+1
            i_r <= k_r + 1;
          end else begin
            // Last D done; prepare for forward solve
            k_r <= '0;
            i_r <= '0;
            r_r <= '0;
          end
        end

        ST_FACTOR_L_INIT: begin
          acc_r <= A[i_r][k_r];
          j_r   <= '0;
        end

        ST_FACTOR_L_MUL1_WAIT: begin
          if (mul_v_o) f0_r <= mul_z_o;
        end

        ST_FACTOR_L_MUL2_WAIT: begin
          if (mul_v_o) f0_r <= mul_z_o;
        end

        ST_FACTOR_L_SUB_WAIT: begin
          if (add_v_o) begin
            acc_r <= add_z_o;
            j_r   <= j_r + 1;
          end
        end

        ST_FACTOR_L_DIV_WAIT: begin
          if (div_out_valid) begin
            L[i_r][k_r] <= div_z_o;
            if (is_nan_or_inf(div_z_o)) nan_guard_r <= 1'b1;
          end
        end

        ST_FACTOR_L_FINISH: begin
          j_r <= '0;
          if (i_r + 1 < d) begin
            i_r <= i_r + 1;
          end else if (k_r + 1 < d) begin
            k_r <= k_r + 1;
            i_r <= k_r + 2;
          end else begin
            // All L computed, move to forward solve
            k_r <= '0;
            i_r <= '0;
            r_r <= '0;
          end
        end

        ST_FORWARD_INIT: begin
          acc_r <= B[i_r][r_r];
          j_r   <= '0;
        end

        ST_FORWARD_MUL_WAIT: begin
          if (mul_v_o) f0_r <= mul_z_o;
        end

        ST_FORWARD_SUB_WAIT: begin
          if (add_v_o) begin
            acc_r <= add_z_o;
            j_r   <= j_r + 1;
          end
        end

        ST_FORWARD_FINISH: begin
          B[i_r][r_r] <= acc_r;
          if (is_nan_or_inf(acc_r)) nan_guard_r <= 1'b1;
          if (r_r + 1 < req_nrhs_i) begin
            r_r <= r_r + 1;
          end else begin
            r_r <= '0;
            if (i_r + 1 < d) i_r <= i_r + 1;
            else i_r <= '0;
          end
        end

        ST_DIAG_DIV_WAIT: begin
          if (div_out_valid) begin
            B[i_r][r_r] <= div_z_o;
            if (is_nan_or_inf(div_z_o)) nan_guard_r <= 1'b1;
          end
        end

        ST_DIAG_FINISH: begin
          if (r_r + 1 < req_nrhs_i) begin
            r_r <= r_r + 1;
          end else begin
            r_r <= '0;
            if (i_r + 1 < d) begin
              i_r <= i_r + 1;
            end else begin
              // Last diagonal scale done; prepare to start backward solve at row d-1
              i_r <= d - 1;
            end
          end
        end

        ST_BACKWARD_INIT: begin
          acc_r <= B[i_r][r_r];
          j_r   <= d - 1;
        end

        ST_BACKWARD_MUL_WAIT: begin
          if (mul_v_o) f0_r <= mul_z_o;
        end

        ST_BACKWARD_SUB_WAIT: begin
          if (add_v_o) begin
            acc_r <= add_z_o;
            j_r   <= j_r - 1;
          end
        end

        ST_BACKWARD_FINISH: begin
          B[i_r][r_r] <= acc_r;
          if (is_nan_or_inf(acc_r)) nan_guard_r <= 1'b1;
          if (r_r + 1 < req_nrhs_i) begin
            r_r <= r_r + 1;
          end else begin
            r_r <= '0;
            if (i_r != 0) i_r <= i_r - 1;
          end
        end

        ST_PACK: begin
          for (int ii = 0; ii < GBP_MAX_VAR_DIM; ii++) begin
            for (int rr = 0; rr < GBP_MAX_RHS; rr++) begin
              if (ii < d && rr < req_nrhs_i) rsp_X_flat_o[(ii*GBP_MAX_RHS+rr)*32+:32] <= B[ii][rr];
              else rsp_X_flat_o[(ii*GBP_MAX_RHS+rr)*32+:32] <= '0;
            end
          end
        end

        default: ;
      endcase
    end
  end

  // ----------------------------------------------------------
  // Output assignments
  // ----------------------------------------------------------
  assign req_ready_o = (state_r == ST_IDLE);
  assign rsp_valid_o = (state_r == ST_DONE);
  assign rsp_dim_o = req_dim_i;
  assign rsp_nrhs_o = req_nrhs_i;
  assign rsp_fail_o = fail_r;
  assign rsp_regularized_o = regularized_r;
  assign rsp_nan_guard_o = nan_guard_r;
  assign rsp_min_pivot_o = min_pivot_r;

endmodule
