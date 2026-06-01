// mic_write
// memory interface controller (write)

import gbp_pkg::*;
module mic_write
(
    input logic clk_i,
    input logic reset_i,

    input logic addr_valid_i,
    input logic [SPM_ADDR_W-1:0] addr_data_i,
    output logic addr_unqueue_o,

    input logic data_valid_i,
    input logic [BEAT_BITS-1:0] data_data_i,
    output logic data_unqueue_o,

    mic_spm_arbiter_wr_if.master mic_to_spm_arbiter
);

  typedef enum logic {
    IDLE,
    REQ_SEND
  } state_e;

  state_e state_r, state_n;
  logic [SPM_ADDR_W-1:0] addr_r;
  logic [BEAT_BITS-1:0] data_r;
  logic load_req;

  assign load_req = (state_r == IDLE) & addr_valid_i & data_valid_i;

  assign mic_to_spm_arbiter.spm_wr_req_valid = (state_r == REQ_SEND);
  assign mic_to_spm_arbiter.spm_wr_req_addr = addr_r;
  assign mic_to_spm_arbiter.spm_wr_req_data = data_r;
  assign mic_to_spm_arbiter.spm_wr_req_wstrb = '1;

  assign addr_unqueue_o = load_req;
  assign data_unqueue_o = load_req;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= IDLE;
      addr_r <= '0;
      data_r <= '0;
    end else begin
      state_r <= state_n;
      if (load_req) begin
        addr_r <= addr_data_i;
        data_r <= data_data_i;
      end
    end
  end

  always_comb begin
    state_n = state_r;
    case (state_r)
      IDLE: begin
        if (addr_valid_i & data_valid_i) begin
          state_n = REQ_SEND;
        end
      end
      REQ_SEND: begin
        if (mic_to_spm_arbiter.spm_wr_req_ready) begin
          state_n = IDLE;
        end
      end
      default: state_n = IDLE;
    endcase
  end

endmodule
