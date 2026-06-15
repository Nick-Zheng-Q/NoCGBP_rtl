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
    input logic clk_i,
    input logic rst_n_i,

    // ── Descriptor from Compute Unit ──
    input  logic                  desc_valid_i,
    output logic                  desc_ready_o,
    input  logic [SPM_ADDR_W-1:0] desc_base_addr_i,
    input  logic [          15:0] desc_word_count_i,
    input  logic                  desc_is_staging_i,  // informational, unused internally

    // ── Data beats to Compute Unit ──
    output logic                 beat_valid_o,
    input  logic                 beat_ready_i,
    output logic [BEAT_BITS-1:0] beat_data_o,

    // ── SPM read port (to SPM Arbiter) ──
    output logic                  spm_rd_valid_o,
    input  logic                  spm_rd_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic [ BEAT_BITS-1:0] spm_rd_data_i
);

  localparam int WORDS_PER_BEAT = BEAT_BITS / FP32_W;
  localparam int BEAT_ADDR_SHIFT = $clog2(WORDS_PER_BEAT);

  // ------------------------------------------------------------------
  // Descriptor latch
  // ------------------------------------------------------------------
  logic [15:0] desc_count_r;
  logic        desc_staging_r;

  // ------------------------------------------------------------------
  // Activity / completion tracking
  // ------------------------------------------------------------------
  logic        active_r;
  logic        all_issued_r;

  assign desc_ready_o = ~active_r;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      active_r     <= 1'b0;
      all_issued_r <= 1'b0;
    end else begin
      if (desc_valid_i && desc_ready_o) begin
        active_r       <= 1'b1;
        all_issued_r   <= 1'b0;
        desc_count_r   <= desc_word_count_i;
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
  logic                  agu_start;
  logic [SPM_ADDR_W-1:0] agu_base_addr;
  logic [          15:0] agu_beat_count;
  logic                  agu_addr_valid;
  logic                  agu_addr_ready;
  logic [SPM_ADDR_W-1:0] agu_addr;
  logic                  agu_last_addr;

  // AGU uses combinational desc_base_addr_i (stable from assembler's register)
  // instead of desc_base_r, to avoid the same-edge race condition.
  assign agu_start      = desc_valid_i && desc_ready_o;
  assign agu_base_addr  = desc_base_addr_i >> BEAT_ADDR_SHIFT;
  assign agu_beat_count = (desc_word_count_i + WORDS_PER_BEAT[15:0] - 16'd1) >> BEAT_ADDR_SHIFT;

  agu #(
      .SPM_ADDR_W(SPM_ADDR_W)
  ) u_agu (
      .clk_i       (clk_i),
      .rst_n_i     (rst_n_i),
      .start_i     (agu_start),
      .base_addr_i (agu_base_addr),
      .word_count_i(agu_beat_count),
      .addr_valid_o(agu_addr_valid),
      .addr_ready_i(agu_addr_ready),
      .addr_o      (agu_addr),
      .last_addr_o (agu_last_addr)
  );

  // ------------------------------------------------------------------
  // SPM read issue (matches spm_arbiter delayed-ready protocol)
  // ------------------------------------------------------------------
  // The SPM arbiter grants a request combinationally and returns data one
  // cycle later. Its rd_ready_o is therefore a "response valid" indicator
  // in cycle N+1, not a same-cycle request-accept signal in cycle N.
  //
  // We issue requests speculatively (advance the AGU on issue) and retry
  // the same address if the arbiter does not return data in the following
  // cycle (e.g. due to round-robin contention).

  // Outstanding request tracking
  logic                  rd_req_active_r;
  logic [SPM_ADDR_W-1:0] issued_addr_r;

  // Response valid for the output stage.
  logic                  rsp_valid;
  assign rsp_valid = spm_rd_ready_i & rd_req_active_r;

  // We can accept a new response only when the output path has room.
  // The skid buffer holds one beat; if a response is arriving this cycle
  // and the consumer is not ready, the skid is needed for it.
  logic can_issue_read;
  assign can_issue_read = ~skid_valid_r & (~rsp_valid | beat_ready_i);

  // If the previous request was not granted, retry the same address.
  logic retrying;
  assign retrying = rd_req_active_r & ~spm_rd_ready_i;

  // We may start a new request when there is no outstanding request, or
  // when the outstanding request is completing this cycle.
  logic can_start_new;
  assign can_start_new  = ~rd_req_active_r | spm_rd_ready_i;

  assign spm_rd_valid_o = (agu_addr_valid & can_issue_read & can_start_new) | retrying;
  assign spm_rd_addr_o  = retrying ? issued_addr_r : (agu_addr << BEAT_ADDR_SHIFT);

  // Advance the AGU only when we actually issue a fresh request (not a retry).
  logic issue_new;
  assign issue_new      = spm_rd_valid_o & ~retrying;
  assign agu_addr_ready = issue_new;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      rd_req_active_r <= 1'b0;
      issued_addr_r   <= '0;
    end else begin
      if (issue_new) begin
        rd_req_active_r <= 1'b1;
        issued_addr_r   <= agu_addr << BEAT_ADDR_SHIFT;
      end else if (spm_rd_ready_i) begin
        rd_req_active_r <= 1'b0;
      end
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
      if (rsp_valid) begin
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

  assign beat_valid_o = skid_valid_r | rsp_valid;
  assign beat_data_o  = skid_valid_r ? skid_data_r : spm_rd_data_i;

endmodule
