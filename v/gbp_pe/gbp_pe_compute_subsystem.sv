// gbp_pe_compute_subsystem.sv
// Encapsulates the descriptor-driven compute datapath using the new
// gbp_compute_core via compute_unit_wrapper.
//
// V0 scope:
//   - One 64-bit SPM read port (operand streams)
//   - One 64-bit SPM write port (writeback)
//   - Legacy scalar command interface is converted to cu_cmd_t internally.
//   - OP_BELIEF with degree == 0 (prior-only) is the first supported path.

module gbp_pe_compute_subsystem
  import gbp_pkg::*;
  import gbp_op_pkg::*;
#(
    parameter int NODE_ID_W     = gbp_pkg::NODE_ID_W,
    parameter int SPM_ADDR_W    = gbp_pkg::SPM_ADDR_W,
    parameter int STATE_WORDS_W = gbp_pkg::STATE_WORDS_W,
    parameter int ADJ_COUNT_W   = gbp_pkg::ADJ_COUNT_W,
    parameter int DOF_W         = gbp_pkg::DOF_W,
    parameter int BEAT_BITS     = gbp_pkg::BEAT_BITS,
    parameter int FP32_W        = gbp_pkg::FP32_W
) (
    input logic clk,
    input logic rst_n,

    // Command from control subsystem
    input  logic                                cmd_valid_i,
    output logic                                cmd_ready_o,
    input  logic [    NODE_ID_W-1:0]            cmd_node_id_i,
    input  logic                                cmd_is_factor_i,
    input  logic [        DOF_W-1:0]            cmd_dof_i,
    input  logic [  ADJ_COUNT_W-1:0]            cmd_adj_count_i,
    input  logic [STATE_WORDS_W-1:0]            cmd_state_words_i,
    input  logic [   SPM_ADDR_W-1:0]            cmd_state_base_i,
    input  logic [MAX_ADJ_COUNT-1:0][DOF_W-1:0] cmd_neighbor_dofs_i,

    // Neighbor state from accumulator (legacy port; unused in V0 new-core path)
    input  logic              ns_valid_i,
    output logic              ns_ready_o,
    input  logic [FP32_W-1:0] ns_data_i,
    input  logic              ns_last_i,

    // SPM read port 0: operand streams (to memory subsystem)
    output logic                  spm_rd0_valid_o,
    input  logic                  spm_rd0_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd0_addr_o,
    input  logic [ BEAT_BITS-1:0] spm_rd0_data_i,

    // SPM read port 1: unused in V0
    output logic                  spm_rd1_valid_o,
    input  logic                  spm_rd1_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd1_addr_o,
    input  logic [ BEAT_BITS-1:0] spm_rd1_data_i,

    // SPM write port (to memory subsystem)
    output logic                   spm_wr_valid_o,
    input  logic                   spm_wr_ready_i,
    output logic [ SPM_ADDR_W-1:0] spm_wr_addr_o,
    output logic [  BEAT_BITS-1:0] spm_wr_data_o,
    output logic [BEAT_BITS/8-1:0] spm_wr_wstrb_o,

    // Completion
    output logic                 done_valid_o,
    output logic [NODE_ID_W-1:0] done_node_id_o,
    output logic                 done_is_factor_o,

    // Batch completion (to fetch subsystem / response collector)
    output logic batch_done_o
);

  // ------------------------------------------------------------------
  // Command conversion: legacy scalar fields -> cu_cmd_t
  // ------------------------------------------------------------------
  cu_cmd_t cu_cmd;
  logic [2:0] cu_cmd_d_i;
  logic [15:0] cu_cmd_e_i;
  logic [15:0] cu_cmd_nbeats_per_stream;

  always_comb begin
    cu_cmd.op = cmd_is_factor_i ? OP_MSG_F2V : OP_BELIEF;

    case (cmd_dof_i)
      4'd1: cu_cmd.factor_type = FACTOR_SCALAR;
      4'd3: cu_cmd.factor_type = FACTOR_SE2;
      4'd6: cu_cmd.factor_type = FACTOR_SE3;
      default: cu_cmd.factor_type = FACTOR_BA;
    endcase
    if (!cmd_is_factor_i) cu_cmd.factor_type = FACTOR_SCALAR;

    case (cmd_dof_i)
      4'd3: begin
        cu_cmd.dim_i = DIM_3;
        cu_cmd.dim_o = DIM_3;
      end
      4'd6: begin
        cu_cmd.dim_i = DIM_6;
        cu_cmd.dim_o = DIM_6;
      end
      default: begin
        cu_cmd.dim_i = DIM_1;
        cu_cmd.dim_o = DIM_1;
      end
    endcase

    cu_cmd.direction         = 1'b0;
    cu_cmd.ctx_id            = 1'b0;
    cu_cmd.op_id             = {{32 - NODE_ID_W{1'b0}}, cmd_node_id_i};
    cu_cmd.node_id           = {{32 - NODE_ID_W{1'b0}}, cmd_node_id_i};
    cu_cmd.factor_id         = '0;
    cu_cmd.dst_addr          = {32'(SPM_ADDR_W'(cmd_node_id_i) << 4)};
    cu_cmd.aux_addr          = '0;
    cu_cmd.damping           = '0;
    cu_cmd.diag_lambda       = '0;
    cu_cmd.pivot_eps         = 32'h3DCCCCCD;  // ~1e-12
    cu_cmd.regularize_en     = 1'b0;
    cu_cmd.degree            = 16'(cmd_adj_count_i);

    // Decode input dimension and message scalar count E(d) = d + P(d).
    cu_cmd_d_i               = (cmd_dof_i == 4'd6) ? 3'd6 : (cmd_dof_i == 4'd3) ? 3'd3 : 3'd1;
    cu_cmd_e_i               = 16'(E(int'(cu_cmd_d_i)));
    cu_cmd_nbeats_per_stream = (cu_cmd_e_i + 16'd15) / 16'd16;

    if (cmd_is_factor_i) begin
      // Factor (OP_MSG_F2V): four cavity operand streams, each E(d) scalars.
      cu_cmd.operand_desc[0].valid = 1'b1;
      cu_cmd.operand_desc[0].kind = OST_MSG_STATIC;
      cu_cmd.operand_desc[0].base_addr = {{32 - SPM_ADDR_W{1'b0}}, cmd_state_base_i};
      cu_cmd.operand_desc[0].nbeats = cu_cmd_nbeats_per_stream;

      cu_cmd.operand_desc[1].valid = 1'b1;
      cu_cmd.operand_desc[1].kind = OST_CAV_FACTOR_O;
      cu_cmd.operand_desc[1].base_addr = {
        {32 - SPM_ADDR_W{1'b0}}, SPM_ADDR_W'(cmd_state_base_i + cu_cmd_e_i)
      };
      cu_cmd.operand_desc[1].nbeats = cu_cmd_nbeats_per_stream;

      cu_cmd.operand_desc[2].valid = 1'b1;
      cu_cmd.operand_desc[2].kind = OST_CAV_BELIEF_O;
      cu_cmd.operand_desc[2].base_addr = {
        {32 - SPM_ADDR_W{1'b0}}, SPM_ADDR_W'(cmd_state_base_i + 2 * cu_cmd_e_i)
      };
      cu_cmd.operand_desc[2].nbeats = cu_cmd_nbeats_per_stream;

      cu_cmd.operand_desc[3].valid = 1'b1;
      cu_cmd.operand_desc[3].kind = OST_CAV_OLD_TO_O;
      cu_cmd.operand_desc[3].base_addr = {
        {32 - SPM_ADDR_W{1'b0}}, SPM_ADDR_W'(cmd_state_base_i + 3 * cu_cmd_e_i)
      };
      cu_cmd.operand_desc[3].nbeats = cu_cmd_nbeats_per_stream;

      for (int i = 4; i < 8; i++) begin
        cu_cmd.operand_desc[i].valid     = 1'b0;
        cu_cmd.operand_desc[i].kind      = OST_MSG_STATIC;
        cu_cmd.operand_desc[i].base_addr = '0;
        cu_cmd.operand_desc[i].nbeats    = '0;
      end
    end else begin
      // Belief (OP_BELIEF): prior stream + message stream (if degree >= 1)
      cu_cmd.operand_desc[0].valid     = 1'b1;
      cu_cmd.operand_desc[0].kind      = OST_BELIEF_PRIOR;
      cu_cmd.operand_desc[0].base_addr = {{32 - SPM_ADDR_W{1'b0}}, cmd_state_base_i};
      cu_cmd.operand_desc[0].nbeats    = (16'(cmd_state_words_i) + 16'd15) / 16'd16;

      cu_cmd.operand_desc[1].valid     = (cmd_adj_count_i != '0);
      cu_cmd.operand_desc[1].kind      = OST_BELIEF_MSG;
      cu_cmd.operand_desc[1].base_addr = {
        {32 - SPM_ADDR_W{1'b0}}, SPM_ADDR_W'(cmd_state_base_i + cu_cmd_e_i)
      };
      cu_cmd.operand_desc[1].nbeats    = cu_cmd_nbeats_per_stream;

      for (int i = 2; i < 8; i++) begin
        cu_cmd.operand_desc[i].valid     = 1'b0;
        cu_cmd.operand_desc[i].kind      = OST_MSG_STATIC;
        cu_cmd.operand_desc[i].base_addr = '0;
        cu_cmd.operand_desc[i].nbeats    = '0;
      end
    end
  end

  // ------------------------------------------------------------------
  // Compute unit wrapper + adapters
  // ------------------------------------------------------------------
  logic wrapper_cmd_valid, wrapper_cmd_ready;

  logic wrapper_rd_req_valid, wrapper_rd_req_ready;
  logic [31:0] wrapper_rd_req_op_id;
  logic [ 3:0] wrapper_rd_req_op;
  logic [31:0] wrapper_rd_req_base_addr;
  logic [15:0] wrapper_rd_req_nbeats;
  logic [ 3:0] wrapper_rd_req_kind;

  logic dispatcher_desc_valid, dispatcher_desc_ready;
  logic                 [          31:0] dispatcher_desc_op_id;
  operand_stream_kind_e                  dispatcher_desc_kind;
  logic                 [SPM_ADDR_W-1:0] dispatcher_desc_base_addr;
  logic                 [          15:0] dispatcher_desc_nbeats;

  logic wrapper_wb_valid, wrapper_wb_ready;
  logic              [                       31:0] wrapper_wb_addr;
  logic              [                       15:0] wrapper_wb_nwords;
  logic              [                        3:0] wrapper_wb_kind;
  logic              [  GBP_MAX_WB_SCALARS*32-1:0] wrapper_wb_payload_flat;
  logic                                            wrapper_wb_fail;
  logic                                            wrapper_wb_regularized;
  logic                                            wrapper_wb_nan_guard;
  writeback_record_t                               wrapper_wb;

  logic                                            asm_operand_ready;
  logic              [OPERAND_STREAM_WIDTH*32-1:0] asm_operand_data_flat;

  logic wrapper_done_valid, wrapper_done_ready;
  logic [31:0] wrapper_done_node_id;
  logic [ 3:0] wrapper_done_op;

  // Reconstruct writeback_record_t for wb_to_wse_adapter
  always_comb begin
    wrapper_wb.addr        = wrapper_wb_addr;
    wrapper_wb.nwords      = wrapper_wb_nwords;
    wrapper_wb.kind        = wb_kind_e'(wrapper_wb_kind);
    wrapper_wb.fail        = wrapper_wb_fail;
    wrapper_wb.regularized = wrapper_wb_regularized;
    wrapper_wb.nan_guard   = wrapper_wb_nan_guard;
    for (int i = 0; i < GBP_MAX_WB_SCALARS; i++)
    wrapper_wb.payload[i] = wrapper_wb_payload_flat[i*32+:32];
  end

  // Command: pass through; wrapper latches on cmd_ready
  assign wrapper_cmd_valid = cmd_valid_i;
  assign cmd_ready_o       = wrapper_cmd_ready;

  // Descriptor FIFO: wrapper issues all operand descriptors up-front; the
  // single operand_stream_assembler consumes them one at a time.
  operand_stream_dispatcher u_op_dispatcher (
      .clk_i           (clk),
      .rst_n_i         (rst_n),
      .req_valid_i     (wrapper_rd_req_valid),
      .req_ready_o     (wrapper_rd_req_ready),
      .req_op_id_i     (wrapper_rd_req_op_id),
      .req_kind_i      (operand_stream_kind_e'(wrapper_rd_req_kind)),
      .req_base_addr_i (SPM_ADDR_W'(wrapper_rd_req_base_addr)),
      .req_nbeats_i    (wrapper_rd_req_nbeats),
      .desc_valid_o    (dispatcher_desc_valid),
      .desc_ready_i    (dispatcher_desc_ready),
      .desc_op_id_o    (dispatcher_desc_op_id),
      .desc_kind_o     (dispatcher_desc_kind),
      .desc_base_addr_o(dispatcher_desc_base_addr),
      .desc_nbeats_o   (dispatcher_desc_nbeats)
  );

  // Read request -> operand stream assembler
  operand_stream_assembler u_op_asm (
      .clk_i           (clk),
      .rst_n_i         (rst_n),
      .desc_valid_i    (dispatcher_desc_valid),
      .desc_ready_o    (dispatcher_desc_ready),
      .desc_kind_i     (dispatcher_desc_kind),
      .desc_op_id_i    (dispatcher_desc_op_id),
      .desc_base_addr_i(dispatcher_desc_base_addr),
      .desc_nbeats_i   (dispatcher_desc_nbeats),
      .operand_valid_o (),
      .operand_ready_i (asm_operand_ready),
      .operand_o       (),
      .spm_rd_valid_o  (spm_rd0_valid_o),
      .spm_rd_ready_i  (spm_rd0_ready_i),
      .spm_rd_addr_o   (spm_rd0_addr_o),
      .spm_rd_data_i   (spm_rd0_data_i)
  );

  // Flatten operand_stream_beat_t from assembler
  always_comb begin
    asm_operand_data_flat = '0;
    for (int i = 0; i < OPERAND_STREAM_WIDTH; i++)
    asm_operand_data_flat[i*32+:32] = u_op_asm.operand_o.data[i];
  end

  compute_unit_wrapper u_compute_unit_wrapper (
      .clk_i(clk),
      .reset_i(~rst_n),
      .cmd_valid_i(wrapper_cmd_valid),
      .cmd_ready_o(wrapper_cmd_ready),
      .cmd_op_i(cu_cmd.op),
      .cmd_factor_type_i(cu_cmd.factor_type),
      .cmd_dim_i_i(cu_cmd.dim_i),
      .cmd_dim_o_i(cu_cmd.dim_o),
      .cmd_direction_i(cu_cmd.direction),
      .cmd_ctx_id_i(cu_cmd.ctx_id),
      .cmd_op_id_i(cu_cmd.op_id),
      .cmd_node_id_i(cu_cmd.node_id),
      .cmd_factor_id_i(cu_cmd.factor_id),
      .cmd_dst_addr_i(cu_cmd.dst_addr),
      .cmd_aux_addr_i(cu_cmd.aux_addr),
      .cmd_degree_i(cu_cmd.degree),
      .cmd_damping_i(cu_cmd.damping),
      .cmd_diag_lambda_i(cu_cmd.diag_lambda),
      .cmd_pivot_eps_i(cu_cmd.pivot_eps),
      .cmd_regularize_en_i(cu_cmd.regularize_en),
      .cmd_operand_desc_valid_i({
        cu_cmd.operand_desc[7].valid,
        cu_cmd.operand_desc[6].valid,
        cu_cmd.operand_desc[5].valid,
        cu_cmd.operand_desc[4].valid,
        cu_cmd.operand_desc[3].valid,
        cu_cmd.operand_desc[2].valid,
        cu_cmd.operand_desc[1].valid,
        cu_cmd.operand_desc[0].valid
      }),
      .cmd_operand_desc_kind_i({
        cu_cmd.operand_desc[7].kind,
        cu_cmd.operand_desc[6].kind,
        cu_cmd.operand_desc[5].kind,
        cu_cmd.operand_desc[4].kind,
        cu_cmd.operand_desc[3].kind,
        cu_cmd.operand_desc[2].kind,
        cu_cmd.operand_desc[1].kind,
        cu_cmd.operand_desc[0].kind
      }),
      .cmd_operand_desc_base_addr_i({
        cu_cmd.operand_desc[7].base_addr,
        cu_cmd.operand_desc[6].base_addr,
        cu_cmd.operand_desc[5].base_addr,
        cu_cmd.operand_desc[4].base_addr,
        cu_cmd.operand_desc[3].base_addr,
        cu_cmd.operand_desc[2].base_addr,
        cu_cmd.operand_desc[1].base_addr,
        cu_cmd.operand_desc[0].base_addr
      }),
      .cmd_operand_desc_nbeats_i({
        cu_cmd.operand_desc[7].nbeats,
        cu_cmd.operand_desc[6].nbeats,
        cu_cmd.operand_desc[5].nbeats,
        cu_cmd.operand_desc[4].nbeats,
        cu_cmd.operand_desc[3].nbeats,
        cu_cmd.operand_desc[2].nbeats,
        cu_cmd.operand_desc[1].nbeats,
        cu_cmd.operand_desc[0].nbeats
      }),

      .rd_req_valid_o    (wrapper_rd_req_valid),
      .rd_req_ready_i    (wrapper_rd_req_ready),
      .rd_req_op_id_o    (wrapper_rd_req_op_id),
      .rd_req_op_o       (wrapper_rd_req_op),
      .rd_req_base_addr_o(wrapper_rd_req_base_addr),
      .rd_req_nbeats_o   (wrapper_rd_req_nbeats),
      .rd_req_kind_o     (wrapper_rd_req_kind),

      .operand_valid_i    (u_op_asm.operand_valid_o),
      .operand_ready_o    (asm_operand_ready),
      .operand_kind_i     (u_op_asm.operand_o.kind),
      .operand_ctx_id_i   (u_op_asm.operand_o.ctx_id),
      .operand_op_id_i    (u_op_asm.operand_o.op_id),
      .operand_beat_idx_i (u_op_asm.operand_o.beat_idx),
      .operand_last_i     (u_op_asm.operand_o.last),
      .operand_data_flat_i(asm_operand_data_flat),

      .wb_valid_o (wrapper_wb_valid),
      .wb_ready_i (wrapper_wb_ready),
      .wb_addr_o  (wrapper_wb_addr),
      .wb_nwords_o (wrapper_wb_nwords),
      .wb_kind_o   (wrapper_wb_kind),
      .wb_payload_flat_o (wrapper_wb_payload_flat),
      .wb_fail_o   (wrapper_wb_fail),
      .wb_regularized_o (wrapper_wb_regularized),
      .wb_nan_guard_o   (wrapper_wb_nan_guard),

      .done_valid_o          (wrapper_done_valid),
      .done_ready_i          (wrapper_done_ready),
      .done_node_id_o        (wrapper_done_node_id),
      .done_factor_id_o      (),
      .done_op_o             (wrapper_done_op),
      .done_ctx_id_o         (),
      .done_success_o        (),
      .done_fail_o           (),
      .done_regularized_o    (),
      .done_nan_guard_o      (),
      .done_degree_mismatch_o(),
      .done_stream_error_o   (),
      .done_residual_o       (),
      .done_min_pivot_o      ()
  );

  // Writeback -> write_stream_engine adapter
  logic wse_desc_valid, wse_desc_ready;
  logic [SPM_ADDR_W-1:0] wse_desc_base_addr;
  logic [          15:0] wse_desc_word_count;
  logic wse_word_valid, wse_word_ready;
  logic [FP32_W-1:0] wse_word_data;

  wb_to_wse_adapter u_wb_adapter (
      .clk_i            (clk),
      .rst_n_i          (rst_n),
      .wb_valid_i       (wrapper_wb_valid),
      .wb_ready_o       (wrapper_wb_ready),
      .wb_i             (wrapper_wb),
      .desc_valid_o     (wse_desc_valid),
      .desc_ready_i     (wse_desc_ready),
      .desc_base_addr_o (wse_desc_base_addr),
      .desc_word_count_o(wse_desc_word_count),
      .word_valid_o     (wse_word_valid),
      .word_ready_i     (wse_word_ready),
      .word_data_o      (wse_word_data)
  );

  write_stream_engine u_wse (
      .clk_i            (clk),
      .rst_n_i          (rst_n),
      .desc_valid_i     (wse_desc_valid),
      .desc_ready_o     (wse_desc_ready),
      .desc_base_addr_i (wse_desc_base_addr),
      .desc_word_count_i(wse_desc_word_count),
      .word_valid_i     (wse_word_valid),
      .word_ready_o     (wse_word_ready),
      .word_data_i      (wse_word_data),
      .spm_wr_valid_o,
      .spm_wr_ready_i,
      .spm_wr_addr_o,
      .spm_wr_data_o,
      .spm_wr_wstrb_o
  );

  // Done -> legacy completion
  assign wrapper_done_ready = 1'b1;
  assign done_valid_o       = wrapper_done_valid;
  assign done_node_id_o     = NODE_ID_W'(wrapper_done_node_id);
  assign done_is_factor_o   = (gbp_op_e'(wrapper_done_op) == OP_MSG_F2V);
  assign batch_done_o       = wrapper_done_valid;

  // Unused legacy / V0 ports
  assign spm_rd1_valid_o    = 1'b0;
  assign spm_rd1_addr_o     = '0;
  assign ns_ready_o         = 1'b0;

endmodule
