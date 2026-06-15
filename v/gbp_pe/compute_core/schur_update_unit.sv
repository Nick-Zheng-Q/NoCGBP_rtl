// schur_update_unit.sv
// GBP Compute Core v0.7 — Schur update unit
// Computes raw message: msg_L = L_ii - L_io * X_Lambda, msg_eta = eta_i - L_io * X_eta
// Source: docs/gbp_pe/08_NEW_COMPUTE_UNIT.md §14

module schur_update_unit
  import gbp_op_pkg::*;
(
  input  logic     clk_i,
  input  logic     reset_i,

  // Input
  input  logic     valid_i,
  output logic     ready_o,
  input  gbp_dim_e dim_i_i,
  input  gbp_dim_e dim_o_i,

  input  logic [GBP_MAX_VAR_DIM*32-1:0]                 factor_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0]              factor_L_ii_flat_i,
  input  logic [GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM*32-1:0] L_io_dense_flat_i,
  input  logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0]    solve_X_flat_i,

  // Output
  output logic     valid_o,
  input  logic     ready_i,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    msg_eta_raw_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] msg_L_raw_flat_o
);

  localparam int FP_E = 8;
  localparam int FP_M = 23;

  // ----------------------------------------------------------
  // Dimension decode
  // ----------------------------------------------------------
  logic [2:0] d_i, d_o;
  assign d_i = dim_to_val(dim_i_i);
  assign d_o = dim_to_val(dim_o_i);

  // ----------------------------------------------------------
  // Unpacked storage
  // ----------------------------------------------------------
  fp32_t factor_eta [GBP_MAX_VAR_DIM];
  fp32_t factor_L_ii [GBP_MAX_VAR_DIM][GBP_MAX_VAR_DIM];
  fp32_t L_io [GBP_MAX_VAR_DIM][GBP_MAX_VAR_DIM];
  fp32_t solve_X [GBP_MAX_VAR_DIM][GBP_MAX_RHS];

  // Output registers
  fp32_t msg_eta [GBP_MAX_VAR_DIM];
  fp32_t msg_L [GBP_MAX_VAR_DIM][GBP_MAX_VAR_DIM];

  // Working registers
  fp32_t acc_r;
  logic [2:0] p_r, q_r, m_r;

  // ----------------------------------------------------------
  // FP add/sub unit
  // ----------------------------------------------------------
  logic        add_v_i, add_ready, add_v_o;
  fp32_t       add_a_i, add_b_i, add_z_o;
  logic        add_sub_i;
  logic        add_yumi;

  bsg_fpu_add_sub #(.e_p(FP_E), .m_p(FP_M)) u_add (
    .clk_i(clk_i), .reset_i(reset_i), .en_i(1'b1),
    .v_i(add_v_i), .a_i(add_a_i), .b_i(add_b_i), .sub_i(add_sub_i),
    .ready_and_o(add_ready),
    .v_o(add_v_o), .z_o(add_z_o),
    .unimplemented_o(), .invalid_o(), .overflow_o(), .underflow_o(),
    .yumi_i(add_yumi)
  );

  // ----------------------------------------------------------
  // FP mul unit
  // ----------------------------------------------------------
  logic        mul_v_i, mul_ready, mul_v_o;
  fp32_t       mul_a_i, mul_b_i, mul_z_o;
  logic        mul_yumi;

  bsg_fpu_mul #(.e_p(FP_E), .m_p(FP_M)) u_mul (
    .clk_i(clk_i), .reset_i(reset_i), .en_i(1'b1),
    .v_i(mul_v_i), .a_i(mul_a_i), .b_i(mul_b_i),
    .ready_and_o(mul_ready),
    .v_o(mul_v_o), .z_o(mul_z_o),
    .unimplemented_o(), .invalid_o(), .overflow_o(), .underflow_o(),
    .yumi_i(mul_yumi)
  );

  // ----------------------------------------------------------
  // FSM
  // ----------------------------------------------------------
  typedef enum logic [4:0] {
    ST_IDLE,
    ST_ETA_INIT,
    ST_ETA_MUL,
    ST_ETA_MUL_WAIT,
    ST_ETA_ACC,
    ST_ETA_ACC_WAIT,
    ST_ETA_SUB,
    ST_ETA_SUB_WAIT,
    ST_ETA_FINISH,
    ST_L_INIT,
    ST_L_MUL,
    ST_L_MUL_WAIT,
    ST_L_ACC,
    ST_L_ACC_WAIT,
    ST_L_SUB,
    ST_L_SUB_WAIT,
    ST_L_FINISH,
    ST_DONE
  } state_e;

  state_e state_r, next_state;

  // ----------------------------------------------------------
  // Unpacking helper: packed upper-triangular index for (p,q), p<=q
  // idx = p*d_i - p*(p-1)/2 + (q-p)
  // ----------------------------------------------------------
  function automatic int packed_idx(input int p, input int q, input int dim);
    packed_idx = p * dim - p * (p - 1) / 2 + (q - p);
  endfunction

  // ----------------------------------------------------------
  // Combinational logic
  // ----------------------------------------------------------
  always_comb begin
    next_state = state_r;
    add_v_i    = 1'b0;
    add_a_i    = '0;
    add_b_i    = '0;
    add_sub_i  = 1'b0;
    add_yumi   = 1'b0;
    mul_v_i    = 1'b0;
    mul_a_i    = '0;
    mul_b_i    = '0;
    mul_yumi   = 1'b0;

    case (state_r)
      ST_IDLE: begin
        if (valid_i && ready_o)
          next_state = ST_ETA_INIT;
      end

      // --------------------------------------------------
      // msg_eta[p] = factor_eta[p] - sum_m L_io[p][m] * solve_X[m][d_i]
      // --------------------------------------------------
      ST_ETA_INIT: begin
        next_state = ST_ETA_MUL;
      end

      ST_ETA_MUL: begin
        if (m_r < d_o) begin
          if (mul_ready) begin
            mul_v_i = 1'b1;
            mul_a_i = L_io[p_r][m_r];
            mul_b_i = solve_X[m_r][d_i];
            next_state = ST_ETA_MUL_WAIT;
          end
        end else begin
          next_state = ST_ETA_SUB;
        end
      end

      ST_ETA_MUL_WAIT: begin
        if (mul_v_o) begin
          mul_yumi = 1'b1;
          next_state = ST_ETA_ACC;
        end
      end

      ST_ETA_ACC: begin
        if (add_ready) begin
          add_v_i   = 1'b1;
          add_a_i   = acc_r;
          add_b_i   = mul_z_o;
          add_sub_i = 1'b0;
          next_state = ST_ETA_ACC_WAIT;
        end
      end

      ST_ETA_ACC_WAIT: begin
        if (add_v_o) begin
          add_yumi = 1'b1;
          next_state = ST_ETA_MUL;
        end
      end

      ST_ETA_SUB: begin
        if (add_ready) begin
          add_v_i   = 1'b1;
          add_a_i   = factor_eta[p_r];
          add_b_i   = acc_r;
          add_sub_i = 1'b1;
          next_state = ST_ETA_SUB_WAIT;
        end
      end

      ST_ETA_SUB_WAIT: begin
        if (add_v_o) begin
          add_yumi = 1'b1;
          next_state = ST_ETA_FINISH;
        end
      end

      ST_ETA_FINISH: begin
        if (p_r + 1 < d_i)
          next_state = ST_ETA_INIT;
        else
          next_state = ST_L_INIT;
      end

      // --------------------------------------------------
      // msg_L[p][q] = factor_L_ii[p][q] - sum_m L_io[p][m] * solve_X[m][q]
      // for p <= q (upper triangular)
      // --------------------------------------------------
      ST_L_INIT: begin
        next_state = ST_L_MUL;
      end

      ST_L_MUL: begin
        if (m_r < d_o) begin
          if (mul_ready) begin
            mul_v_i = 1'b1;
            mul_a_i = L_io[p_r][m_r];
            mul_b_i = solve_X[m_r][q_r];
            next_state = ST_L_MUL_WAIT;
          end
        end else begin
          next_state = ST_L_SUB;
        end
      end

      ST_L_MUL_WAIT: begin
        if (mul_v_o) begin
          mul_yumi = 1'b1;
          next_state = ST_L_ACC;
        end
      end

      ST_L_ACC: begin
        if (add_ready) begin
          add_v_i   = 1'b1;
          add_a_i   = acc_r;
          add_b_i   = mul_z_o;
          add_sub_i = 1'b0;
          next_state = ST_L_ACC_WAIT;
        end
      end

      ST_L_ACC_WAIT: begin
        if (add_v_o) begin
          add_yumi = 1'b1;
          next_state = ST_L_MUL;
        end
      end

      ST_L_SUB: begin
        if (add_ready) begin
          add_v_i   = 1'b1;
          add_a_i   = factor_L_ii[p_r][q_r];
          add_b_i   = acc_r;
          add_sub_i = 1'b1;
          next_state = ST_L_SUB_WAIT;

        end
      end

      ST_L_SUB_WAIT: begin
        if (add_v_o) begin
          add_yumi = 1'b1;
          next_state = ST_L_FINISH;
        end
      end

      ST_L_FINISH: begin
        if (q_r + 1 < d_i)
          next_state = ST_L_INIT;
        else if (p_r + 1 < d_i)
          next_state = ST_L_INIT;
        else
          next_state = ST_DONE;
      end

      ST_DONE: begin
        if (ready_i)
          next_state = ST_IDLE;
      end

      default: next_state = ST_IDLE;
    endcase
  end

  // ----------------------------------------------------------
  // Sequential logic
  // ----------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= ST_IDLE;
      p_r     <= '0;
      q_r     <= '0;
      m_r     <= '0;
      acc_r   <= '0;
      valid_o <= 1'b0;
    end else begin
      state_r <= next_state;

      case (state_r)
        ST_IDLE: begin
          valid_o <= 1'b0;
          if (valid_i && ready_o) begin
            p_r <= '0;
            q_r <= '0;
            m_r <= '0;
            acc_r <= '0;


            // Unpack all inputs in one cycle (d <= 6)
            for (int p = 0; p < GBP_MAX_VAR_DIM; p++) begin
              factor_eta[p] <= factor_eta_flat_i[p*32 +: 32];
              for (int q = 0; q < GBP_MAX_VAR_DIM; q++) begin
                L_io[p][q] <= L_io_dense_flat_i[(p*GBP_MAX_VAR_DIM + q)*32 +: 32];
                solve_X[p][q] <= solve_X_flat_i[(p*GBP_MAX_RHS + q)*32 +: 32];

              end
            end

            // Unpack factor_L_ii from packed upper-triangular to dense symmetric
            for (int p = 0; p < GBP_MAX_VAR_DIM; p++) begin
              for (int q = 0; q < GBP_MAX_VAR_DIM; q++) begin
                if (p < d_i && q < d_i) begin
                  if (p <= q)
                    factor_L_ii[p][q] <= factor_L_ii_flat_i[packed_idx(p,q,d_i)*32 +: 32];
                  else
                    factor_L_ii[p][q] <= factor_L_ii_flat_i[packed_idx(q,p,d_i)*32 +: 32];
                end else begin
                  factor_L_ii[p][q] <= '0;
                end
              end
            end
          end
        end

        ST_ETA_INIT: begin
          acc_r <= '0;
          m_r   <= '0;
        end

        ST_ETA_ACC_WAIT: begin
          if (add_v_o) begin
            acc_r <= add_z_o;
            m_r   <= m_r + 1;
          end
        end

        ST_ETA_SUB_WAIT: begin
          if (add_v_o)
            acc_r <= add_z_o;
        end

        ST_ETA_FINISH: begin
          msg_eta[p_r] <= acc_r;
          if (p_r + 1 < d_i)
            p_r <= p_r + 1;
          else
            p_r <= '0;  // reset p for L phase
          m_r <= '0;
        end

        ST_L_INIT: begin
          acc_r <= '0;
          m_r   <= '0;
        end

        ST_L_ACC_WAIT: begin
          if (add_v_o) begin
            acc_r <= add_z_o;
            m_r   <= m_r + 1;
          end
        end

        ST_L_SUB_WAIT: begin
          if (add_v_o) begin
            acc_r <= add_z_o;

          end
        end

        ST_L_FINISH: begin
          msg_L[p_r][q_r] <= acc_r;
          msg_L[q_r][p_r] <= acc_r;

          m_r <= '0;
          if (q_r + 1 < d_i) begin
            q_r <= q_r + 1;
          end else if (p_r + 1 < d_i) begin
            p_r <= p_r + 1;
            q_r <= p_r + 1;
          end
        end

        ST_DONE: begin
          valid_o <= 1'b1;
        end

        default: ;
      endcase
    end
  end

  // ----------------------------------------------------------
  // Output assignments
  // ----------------------------------------------------------
  assign ready_o = (state_r == ST_IDLE);

  always_comb begin
    for (int p = 0; p < GBP_MAX_VAR_DIM; p++) begin
      if (p < d_i)
        msg_eta_raw_flat_o[p*32 +: 32] = msg_eta[p];
      else
        msg_eta_raw_flat_o[p*32 +: 32] = '0;
    end
  end

  // Pack msg_L upper-triangular into flat output (dense in first d_i*(d_i+1)/2 slots)
  always_comb begin
    msg_L_raw_flat_o = '0;
    for (int p = 0; p < GBP_MAX_VAR_DIM; p++) begin
      for (int q = p; q < GBP_MAX_VAR_DIM; q++) begin
        if (p < d_i && q < d_i)
          msg_L_raw_flat_o[packed_idx(p, q, d_i)*32 +: 32] = msg_L[p][q];
      end
    end
  end

endmodule
