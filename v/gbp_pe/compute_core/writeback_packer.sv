// writeback_packer.sv
// GBP Compute Core v0.6 — writeback packer

module writeback_packer
  import gbp_op_pkg::*;
(
  input  logic          clk_i,
  input  logic          reset_i,

  // Input
  input  logic          valid_i,
  output logic          ready_o,
  input  gbp_op_e       rsp_op_i,
  input  logic [31:0]   rsp_dst_addr_i,
  input  logic [31:0]   rsp_node_id_i,
  input  logic [31:0]   rsp_factor_id_i,

  // msg_result fields
  input  gbp_dim_e      msg_dim_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    msg_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] msg_L_flat_i,
  input  logic          msg_fail_i,
  input  logic          msg_regularized_i,
  input  logic          msg_nan_guard_i,
  input  logic [31:0]   msg_min_pivot_i,

  // belief_result fields
  input  gbp_dim_e      bel_dim_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    bel_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] bel_L_flat_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    bel_mu_flat_i,
  input  logic [31:0]   bel_residual_i,
  input  logic          bel_fail_i,
  input  logic          bel_regularized_i,
  input  logic          bel_nan_guard_i,
  input  logic          bel_degree_mismatch_i,
  input  logic [31:0]   bel_min_pivot_i,

  // Output
  output logic          wb_valid_o,
  input  logic          wb_ready_i,
  output logic [31:0]   wb_addr_o,
  output logic [15:0]   wb_nwords_o,
  output wb_kind_e      wb_kind_o,
  output logic [GBP_MAX_WB_SCALARS*32-1:0] wb_payload_flat_o,
  output logic          wb_fail_o,
  output logic          wb_regularized_o,
  output logic          wb_nan_guard_o
);

  logic valid_r;
  logic [31:0] addr_r;
  logic [15:0] nwords_r;
  wb_kind_e    kind_r;
  logic [31:0] payload_r [GBP_MAX_WB_SCALARS];
  logic        fail_r, reg_r, nan_r;

  // Dimension values
  logic [2:0] dim_val, bel_dim_val;
  assign dim_val = dim_to_val(msg_dim_i);
  assign bel_dim_val = dim_to_val(bel_dim_i);

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      valid_r <= 1'b0;
    end else if (valid_i && ready_o) begin
      valid_r <= 1'b1;
      addr_r  <= rsp_dst_addr_i;
      fail_r  <= (rsp_op_i == OP_BELIEF) ? bel_fail_i : msg_fail_i;
      reg_r   <= (rsp_op_i == OP_BELIEF) ? bel_regularized_i : msg_regularized_i;
      nan_r   <= (rsp_op_i == OP_BELIEF) ? bel_nan_guard_i : msg_nan_guard_i;

      // Zero payload
      for (int i = 0; i < GBP_MAX_WB_SCALARS; i++)
        payload_r[i] <= '0;

      case (rsp_op_i)
        OP_MSG_F2V: begin
          kind_r  <= WB_MSG;
          nwords_r <= 16'(E(dim_val));
          // Compact: eta[0..d-1] + L_packed[0..P(d)-1]
          for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
            if (i < dim_val)
              payload_r[i] <= msg_eta_flat_i[i*32 +: 32];
            else
              payload_r[i] <= '0;
          end
          for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
            if (i < P(dim_val))
              payload_r[dim_val + i] <= msg_L_flat_i[i*32 +: 32];
            else
              payload_r[dim_val + i] <= '0;
          end
        end
        OP_BELIEF: begin
          kind_r  <= WB_BELIEF;
          nwords_r <= 16'(E(bel_dim_val) + bel_dim_val + 1);
          for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
            if (i < bel_dim_val)
              payload_r[i] <= bel_eta_flat_i[i*32 +: 32];
            else
              payload_r[i] <= '0;
          end
          for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
            if (i < P(bel_dim_val))
              payload_r[bel_dim_val + i] <= bel_L_flat_i[i*32 +: 32];
            else
              payload_r[bel_dim_val + i] <= '0;
          end
          for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
            if (i < bel_dim_val)
              payload_r[E(bel_dim_val) + i] <= bel_mu_flat_i[i*32 +: 32];
            else
              payload_r[E(bel_dim_val) + i] <= '0;
          end
          payload_r[E(bel_dim_val) + bel_dim_val] <= bel_residual_i;
        end
        default: begin
          kind_r <= WB_MSG;
          nwords_r <= '0;
        end
      endcase

    end else if (valid_r && wb_ready_i) begin
      valid_r <= 1'b0;
    end
  end

  assign wb_valid_o      = valid_r;
  assign wb_addr_o       = addr_r;
  assign wb_nwords_o     = nwords_r;
  assign wb_kind_o       = kind_r;
  assign wb_fail_o       = fail_r;
  assign wb_regularized_o = reg_r;
  assign wb_nan_guard_o  = nan_r;
  assign ready_o         = ~valid_r || wb_ready_i;

  always_comb begin
    for (int i = 0; i < GBP_MAX_WB_SCALARS; i++)
      wb_payload_flat_o[i*32 +: 32] = payload_r[i];
  end

endmodule
