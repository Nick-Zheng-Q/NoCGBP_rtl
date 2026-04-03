module control_unit (
    input logic clk_i,
    input logic reset_i,
    control_dispatch_if.master control_dispatch_if,
    control_compute_if.master control_compute_if,
    stream_control_if.slave stream_control_if_read,
    stream_control_if.slave stream_control_if_write
);

  import gbp_pkg::*;

  typedef enum logic [1:0] {
    S_IDLE,
    S_ISSUE_META,
    S_ISSUE_FOLLOWUP
  } state_e;

  state_e state_r, state_n;
  logic [1:0] followup_idx_r, followup_idx_n;
  logic meta_issued_r, meta_issued_n;
  logic meta_parsed_r, meta_parsed_n;
  logic compute_running_r, compute_running_n;
  logic compute_cmd_pending_r, compute_cmd_pending_n;
  logic [SPM_ADDR_W-1:0] state_addr_r, state_addr_n;
  logic [SPM_ADDR_W-1:0] message_addr_r, message_addr_n;
  logic [XFER_BYTES_W-1:0] state_xfer_bytes_r, state_xfer_bytes_n;
  logic [XFER_BYTES_W-1:0] message_xfer_bytes_r, message_xfer_bytes_n;
  logic [STEP_BYTES_W-1:0] state_step_bytes_r, state_step_bytes_n;
  logic [STEP_BYTES_W-1:0] message_step_bytes_r, message_step_bytes_n;
  logic [TXN_ID_W-1:0] meta_txn_id_r, meta_txn_id_n;
  logic dispatch_ready_r;
  logic dispatch_ready_rise_r;

  // Address map defaults (can be tuned later)
  localparam logic [SPM_ADDR_W-1:0] META_BASE_ADDR_LP    = 'h00000;  // B0
  localparam int unsigned ROW_BYTES_LG_LP                = BYTE_OFF_W + WORD_OFF_W;

  localparam logic [XFER_BYTES_W-1:0] META_XFER_BYTES_LP     = XFER_BYTES_W'(BEAT_BYTES);
  localparam logic [XFER_BYTES_W-1:0] FOLLOWUP_XFER_BYTES_LP = XFER_BYTES_W'(BEAT_BYTES);
  localparam logic [STEP_BYTES_W-1:0] STEP_BYTES_LP          = STEP_BYTES_W'(BEAT_BYTES);

  logic [31:0] meta_word0_lo;
  logic [31:0] meta_word1_lo;
  logic [31:0] meta_word2_lo;
  logic [31:0] meta_word3_lo;
  logic [31:0] meta_word4_lo;
  logic [BANK_ID_W-1:0] state_bank_hint_lo;
  logic [BANK_ID_W-1:0] message_bank_hint_lo;

  function automatic logic [31:0] meta_word(
      input logic [BEAT_BITS-1:0] meta_payload,
      input int unsigned word_idx
  );
    begin
      meta_word = meta_payload[word_idx*32 +: 32];
    end
  endfunction

  function automatic logic [SPM_ADDR_W-1:0] force_state_bank(
      input logic [SPM_ADDR_W-1:0] base_addr,
      input logic [BANK_ID_W-1:0] bank_hint
  );
    logic [SPM_ADDR_W-1:0] tmp;
    logic [BANK_ID_W-1:0] mapped_bank;
    begin
      tmp = base_addr;
      unique case (bank_hint)
        3'd1: mapped_bank = 3'd2;
        3'd2: mapped_bank = 3'd3;
        default: mapped_bank = 3'd1;
      endcase
      tmp[ROW_BYTES_LG_LP +: BANK_ID_W] = mapped_bank;
      force_state_bank = tmp;
    end
  endfunction

  function automatic logic [SPM_ADDR_W-1:0] force_message_bank(
      input logic [SPM_ADDR_W-1:0] base_addr,
      input logic [BANK_ID_W-1:0] bank_hint
  );
    logic [SPM_ADDR_W-1:0] tmp;
    logic [BANK_ID_W-1:0] mapped_bank;
    begin
      tmp = base_addr;
      unique case (bank_hint[1:0])
        2'd1: mapped_bank = 3'd5;
        2'd2: mapped_bank = 3'd6;
        2'd3: mapped_bank = 3'd7;
        default: mapped_bank = 3'd4;
      endcase
      tmp[ROW_BYTES_LG_LP +: BANK_ID_W] = mapped_bank;
      force_message_bank = tmp;
    end
  endfunction

  logic issue_hs;
  assign issue_hs = control_dispatch_if.valid & control_dispatch_if.ready;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= S_IDLE;
      followup_idx_r <= '0;
      meta_issued_r <= 1'b0;
      meta_parsed_r <= 1'b0;
      compute_running_r <= 1'b0;
      compute_cmd_pending_r <= 1'b0;
      state_addr_r <= '0;
      message_addr_r <= '0;
      state_xfer_bytes_r <= '0;
      message_xfer_bytes_r <= '0;
      state_step_bytes_r <= '0;
      message_step_bytes_r <= '0;
      meta_txn_id_r <= '0;
      dispatch_ready_r <= 1'b0;
      dispatch_ready_rise_r <= 1'b0;
    end else begin
      state_r <= state_n;
      followup_idx_r <= followup_idx_n;
      meta_issued_r <= meta_issued_n;
      meta_parsed_r <= meta_parsed_n;
      compute_running_r <= compute_running_n;
      compute_cmd_pending_r <= compute_cmd_pending_n;
      state_addr_r <= state_addr_n;
      message_addr_r <= message_addr_n;
      state_xfer_bytes_r <= state_xfer_bytes_n;
      message_xfer_bytes_r <= message_xfer_bytes_n;
      state_step_bytes_r <= state_step_bytes_n;
      message_step_bytes_r <= message_step_bytes_n;
      meta_txn_id_r <= meta_txn_id_n;
      dispatch_ready_rise_r <= control_dispatch_if.ready & ~dispatch_ready_r;
      dispatch_ready_r <= control_dispatch_if.ready;
    end
  end

  always_comb begin
    state_n = state_r;
    followup_idx_n = followup_idx_r;
    meta_issued_n = meta_issued_r;
    meta_parsed_n = meta_parsed_r;
    compute_running_n = compute_running_r;
    compute_cmd_pending_n = compute_cmd_pending_r;
    state_addr_n = state_addr_r;
    message_addr_n = message_addr_r;
    state_xfer_bytes_n = state_xfer_bytes_r;
    message_xfer_bytes_n = message_xfer_bytes_r;
    state_step_bytes_n = state_step_bytes_r;
    message_step_bytes_n = message_step_bytes_r;
    meta_txn_id_n = meta_txn_id_r;

    meta_word0_lo = meta_word(stream_control_if_read.meta_data, 0);
    meta_word1_lo = meta_word(stream_control_if_read.meta_data, 1);
    meta_word2_lo = meta_word(stream_control_if_read.meta_data, 2);
    meta_word3_lo = meta_word(stream_control_if_read.meta_data, 3);
    meta_word4_lo = meta_word(stream_control_if_read.meta_data, 4);
    state_bank_hint_lo = meta_word1_lo[11:9];
    message_bank_hint_lo = meta_word2_lo[11:9];

    control_dispatch_if.valid = 1'b0;
    control_dispatch_if.mode = STREAM_META;
    control_dispatch_if.node_address = '0;
    control_dispatch_if.xfer_bytes = '0;
    control_dispatch_if.addr_step_bytes = '0;

    control_compute_if.start = compute_cmd_pending_r;
    control_compute_if.mode = 1'b0;
    control_compute_if.cmd_valid = compute_cmd_pending_r;
    control_compute_if.cmd_kind = 1'b0;
    control_compute_if.cmd_node_idx = '0;
    control_compute_if.cmd_iter0 = 1'b0;
    control_compute_if.cmd_dofs = 3'd2;
    control_compute_if.cmd_adj_count = 4'd1;
    control_compute_if.cmd_msg_count = 4'd1;
    control_compute_if.cmd_txn_id = meta_txn_id_r;
    control_compute_if.cmd_wr_addr = state_addr_r;
    control_compute_if.cmd_wr_xfer_bytes = state_xfer_bytes_r;

    unique case (state_r)
      S_IDLE: begin
        if (compute_cmd_pending_r && dispatch_ready_rise_r) begin
          control_dispatch_if.valid = 1'b1;
          control_dispatch_if.mode = STREAM_MESSAGE;
          $display("CTRL_UNIT_DBG IDLE: issuing compute cmd");
        end

        if (compute_cmd_pending_r && control_compute_if.cmd_ready) begin
          compute_cmd_pending_n = 1'b0;
          compute_running_n = 1'b1;
          $display("CTRL_UNIT_DBG IDLE: compute cmd accepted, running=1");
        end

        if (control_compute_if.rsp_done) begin
          compute_running_n = 1'b0;
          $display("CTRL_UNIT_DBG IDLE: compute rsp_done");
        end

        if ((!compute_running_n && !compute_cmd_pending_n) || control_compute_if.rsp_done) begin
          state_n = S_ISSUE_META;
          followup_idx_n = '0;
          meta_issued_n = 1'b0;
          meta_parsed_n = 1'b0;
        end
      end

      S_ISSUE_META: begin
        if (!meta_issued_r) begin
          control_dispatch_if.valid = 1'b1;
          control_dispatch_if.mode = STREAM_META;
          control_dispatch_if.node_address = META_BASE_ADDR_LP;
          control_dispatch_if.xfer_bytes = META_XFER_BYTES_LP;
          control_dispatch_if.addr_step_bytes = STEP_BYTES_LP;
          if (issue_hs) begin
            meta_issued_n = 1'b1;
          end
        end

        if (meta_issued_r && stream_control_if_read.meta_valid && !meta_parsed_r) begin
          meta_txn_id_n = meta_word0_lo[7:0];
          state_addr_n = force_state_bank(meta_word1_lo[31:12], state_bank_hint_lo);
          message_addr_n = force_message_bank(meta_word2_lo[31:12], message_bank_hint_lo);
          state_xfer_bytes_n = meta_word3_lo[31:16];
          message_xfer_bytes_n = meta_word3_lo[15:0];
          state_step_bytes_n = meta_word4_lo[31:24];
          message_step_bytes_n = meta_word4_lo[23:16];
          meta_parsed_n = 1'b1;
          // Debug
          $display("CTRL_UNIT_DBG META_PARSE: txn_id=%h state_addr=%h msg_addr=%h state_xfer=%d msg_xfer=%d",
                   meta_word0_lo[7:0], state_addr_n, message_addr_n, state_xfer_bytes_n, message_xfer_bytes_n);
        end

        if (meta_parsed_r) begin
          state_n = S_ISSUE_FOLLOWUP;
          followup_idx_n = '0;
        end
      end

      S_ISSUE_FOLLOWUP: begin
        control_dispatch_if.valid = 1'b1;
        control_dispatch_if.mode = STREAM_VEC;
        control_dispatch_if.xfer_bytes = FOLLOWUP_XFER_BYTES_LP;
        control_dispatch_if.addr_step_bytes = STEP_BYTES_LP;

        if (followup_idx_r == 2'd0) begin
          control_dispatch_if.mode = STREAM_VEC;
          control_dispatch_if.node_address = state_addr_r;
          control_dispatch_if.xfer_bytes = state_xfer_bytes_r;
          control_dispatch_if.addr_step_bytes = state_step_bytes_r;
        end else begin
          control_dispatch_if.mode = STREAM_MESSAGE;
          control_dispatch_if.node_address = message_addr_r;
          control_dispatch_if.xfer_bytes = message_xfer_bytes_r;
          control_dispatch_if.addr_step_bytes = message_step_bytes_r;
        end

        if (issue_hs) begin
          if (followup_idx_r == 2'd0) begin
            followup_idx_n = 2'd1;
            $display("CTRL_UNIT_DBG FOLLOWUP: state read done, starting message read");
          end else begin
            compute_cmd_pending_n = 1'b1;
            state_n = S_IDLE;
            followup_idx_n = '0;
            $display("CTRL_UNIT_DBG FOLLOWUP: message read done, cmd_pending=1");
          end
        end
      end

      default: begin
        state_n = S_IDLE;
      end
    endcase
  end

endmodule
