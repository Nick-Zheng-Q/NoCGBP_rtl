// compute_unit.sv
// New-architecture compute unit.
// Wraps gbp_compute_engine with the interface from 05_INTERFACES.md §2.10.
// Performs all width conversion internally:
//   - External rd_beat is 64-bit (per spec BEAT_BITS=64)
//   - gbp_compute_engine stream_in/stream_out is 256-bit
//   - External wr_word is 32-bit
//
// Data flow:
//   rd_beat (64b) ──► [assemble 4 beats → 256b] ──► stream_in_data
//   ns_data_i (32b) ──► [assemble 8 words → 256b] ──► stream_in_data
//   stream_out_data (256b) ──► [disassemble to 32b words] ──► wr_word

module compute_unit
  import gbp_pkg::*;
(
    input  logic clk_i,
    input  logic rst_n_i,

    // ── Command (from Node Scheduler + Metadata Scanner) ──
    input  logic                 cmd_valid_i,
    output logic                 cmd_ready_o,
    input  logic [NODE_ID_W-1:0] cmd_node_id_i,
    input  logic                 cmd_is_factor_i,
    input  logic [DOF_W-1:0]     cmd_dof_i,
    input  logic [ADJ_COUNT_W-1:0] cmd_adj_count_i,
    input  logic [STATE_WORDS_W-1:0] cmd_state_words_i,
    input  logic [SPM_ADDR_W-1:0]  cmd_state_base_i,
    input  logic [MAX_ADJ_COUNT-1:0][DOF_W-1:0] cmd_neighbor_dofs_i,

    // ── Neighbor state (from Accumulator) ──
    input  logic                 ns_valid_i,
    output logic                 ns_ready_o,
    input  logic [FP32_W-1:0]    ns_data_i,
    input  logic                 ns_last_i,

    // ── Read Stream Engine interface ──
    output logic                 rd_desc_valid_o,
    input  logic                 rd_desc_ready_i,
    output logic [SPM_ADDR_W-1:0] rd_desc_base_addr_o,
    output logic [15:0]          rd_desc_word_count_o,
    output logic                 rd_desc_is_staging_o,

    input  logic                 rd_beat_valid_i,
    output logic                 rd_beat_ready_o,
    input  logic [BEAT_BITS-1:0] rd_beat_data_i,

    // ── Write Stream Engine interface ──
    output logic                 wr_desc_valid_o,
    input  logic                 wr_desc_ready_i,
    output logic [SPM_ADDR_W-1:0] wr_desc_base_addr_o,
    output logic [15:0]          wr_desc_word_count_o,

    output logic                 wr_word_valid_o,
    input  logic                 wr_word_ready_i,
    output logic [FP32_W-1:0]    wr_word_data_o,

    // ── Completion ──
    output logic                 done_valid_o,
    output logic [NODE_ID_W-1:0] done_node_id_o,
    output logic                 done_is_factor_o,
    output logic                 batch_done_o
);

  localparam int GBP_IN_BITS = 256;
  localparam int GBP_OUT_BITS = 256;
  localparam int WORDS_PER_GBP_IN = GBP_IN_BITS / FP32_W;  // 8
  localparam int WORDS_PER_GBP_OUT = GBP_OUT_BITS / FP32_W; // 8
  localparam int BEATS_PER_GBP_IN = GBP_IN_BITS / BEAT_BITS; // 4 for 64b beat

  // ------------------------------------------------------------------
  // Internal command latches (for done_node_id/done_is_factor)
  // ------------------------------------------------------------------
  logic [NODE_ID_W-1:0] cmd_node_id_r;
  logic                 cmd_is_factor_r;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      cmd_node_id_r  <= '0;
      cmd_is_factor_r <= 1'b0;
    end else if (cmd_valid_i && cmd_ready_o) begin
      cmd_node_id_r   <= cmd_node_id_i;
      cmd_is_factor_r <= cmd_is_factor_i;
    end
  end

  // ------------------------------------------------------------------
  // GBP compute engine instantiation
  // ------------------------------------------------------------------
  logic               gbp_cmd_valid;
  logic               gbp_cmd_ready;
  logic               gbp_cmd_is_factor;
  logic [7:0]         gbp_cmd_node_idx;
  logic [2:0]         gbp_cmd_dofs;
  logic [3:0]         gbp_cmd_adj_count;
  logic [3:0]         gbp_cmd_msg_count;
  logic [7:0][2:0]    gbp_cmd_neighbor_dofs;
  logic               gbp_compute_done;
  logic               gbp_rsp_done;

  logic               stream_in_ready;
  logic               stream_in_valid;
  logic [GBP_IN_BITS-1:0] stream_in_data;

  logic               stream_out_ready;
  logic               stream_out_valid;
  logic [GBP_OUT_BITS-1:0] stream_out_data;

  // Output disassembler always produces WORDS_PER_GBP_OUT (8) words per beat.
  // write_stream_engine expects wr_desc_word_count_o words; the engine must
  // output exactly one beat so that disassembler finishes cleanly.
  localparam int WR_WORDS_PER_BEAT = 8;
  localparam int WR_XFER_BYTES = WR_WORDS_PER_BEAT * 4;

  gbp_compute_engine #(
    .LANES(16),
    .MAX_DOFS(6),
    .MAX_ADJACENT(8),
    .STAGING_DEPTH(1024)
  ) u_engine (
    .clk_i(clk_i),
    .rst_n_i(rst_n_i),
    .cmd_valid_i(gbp_cmd_valid),
    .cmd_is_factor_i(gbp_cmd_is_factor),
    .cmd_node_idx_i(gbp_cmd_node_idx),
    .cmd_dofs_i(gbp_cmd_dofs),
    .cmd_adj_count_i(gbp_cmd_adj_count),
    .cmd_msg_count_i(gbp_cmd_msg_count),
    .cmd_neighbor_dofs_i(gbp_cmd_neighbor_dofs),
    .cmd_state_words_i(cmd_state_words_i),
    .cmd_wr_xfer_bytes_i(16'(WR_XFER_BYTES)),
    .cmd_ready_o(gbp_cmd_ready),
    .compute_done_o(gbp_compute_done),
    .rsp_done_o(gbp_rsp_done),
    .stream_in_ready(stream_in_ready),
    .stream_in_valid(stream_in_valid),
    .stream_in_data(stream_in_data),
    .stream_out_ready(stream_out_ready),
    .stream_out_valid(stream_out_valid),
    .stream_out_data(stream_out_data),
    .damping_factor_i(32'h3E99999A)
  );

  // ------------------------------------------------------------------
  // Command mapping
  // ------------------------------------------------------------------
  assign gbp_cmd_valid     = cmd_valid_i;
  assign cmd_ready_o         = gbp_cmd_ready;
  assign gbp_cmd_is_factor = cmd_is_factor_i;
  assign gbp_cmd_node_idx  = cmd_node_id_i[7:0];
  assign gbp_cmd_dofs      = cmd_dof_i;
  assign gbp_cmd_adj_count = cmd_adj_count_i;
  // For variable node: msg_count = adj_count (one message per adjacent factor)
  // For factor node: msg_count = adj_count (one belief per adjacent variable)
  assign gbp_cmd_msg_count = cmd_adj_count_i;
  assign gbp_cmd_neighbor_dofs = {8{cmd_dof_i}};  // uniform DOF for all neighbors

  always_ff @(posedge clk_i) begin
    if (cmd_valid_i && cmd_ready_o) begin
      $display("COMPUTE_UNIT_DBG: cmd_adj_count_i=%d gbp_cmd_msg_count=%d", cmd_adj_count_i, gbp_cmd_msg_count);
    end
  end

  // ------------------------------------------------------------------
  // Read descriptor: issue once on cmd_valid_i
  // ------------------------------------------------------------------
  assign rd_desc_valid_o      = cmd_valid_i & cmd_ready_o;
  assign rd_desc_base_addr_o  = cmd_state_base_i;
  assign rd_desc_word_count_o = {7'b0, cmd_state_words_i};
  assign rd_desc_is_staging_o = 1'b0;

  // ------------------------------------------------------------------
  // Input assembler 64b beat / 32b word → 256b engine word
  // ------------------------------------------------------------------
  // Two input sources share one 256b engine input:
  //   Source A: rd_beat (64b SPM beats)   → 4 beats assemble to 1 engine word
  //   Source B: ns_data_i (32b words)       → 8 words assemble to 1 engine word
  // Priority: rd_beat first, then ns_data_i.

  // Source A: 64b beat assembler
  logic [BEATS_PER_GBP_IN-1:0][BEAT_BITS-1:0] rd_beat_buffer_r;
  logic [$clog2(BEATS_PER_GBP_IN)-1:0]        rd_beat_idx_r;
  logic                                       rd_word_valid_r;
  logic [GBP_IN_BITS-1:0]                     rd_word_data_r;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      rd_beat_idx_r   <= '0;
      rd_word_valid_r <= 1'b0;
    end else begin
      if (rd_beat_valid_i && rd_beat_ready_o) begin
        rd_beat_buffer_r[rd_beat_idx_r] <= rd_beat_data_i;
        if (rd_beat_idx_r == $clog2(BEATS_PER_GBP_IN)'(BEATS_PER_GBP_IN - 1)) begin
          rd_beat_idx_r   <= '0;
          rd_word_valid_r <= 1'b1;
        end else begin
          rd_beat_idx_r <= rd_beat_idx_r + 1'b1;
        end
      end
      if (stream_in_valid && stream_in_ready && rd_word_valid_r) begin
        rd_word_valid_r <= 1'b0;
      end
    end
  end

  // Source B: 32b ns word assembler
  logic [WORDS_PER_GBP_IN-1:0][FP32_W-1:0] ns_word_buffer_r;
  logic [$clog2(WORDS_PER_GBP_IN)-1:0]     ns_word_idx_r;
  logic                                    ns_word_valid_r;
  logic [GBP_IN_BITS-1:0]                  ns_word_data_r;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      ns_word_idx_r   <= '0;
      ns_word_valid_r <= 1'b0;
    end else begin
      if (ns_valid_i && ns_ready_o) begin
        ns_word_buffer_r[ns_word_idx_r] <= ns_data_i;
        if (ns_word_idx_r == $clog2(WORDS_PER_GBP_IN)'(WORDS_PER_GBP_IN - 1) || ns_last_i) begin
          ns_word_idx_r   <= '0;
          ns_word_valid_r <= 1'b1;
        end else begin
          ns_word_idx_r <= ns_word_idx_r + 1'b1;
        end
      end
      if (stream_in_valid && stream_in_ready && ns_word_valid_r && !rd_word_valid_r) begin
        ns_word_valid_r <= 1'b0;
      end
    end
  end

  // Flatten assembled buffers to 256b words (generate blocks for tool compatibility)
  generate
    for (genvar i = 0; i < BEATS_PER_GBP_IN; i++) begin : g_rd_flat
      assign rd_word_data_r[i*BEAT_BITS +: BEAT_BITS] = rd_beat_buffer_r[i];
    end
    for (genvar i = 0; i < WORDS_PER_GBP_IN; i++) begin : g_ns_flat
      assign ns_word_data_r[i*FP32_W +: FP32_W] = ns_word_buffer_r[i];
    end
  endgenerate

  assign ns_ready_o = ~ns_word_valid_r;
  assign rd_beat_ready_o = ~rd_word_valid_r;

  assign stream_in_valid = rd_word_valid_r | ns_word_valid_r;
  assign stream_in_data  = rd_word_valid_r ? rd_word_data_r : ns_word_data_r;

  // ------------------------------------------------------------------
  // Output disassembly: 256b engine word → 32b words
  // ------------------------------------------------------------------
  logic [GBP_OUT_BITS-1:0] out_word_buffer_r;
  logic [$clog2(WORDS_PER_GBP_OUT)-1:0] out_word_idx_r;
  logic out_active_r;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      out_active_r    <= 1'b0;
      out_word_idx_r  <= '0;
    end else begin
      if (stream_out_valid && stream_out_ready && !out_active_r) begin
        out_word_buffer_r <= stream_out_data;
        out_active_r   <= 1'b1;
        out_word_idx_r <= '0;
      end else if (wr_word_valid_o && wr_word_ready_i) begin
        out_word_idx_r <= out_word_idx_r + 1'b1;
        if (out_word_idx_r == $clog2(WORDS_PER_GBP_OUT)'(WORDS_PER_GBP_OUT - 1)) begin
          out_active_r <= 1'b0;
        end
      end
    end
  end

  assign stream_out_ready = ~out_active_r;

  // Capture writeback base address from command
  logic [SPM_ADDR_W-1:0] wr_base_addr_r;
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      wr_base_addr_r <= '0;
    end else if (cmd_valid_i && cmd_ready_o) begin
      wr_base_addr_r <= cmd_state_base_i;
    end
  end

  assign wr_desc_valid_o      = out_active_r;
  assign wr_desc_base_addr_o  = wr_base_addr_r;
  assign wr_desc_word_count_o = 16'd8;
  assign wr_word_valid_o      = out_active_r;
  assign wr_word_data_o       = out_word_buffer_r[out_word_idx_r * FP32_W +: FP32_W];

  always_ff @(posedge clk_i) begin
    if (stream_out_valid && stream_out_ready) begin
      $display("COMPUTE_OUT_DBG: stream_out_data=%h %h %h %h %h %h %h %h",
               stream_out_data[255:224], stream_out_data[223:192], stream_out_data[191:160],
               stream_out_data[159:128], stream_out_data[127:96],  stream_out_data[95:64],
               stream_out_data[63:32],   stream_out_data[31:0]);
    end
    if (wr_word_valid_o && wr_word_ready_i) begin
      $display("COMPUTE_OUT_DBG: wr_word[%d]=%h (%f)", out_word_idx_r, wr_word_data_o, $bitstoreal(wr_word_data_o));
    end
  end

  // ------------------------------------------------------------------
  // Completion
  // ------------------------------------------------------------------
  assign done_valid_o     = gbp_rsp_done;
  assign done_node_id_o   = cmd_node_id_r;
  assign done_is_factor_o = cmd_is_factor_r;
  assign batch_done_o     = gbp_compute_done;

endmodule
