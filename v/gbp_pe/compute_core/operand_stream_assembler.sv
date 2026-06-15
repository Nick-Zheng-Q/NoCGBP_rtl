// operand_stream_assembler.sv
// GBP Compute Core v0.7 — read-side width/format adapter
// Takes 64-bit SPM beats from read_stream_engine and assembles them into
// 16-word operand_stream_beat_t beats for compute_unit_wrapper.

module operand_stream_assembler
  import gbp_pkg::*;
  import gbp_op_pkg::*;
#(
    parameter  int SPM_ADDR_W            = gbp_pkg::SPM_ADDR_W,
    parameter  int BEAT_BITS             = gbp_pkg::BEAT_BITS,
    parameter  int FP32_W                = gbp_pkg::FP32_W,
    localparam int WORDS_PER_RSE_BEAT    = BEAT_BITS / FP32_W,                        // 2
    localparam int RSE_BEATS_PER_OPERAND = OPERAND_STREAM_WIDTH / WORDS_PER_RSE_BEAT  // 8
) (
    input logic clk_i,
    input logic rst_n_i,

    // Descriptor from compute_unit_wrapper
    input  logic                                  desc_valid_i,
    output logic                                  desc_ready_o,
    input  operand_stream_kind_e                  desc_kind_i,
    input  logic                 [          31:0] desc_op_id_i,
    input  logic                 [SPM_ADDR_W-1:0] desc_base_addr_i,
    input  logic                 [          15:0] desc_nbeats_i,

    // Operand stream to compute_unit_wrapper
    output logic                 operand_valid_o,
    input  logic                 operand_ready_i,
    output operand_stream_beat_t operand_o,

    // 64-bit SPM read port (to SPM arbiter / memory subsystem)
    output logic                  spm_rd_valid_o,
    input  logic                  spm_rd_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic [ BEAT_BITS-1:0] spm_rd_data_i
);

  // ------------------------------------------------------------------
  // Internal read_stream_engine (64-bit beat producer)
  // ------------------------------------------------------------------
  logic                  rse_desc_valid;
  logic                  rse_desc_ready;
  logic [SPM_ADDR_W-1:0] rse_desc_base;
  logic [          15:0] rse_desc_word_count;
  logic                  rse_desc_is_staging;

  logic                  rse_beat_valid;
  logic                  rse_beat_ready;
  logic [ BEAT_BITS-1:0] rse_beat_data;

  read_stream_engine u_rse (
      .clk_i            (clk_i),
      .rst_n_i          (rst_n_i),
      .desc_valid_i     (rse_desc_valid),
      .desc_ready_o     (rse_desc_ready),
      .desc_base_addr_i (rse_desc_base),
      .desc_word_count_i(rse_desc_word_count),
      .desc_is_staging_i(rse_desc_is_staging),
      .beat_valid_o     (rse_beat_valid),
      .beat_ready_i     (rse_beat_ready),
      .beat_data_o      (rse_beat_data),
      .spm_rd_valid_o   (spm_rd_valid_o),
      .spm_rd_ready_i   (spm_rd_ready_i),
      .spm_rd_addr_o    (spm_rd_addr_o),
      .spm_rd_data_i    (spm_rd_data_i)
  );

  // ------------------------------------------------------------------
  // State
  // ------------------------------------------------------------------
  typedef enum logic [1:0] {
    S_IDLE,
    S_ISSUE,
    S_STREAM
  } state_e;

  state_e                                                               state_r;
  logic                 [                             15:0]             total_beats_r;
  logic                 [                             15:0]             beat_idx_r;
  logic                 [$clog2(RSE_BEATS_PER_OPERAND)-1:0]             rse_beat_cnt_r;
  operand_stream_kind_e                                                 kind_r;
  logic                 [                             31:0]             op_id_r;
  logic                 [         OPERAND_STREAM_WIDTH-1:0][FP32_W-1:0] data_buffer_r;
  logic                                                                 operand_valid_r;

  // ------------------------------------------------------------------
  // RSE descriptor (registered, synced with state_r)
  // ------------------------------------------------------------------
  logic [SPM_ADDR_W-1:0] rse_desc_base_r;
  logic [15:0]           rse_desc_word_count_r;

  assign rse_desc_valid      = (state_r == S_ISSUE);
  assign rse_desc_base       = rse_desc_base_r;
  assign rse_desc_word_count = rse_desc_word_count_r;
  assign rse_desc_is_staging = 1'b0;

  // We are ready for a new descriptor only when idle
  assign desc_ready_o        = (state_r == S_IDLE);

  // Accept SPM beats once the descriptor has been issued.
  assign rse_beat_ready      = ((state_r == S_ISSUE) || (state_r == S_STREAM)) && !operand_valid_r;

  // ------------------------------------------------------------------
  // Output assembly
  // ------------------------------------------------------------------
  always_comb begin
    operand_o.kind     = kind_r;
    operand_o.ctx_id   = 1'b0;
    operand_o.op_id    = op_id_r;
    operand_o.beat_idx = beat_idx_r;
    operand_o.last     = (beat_idx_r + 16'd1 == total_beats_r);
    for (int i = 0; i < OPERAND_STREAM_WIDTH; i++) operand_o.data[i] = data_buffer_r[i];
  end

  assign operand_valid_o = operand_valid_r;

  // ------------------------------------------------------------------
  // Sequential logic
  // ------------------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      state_r              <= S_IDLE;
      total_beats_r        <= '0;
      beat_idx_r           <= '0;
      rse_beat_cnt_r       <= '0;
      kind_r               <= OST_MSG_STATIC;
      op_id_r              <= '0;
      data_buffer_r        <= '0;
      operand_valid_r      <= 1'b0;
      rse_desc_base_r      <= '0;
      rse_desc_word_count_r <= '0;
    end else begin
      case (state_r)
        S_IDLE: begin
          operand_valid_r <= 1'b0;
          if (desc_valid_i && desc_ready_o) begin
            total_beats_r        <= desc_nbeats_i;
            beat_idx_r           <= '0;
            rse_beat_cnt_r       <= '0;
            kind_r               <= desc_kind_i;
            op_id_r              <= desc_op_id_i;
            data_buffer_r        <= '0;
            rse_desc_base_r      <= desc_base_addr_i;
            rse_desc_word_count_r <= desc_nbeats_i * 16'd16;
            state_r              <= S_ISSUE;
          end
        end

        S_ISSUE: begin
          if (rse_desc_valid && rse_desc_ready) state_r <= S_STREAM;
        end

        S_STREAM: begin
          // Consume a completed operand beat
          if (operand_valid_r && operand_ready_i) begin
            operand_valid_r <= 1'b0;
            beat_idx_r      <= beat_idx_r + 16'd1;
            if (beat_idx_r + 16'd1 == total_beats_r) begin
              state_r <= S_IDLE;
            end
          end

          // Accept 64-bit SPM beats and pack them into the operand buffer
          if (rse_beat_valid && rse_beat_ready) begin
            for (int i = 0; i < WORDS_PER_RSE_BEAT; i++) begin
              data_buffer_r[rse_beat_cnt_r*WORDS_PER_RSE_BEAT[2:0] + i[2:0]]
                  <= rse_beat_data[i*FP32_W +: FP32_W];
            end

            if (rse_beat_cnt_r == RSE_BEATS_PER_OPERAND - 1) begin
              // This beat completes one 16-word operand
              operand_valid_r <= 1'b1;
              rse_beat_cnt_r  <= '0;
            end else begin
              rse_beat_cnt_r <= rse_beat_cnt_r + 1'b1;
            end
          end
        end

        default: state_r <= S_IDLE;
      endcase
    end
  end

endmodule
