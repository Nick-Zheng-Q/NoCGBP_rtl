// write_stream_engine.sv
// New-architecture write stream engine.
// Accepts a stream_descriptor_t plus 32-bit FP32 words from the compute
// unit, assembles them into 64-bit beats, and writes beats to SPM via
// the arbiter.
//
// No SystemVerilog interfaces — all explicit valid/ready ports.

module write_stream_engine
  import gbp_pkg::*;
(
    input  logic clk_i,
    input  logic rst_n_i,

    // ── Descriptor from Compute Unit ──
    input  logic                 desc_valid_i,
    output logic                 desc_ready_o,
    input  logic [SPM_ADDR_W-1:0] desc_base_addr_i,
    input  logic [15:0]          desc_word_count_i,

    // ── Data words from Compute Unit ──
    input  logic                 word_valid_i,
    output logic                 word_ready_o,
    input  logic [FP32_W-1:0]    word_data_i,

    // ── SPM write port (to SPM Arbiter) ──
    output logic                 spm_wr_valid_o,
    input  logic                 spm_wr_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_wr_addr_o,
    output logic [BEAT_BITS-1:0] spm_wr_data_o,
    output logic [BEAT_BYTES-1:0] spm_wr_wstrb_o
);

  localparam int WORDS_PER_BEAT  = BEAT_BITS / FP32_W;
  localparam int BEAT_ADDR_SHIFT = $clog2(WORDS_PER_BEAT);
  localparam int WORD_IDX_W      = $clog2(WORDS_PER_BEAT);

  // ------------------------------------------------------------------
  // Descriptor latch
  // ------------------------------------------------------------------
  logic [SPM_ADDR_W-1:0] desc_base_r;
  logic [15:0]           desc_count_r;

  // ------------------------------------------------------------------
  // Activity tracking
  // ------------------------------------------------------------------
  logic active_r;
  logic all_words_received_r;
  logic all_beats_written_r;

  assign desc_ready_o = ~active_r;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      active_r             <= 1'b0;
      all_words_received_r <= 1'b0;
      all_beats_written_r  <= 1'b0;
    end else begin
      if (desc_valid_i && desc_ready_o) begin
        active_r             <= 1'b1;
        all_words_received_r <= 1'b0;
        all_beats_written_r  <= 1'b0;
        desc_base_r          <= desc_base_addr_i;
        desc_count_r         <= desc_word_count_i;
      end else begin
        if (word_valid_i && word_ready_o &&
            (word_cnt_r + 16'd1 == desc_count_r)) begin
          all_words_received_r <= 1'b1;
        end
        if (agu_last_addr && agu_addr_ready) begin
          all_beats_written_r <= 1'b1;
        end
        if (all_beats_written_r && !spm_wr_valid_o) begin
          active_r <= 1'b0;
        end
      end
    end
  end

  // ------------------------------------------------------------------
  // Word assembler (32b → 64b)
  // ------------------------------------------------------------------
  logic [WORDS_PER_BEAT-1:0][FP32_W-1:0] beat_assembler_r;
  logic [WORD_IDX_W-1:0]                 word_idx_r;
  logic [15:0]                           word_cnt_r;
  logic                                  beat_full;
  logic                                  partial_beat_trigger;

  assign word_ready_o = active_r && ~all_words_received_r;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      word_idx_r   <= '0;
      word_cnt_r   <= '0;
      beat_assembler_r <= '0;
    end else begin
      if (desc_valid_i && desc_ready_o) begin
        word_idx_r   <= '0;
        word_cnt_r   <= '0;
        beat_assembler_r <= '0;
      end else if (word_valid_i && word_ready_o) begin
        beat_assembler_r[word_idx_r] <= word_data_i;
        word_idx_r <= word_idx_r + 1'b1;
        word_cnt_r <= word_cnt_r + 16'd1;
        if (word_idx_r == WORDS_PER_BEAT[WORD_IDX_W-1:0] - 1'b1) begin
          word_idx_r <= '0;
        end
      end
    end
  end

  assign beat_full = (word_valid_i && word_ready_o) &&
                     (word_idx_r == WORDS_PER_BEAT[WORD_IDX_W-1:0] - 1'b1);

  assign partial_beat_trigger = (word_valid_i && word_ready_o) &&
                                (word_cnt_r + 16'd1 == desc_count_r) &&
                                (word_idx_r != WORDS_PER_BEAT[WORD_IDX_W-1:0] - 1'b1);

  // ------------------------------------------------------------------
  // Write-pending flag (one per beat)
  // ------------------------------------------------------------------
  logic write_pending_r;
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      write_pending_r <= 1'b0;
    end else begin
      if (beat_full || partial_beat_trigger) begin
        write_pending_r <= 1'b1;
      end else if (spm_wr_valid_o && spm_wr_ready_i) begin
        write_pending_r <= 1'b0;
      end
    end
  end

  // ------------------------------------------------------------------
  // Flatten assembler to beat vector
  // ------------------------------------------------------------------
  generate
    for (genvar i = 0; i < WORDS_PER_BEAT; i++) begin : g_beat
      assign spm_wr_data_o[i*FP32_W +: FP32_W] = beat_assembler_r[i];
    end
  endgenerate

  // ------------------------------------------------------------------
  // AGU (beat-level addressing)
  // ------------------------------------------------------------------
  logic                 agu_start;
  logic [SPM_ADDR_W-1:0] agu_base_addr;
  logic [15:0]          agu_beat_count;
  logic                 agu_addr_valid;
  logic                 agu_addr_ready;
  logic [SPM_ADDR_W-1:0] agu_addr;
  logic                 agu_last_addr;

  assign agu_start      = desc_valid_i && desc_ready_o;
  assign agu_base_addr  = desc_base_addr_i >> BEAT_ADDR_SHIFT;
  assign agu_beat_count = (desc_word_count_i + WORDS_PER_BEAT[15:0] - 16'd1) >> BEAT_ADDR_SHIFT;

  agu #(.SPM_ADDR_W(SPM_ADDR_W)) u_agu (
    .clk_i        (clk_i),
    .rst_n_i      (rst_n_i),
    .start_i      (agu_start),
    .base_addr_i  (agu_base_addr),
    .word_count_i (agu_beat_count),
    .addr_valid_o (agu_addr_valid),
    .addr_ready_i (agu_addr_ready),
    .addr_o       (agu_addr),
    .last_addr_o  (agu_last_addr)
  );

  // ------------------------------------------------------------------
  // SPM write issue
  // ------------------------------------------------------------------
  assign spm_wr_valid_o = active_r && agu_addr_valid && write_pending_r;
  assign spm_wr_addr_o  = agu_addr << BEAT_ADDR_SHIFT;
  assign agu_addr_ready = spm_wr_valid_o && spm_wr_ready_i;

  // Byte-enable: full beat unless last partial beat
  always_comb begin
    spm_wr_wstrb_o = {BEAT_BYTES{1'b1}};
    // Partial beat: only lower (word_idx_r * WORD_BYTES) bytes valid.
    // word_idx_r reflects how many words were assembled BEFORE wrapping.
    if (write_pending_r && all_words_received_r && (word_idx_r != '0)) begin
      for (int b = 0; b < BEAT_BYTES; b++) begin
        spm_wr_wstrb_o[b] = (b < int'(word_idx_r) * int'(WORD_BYTES));
      end
    end
  end

endmodule
