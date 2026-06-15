// gbp_compute_core.sv
// GBP Compute Core v0.7 — top-level compute core
// Receives assembled operand beats and produces packed result packets.
// Source: docs/gbp_pe/08_NEW_COMPUTE_UNIT.md §8

module gbp_compute_core
  import gbp_op_pkg::*;
(
    input  logic clk_i,
    input  logic reset_i,

    // Command channel
    input  logic          cmd_valid_i,
    output logic          cmd_ready_o,
    input  gbp_core_req_t cmd_i,

    // Operand stream channel
    input  logic                 operand_valid_i,
    output logic                 operand_ready_o,
    input  operand_stream_beat_t operand_i,

    // Response channel
    output logic          rsp_valid_o,
    input  logic          rsp_ready_i,
    output gbp_core_rsp_t rsp_o
);

  // ----------------------------------------------------------
  // Latched command and decode
  // ----------------------------------------------------------
  gbp_core_req_t cmd_r;

  // Flatten unpacked operand data array for submodules that expect a bus
  logic [OPERAND_STREAM_WIDTH*32-1:0] operand_data_flat;
  always_comb begin
    for (int i = 0; i < OPERAND_STREAM_WIDTH; i++)
      operand_data_flat[i*32 +: 32] = operand_i.data[i];
  end

  // Flatten mu_old register for belief_result_builder
  logic [GBP_MAX_VAR_DIM*32-1:0] mu_old_flat;
  always_comb begin
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++)
      mu_old_flat[i*32 +: 32] = mu_old_r[i];
  end

  logic        is_msg_r, is_belief_r;
  logic [2:0]  dim_i_val_r, dim_o_val_r;
  logic [4:0]  e_i_r, e_o_r, p_i_r, p_o_r;
  logic [2:0]  nrhs_r;
  logic        legal_r;

  // Combinational decode of current/latched command
  logic        dec_is_msg, dec_is_belief, dec_legal;
  logic [2:0]  dec_dim_i_val, dec_dim_o_val;
  logic [4:0]  dec_e_i, dec_e_o, dec_p_i, dec_p_o;
  logic [2:0]  dec_nrhs;

  op_decoder u_dec (
      .cmd_op_i          (cmd_i.op),
      .cmd_factor_type_i (cmd_i.factor_type),
      .cmd_dim_i_i       (cmd_i.dim_i),
      .cmd_dim_o_i       (cmd_i.dim_o),
      .cmd_direction_i   (cmd_i.direction),
      .is_msg_o          (dec_is_msg),
      .is_belief_o       (dec_is_belief),
      .is_relin_o        (),
      .is_robust_o       (),
      .dim_i_val_o       (dec_dim_i_val),
      .dim_o_val_o       (dec_dim_o_val),
      .e_i_o             (dec_e_i),
      .e_o_o             (dec_e_o),
      .p_i_o             (dec_p_i),
      .p_o_o             (dec_p_o),
      .nrhs_o            (dec_nrhs),
      .legal_o           (dec_legal),
      .illegal_dim_o     (),
      .illegal_factor_o  (),
      .illegal_op_o      ()
  );

  // ----------------------------------------------------------
  // FSM
  // ----------------------------------------------------------
  typedef enum logic [4:0] {
    ST_IDLE,
    ST_RSP,
    // OP_MSG_F2V path
    ST_MSG_M1_STATIC,
    ST_MSG_M2_CAVITY,
    ST_MSG_M3_RHS,
    ST_MSG_M4_SOLVE,
    ST_MSG_M5_SCHUR,
    ST_MSG_M6_DAMP,
    // OP_BELIEF path
    ST_BEL_B1_PRIOR,
    ST_BEL_B2_MSG,
    ST_BEL_B3_ADAPT,
    ST_BEL_B4_SOLVE,
    ST_BEL_B5_RESULT
  } state_e;

  state_e state_r, state_1d_r;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r   <= ST_IDLE;
      state_1d_r <= ST_IDLE;
    end else begin
      state_1d_r <= state_r;
    end
  end

  // One-cycle entry pulses for modules that need them
  logic ow_start, cav_start;
  assign ow_start  = (state_r == ST_MSG_M1_STATIC) && (state_1d_r != ST_MSG_M1_STATIC);
  assign cav_start = (state_r == ST_MSG_M2_CAVITY) && (state_1d_r != ST_MSG_M2_CAVITY);

  // ----------------------------------------------------------
  // Status / result registers
  // ----------------------------------------------------------
  logic        rsp_fail_r, rsp_regularized_r, rsp_nan_guard_r;
  logic        rsp_degree_mismatch_r, rsp_stream_error_r;
  logic [31:0] rsp_min_pivot_r;

  logic [31:0] msg_eta_r [GBP_MAX_VAR_DIM];
  logic [31:0] msg_L_r   [GBP_MAX_PACKED_VAR];

  logic [31:0] bel_eta_r     [GBP_MAX_VAR_DIM];
  logic [31:0] bel_L_r       [GBP_MAX_PACKED_VAR];
  logic [31:0] bel_mu_r      [GBP_MAX_VAR_DIM];
  logic [31:0] bel_residual_r;
  logic [31:0] mu_old_r      [GBP_MAX_VAR_DIM];

  // ----------------------------------------------------------
  // Submodules
  // ----------------------------------------------------------

  // Operand window (active-low reset)
  logic ow_load_valid, ow_load_ready, ow_static_valid, ow_clear;
  logic [GBP_MAX_VAR_DIM*32-1:0]                ow_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0]             ow_L_ii_flat;
  logic [GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM*32-1:0] ow_L_io_flat;
  logic [GBP_MAX_VAR_DIM*32-1:0]                ow_old_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0]             ow_old_L_flat;

  operand_window u_ow (
      .clk_i              (clk_i),
      .rst_n_i            (~reset_i),
      .dim_i_i            (cmd_r.dim_i),
      .dim_o_i            (cmd_r.dim_o),
      .start_i            (ow_start),
      .load_valid_i       (ow_load_valid),
      .load_ready_o       (ow_load_ready),
      .load_kind_i        (operand_i.kind),
      .load_data_flat_i   (operand_data_flat),
      .load_last_i        (operand_i.last),
      .msg_dim_i_o        (),
      .msg_dim_o_o        (),
      .msg_eta_flat_o     (ow_eta_flat),
      .msg_L_ii_flat_o    (ow_L_ii_flat),
      .msg_L_io_flat_o    (ow_L_io_flat),
      .msg_old_eta_flat_o (ow_old_eta_flat),
      .msg_old_L_flat_o   (ow_old_L_flat),
      .msg_static_valid_o (ow_static_valid),
      .clear_i            (ow_clear)
  );

  // Cavity builder
  logic cav_start_valid, cav_start_ready, cav_beat_valid, cav_beat_ready;
  logic cav_valid, cav_ready;
  logic [GBP_MAX_VAR_DIM*32-1:0]    cav_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0] cav_L_flat;
  logic cav_stream_error;

  cavity_builder u_cav (
      .clk_i              (clk_i),
      .reset_i            (reset_i),
      .dim_o_i            (cmd_r.dim_o),
      .start_valid_i      (cav_start_valid),
      .start_ready_o      (cav_start_ready),
      .beat_valid_i       (cav_beat_valid),
      .beat_ready_o       (cav_beat_ready),
      .beat_kind_i        (operand_i.kind),
      .beat_data_flat_i   (operand_data_flat),
      .beat_last_i        (operand_i.last),
      .cav_valid_o        (cav_valid),
      .cav_ready_i        (cav_ready),
      .cav_eta_flat_o     (cav_eta_flat),
      .cav_L_flat_o       (cav_L_flat),
      .stream_error_o     (cav_stream_error)
  );

  // RHS builder (combinational)
  logic [2:0] rhs_nrhs;
  logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] rhs_B_flat;

  rhs_builder_for_message u_rhs (
      .dim_i_i          (cmd_r.dim_i),
      .dim_o_i          (cmd_r.dim_o),
      .L_io_dense_flat_i(ow_L_io_flat),
      .cav_eta_flat_i   (cav_eta_flat),
      .nrhs_o           (rhs_nrhs),
      .B_flat_o         (rhs_B_flat)
  );

  // LDLT solver
  logic ldlt_req_valid, ldlt_req_ready;
  logic ldlt_rsp_valid, ldlt_rsp_ready;
  logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] ldlt_X_flat;
  logic        ldlt_fail, ldlt_regularized, ldlt_nan_guard;
  logic [31:0] ldlt_min_pivot;

  // LDLT request muxing between message and belief paths
  gbp_dim_e                              ldlt_req_dim;
  logic [2:0]                            ldlt_req_nrhs;
  logic [GBP_MAX_PACKED_VAR*32-1:0]      ldlt_req_A_flat;
  logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0] ldlt_req_B_flat;

  always_comb begin
    if (is_belief_r) begin
      ldlt_req_dim     = cmd_r.dim_i;
      ldlt_req_nrhs    = bsa_req_nrhs_o;
      ldlt_req_A_flat  = bsa_A_flat;
      ldlt_req_B_flat  = bsa_B_flat;
    end else begin
      ldlt_req_dim     = cmd_r.dim_o;
      ldlt_req_nrhs    = rhs_nrhs;
      ldlt_req_A_flat  = cav_L_flat;
      ldlt_req_B_flat  = rhs_B_flat;
    end
  end

  ldlt_solve_core u_ldlt (
      .clk_i              (clk_i),
      .reset_i            (reset_i),
      .req_valid_i        (ldlt_req_valid),
      .req_ready_o        (ldlt_req_ready),
      .req_dim_i          (ldlt_req_dim),
      .req_nrhs_i         (ldlt_req_nrhs),
      .req_A_flat_i       (ldlt_req_A_flat),
      .req_B_flat_i       (ldlt_req_B_flat),
      .req_diag_lambda_i  (cmd_r.diag_lambda),
      .req_pivot_eps_i    (cmd_r.pivot_eps),
      .req_regularize_en_i(cmd_r.regularize_en),
      .rsp_valid_o        (ldlt_rsp_valid),
      .rsp_ready_i        (ldlt_rsp_ready),
      .rsp_dim_o          (),
      .rsp_nrhs_o         (),
      .rsp_X_flat_o       (ldlt_X_flat),
      .rsp_fail_o         (ldlt_fail),
      .rsp_regularized_o  (ldlt_regularized),
      .rsp_nan_guard_o    (ldlt_nan_guard),
      .rsp_min_pivot_o    (ldlt_min_pivot)
  );

  // Schur update unit
  logic schur_valid_i, schur_ready_o, schur_valid_o, schur_ready_i;
  logic [GBP_MAX_VAR_DIM*32-1:0]    schur_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0] schur_L_flat;

  schur_update_unit u_schur (
      .clk_i              (clk_i),
      .reset_i            (reset_i),
      .valid_i            (schur_valid_i),
      .ready_o            (schur_ready_o),
      .dim_i_i            (cmd_r.dim_i),
      .dim_o_i            (cmd_r.dim_o),
      .factor_eta_flat_i  (ow_eta_flat),
      .factor_L_ii_flat_i (ow_L_ii_flat),
      .L_io_dense_flat_i  (ow_L_io_flat),
      .solve_X_flat_i     (ldlt_X_flat),
      .valid_o            (schur_valid_o),
      .ready_i            (schur_ready_i),
      .msg_eta_raw_flat_o (schur_eta_flat),
      .msg_L_raw_flat_o   (schur_L_flat)
  );

  // Damping unit
  logic damp_valid_i, damp_ready_o, damp_valid_o, damp_ready_i;
  logic [GBP_MAX_VAR_DIM*32-1:0]    damp_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0] damp_L_flat;

  damping_unit u_damp (
      .clk_i               (clk_i),
      .reset_i             (reset_i),
      .valid_i             (damp_valid_i),
      .ready_o             (damp_ready_o),
      .dim_i_i             (cmd_r.dim_i),
      .damping_i           (cmd_r.damping),
      .msg_eta_raw_flat_i  (schur_eta_flat),
      .msg_L_raw_flat_i    (schur_L_flat),
      .old_msg_eta_flat_i  (ow_old_eta_flat),
      .old_msg_L_flat_i    (ow_old_L_flat),
      .valid_o             (damp_valid_o),
      .ready_i             (damp_ready_i),
      .msg_eta_flat_o      (damp_eta_flat),
      .msg_L_flat_o        (damp_L_flat)
  );

  // Belief operand unpacker
  logic bop_beat_valid, bop_beat_ready;
  logic bop_prior_valid, bop_prior_ready;
  logic bop_msg_valid, bop_msg_ready;
  logic bop_msg_last;
  logic [GBP_MAX_VAR_DIM*32-1:0]    bop_prior_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0] bop_prior_L_flat;
  logic [GBP_MAX_VAR_DIM*32-1:0]    bop_prior_mu_old_flat;
  logic [GBP_MAX_VAR_DIM*32-1:0]    bop_msg_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0] bop_msg_L_flat;
  logic bop_stream_error;

  belief_operand_unpacker u_bop (
      .clk_i                (clk_i),
      .reset_i              (reset_i),
      .dim_i                (cmd_r.dim_i),
      .degree_i             (cmd_r.degree),
      .op_id_i              (cmd_r.op_id),
      .beat_valid_i         (bop_beat_valid),
      .beat_ready_o         (bop_beat_ready),
      .beat_kind_i          (operand_i.kind),
      .beat_data_flat_i     (operand_data_flat),
      .beat_op_id_i         (operand_i.op_id),
      .beat_beat_idx_i      (operand_i.beat_idx),
      .beat_last_i          (operand_i.last),
      .prior_valid_o        (bop_prior_valid),
      .prior_ready_i        (bop_prior_ready),
      .prior_dim_o          (),
      .prior_degree_o       (),
      .prior_eta_flat_o     (bop_prior_eta_flat),
      .prior_L_flat_o       (bop_prior_L_flat),
      .prior_mu_old_flat_o  (bop_prior_mu_old_flat),
      .msg_valid_o          (bop_msg_valid),
      .msg_ready_i          (bop_msg_ready),
      .msg_dim_o            (),
      .msg_eta_flat_o       (bop_msg_eta_flat),
      .msg_L_flat_o         (bop_msg_L_flat),
      .msg_last_o           (bop_msg_last),
      .stream_error_o       (bop_stream_error)
  );

  // Packed accumulator
  logic acc_start_valid, acc_start_ready;
  logic acc_msg_valid, acc_msg_ready;
  logic acc_valid, acc_ready;
  logic [15:0] acc_msg_count;
  logic        acc_degree_mismatch;
  logic [GBP_MAX_VAR_DIM*32-1:0]    acc_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0] acc_L_flat;

  packed_accumulator u_acc (
      .clk_i              (clk_i),
      .reset_i            (reset_i),
      .start_valid_i      (acc_start_valid),
      .start_ready_o      (acc_start_ready),
      .dim_i              (cmd_r.dim_i),
      .prior_eta_flat_i   (bop_prior_eta_flat),
      .prior_L_flat_i     (bop_prior_L_flat),
      .degree_i           (cmd_r.degree),
      .msg_valid_i        (acc_msg_valid),
      .msg_ready_o        (acc_msg_ready),
      .msg_eta_flat_i     (bop_msg_eta_flat),
      .msg_L_flat_i       (bop_msg_L_flat),
      .msg_last_i         (bop_msg_last),
      .acc_valid_o        (acc_valid),
      .acc_ready_i        (acc_ready),
      .dim_o              (),
      .acc_eta_flat_o     (acc_eta_flat),
      .acc_L_flat_o       (acc_L_flat),
      .msg_count_o        (acc_msg_count),
      .degree_mismatch_o  (acc_degree_mismatch)
  );

  // Belief solve adapter
  logic bsa_valid, bsa_ready;
  logic bsa_req_valid, bsa_req_ready;
  logic [2:0]                                  bsa_req_nrhs_o;
  logic [GBP_MAX_PACKED_VAR*32-1:0]            bsa_A_flat;
  logic [GBP_MAX_VAR_DIM*GBP_MAX_RHS*32-1:0]   bsa_B_flat;

  belief_solve_adapter u_bsa (
      .clk_i                   (clk_i),
      .reset_i                 (reset_i),
      .valid_i                 (bsa_valid),
      .ready_o                 (bsa_ready),
      .dim_i                   (cmd_r.dim_i),
      .acc_eta_flat_i          (acc_eta_flat),
      .acc_L_flat_i            (acc_L_flat),
      .diag_lambda_i           (cmd_r.diag_lambda),
      .pivot_eps_i             (cmd_r.pivot_eps),
      .regularize_en_i         (cmd_r.regularize_en),
      .solve_req_valid_o       (bsa_req_valid),
      .solve_req_ready_i       (bsa_req_ready),
      .solve_req_dim_o         (),
      .solve_req_nrhs_o        (bsa_req_nrhs_o),
      .solve_req_A_flat_o      (bsa_A_flat),
      .solve_req_B_flat_o      (bsa_B_flat),
      .solve_req_diag_lambda_o (),
      .solve_req_pivot_eps_o   (),
      .solve_req_regularize_en_o()
  );

  // Belief result builder
  logic brb_valid, brb_ready, brb_valid_o, brb_ready_i;
  logic [GBP_MAX_VAR_DIM*32-1:0]    brb_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0] brb_L_flat;
  logic [GBP_MAX_VAR_DIM*32-1:0]    brb_mu_flat;
  logic [31:0] brb_residual;
  logic        brb_fail, brb_regularized, brb_nan_guard, brb_degree_mismatch;
  logic [31:0] brb_min_pivot;

  belief_result_builder u_brb (
      .clk_i                     (clk_i),
      .reset_i                   (reset_i),
      .valid_i                   (brb_valid),
      .ready_o                   (brb_ready),
      .dim_i                     (cmd_r.dim_i),
      .acc_eta_flat_i            (acc_eta_flat),
      .acc_L_flat_i              (acc_L_flat),
      .mu_old_flat_i             (mu_old_flat),
      .solve_dim_i               (cmd_r.dim_i),
      .solve_X_flat_i            (ldlt_X_flat),
      .solve_fail_i              (ldlt_fail),
      .solve_regularized_i       (ldlt_regularized),
      .solve_nan_guard_i         (ldlt_nan_guard),
      .solve_min_pivot_i         (ldlt_min_pivot),
      .valid_o                   (brb_valid_o),
      .ready_i                   (brb_ready_i),
      .result_dim_o              (),
      .result_eta_flat_o         (brb_eta_flat),
      .result_L_flat_o           (brb_L_flat),
      .result_mu_flat_o          (brb_mu_flat),
      .result_residual_o         (brb_residual),
      .result_fail_o             (brb_fail),
      .result_regularized_o      (brb_regularized),
      .result_nan_guard_o        (brb_nan_guard),
      .result_degree_mismatch_o  (brb_degree_mismatch),
      .result_min_pivot_o        (brb_min_pivot)
  );

  // ----------------------------------------------------------
  // Output response assembly
  // ----------------------------------------------------------
  always_comb begin
    rsp_o.op               = cmd_r.op;
    rsp_o.ctx_id           = cmd_r.ctx_id;
    rsp_o.op_id            = cmd_r.op_id;
    rsp_o.dst_addr         = cmd_r.dst_addr;
    rsp_o.aux_addr         = cmd_r.aux_addr;
    rsp_o.node_id          = cmd_r.node_id;
    rsp_o.factor_id        = cmd_r.factor_id;

    rsp_o.fail             = rsp_fail_r;
    rsp_o.regularized      = rsp_regularized_r;
    rsp_o.nan_guard        = rsp_nan_guard_r;
    rsp_o.degree_mismatch  = rsp_degree_mismatch_r;
    rsp_o.stream_error     = rsp_stream_error_r;
    rsp_o.min_pivot        = rsp_min_pivot_r;

    rsp_o.msg_result.dim        = cmd_r.dim_i;
    rsp_o.msg_result.fail       = rsp_fail_r;
    rsp_o.msg_result.regularized= rsp_regularized_r;
    rsp_o.msg_result.nan_guard  = rsp_nan_guard_r;
    rsp_o.msg_result.min_pivot  = rsp_min_pivot_r;

    rsp_o.belief_result.dim             = cmd_r.dim_i;
    rsp_o.belief_result.fail            = rsp_fail_r;
    rsp_o.belief_result.regularized     = rsp_regularized_r;
    rsp_o.belief_result.nan_guard       = rsp_nan_guard_r;
    rsp_o.belief_result.degree_mismatch = rsp_degree_mismatch_r;
    rsp_o.belief_result.min_pivot       = rsp_min_pivot_r;
    rsp_o.belief_result.residual        = bel_residual_r;

    for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
      rsp_o.msg_result.eta[i]    = msg_eta_r[i];
      rsp_o.belief_result.eta[i] = bel_eta_r[i];
      rsp_o.belief_result.mu[i]  = bel_mu_r[i];
    end
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
      rsp_o.msg_result.L_packed[i]    = msg_L_r[i];
      rsp_o.belief_result.L_packed[i] = bel_L_r[i];
    end

    rsp_o.relin_result.need_refresh  = 1'b0;
    rsp_o.relin_result.delta_norm_sq = '0;
    rsp_o.robust_result.factor_type = FACTOR_SCALAR;
    rsp_o.robust_result.dim0        = DIM_1;
    rsp_o.robust_result.dim1        = DIM_1;
    rsp_o.robust_result.weight      = '0;
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
      rsp_o.robust_result.eta0[i] = '0;
      rsp_o.robust_result.eta1[i] = '0;
    end
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
      rsp_o.robust_result.L00_packed[i] = '0;
      rsp_o.robust_result.L11_packed[i] = '0;
    end
    for (int i = 0; i < GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM; i++) begin
      rsp_o.robust_result.L01_dense[i] = '0;
    end
  end

  // ----------------------------------------------------------
  // Control / handshake routing
  // ----------------------------------------------------------
  assign cmd_ready_o = (state_r == ST_IDLE);

  // Operand stream ready mux
  assign ow_load_valid  = (state_r == ST_MSG_M1_STATIC) ? operand_valid_i : 1'b0;
  assign cav_beat_valid = (state_r == ST_MSG_M2_CAVITY) ? operand_valid_i : 1'b0;
  assign bop_beat_valid = ((state_r == ST_BEL_B1_PRIOR) || (state_r == ST_BEL_B2_MSG)) ? operand_valid_i : 1'b0;

  always_comb begin
    case (state_r)
      ST_MSG_M1_STATIC:  operand_ready_o = ow_load_ready;
      ST_MSG_M2_CAVITY:  operand_ready_o = cav_beat_ready;
      ST_BEL_B1_PRIOR,
      ST_BEL_B2_MSG:     operand_ready_o = bop_beat_ready;
      default:           operand_ready_o = 1'b0;
    endcase
  end

  // Cavity builder start pulse
  assign cav_start_valid = cav_start;

  // Packed accumulator / belief unpacker hookup
  assign acc_start_valid = bop_prior_valid;
  assign bop_prior_ready = acc_start_ready;
  assign acc_msg_valid   = bop_msg_valid;
  assign bop_msg_ready   = acc_msg_ready;

  // Belief solve adapter input
  assign bsa_valid = (state_r == ST_BEL_B3_ADAPT) && acc_valid;
  assign bsa_req_ready = ldlt_req_ready;

  // LDLT request muxing
  always_comb begin
    if (is_msg_r) begin
      ldlt_req_valid        = (state_r == ST_MSG_M3_RHS);
    end else if (is_belief_r) begin
      ldlt_req_valid        = bsa_req_valid && (state_r == ST_BEL_B3_ADAPT);
    end else begin
      ldlt_req_valid        = 1'b0;
    end
  end

  // LDLT response ready
  assign ldlt_rsp_ready = (state_r == ST_MSG_M4_SOLVE) || (state_r == ST_BEL_B4_SOLVE);

  // Schur input
  assign schur_valid_i = (state_r == ST_MSG_M5_SCHUR);

  // Damping input
  assign damp_valid_i = (state_r == ST_MSG_M6_DAMP);

  // Result builder input pulse
  assign brb_valid = (state_r == ST_BEL_B5_RESULT) && (state_1d_r != ST_BEL_B5_RESULT);

  // Ready pulses to consume submodule outputs
  assign cav_ready     = (state_r == ST_MSG_M3_RHS) && ldlt_req_ready && is_msg_r;
  assign schur_ready_i = (state_r == ST_MSG_M6_DAMP) && (state_1d_r != ST_MSG_M6_DAMP);
  assign damp_ready_i  = (state_r == ST_RSP) && (state_1d_r != ST_RSP) && is_msg_r;
  assign brb_ready_i   = (state_r == ST_RSP) && (state_1d_r != ST_RSP) && is_belief_r;

  // Accumulator ready: release once result builder has accepted its input
  assign acc_ready     = (state_r == ST_BEL_B5_RESULT) && (state_1d_r != ST_BEL_B5_RESULT);

  // Operand window clear on completion
  assign ow_clear = (state_r == ST_IDLE) && (state_1d_r == ST_RSP);

  // ----------------------------------------------------------
  // Sequential FSM and register updates
  // ----------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= ST_IDLE;
      cmd_r.op            <= OP_MSG_F2V;
      cmd_r.factor_type   <= FACTOR_SCALAR;
      cmd_r.dim_i         <= DIM_1;
      cmd_r.dim_o         <= DIM_1;
      cmd_r.direction     <= 1'b0;
      cmd_r.ctx_id        <= 1'b0;
      cmd_r.op_id         <= '0;
      cmd_r.node_id       <= '0;
      cmd_r.factor_id     <= '0;
      cmd_r.dst_addr      <= '0;
      cmd_r.aux_addr      <= '0;
      cmd_r.damping       <= '0;
      cmd_r.diag_lambda   <= '0;
      cmd_r.pivot_eps     <= '0;
      cmd_r.regularize_en <= 1'b0;
      cmd_r.degree        <= '0;
      is_msg_r <= 1'b0;
      is_belief_r <= 1'b0;
      dim_i_val_r <= '0;
      dim_o_val_r <= '0;
      e_i_r <= '0; e_o_r <= '0;
      p_i_r <= '0; p_o_r <= '0;
      nrhs_r <= '0;
      legal_r <= 1'b0;

      rsp_fail_r <= 1'b0;
      rsp_regularized_r <= 1'b0;
      rsp_nan_guard_r <= 1'b0;
      rsp_degree_mismatch_r <= 1'b0;
      rsp_stream_error_r <= 1'b0;
      rsp_min_pivot_r <= '0;

      bel_residual_r <= '0;
    end else begin
      case (state_r)
        ST_IDLE: begin
          rsp_valid_o <= 1'b0;
          if (cmd_valid_i && cmd_ready_o) begin
            cmd_r <= cmd_i;
            is_msg_r     <= dec_is_msg;
            is_belief_r  <= dec_is_belief;
            dim_i_val_r  <= dec_dim_i_val;
            dim_o_val_r  <= dec_dim_o_val;
            e_i_r        <= dec_e_i;
            e_o_r        <= dec_e_o;
            p_i_r        <= dec_p_i;
            p_o_r        <= dec_p_o;
            nrhs_r       <= dec_nrhs;
            legal_r      <= dec_legal;

            rsp_fail_r            <= 1'b0;
            rsp_regularized_r     <= 1'b0;
            rsp_nan_guard_r       <= 1'b0;
            rsp_degree_mismatch_r <= 1'b0;
            rsp_stream_error_r    <= 1'b0;
            rsp_min_pivot_r       <= '0;
            bel_residual_r        <= '0;

            if (!dec_legal) begin
              rsp_fail_r <= 1'b1;
              state_r    <= ST_RSP;
            end else if (dec_is_msg) begin
              state_r <= ST_MSG_M1_STATIC;
            end else if (dec_is_belief) begin
              state_r <= ST_BEL_B1_PRIOR;
            end else begin
              // relin/robust not supported
              rsp_fail_r <= 1'b1;
              state_r    <= ST_RSP;
            end
          end
        end

        ST_MSG_M1_STATIC: begin
          if (cav_stream_error) begin
            rsp_stream_error_r <= 1'b1;
            state_r <= ST_RSP;
          end else if (ow_static_valid) begin
            state_r <= ST_MSG_M2_CAVITY;
          end
        end

        ST_MSG_M2_CAVITY: begin
          if (cav_stream_error) begin
            rsp_stream_error_r <= 1'b1;
            state_r <= ST_RSP;
          end else if (cav_valid) begin
            state_r <= ST_MSG_M3_RHS;
          end
        end

        ST_MSG_M3_RHS: begin
          if (ldlt_req_ready) begin
            state_r <= ST_MSG_M4_SOLVE;
          end
        end

        ST_MSG_M4_SOLVE: begin
          if (ldlt_rsp_valid) begin
            rsp_fail_r        <= ldlt_fail;
            rsp_regularized_r <= ldlt_regularized;
            rsp_nan_guard_r   <= ldlt_nan_guard;
            rsp_min_pivot_r   <= ldlt_min_pivot;
            state_r <= ST_MSG_M5_SCHUR;
          end
        end

        ST_MSG_M5_SCHUR: begin
          if (schur_valid_o) begin
            state_r <= ST_MSG_M6_DAMP;
          end
        end

        ST_MSG_M6_DAMP: begin
          if (damp_valid_o) begin
            for (int i = 0; i < GBP_MAX_VAR_DIM; i++)
              msg_eta_r[i] <= damp_eta_flat[i*32 +: 32];
            for (int i = 0; i < GBP_MAX_PACKED_VAR; i++)
              msg_L_r[i]   <= damp_L_flat[i*32 +: 32];
            state_r <= ST_RSP;
          end
        end

        ST_BEL_B1_PRIOR: begin
          if (bop_stream_error) begin
            rsp_stream_error_r <= 1'b1;
            state_r <= ST_RSP;
          end else if (bop_prior_valid && acc_start_ready) begin
            for (int i = 0; i < GBP_MAX_VAR_DIM; i++)
              mu_old_r[i] <= bop_prior_mu_old_flat[i*32 +: 32];
            state_r <= ST_BEL_B2_MSG;
          end
        end

        ST_BEL_B2_MSG: begin
          if (bop_stream_error) begin
            rsp_stream_error_r <= 1'b1;
            state_r <= ST_RSP;
          end else if (acc_valid) begin
            rsp_degree_mismatch_r <= acc_degree_mismatch;
            state_r <= ST_BEL_B3_ADAPT;
          end
        end

        ST_BEL_B3_ADAPT: begin
          if (bsa_req_valid) begin
            state_r <= ST_BEL_B4_SOLVE;
          end
        end

        ST_BEL_B4_SOLVE: begin
          if (ldlt_rsp_valid) begin
            rsp_fail_r        <= ldlt_fail;
            rsp_regularized_r <= ldlt_regularized;
            rsp_nan_guard_r   <= ldlt_nan_guard;
            rsp_min_pivot_r   <= ldlt_min_pivot;
            state_r <= ST_BEL_B5_RESULT;
          end
        end

        ST_BEL_B5_RESULT: begin
          if (brb_valid_o) begin
            for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
              bel_eta_r[i] <= brb_eta_flat[i*32 +: 32];
              bel_mu_r[i]  <= brb_mu_flat[i*32 +: 32];
            end
            for (int i = 0; i < GBP_MAX_PACKED_VAR; i++)
              bel_L_r[i] <= brb_L_flat[i*32 +: 32];
            bel_residual_r <= brb_residual;
            state_r <= ST_RSP;
          end
        end

        ST_RSP: begin
          rsp_valid_o <= 1'b1;
          if (rsp_ready_i) begin
            rsp_valid_o <= 1'b0;
            state_r <= ST_IDLE;
          end
        end

        default: state_r <= ST_IDLE;
      endcase
    end
  end

endmodule
