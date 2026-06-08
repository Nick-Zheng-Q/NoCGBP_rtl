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
    input  logic clk,
    input  logic rst_n,

    // ── Descriptor from Compute Unit ──
    input  logic                 desc_valid,
    output logic                 desc_ready,
    input  logic [SPM_ADDR_W-1:0] desc_base_addr,
    input  logic [15:0]          desc_word_count,
    input  logic                 desc_is_staging,   // informational, unused internally

    // ── Data beats to Compute Unit ──
    output logic                 beat_valid,
    input  logic                 beat_ready,
    output logic [BEAT_BITS-1:0] beat_data,

    // ── SPM read port (to SPM Arbiter) ──
    output logic                 spm_rd_valid,
    input  logic                 spm_rd_ready,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr,
    input  logic [BEAT_BITS-1:0] spm_rd_data
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

  assign desc_ready = ~active_r;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      active_r     <= 1'b0;
      all_issued_r <= 1'b0;
    end else begin
      if (desc_valid && desc_ready) begin
        active_r     <= 1'b1;
        all_issued_r <= 1'b0;
        desc_base_r  <= desc_base_addr;
        desc_count_r <= desc_word_count;
        desc_staging_r <= desc_is_staging;
      end else begin
        if (agu_last_addr && agu_addr_ready) begin
          all_issued_r <= 1'b1;
        end
        if (all_issued_r && !beat_valid) begin
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
  // asserted (when desc_valid && desc_ready).
  assign agu_start      = desc_valid && desc_ready;
  assign agu_base_addr  = desc_base_addr >> BEAT_ADDR_SHIFT;
  assign agu_beat_count = (desc_word_count + WORDS_PER_BEAT[15:0] - 16'd1) >> BEAT_ADDR_SHIFT;

  agu #(.SPM_ADDR_W(SPM_ADDR_W)) u_agu (
    .clk        (clk),
    .rst_n      (rst_n),
    .start      (agu_start),
    .base_addr  (agu_base_addr),
    .word_count (agu_beat_count),
    .addr_valid (agu_addr_valid),
    .addr_ready (agu_addr_ready),
    .addr       (agu_addr),
    .last_addr  (agu_last_addr)
  );

  // ------------------------------------------------------------------
  // SPM read issue
  // ------------------------------------------------------------------
  // Back-pressure: stop issuing if output skid is full.
  logic can_issue_read;
  assign can_issue_read = ~skid_valid_r & ~(spm_rd_issued_r & ~beat_ready);

  assign spm_rd_valid = can_issue_read & agu_addr_valid;
  assign spm_rd_addr  = agu_addr << BEAT_ADDR_SHIFT;
  assign agu_addr_ready = spm_rd_valid & spm_rd_ready;

  // Delayed read-issue flag: data returns 1 cycle after issue
  logic spm_rd_issued_r;
  always_ff @(posedge clk) begin
    if (!rst_n) begin
      spm_rd_issued_r <= 1'b0;
    end else begin
      spm_rd_issued_r <= spm_rd_valid & spm_rd_ready;
    end
  end

  // ------------------------------------------------------------------
  // Skid buffer (1-entry) — holds beat when beat_ready is low
  // ------------------------------------------------------------------
  logic skid_valid_r;
  logic [BEAT_BITS-1:0] skid_data_r;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      skid_valid_r <= 1'b0;
    end else begin
      if (spm_rd_issued_r) begin
        if (!beat_valid || beat_ready) begin
          // Output path consumed or not valid — no need to skid
          skid_valid_r <= 1'b0;
        end else begin
          // Output blocked — store in skid
          skid_valid_r <= 1'b1;
          skid_data_r  <= spm_rd_data;
        end
      end
      if (beat_valid & beat_ready) begin
        skid_valid_r <= 1'b0;
      end
    end
  end

  assign beat_valid = skid_valid_r | spm_rd_issued_r;
  assign beat_data  = skid_valid_r ? skid_data_r : spm_rd_data;

endmodule
