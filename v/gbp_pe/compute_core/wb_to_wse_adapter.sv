// wb_to_wse_adapter.sv
// GBP Compute Core v0.7 — writeback format adapter
// Converts compute_unit_wrapper's writeback_record_t into the legacy
// write_stream_engine interface (descriptor + 32-bit words).

module wb_to_wse_adapter
  import gbp_pkg::*;
  import gbp_op_pkg::*;
#(
    parameter int SPM_ADDR_W = gbp_pkg::SPM_ADDR_W
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // Writeback record from compute_unit_wrapper
    input  logic               wb_valid_i,
    output logic               wb_ready_o,
    input  writeback_record_t  wb_i,

    // write_stream_engine descriptor
    output logic                desc_valid_o,
    input  logic                desc_ready_i,
    output logic [SPM_ADDR_W-1:0] desc_base_addr_o,
    output logic [15:0]         desc_word_count_o,

    // write_stream_engine data words
    output logic                word_valid_o,
    input  logic                word_ready_i,
    output logic [FP32_W-1:0]   word_data_o
);

  typedef enum logic [1:0] {
    W_IDLE,
    W_DESC,
    W_STREAM
  } state_e;

  state_e            state_r;
  logic [15:0]       word_cnt_r;
  writeback_record_t wb_r;

  assign desc_valid_o      = (state_r == W_DESC);
  assign desc_base_addr_o  = wb_r.addr;
  assign desc_word_count_o = wb_r.nwords;

  assign word_valid_o      = (state_r == W_STREAM) && (word_cnt_r < wb_r.nwords);
  assign word_data_o       = wb_r.payload[word_cnt_r];
  assign wb_ready_o        = (state_r == W_IDLE);

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      state_r    <= W_IDLE;
      word_cnt_r <= '0;
      wb_r.addr        <= '0;
      wb_r.nwords      <= '0;
      wb_r.kind        <= WB_MSG;
      wb_r.fail        <= 1'b0;
      wb_r.regularized <= 1'b0;
      wb_r.nan_guard   <= 1'b0;
    end else begin
      case (state_r)
        W_IDLE: begin
          if (wb_valid_i && wb_ready_o) begin
            wb_r    <= wb_i;
            state_r <= W_DESC;
          end
        end

        W_DESC: begin
          if (desc_valid_o && desc_ready_i) begin
            word_cnt_r <= '0;
            state_r    <= W_STREAM;
          end
        end

        W_STREAM: begin
          if (word_valid_o && word_ready_i) begin
            word_cnt_r <= word_cnt_r + 16'd1;
            if (word_cnt_r + 16'd1 == wb_r.nwords)
              state_r <= W_IDLE;
          end
        end

        default: state_r <= W_IDLE;
      endcase
    end
  end

endmodule
