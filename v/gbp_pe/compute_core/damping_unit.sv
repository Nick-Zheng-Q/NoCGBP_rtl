// damping_unit.sv
// GBP Compute Core v0.6 — message damping unit
// Uses bsg_fpu_mul and bsg_fpu_add_sub for FP32 arithmetic.
// msg_new = msg_raw + damping * (msg_old - msg_raw)

module damping_unit
  import gbp_op_pkg::*;
(
  input  logic     clk_i,
  input  logic     reset_i,

  input  logic     valid_i,
  output logic     ready_o,
  input  gbp_dim_e dim_i_i,
  input  logic [31:0] damping_i,

  input  logic [GBP_MAX_VAR_DIM*32-1:0]    msg_eta_raw_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] msg_L_raw_flat_i,
  input  logic [GBP_MAX_VAR_DIM*32-1:0]    old_msg_eta_flat_i,
  input  logic [GBP_MAX_PACKED_VAR*32-1:0] old_msg_L_flat_i,

  output logic     valid_o,
  input  logic     ready_i,
  output logic [GBP_MAX_VAR_DIM*32-1:0]    msg_eta_flat_o,
  output logic [GBP_MAX_PACKED_VAR*32-1:0] msg_L_flat_o
);

  localparam int FP_E = 8;
  localparam int FP_M = 23;

  logic [2:0] d_i;
  logic [4:0] p_i, total_elems;
  assign d_i = dim_to_val(dim_i_i);
  assign p_i = P(d_i);
  assign total_elems = d_i + p_i;

  // Unpack inputs
  logic [31:0] raw_vals [0:GBP_MAX_VAR_DIM + GBP_MAX_PACKED_VAR - 1];
  logic [31:0] old_vals [0:GBP_MAX_VAR_DIM + GBP_MAX_PACKED_VAR - 1];

  always_comb begin
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
      raw_vals[i] = msg_eta_raw_flat_i[i*32 +: 32];
      old_vals[i] = old_msg_eta_flat_i[i*32 +: 32];
    end
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
      raw_vals[GBP_MAX_VAR_DIM + i] = msg_L_raw_flat_i[i*32 +: 32];
      old_vals[GBP_MAX_VAR_DIM + i] = old_msg_L_flat_i[i*32 +: 32];
    end
  end

  // Output
  logic        valid_r;
  logic [31:0] result_r [0:GBP_MAX_VAR_DIM + GBP_MAX_PACKED_VAR - 1];

  // FPU instances
  // SUB: diff = old - raw
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

  // MUL: scaled = damping * diff
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

  // ADD: result = raw + scaled
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
  // Pipeline control
  // ----------------------------------------------------------
  // The 3 FP units form a pipeline:
  //   SUB → MUL → ADD
  // Each has 3-cycle latency.
  // We process one element at a time through this pipeline.

  typedef enum logic [2:0] {
    ST_IDLE,
    ST_ISSUE_SUB,
    WAIT_SUB,
    ISSUE_MUL,
    WAIT_MUL,
    ISSUE_ADD,
    WAIT_ADD,
    ST_DONE
  } state_e;

  state_e state_r;
  logic [4:0] idx_r;

  // Index mapping: idx_r -> raw_vals/old_vals position
  logic [4:0] val_idx;
  always_comb begin
    if (idx_r < d_i)
      val_idx = idx_r;                    // eta part
    else
      val_idx = GBP_MAX_VAR_DIM + (idx_r - d_i);  // L_packed part
  end

  // Hold raw value for add stage
  logic [31:0] raw_hold_r;

  // FPU input defaults
  always_comb begin
    sub_v_i = 1'b0; sub_a_i = '0; sub_b_i = '0; sub_yumi = 1'b0;
    mul_v_i = 1'b0; mul_a_i = '0; mul_b_i = '0; mul_yumi = 1'b0;
    add_v_i = 1'b0; add_a_i = '0; add_b_i = '0; add_yumi = 1'b0;

    case (state_r)
      ST_ISSUE_SUB: begin
        sub_v_i = 1'b1;
        sub_a_i = old_vals[val_idx];
        sub_b_i = raw_vals[val_idx];
      end
      ISSUE_MUL: begin
        mul_v_i = 1'b1;
        mul_a_i = damping_i;
        mul_b_i = sub_z_o;
        sub_yumi = 1'b1;
      end
      ISSUE_ADD: begin
        add_v_i = 1'b1;
        add_a_i = raw_hold_r;
        add_b_i = mul_z_o;
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
      idx_r   <= '0;
      valid_r <= 1'b0;
    end else begin
      case (state_r)
        ST_IDLE: begin
          if (valid_i && ready_o) begin
            state_r <= ST_ISSUE_SUB;
            idx_r   <= '0;
            valid_r <= 1'b0;
          end
        end

        ST_ISSUE_SUB: begin
          if (sub_ready) begin
            state_r <= WAIT_SUB;
            raw_hold_r <= raw_vals[val_idx];
          end
        end

        WAIT_SUB: begin
          // Wait for subtract output (3-cycle pipeline)
          if (sub_v_o) begin
            state_r <= ISSUE_MUL;
          end
        end

        ISSUE_MUL: begin
          if (mul_ready) begin
            state_r <= WAIT_MUL;
          end
        end

        WAIT_MUL: begin
          if (mul_v_o) begin
            state_r <= ISSUE_ADD;
          end
        end

        ISSUE_ADD: begin
          if (add_ready) begin
            state_r <= WAIT_ADD;
          end
        end

        WAIT_ADD: begin
          if (add_v_o) begin
            result_r[val_idx] <= add_z_o;

            if (idx_r + 1 >= total_elems) begin
              state_r <= ST_DONE;
              valid_r <= 1'b1;
            end else begin
              idx_r   <= idx_r + 1;
              state_r <= ST_ISSUE_SUB;
            end
          end
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
  assign valid_o = valid_r;
  assign ready_o = (state_r == ST_IDLE);

  always_comb begin
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++)
      msg_eta_flat_o[i*32 +: 32] = result_r[i];
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++)
      msg_L_flat_o[i*32 +: 32] = result_r[GBP_MAX_VAR_DIM + i];
  end

endmodule
