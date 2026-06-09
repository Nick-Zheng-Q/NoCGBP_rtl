// reverse_index_lookup
// Given a NOTIFICATION (source_node_id), looks up Reverse CSR in SPM to find
// all S1 nodes that have this neighbor. Outputs affected_local_id stream.
//
// v3: Reverse CSR implementation. O(affected_degree) SPM reads.
//     RevKeyHash -> RevHeader -> RevEntryArray

module reverse_index_lookup
  import gbp_pkg::*;
#(
    parameter int NODE_ID_W     = gbp_pkg::NODE_ID_W,
    parameter int SPM_ADDR_W    = gbp_pkg::SPM_ADDR_W,
    parameter int BEAT_BITS     = gbp_pkg::BEAT_BITS,
    parameter int REV_KEY_ENTRIES = gbp_pkg::REV_KEY_ENTRIES,
    parameter int REV_MAX_PROBE   = gbp_pkg::REV_MAX_PROBE,

    // SPM base addresses for Reverse CSR regions (word addresses)
    // NodeHeader occupies 0x0000~0x07FF (1024 nodes * 2 words)
    // Reverse CSR placed after header region to avoid overlap.
    parameter int REV_KEY_BASE    = 32'h0000_0800,  // 2K words
    parameter int REV_HEADER_BASE = 32'h0000_1000,  // 3K words
    parameter int REV_ENTRY_BASE  = 32'h0000_1800   // 4K words
) (
    input  logic clk_i,
    input  logic rst_n_i,

    // NOTIFICATION input
    input  logic                 rx_notif_valid_i,
    output logic                 rx_notif_ready_o,
    input  logic [NODE_ID_W-1:0] rx_notif_source_node_id_i,

    // Affected node output (to node_scheduler pending_queue)
    output logic                 affected_valid_o,
    input  logic                 affected_ready_i,
    output logic [NODE_ID_W-1:0] affected_local_id_o,

    // SPM read port (to memory subsystem)
    output logic                 spm_rd_valid_o,
    input  logic                 spm_rd_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd_addr_o,
    input  logic [BEAT_BITS-1:0] spm_rd_data_i
);

  logic rst_i;
  assign rst_i = ~rst_n_i;

  // Each struct fits in 32 bits (1 word). SPM beat is 64 bits = 2 words.
  // We read 1 word at a time, using lower 32 bits of beat.
  localparam int KEY_WORDS     = 1;
  localparam int HEADER_WORDS  = 1;
  localparam int ENTRY_WORDS   = 1;

  // FSM states
  localparam S_IDLE        = 4'd0;
  localparam S_HASH        = 4'd1;
  localparam S_RD_KEY      = 4'd2;
  localparam S_CHECK_KEY   = 4'd3;
  localparam S_RD_HEADER   = 4'd4;
  localparam S_PARSE_HDR   = 4'd5;
  localparam S_RD_ENTRY    = 4'd6;
  localparam S_OUTPUT      = 4'd7;
  localparam S_DONE        = 4'd8;

  logic [3:0] state_r;

  // Target node from NOTIFICATION
  logic [NODE_ID_W-1:0] target_node_r;

  // Hash probe state
  logic [SPM_ADDR_W-1:0] hash_addr_r;
  logic [$clog2(REV_MAX_PROBE):0] probe_cnt_r;

  // RevHeader latched fields
  logic [REV_LEN_W-1:0] rev_len_r;
  logic [SPM_ADDR_W-1:0] rev_base_r;

  // Entry iteration
  logic [REV_LEN_W-1:0] entry_idx_r;

  // Latched entry data
  logic [NODE_ID_W-1:0] entry_local_id_r;

  // Extract lower 32 bits from 64-bit SPM beat
  wire [31:0] spm_word_w = spm_rd_data_i[31:0];

  // Parse RevKeyHash entry from SPM word
  wire rev_key_valid_w   = spm_word_w[0];
  wire [NODE_ID_W-1:0] rev_key_key_w = spm_word_w[NODE_ID_W:1];
  wire [REV_ID_W-1:0]  rev_key_rev_id_w = spm_word_w[NODE_ID_W+REV_ID_W:NODE_ID_W+1];

  // Parse RevHeader from SPM word
  wire [REV_LEN_W-1:0]  rev_hdr_len_w  = spm_word_w[REV_LEN_W-1:0];
  wire [SPM_ADDR_W-1:0] rev_hdr_base_w = spm_word_w[SPM_ADDR_W+REV_LEN_W-1:REV_LEN_W];

  // Parse RevEntry from SPM word
  wire [NODE_ID_W-1:0]  rev_entry_local_w = spm_word_w[NODE_ID_W-1:0];

  // Output assignments
  // Ready in S_IDLE so notification can be captured immediately.
  assign rx_notif_ready_o = (state_r == S_IDLE);

  assign spm_rd_valid_o = (state_r == S_RD_KEY)
                       || (state_r == S_RD_HEADER)
                       || (state_r == S_RD_ENTRY);

  assign spm_rd_addr_o  = (state_r == S_RD_KEY)     ? (REV_KEY_BASE + hash_addr_r)
                        : (state_r == S_RD_HEADER) ? (REV_HEADER_BASE + SPM_ADDR_W'(rev_key_rev_id_w))
                        : (state_r == S_RD_ENTRY)  ? (REV_ENTRY_BASE + rev_base_r + SPM_ADDR_W'(entry_idx_r))
                        : '0;

  assign affected_valid_o = (state_r == S_OUTPUT);
  assign affected_local_id_o = entry_local_id_r;

  // FSM
  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      state_r         <= S_IDLE;
      target_node_r   <= '0;
      hash_addr_r     <= '0;
      probe_cnt_r     <= '0;
      rev_len_r       <= '0;
      rev_base_r      <= '0;
      entry_idx_r     <= '0;
      entry_local_id_r<= '0;
    end else begin
      case (state_r)
        S_IDLE: begin
          if (rx_notif_valid_i) begin
            target_node_r <= rx_notif_source_node_id_i;
            probe_cnt_r   <= '0;
            state_r       <= S_HASH;
          end
        end

        S_HASH: begin
          // hash = target & (REV_KEY_ENTRIES - 1)
          // Store hash index (0 ~ REV_KEY_ENTRIES-1) for wrap-around probing.
          hash_addr_r <= SPM_ADDR_W'(target_node_r & NODE_ID_W'(REV_KEY_ENTRIES - 1));
          state_r     <= S_RD_KEY;
        end

        S_RD_KEY: begin
          if (spm_rd_ready_i) begin
            state_r <= S_CHECK_KEY;
          end
        end

        S_CHECK_KEY: begin
          if (rev_key_valid_w && (rev_key_key_w == target_node_r)) begin
            // Hit: read RevHeader
            state_r <= S_RD_HEADER;
          end else begin
            // Miss: probe next bucket with wrap-around
            if (probe_cnt_r + 1 >= REV_MAX_PROBE) begin
              // Max probes reached: no match
              state_r <= S_DONE;
            end else begin
              hash_addr_r <= (hash_addr_r + SPM_ADDR_W'(1)) & SPM_ADDR_W'(REV_KEY_ENTRIES - 1);
              probe_cnt_r <= probe_cnt_r + 1;
              state_r     <= S_RD_KEY;
            end
          end
        end

        S_RD_HEADER: begin
          if (spm_rd_ready_i) begin
            state_r <= S_PARSE_HDR;
          end
        end

        S_PARSE_HDR: begin
          rev_len_r  <= rev_hdr_len_w;
          rev_base_r <= rev_hdr_base_w;
          entry_idx_r<= '0;
          if (rev_hdr_len_w == '0) begin
            state_r <= S_DONE;
          end else begin
            state_r <= S_RD_ENTRY;
          end
        end

        S_RD_ENTRY: begin
          if (spm_rd_ready_i) begin
            entry_local_id_r <= rev_entry_local_w;
            state_r <= S_OUTPUT;
          end
        end

        S_OUTPUT: begin
          if (affected_ready_i) begin
            if (entry_idx_r + 1 < rev_len_r) begin
              entry_idx_r <= entry_idx_r + 1;
              state_r     <= S_RD_ENTRY;
            end else begin
              state_r <= S_DONE;
            end
          end
        end

        S_DONE: begin
          state_r <= S_IDLE;
        end

        default: state_r <= S_IDLE;
      endcase
    end
  end

endmodule
