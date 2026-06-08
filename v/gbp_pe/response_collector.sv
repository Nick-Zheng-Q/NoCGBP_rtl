// response_collector.sv
// Receives FETCH_RESPONSE from NoC Adapter.
// Streams data to accumulator AND writes to STAGING SPM.
// Manages STAGING allocation and batch control.
// Assembles 32-bit NoC flits into 64-bit SPM beats.

module response_collector
  import gbp_pkg::*;
#(
    parameter int NODE_ID_W     = gbp_pkg::NODE_ID_W
    , parameter int STATE_WORDS_W = gbp_pkg::STATE_WORDS_W
    , parameter int TXN_ID_W    = gbp_pkg::TXN_ID_W
    , parameter int DATA_WIDTH  = gbp_pkg::NOC_DATA_W
    , parameter int SPM_ADDR_W  = gbp_pkg::SPM_ADDR_W
    , parameter int BEAT_WIDTH  = gbp_pkg::BEAT_BITS
) (
    input  logic clk_i
    , input  logic rst_n_i

    // Fetch response ingress (from NoC Adapter RX)
    , input  logic                     rx_fetch_resp_valid_i
    , output logic                     rx_fetch_resp_ready_o
    , input  logic                     rx_fetch_resp_is_factor_i
    , input  logic [STATE_WORDS_W-1:0] rx_fetch_resp_state_words_i
    , input  logic [DATA_WIDTH-1:0]    rx_fetch_resp_data_i
    , input  logic                     rx_fetch_resp_data_valid_i
    , input  logic                     rx_fetch_resp_last_i
    , input  logic                     rx_fetch_resp_done_valid_i
    , input  logic [TXN_ID_W-1:0]      rx_fetch_resp_txn_id_i
    , input  logic [NODE_ID_W-1:0]     rx_fetch_resp_node_id_i
    , input  logic [NODE_ID_W-1:0]     rx_fetch_resp_consumer_node_id_i

    // STAGING write port (to SPM Arbiter)
    , output logic                 staging_wr_valid_o
    , input  logic                 staging_wr_ready_i
    , output logic [SPM_ADDR_W-1:0] staging_wr_addr_o
    , output logic [BEAT_WIDTH-1:0] staging_wr_data_o
    , output logic [WSTRB_W-1:0]    staging_wr_wstrb_o

    // Remote neighbor state stream to Accumulator
    , output logic                 remote_valid_o
    , input  logic                 remote_ready_i
    , output logic [DATA_WIDTH-1:0] remote_data_o
    , output logic                 remote_last_o

    // STAGING Allocator coordination (from ScoreboardPrefetcher)
    , input  logic                     staging_reserve_valid_i
    , input  logic [STATE_WORDS_W-1:0] staging_reserve_words_i
    , output logic                     staging_reserve_ready_o

    // Batch control
    , output logic                     staging_batch_closed_o
    , input  logic                     staging_batch_done_i

    // To ScoreboardPrefetcher (completion notification)
    , output logic                 complete_valid_o
    , output logic [TXN_ID_W-1:0]  complete_txn_id_o
    , output logic [NODE_ID_W-1:0] complete_node_id_o
    , output logic [NODE_ID_W-1:0] complete_consumer_node_id_o
);

  // STAGING allocator state
  logic [SPM_ADDR_W-1:0] staging_bump_r;
  logic [SPM_ADDR_W-1:0] staging_reserved_r;
  logic [STATE_WORDS_W-1:0] batch_outstanding_r;

  // Transaction table (simplified: single entry for now)
  logic                     txn_valid_r;
  logic [SPM_ADDR_W-1:0]   txn_base_r;
  logic [STATE_WORDS_W-1:0] txn_words_r;
  logic [STATE_WORDS_W-1:0] txn_received_r;
  logic [TXN_ID_W-1:0]      txn_id_r;

  // Data write state
  logic writing_r;

  // 32 -> 64 beat assembly
  logic [DATA_WIDTH-1:0]    beat_hold_r;
  logic                     beat_pending_r;
  logic [SPM_ADDR_W-1:0]    beat_addr_r;
  logic                     beat_last_r; // flush pending on last

  // STAGING reservation: always accept if space available
  localparam STAGING_REGION_WORDS = 64;  // parameterize later
  wire [SPM_ADDR_W-1:0] reserve_words = SPM_ADDR_W'(staging_reserve_words_i);
  assign staging_reserve_ready_o = (staging_reserved_r + reserve_words <= STAGING_REGION_WORDS);

  // Batch closed when outstanding count reaches limit
  localparam OUTSTANDING_DEPTH = 8;
  assign staging_batch_closed_o = (batch_outstanding_r >= OUTSTANDING_DEPTH);

  logic rst_i;
  assign rst_i = ~rst_n_i;

  // Data pass-through to accumulator
  assign remote_valid_o = rx_fetch_resp_data_valid_i;
  assign remote_data_o  = rx_fetch_resp_data_i;
  assign remote_last_o  = rx_fetch_resp_last_i;

  // rx_ready: always ready
  assign rx_fetch_resp_ready_o = 1'b1;

  // Completion
  assign complete_valid_o = rx_fetch_resp_done_valid_i;
  assign complete_txn_id_o = rx_fetch_resp_txn_id_i;
  assign complete_node_id_o = rx_fetch_resp_node_id_i;
  assign complete_consumer_node_id_o = rx_fetch_resp_consumer_node_id_i;

  // STAGING write: output from beat assembly (registered)
  // beat_pending_r is set whenever a complete or final half-beat is ready.
  // Simplified: no backpressure on RX path; assumes SPM accepts before next beat.
  assign staging_wr_valid_o = beat_pending_r;
  assign staging_wr_data_o  = BEAT_WIDTH'(beat_hold_r);
  assign staging_wr_addr_o  = beat_addr_r;
  assign staging_wr_wstrb_o = beat_last_r ? WSTRB_W'(4'b1111) : {WSTRB_W{1'b1}};

  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      staging_bump_r <= '0;
      staging_reserved_r <= '0;
      batch_outstanding_r <= '0;
      txn_valid_r <= 1'b0;
      txn_base_r <= '0;
      txn_words_r <= '0;
      txn_received_r <= '0;
      txn_id_r <= '0;
      writing_r <= 1'b0;
      beat_hold_r <= '0;
      beat_pending_r <= 1'b0;
      beat_addr_r <= '0;
      beat_last_r <= 1'b0;
    end else begin
      // Clear pending when accepted
      if (staging_wr_valid_o && staging_wr_ready_i) begin
        beat_pending_r <= 1'b0;
        beat_last_r <= 1'b0;
      end

      // Handle reservation
      if (staging_reserve_valid_i && staging_reserve_ready_o) begin
        staging_reserved_r <= staging_reserved_r + reserve_words;
        batch_outstanding_r <= batch_outstanding_r + 1;
      end

      // Handle response metadata
      if (rx_fetch_resp_valid_i && !rx_fetch_resp_data_valid_i && !rx_fetch_resp_done_valid_i) begin
        // Metadata store: allocate STAGING block
        txn_valid_r <= 1'b1;
        txn_base_r <= staging_bump_r;
        txn_words_r <= rx_fetch_resp_state_words_i;
        txn_received_r <= '0;
        txn_id_r <= rx_fetch_resp_txn_id_i;
        staging_bump_r <= staging_bump_r + SPM_ADDR_W'((rx_fetch_resp_state_words_i + 1) >> 1); // beats = ceil(words/2)
        writing_r <= 1'b1;
      end

      // Handle data words
      if (rx_fetch_resp_data_valid_i && writing_r) begin
        if (txn_received_r[0] == 1'b0) begin
          // First word of 64-bit beat
          beat_hold_r <= rx_fetch_resp_data_i;
          beat_addr_r <= txn_base_r + SPM_ADDR_W'(txn_received_r >> 1);
        end else begin
          // Second word: assemble full beat and mark ready
          beat_hold_r <= {rx_fetch_resp_data_i, beat_hold_r};
          beat_pending_r <= 1'b1;
          beat_last_r <= rx_fetch_resp_last_i; // final beat if this is last word
        end

        txn_received_r <= txn_received_r + 1;

        if (rx_fetch_resp_last_i) begin
          writing_r <= 1'b0;
          if (txn_received_r[0] == 1'b0) begin
            // Odd word count: flush single 32-bit word in lower half
            beat_pending_r <= 1'b1;
            beat_last_r <= 1'b1;
          end
        end
      end

      // Handle done
      if (rx_fetch_resp_done_valid_i) begin
        txn_valid_r <= 1'b0;
        if (batch_outstanding_r > 0) begin
          batch_outstanding_r <= batch_outstanding_r - 1;
        end
      end

      // Handle batch done
      if (staging_batch_done_i) begin
        staging_bump_r <= '0;
        staging_reserved_r <= '0;
      end
    end
  end

endmodule
