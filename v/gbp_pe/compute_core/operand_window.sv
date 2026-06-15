// operand_window.sv
// GBP Compute Core v0.6 — operand window
// Buffers the static operand window (target-side + cross + old target msg).
// Source: docs/gbp_pe/08_NEW_COMPUTE_UNIT.md §10

module operand_window
  import gbp_op_pkg::*;
(
    input logic clk_i,
    input logic rst_n_i,

    // Configuration (must be stable before first load beat)
    input gbp_dim_e dim_i_i,
    input gbp_dim_e dim_o_i,
    input logic     start_i,

    // Load from operand stream
    input  logic                                               load_valid_i,
    output logic                                               load_ready_o,
    input  operand_stream_kind_e                               load_kind_i,
    input  logic                 [OPERAND_STREAM_WIDTH*32-1:0] load_data_flat_i,
    input  logic                                               load_last_i,

    // Downstream read (combinational)
    output gbp_dim_e                                          msg_dim_i_o,
    output gbp_dim_e                                          msg_dim_o_o,
    output logic     [                GBP_MAX_VAR_DIM*32-1:0] msg_eta_flat_o,
    output logic     [             GBP_MAX_PACKED_VAR*32-1:0] msg_L_ii_flat_o,
    output logic     [GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM*32-1:0] msg_L_io_flat_o,
    output logic     [                GBP_MAX_VAR_DIM*32-1:0] msg_old_eta_flat_o,
    output logic     [             GBP_MAX_PACKED_VAR*32-1:0] msg_old_L_flat_o,
    output logic                                              msg_static_valid_o,

    // Clear on operation complete
    input logic clear_i
);

  // ----------------------------------------------------------
  // Storage registers (max-size, only first d_i/P(d_i) valid)
  // ----------------------------------------------------------
  logic [31:0] eta_r     [GBP_MAX_VAR_DIM];
  logic [31:0] L_ii_r    [GBP_MAX_PACKED_VAR];
  logic [31:0] L_io_r    [GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM];
  logic [31:0] old_eta_r [GBP_MAX_VAR_DIM];
  logic [31:0] old_L_r   [GBP_MAX_PACKED_VAR];

  // ----------------------------------------------------------
  // Dimension decode
  // ----------------------------------------------------------
  logic [2:0] d_i, d_o;
  assign d_i = dim_to_val(dim_i_i);
  assign d_o = dim_to_val(dim_o_i);

  // Pack offsets
  logic [4:0] offset1;  // E(d_i)
  logic [5:0] offset2;  // E(d_i) + d_i*d_o
  logic [6:0] offset3;  // 2*E(d_i) + d_i*d_o
  assign offset1 = E(d_i);
  assign offset2 = E(d_i) + d_i * d_o;
  assign offset3 = 2 * E(d_i) + d_i * d_o;

  // ----------------------------------------------------------
  // Scalar position counter (tracks position across beats)
  // ----------------------------------------------------------
  logic [6:0] scalar_idx;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i)
      scalar_idx <= '0;
    else if (start_i)
      scalar_idx <= '0;
    else if (load_valid_i && load_ready_o)
      scalar_idx <= scalar_idx + OPERAND_STREAM_WIDTH;
  end

  // ----------------------------------------------------------
  // Combinational index computation
  // ----------------------------------------------------------
  logic [6:0] abs_idx [OPERAND_STREAM_WIDTH];
  logic [6:0] rel1    [OPERAND_STREAM_WIDTH];
  logic [6:0] rel2    [OPERAND_STREAM_WIDTH];

  always_comb begin
    for (int i = 0; i < OPERAND_STREAM_WIDTH; i++) begin
      abs_idx[i] = scalar_idx[6:0] + 7'(i);
      rel1[i]    = abs_idx[i] - {2'b0, offset1[4:0]};
      rel2[i]    = abs_idx[i] - {1'b0, offset2[5:0]};
    end
  end

  // ----------------------------------------------------------
  // Load FSM
  //   ST_IDLE    → ST_LOADING on start_i
  //   ST_LOADING → ST_READY  on load_last_i
  //   ST_READY   → ST_IDLE   on clear_i
  // ----------------------------------------------------------
  localparam ST_IDLE    = 2'd0;
  localparam ST_LOADING = 2'd1;
  localparam ST_READY   = 2'd2;

  logic [1:0] state;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i)
      state <= ST_IDLE;
    else if (start_i)
      state <= ST_LOADING;
    else if (clear_i && state == ST_READY)
      state <= ST_IDLE;
    else if (load_valid_i && load_ready_o && load_last_i)
      state <= ST_READY;
  end

  assign load_ready_o = (state == ST_LOADING);

  // ----------------------------------------------------------
  // Storage write (non-blocking assignments only)
  //   For each of the 16 scalars in the beat:
  //     abs_idx = scalar_idx + i
  //     Region 0 [0, offset1):           eta / L_ii
  //     Region 1 [offset1, offset2):     L_io
  //     Region 2 [offset2, offset3):     old_eta / old_L
  // ----------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
        eta_r[i]     <= '0;
        old_eta_r[i] <= '0;
      end
      for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
        L_ii_r[i]  <= '0;
        old_L_r[i] <= '0;
      end
      for (int i = 0; i < GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM; i++)
        L_io_r[i] <= '0;
    end else if (load_valid_i && load_ready_o && load_kind_i == OST_MSG_STATIC) begin
      for (int i = 0; i < OPERAND_STREAM_WIDTH; i++) begin
        if (abs_idx[i] < offset1) begin
          if (abs_idx[i] < d_i)
            eta_r[abs_idx[i][2:0]] <= load_data_flat_i[i*32 +: 32];
          else
            L_ii_r[abs_idx[i][4:0] - d_i] <= load_data_flat_i[i*32 +: 32];
        end else if (abs_idx[i] < offset2) begin
          // L_io stream is row-major d_i x d_o; store into max-size dense buffer
          // with GBP_MAX_VAR_DIM row stride.
          L_io_r[(rel1[i] / d_o) * GBP_MAX_VAR_DIM + (rel1[i] % d_o)] <= load_data_flat_i[i*32 +: 32];
        end else if (abs_idx[i] < offset3) begin
          if (rel2[i] < d_i)
            old_eta_r[rel2[i][2:0]] <= load_data_flat_i[i*32 +: 32];
          else
            old_L_r[rel2[i][4:0] - d_i] <= load_data_flat_i[i*32 +: 32];
        end
      end
    end
  end

  // ----------------------------------------------------------
  // Output: combinational read from storage
  // ----------------------------------------------------------
  assign msg_static_valid_o = (state == ST_READY);
  assign msg_dim_i_o = dim_i_i;
  assign msg_dim_o_o = dim_o_i;

  always_comb begin
    for (int i = 0; i < GBP_MAX_VAR_DIM; i++) begin
      msg_eta_flat_o[i*32 +: 32]     = eta_r[i];
      msg_old_eta_flat_o[i*32 +: 32] = old_eta_r[i];
    end
    for (int i = 0; i < GBP_MAX_PACKED_VAR; i++) begin
      msg_L_ii_flat_o[i*32 +: 32]  = L_ii_r[i];
      msg_old_L_flat_o[i*32 +: 32] = old_L_r[i];
    end
    for (int i = 0; i < GBP_MAX_VAR_DIM*GBP_MAX_VAR_DIM; i++)
      msg_L_io_flat_o[i*32 +: 32] = L_io_r[i];
  end

endmodule
