// read_stream_engine.sv
// New-architecture read stream engine.
// Accepts a stream_descriptor_t, drives an internal AGU to generate beat
// addresses, issues SPM reads via the arbiter, and returns 64-bit beats
// to the compute unit.
//
// No META handling — metadata is read independently by metadata_scanner.
// No SystemVerilog interfaces — all explicit valid/ready ports.

module read_stream_engine
  import gbp_pkg::*;
(
    input  logic clk_i,
    input  logic rst_n_i,

    // ── Descriptor from Compute Unit ──
    input  logic                 desc_valid_i,
    output logic                 desc_ready_o,
    input  logic [SPM_ADDR_W-1:0] desc_base_addr_i,
    input  logic [15:0]          desc_word_count_i,
    input  logic                 desc_is_staging_i,   // informational, unused internally

    // ── Data beats to Compute Unit ──
    output logic                 beat_valid_o,
    input  logic                 beat_ready_i,
    output logic [BEAT_BITS-1:0] beat_data_o,

    // ── SPM read port (to SPM Arbiter) ──
    output logic                 spm_rd_valid_o,
    input  logic                 spm_rd_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic [BEAT_BITS-1:0] spm_rd_data_i
);

  localparam int WORDS_PER_BEAT = BEAT_BITS / FP32_W;
  localparam int BEAT_ADDR_SHIFT = $clog2(WORDS_PER_BEAT);

  // ------------------------------------------------------------------
  // Descriptor latch
  // ------------------------------------------------------------------
  logic [SPM_ADDR_W-1:0] desc_base_r;
  logic [15:0]           desc_count_r;
  logic                  desc_staging_r;

  // ------------------------------------------------------------------
  // Activity / completion tracking
  // ------------------------------------------------------------------
  logic active_r;
  logic all_issued_r;

  assign desc_ready_o = ~active_r;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      active_r     <= 1'b0;
      all_issued_r <= 1'b0;
    end else begin
      if (desc_valid_i && desc_ready_o) begin
        active_r     <= 1'b1;
        all_issued_r <= 1'b0;
        desc_base_r  <= desc_base_addr_i;
        desc_count_r <= desc_word_count_i;
        desc_staging_r <= desc_is_staging_i;
      end else begin
        if (agu_last_addr && agu_addr_ready) begin
          all_issued_r <= 1'b1;
        end
        if (all_issued_r && !beat_valid_o) begin
          active_r <= 1'b0;
        end
      end
    end
  end

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

  // AGU inputs are driven combinationally from the descriptor ports so that
  // the AGU samples the correct base_addr in the same cycle that start is
  // asserted (when desc_valid_i && desc_ready_o).
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
  // SPM read issue
  // ------------------------------------------------------------------
  // Back-pressure: stop issuing if output skid is full.
  logic can_issue_read;
  assign can_issue_read = ~skid_valid_r & ~(spm_rd_issued_r & ~beat_ready_i);

  assign spm_rd_valid_o = can_issue_read & agu_addr_valid;
  assign spm_rd_addr_o  = agu_addr << BEAT_ADDR_SHIFT;
  assign agu_addr_ready = spm_rd_valid_o & spm_rd_ready_i;

  // Delayed read-issue flag: data returns 1 cycle after issue
  logic spm_rd_issued_r;
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      spm_rd_issued_r <= 1'b0;
    end else begin
      spm_rd_issued_r <= spm_rd_valid_o & spm_rd_ready_i;
    end
  end

  // ------------------------------------------------------------------
  // Skid buffer (1-entry) — holds beat when beat_ready_i is low
  // ------------------------------------------------------------------
  logic skid_valid_r;
  logic [BEAT_BITS-1:0] skid_data_r;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      skid_valid_r <= 1'b0;
    end else begin
      if (spm_rd_issued_r) begin
        if (!beat_valid_o || beat_ready_i) begin
          // Output path consumed or not valid — no need to skid
          skid_valid_r <= 1'b0;
        end else begin
          // Output blocked — store in skid
          skid_valid_r <= 1'b1;
          skid_data_r  <= spm_rd_data_i;
        end
      end
      if (beat_valid_o & beat_ready_i) begin
        skid_valid_r <= 1'b0;
      end
    end
  end

  assign beat_valid_o = skid_valid_r | spm_rd_issued_r;
  assign beat_data_o  = skid_valid_r ? skid_data_r : spm_rd_data_i;

endmodule
