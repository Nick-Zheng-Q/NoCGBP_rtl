// compute_unit_wrapper.sv
// GBP Compute Core v0.7 — wrapper between PE stream engines and gbp_compute_core
// Source: docs/gbp_pe/08_NEW_COMPUTE_UNIT.md §24
//
// All struct ports are flattened to scalars/flat vectors because Verilator
// flattens unpacked-struct module ports to a single bit, which breaks field
// accesses in parent modules.

module compute_unit_wrapper
  import gbp_op_pkg::*;
(
    input  logic clk_i,
    input  logic reset_i,

    // Command from resident scheduler (flattened)
    input  logic         cmd_valid_i,
    output logic         cmd_ready_o,
    input  logic [3:0]   cmd_op_i,
    input  logic [1:0]   cmd_factor_type_i,
    input  logic [1:0]   cmd_dim_i_i,
    input  logic [1:0]   cmd_dim_o_i,
    input  logic         cmd_direction_i,
    input  logic         cmd_ctx_id_i,
    input  logic [31:0]  cmd_op_id_i,
    input  logic [31:0]  cmd_node_id_i,
    input  logic [31:0]  cmd_factor_id_i,
    input  logic [31:0]  cmd_dst_addr_i,
    input  logic [31:0]  cmd_aux_addr_i,
    input  logic [15:0]  cmd_degree_i,
    input  logic [31:0]  cmd_damping_i,
    input  logic [31:0]  cmd_diag_lambda_i,
    input  logic [31:0]  cmd_pivot_eps_i,
    input  logic         cmd_regularize_en_i,
    input  logic [7:0]        cmd_operand_desc_valid_i,
    input  logic [8*4-1:0]    cmd_operand_desc_kind_i,
    input  logic [8*32-1:0]   cmd_operand_desc_base_addr_i,
    input  logic [8*16-1:0]   cmd_operand_desc_nbeats_i,

    // Read request to read stream engine (flattened)
    output logic         rd_req_valid_o,
    input  logic         rd_req_ready_i,
    output logic [31:0]  rd_req_op_id_o,
    output logic [3:0]   rd_req_op_o,
    output logic [31:0]  rd_req_base_addr_o,
    output logic [15:0]  rd_req_nbeats_o,
    output logic [3:0]   rd_req_kind_o,

    // Operand stream from read stream engine (flattened)
    input  logic        operand_valid_i,
    output logic        operand_ready_o,
    input  logic [3:0]  operand_kind_i,
    input  logic        operand_ctx_id_i,
    input  logic [31:0] operand_op_id_i,
    input  logic [15:0] operand_beat_idx_i,
    input  logic        operand_last_i,
    input  logic [OPERAND_STREAM_WIDTH*32-1:0] operand_data_flat_i,

    // Writeback to write stream engine (flattened)
    output logic        wb_valid_o,
    input  logic        wb_ready_i,
    output logic [31:0] wb_addr_o,
    output logic [15:0] wb_nwords_o,
    output logic [3:0]  wb_kind_o,
    output logic [GBP_MAX_WB_SCALARS*32-1:0] wb_payload_flat_o,
    output logic        wb_fail_o,
    output logic        wb_regularized_o,
    output logic        wb_nan_guard_o,

    // Done to scheduler (flattened)
    output logic        done_valid_o,
    input  logic        done_ready_i,
    output logic [31:0] done_node_id_o,
    output logic [31:0] done_factor_id_o,
    output logic [3:0]  done_op_o,
    output logic        done_ctx_id_o,
    output logic        done_success_o,
    output logic        done_fail_o,
    output logic        done_regularized_o,
    output logic        done_nan_guard_o,
    output logic        done_degree_mismatch_o,
    output logic        done_stream_error_o,
    output logic [31:0] done_residual_o,
    output logic [31:0] done_min_pivot_o
);

  // ----------------------------------------------------------
  // Reconstruct command struct from flattened inputs
  // ----------------------------------------------------------
  cu_cmd_t cmd_i;
  always_comb begin
    cmd_i.op            = gbp_op_e'(cmd_op_i);
    cmd_i.factor_type   = gbp_factor_type_e'(cmd_factor_type_i);
    cmd_i.dim_i         = gbp_dim_e'(cmd_dim_i_i);
    cmd_i.dim_o         = gbp_dim_e'(cmd_dim_o_i);
    cmd_i.direction     = cmd_direction_i;
    cmd_i.ctx_id        = cmd_ctx_id_i;
    cmd_i.op_id         = cmd_op_id_i;
    cmd_i.node_id       = cmd_node_id_i;
    cmd_i.factor_id     = cmd_factor_id_i;
    cmd_i.dst_addr      = cmd_dst_addr_i;
    cmd_i.aux_addr      = cmd_aux_addr_i;
    cmd_i.degree        = cmd_degree_i;
    cmd_i.damping       = cmd_damping_i;
    cmd_i.diag_lambda   = cmd_diag_lambda_i;
    cmd_i.pivot_eps     = cmd_pivot_eps_i;
    cmd_i.regularize_en = cmd_regularize_en_i;
    for (int i = 0; i < 8; i++) begin
      cmd_i.operand_desc[i].valid     = cmd_operand_desc_valid_i[i];
      cmd_i.operand_desc[i].kind      = operand_stream_kind_e'(cmd_operand_desc_kind_i[i*4 +: 4]);
      cmd_i.operand_desc[i].base_addr = cmd_operand_desc_base_addr_i[i*32 +: 32];
      cmd_i.operand_desc[i].nbeats    = cmd_operand_desc_nbeats_i[i*16 +: 16];
    end
  end

  // ----------------------------------------------------------
  // Reconstruct operand stream beat from flattened inputs
  // ----------------------------------------------------------
  operand_stream_beat_t operand_i;
  always_comb begin
    operand_i.kind     = operand_stream_kind_e'(operand_kind_i);
    operand_i.ctx_id   = operand_ctx_id_i;
    operand_i.op_id    = operand_op_id_i;
    operand_i.beat_idx = operand_beat_idx_i;
    operand_i.last     = operand_last_i;
    for (int i = 0; i < OPERAND_STREAM_WIDTH; i++)
      operand_i.data[i] = operand_data_flat_i[i*32 +: 32];
  end

  // ----------------------------------------------------------
  // Latched command
  // ----------------------------------------------------------
  cu_cmd_t cmd_r;

  // ----------------------------------------------------------
  // Wrapper FSM
  // ----------------------------------------------------------
  typedef enum logic [2:0] {
    W_IDLE,
    W_ISSUE_READS,
    W_STREAM_OPERANDS,
    W_EMIT_WB,
    W_DONE
  } state_e;

  state_e state_r;
  logic [3:0] desc_idx_r;
  logic       wbp_latched_r;

  // Registered read-request handshake (avoids combinational glitches when
  // state_r transitions in the same cycle that cmd_r is loaded).
  logic       rd_req_valid_r;
  cu_rd_req_t rd_req_r;

  // ----------------------------------------------------------
  // Compute core command construction
  // ----------------------------------------------------------
  gbp_core_req_t core_cmd;

  always_comb begin
    core_cmd.op            = cmd_r.op;
    core_cmd.factor_type   = cmd_r.factor_type;
    core_cmd.dim_i         = cmd_r.dim_i;
    core_cmd.dim_o         = cmd_r.dim_o;
    core_cmd.direction     = cmd_r.direction;
    core_cmd.ctx_id        = cmd_r.ctx_id;
    core_cmd.op_id         = cmd_r.op_id;
    core_cmd.node_id       = cmd_r.node_id;
    core_cmd.factor_id     = cmd_r.factor_id;
    core_cmd.dst_addr      = cmd_r.dst_addr;
    core_cmd.aux_addr      = cmd_r.aux_addr;
    core_cmd.damping       = cmd_r.damping;
    core_cmd.diag_lambda   = cmd_r.diag_lambda;
    core_cmd.pivot_eps     = cmd_r.pivot_eps;
    core_cmd.regularize_en = cmd_r.regularize_en;
    core_cmd.degree        = cmd_r.degree;
  end

  // ----------------------------------------------------------
  // Compute core instance
  // ----------------------------------------------------------
  logic          core_cmd_valid, core_cmd_ready;
  logic          core_operand_valid, core_operand_ready;
  operand_stream_beat_t core_operand;
  logic          core_rsp_valid, core_rsp_ready;
  gbp_core_rsp_t core_rsp;

  gbp_compute_core u_core (
      .clk_i          (clk_i),
      .reset_i        (reset_i),
      .cmd_valid_i    (core_cmd_valid),
      .cmd_ready_o    (core_cmd_ready),
      .cmd_i          (core_cmd),
      .operand_valid_i(core_operand_valid),
      .operand_ready_o(core_operand_ready),
      .operand_i      (core_operand),
      .rsp_valid_o    (core_rsp_valid),
      .rsp_ready_i    (core_rsp_ready),
      .rsp_o          (core_rsp)
  );

  // ----------------------------------------------------------
  // Writeback packer instance
  // ----------------------------------------------------------
  logic        wbp_valid, wbp_ready;
  logic [31:0] wbp_addr;
  logic [15:0] wbp_nwords;
  wb_kind_e    wbp_kind;
  logic [GBP_MAX_WB_SCALARS*32-1:0] wbp_payload_flat;
  logic        wbp_fail, wbp_regularized, wbp_nan_guard;

  // Flatten result structs for writeback_packer
  logic [GBP_MAX_VAR_DIM*32-1:0]    wbp_msg_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0] wbp_msg_L_flat;
  logic [GBP_MAX_VAR_DIM*32-1:0]    wbp_bel_eta_flat;
  logic [GBP_MAX_PACKED_VAR*32-1:0] wbp_bel_L_flat;
  logic [GBP_MAX_VAR_DIM*32-1:0]    wbp_bel_mu_flat;

  generate
    genvar g_i;
    for (g_i = 0; g_i < GBP_MAX_VAR_DIM; g_i++) begin : gen_msg_eta
      assign wbp_msg_eta_flat[g_i*32 +: 32] = core_rsp.msg_result.eta[g_i];
      assign wbp_bel_eta_flat[g_i*32 +: 32] = core_rsp.belief_result.eta[g_i];
      assign wbp_bel_mu_flat[g_i*32 +: 32]  = core_rsp.belief_result.mu[g_i];
    end
    for (g_i = 0; g_i < GBP_MAX_PACKED_VAR; g_i++) begin : gen_msg_L
      assign wbp_msg_L_flat[g_i*32 +: 32] = core_rsp.msg_result.L_packed[g_i];
      assign wbp_bel_L_flat[g_i*32 +: 32] = core_rsp.belief_result.L_packed[g_i];
    end
  endgenerate

  writeback_packer u_wbp (
      .clk_i              (clk_i),
      .reset_i            (reset_i),
      .valid_i            (wbp_valid),
      .ready_o            (wbp_ready),
      .rsp_op_i           (core_rsp.op),
      .rsp_dst_addr_i     (core_rsp.dst_addr),
      .rsp_node_id_i      (core_rsp.node_id),
      .rsp_factor_id_i    (core_rsp.factor_id),
      .msg_dim_i          (core_rsp.msg_result.dim),
      .msg_eta_flat_i     (wbp_msg_eta_flat),
      .msg_L_flat_i       (wbp_msg_L_flat),
      .msg_fail_i         (core_rsp.fail),
      .msg_regularized_i  (core_rsp.regularized),
      .msg_nan_guard_i    (core_rsp.nan_guard),
      .msg_min_pivot_i    (core_rsp.min_pivot),
      .bel_dim_i          (core_rsp.belief_result.dim),
      .bel_eta_flat_i     (wbp_bel_eta_flat),
      .bel_L_flat_i       (wbp_bel_L_flat),
      .bel_mu_flat_i      (wbp_bel_mu_flat),
      .bel_residual_i     (core_rsp.belief_result.residual),
      .bel_fail_i         (core_rsp.fail),
      .bel_regularized_i  (core_rsp.regularized),
      .bel_nan_guard_i    (core_rsp.nan_guard),
      .bel_degree_mismatch_i(core_rsp.degree_mismatch),
      .bel_min_pivot_i    (core_rsp.min_pivot),
      .wb_valid_o         (wb_valid_o),
      .wb_ready_i         (wb_ready_i),
      .wb_addr_o          (wbp_addr),
      .wb_nwords_o        (wbp_nwords),
      .wb_kind_o          (wbp_kind),
      .wb_payload_flat_o  (wbp_payload_flat),
      .wb_fail_o          (wbp_fail),
      .wb_regularized_o   (wbp_regularized),
      .wb_nan_guard_o     (wbp_nan_guard)
  );

  // ----------------------------------------------------------
  // Writeback record output (flattened)
  // ----------------------------------------------------------
  assign wb_addr_o        = wbp_addr;
  assign wb_nwords_o      = wbp_nwords;
  assign wb_kind_o        = wbp_kind;
  assign wb_payload_flat_o= wbp_payload_flat;
  assign wb_fail_o        = wbp_fail;
  assign wb_regularized_o = wbp_regularized;
  assign wb_nan_guard_o   = wbp_nan_guard;

  // ----------------------------------------------------------
  // Done output (flattened)
  // ----------------------------------------------------------
  assign done_node_id_o         = cmd_r.node_id;
  assign done_factor_id_o       = cmd_r.factor_id;
  assign done_op_o              = cmd_r.op;
  assign done_ctx_id_o          = cmd_r.ctx_id;
  assign done_success_o         = !core_rsp.fail && !core_rsp.stream_error;
  assign done_fail_o            = core_rsp.fail;
  assign done_regularized_o     = core_rsp.regularized;
  assign done_nan_guard_o       = core_rsp.nan_guard;
  assign done_degree_mismatch_o = core_rsp.degree_mismatch;
  assign done_stream_error_o    = core_rsp.stream_error;
  assign done_residual_o        = core_rsp.belief_result.residual;
  assign done_min_pivot_o       = core_rsp.min_pivot;

  // ----------------------------------------------------------
  // Operand / command routing
  // ----------------------------------------------------------
  assign core_cmd_valid       = (state_r == W_STREAM_OPERANDS);
  assign core_operand_valid   = operand_valid_i;
  assign core_operand         = operand_i;
  assign operand_ready_o      = core_operand_ready;
  assign core_rsp_ready       = (state_r == W_EMIT_WB) && wbp_latched_r;

  // ----------------------------------------------------------
  // Registered read request generation
  // ----------------------------------------------------------
  assign rd_req_valid_o = rd_req_valid_r;
  assign rd_req_op_id_o = rd_req_r.op_id;
  assign rd_req_op_o    = rd_req_r.op;
  assign rd_req_base_addr_o = rd_req_r.base_addr;
  assign rd_req_nbeats_o= rd_req_r.nbeats;
  assign rd_req_kind_o  = rd_req_r.kind;

  // ----------------------------------------------------------
  // Writeback packer handshake
  // ----------------------------------------------------------
  assign wbp_valid = (state_r == W_EMIT_WB) && !wbp_latched_r;

  // ----------------------------------------------------------
  // FSM
  // ----------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r       <= W_IDLE;
      desc_idx_r    <= '0;
      wbp_latched_r <= 1'b0;
      rd_req_valid_r<= 1'b0;
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
      cmd_r.degree        <= '0;
      cmd_r.damping       <= '0;
      cmd_r.diag_lambda   <= '0;
      cmd_r.pivot_eps     <= '0;
      cmd_r.regularize_en <= 1'b0;
      for (int i = 0; i < 8; i++) begin
        cmd_r.operand_desc[i].valid     <= 1'b0;
        cmd_r.operand_desc[i].kind      <= OST_MSG_STATIC;
        cmd_r.operand_desc[i].base_addr <= '0;
        cmd_r.operand_desc[i].nbeats    <= '0;
      end
      done_valid_o  <= 1'b0;
    end else begin
      case (state_r)
        W_IDLE: begin
          desc_idx_r    <= '0;
          wbp_latched_r <= 1'b0;
          done_valid_o  <= 1'b0;
          if (cmd_valid_i && cmd_ready_o) begin
            cmd_r   <= cmd_i;
            state_r <= W_ISSUE_READS;
          end
        end

        W_ISSUE_READS: begin
          if (rd_req_valid_r && rd_req_ready_i) begin
            // Handshake complete: clear request and advance descriptor index.
            rd_req_valid_r <= 1'b0;
            desc_idx_r     <= desc_idx_r + 4'd1;
          end else if (!rd_req_valid_r && (desc_idx_r < 8)) begin
            if (cmd_r.operand_desc[desc_idx_r].valid) begin
              // Load the next descriptor into the registered request.
              rd_req_valid_r              <= 1'b1;
              rd_req_r.op_id              <= cmd_r.op_id;
              rd_req_r.op                 <= cmd_r.op;
              rd_req_r.base_addr          <= cmd_r.operand_desc[desc_idx_r].base_addr;
              rd_req_r.nbeats             <= cmd_r.operand_desc[desc_idx_r].nbeats;
              rd_req_r.kind               <= cmd_r.operand_desc[desc_idx_r].kind;
            end else begin
              // Invalid descriptor slot, skip it.
              desc_idx_r <= desc_idx_r + 4'd1;
            end
          end else if (desc_idx_r >= 8) begin
            state_r <= W_STREAM_OPERANDS;
          end
        end

        W_STREAM_OPERANDS: begin
          if (core_rsp_valid) begin
            state_r <= W_EMIT_WB;
          end
        end

        W_EMIT_WB: begin
          if (wbp_valid && wbp_ready) begin
            wbp_latched_r <= 1'b1;
          end
          if (wb_valid_o && wb_ready_i) begin
            state_r <= W_DONE;
          end
        end

        W_DONE: begin
          done_valid_o <= 1'b1;
          if (done_ready_i) begin
            state_r <= W_IDLE;
          end
        end

        default: state_r <= W_IDLE;
      endcase
    end
  end

  assign cmd_ready_o = (state_r == W_IDLE);

endmodule
