// control_unit_gbp.sv
// Extended control_unit with full GBP algorithm support
// Supports both Variable Node and Factor Node computation flows

import gbp_pkg::*;

module control_unit_gbp (
    input wire clk_i,
    input wire reset_i,
    control_dispatch_if.master control_dispatch_if,
    control_compute_if.master control_compute_if,
    stream_control_if.slave stream_control_if_read,
    stream_control_if.slave stream_control_if_write,
    output logic [4:0] debug_state_o,
    output logic debug_compute_pending_o,
    output logic debug_compute_running_o,
    output logic meta_consume_o,
    input wire meta_valid_i,  // Explicit meta_valid input to work around Verilator interface issue
    input wire [BEAT_BITS-1:0] meta_data_i,
    input wire [15:0] meta_seq_i
);

  // Use explicit meta_valid input (workaround for Verilator interface issue)
  logic meta_valid_sampled;
  assign meta_valid_sampled = meta_valid_i;
  
  // ========================================================================
  // State definitions
  // ========================================================================
  
  typedef enum logic [4:0] {
    // Common states
    S_IDLE,
    
    // Variable Node states
    S_VAR_READ_STATE,
    S_VAR_READ_MESSAGES,
    S_VAR_COMPUTE,
    S_VAR_WAIT_DONE,
    
    // Factor Node states  
    S_FAC_READ_FACTOR,
    S_FAC_READ_BELIEFS,
    S_FAC_LOOP_INIT,
    S_FAC_CAVITY_ACCUM,
    S_FAC_COMPUTE_MESSAGE,
    S_FAC_WAIT_DONE,
    
    S_DONE
  } state_e;

  state_e state_r, state_n;
  
  // ========================================================================
  // Registers
  // ========================================================================
  
  // META tracking flags (key: track META state without separate state)
  logic meta_issued_r, meta_issued_n;
  logic meta_parsed_r, meta_parsed_n;
  
  // Node configuration from meta
  logic is_factor_r, is_factor_n;
  logic [2:0] dofs_r, dofs_n;
  logic [3:0] adj_count_r, adj_count_n;
  logic [3:0] current_adj_r, current_adj_n;
  
  // Address tracking
  logic [SPM_ADDR_W-1:0] state_addr_r, state_addr_n;
  logic [SPM_ADDR_W-1:0] message_addr_r, message_addr_n;
  logic [SPM_ADDR_W-1:0] factor_addr_r, factor_addr_n;
  logic [XFER_BYTES_W-1:0] state_xfer_bytes_r, state_xfer_bytes_n;
  logic [XFER_BYTES_W-1:0] message_xfer_bytes_r, message_xfer_bytes_n;
  logic scheduler_header_loaded_r, scheduler_header_loaded_n;
  logic phase_r, phase_n;
  logic [ROW_ADDR_W-1:0] var_row_count_r, var_row_count_n;
  logic [ROW_ADDR_W-1:0] fac_row_count_r, fac_row_count_n;
  logic [ROW_ADDR_W-1:0] rr_ptr_var_r, rr_ptr_var_n;
  logic [ROW_ADDR_W-1:0] rr_ptr_fac_r, rr_ptr_fac_n;
  logic [ROW_ADDR_W-1:0] phase_visit_count_r, phase_visit_count_n;
  logic [31:0] var_cmd_accept_count_r, var_cmd_accept_count_n;
  logic [31:0] fac_cmd_accept_count_r, fac_cmd_accept_count_n;
  logic [31:0] phase_flip_count_r, phase_flip_count_n;
  logic [31:0] epoch_count_r, epoch_count_n;
  
  // Transaction ID
  logic [TXN_ID_W-1:0] txn_id_r, txn_id_n;
  
  // Control flags
  logic compute_pending_r, compute_pending_n;
  logic compute_running_r, compute_running_n;
  
  // Counters
  logic [3:0] msg_read_cnt_r, msg_read_cnt_n;
  logic [3:0] cavity_accum_cnt_r, cavity_accum_cnt_n;
  logic [ROW_ADDR_W-1:0] current_meta_row_r, current_meta_row_n;
  logic scan_done_r, scan_done_n;
  logic [15:0] last_meta_seq_r, last_meta_seq_n;
  logic input_issue_done_r, input_issue_done_n;
  
  // Issue_hs captured at clock edge to avoid glitches
  logic issue_hs_r;
  // cmd_valid register to drive interface
  logic cmd_valid_r;

  // ========================================================================
  // Helper function: force message to correct bank (4-7)
  // ========================================================================
  
  function automatic logic [SPM_ADDR_W-1:0] force_message_bank(
      input logic [SPM_ADDR_W-1:0] base_addr,
      input logic [1:0] msg_slot
  );
    logic [SPM_ADDR_W-1:0] tmp;
    logic [BANK_ID_W-1:0] mapped_bank;
    begin
      tmp = base_addr;
      unique case (msg_slot)
        2'd0: mapped_bank = 3'd4;
        2'd1: mapped_bank = 3'd5;
        2'd2: mapped_bank = 3'd6;
        2'd3: mapped_bank = 3'd7;
        default: mapped_bank = 3'd4;
      endcase
      tmp[(BYTE_OFF_W + WORD_OFF_W) +: BANK_ID_W] = mapped_bank;
      force_message_bank = tmp;
    end
  endfunction

  function automatic logic [SPM_ADDR_W-1:0] apply_bank_hint(
      input logic [SPM_ADDR_W-1:0] base_addr,
      input logic [BANK_ID_W-1:0] bank_hint,
      input logic [BANK_ID_W-1:0] default_bank
  );
    logic [SPM_ADDR_W-1:0] tmp;
    begin
      tmp = base_addr;
      if (bank_hint == '0) begin
        tmp[(BYTE_OFF_W + WORD_OFF_W) +: BANK_ID_W] = default_bank;
      end else begin
        tmp[(BYTE_OFF_W + WORD_OFF_W) +: BANK_ID_W] = bank_hint;
      end
      apply_bank_hint = tmp;
    end
  endfunction

  function automatic logic [SPM_ADDR_W-1:0] advance_message_addr(
      input logic [SPM_ADDR_W-1:0] base_addr,
      input logic [3:0] msg_index,
      input logic [XFER_BYTES_W-1:0] xfer_bytes
  );
    logic [SPM_ADDR_W-1:0] tmp;
    logic [ROW_ADDR_W-1:0] base_row;
    logic [ROW_ADDR_W-1:0] row_stride;
    begin
      tmp = base_addr;
      tmp[(BYTE_OFF_W + WORD_OFF_W) +: BANK_ID_W] = 3'd4;
      base_row = tmp[(BYTE_OFF_W + WORD_OFF_W + BANK_ID_W) +: ROW_ADDR_W];
      row_stride = ROW_ADDR_W'((xfer_bytes + XFER_BYTES_W'(BEAT_BYTES - 1)) >> ROW_BYTES_LG);
      tmp[(BYTE_OFF_W + WORD_OFF_W + BANK_ID_W) +: ROW_ADDR_W] = base_row + (ROW_ADDR_W'(msg_index) * row_stride);
      advance_message_addr = tmp;
    end
  endfunction

  function automatic logic [SPM_ADDR_W-1:0] meta_row_addr(
      input logic [ROW_ADDR_W-1:0] row
  );
    begin
      meta_row_addr = SPM_ADDR_W'(row) << (BYTE_OFF_W + WORD_OFF_W + BANK_ID_W);
    end
  endfunction

  function automatic logic select_initial_phase(
      input logic [ROW_ADDR_W-1:0] var_rows,
      input logic [ROW_ADDR_W-1:0] fac_rows
  );
    begin
      if (fac_rows != '0) begin
        select_initial_phase = 1'b1;
      end else begin
        select_initial_phase = 1'b0;
      end
    end
  endfunction

  function automatic logic [ROW_ADDR_W-1:0] select_meta_row(
      input logic phase_sel,
      input logic [ROW_ADDR_W-1:0] var_rows,
      input logic [ROW_ADDR_W-1:0] rr_var,
      input logic [ROW_ADDR_W-1:0] fac_rows,
      input logic [ROW_ADDR_W-1:0] rr_fac
  );
    logic [ROW_ADDR_W-1:0] row;
    begin
      row = ROW_ADDR_W'(1);
      if (phase_sel) begin
        row = ROW_ADDR_W'(1) + var_rows + rr_fac;
      end else begin
        row = ROW_ADDR_W'(1) + rr_var;
      end
      select_meta_row = row;
    end
  endfunction

  // ========================================================================
  // Constants
  // ========================================================================
  
  localparam logic [SPM_ADDR_W-1:0] META_BASE_ADDR_LP = 'h00000;
  localparam logic [XFER_BYTES_W-1:0] META_XFER_BYTES_LP = XFER_BYTES_W'(BEAT_BYTES);
  localparam int unsigned ROW_BYTES_LG = BYTE_OFF_W + WORD_OFF_W;
  localparam logic [STEP_BYTES_W-1:0] SPM_ROW_STEP_BYTES_LP =
      STEP_BYTES_W'(1 << (BYTE_OFF_W + WORD_OFF_W + BANK_ID_W));

  // ========================================================================
  // Logic
  // ========================================================================
  
  logic issue_hs;
  assign issue_hs = control_dispatch_if.valid & control_dispatch_if.ready;

  // Meta word extraction
  logic [31:0] meta_word0, meta_word1, meta_word2, meta_word3, meta_word4;
  logic [BANK_ID_W-1:0] state_bank_hint_lo, message_bank_hint_lo;
  logic meta_entry_valid_lo;
  
  function automatic logic [31:0] get_meta_word(
      input logic [BEAT_BITS-1:0] meta_data,
      input int unsigned word_idx
  );
    return meta_data[word_idx*32 +: 32];
  endfunction
  
  assign meta_word0 = get_meta_word(meta_data_i, 0);
  assign meta_word1 = get_meta_word(meta_data_i, 1);
  assign meta_word2 = get_meta_word(meta_data_i, 2);
  assign meta_word3 = get_meta_word(meta_data_i, 3);
  assign meta_word4 = get_meta_word(meta_data_i, 4);
  assign state_bank_hint_lo = meta_word1[11:9];
  assign message_bank_hint_lo = meta_word2[11:9];
  assign meta_entry_valid_lo = (meta_word0[15:0] != 16'd0) && (meta_word3 != 32'd0);
  
  always @(*) begin
    // Default: hold state
    state_n = state_r;
    is_factor_n = is_factor_r;
    dofs_n = dofs_r;
    adj_count_n = adj_count_r;
    current_adj_n = current_adj_r;
    state_addr_n = state_addr_r;
    message_addr_n = message_addr_r;
    factor_addr_n = factor_addr_r;
    state_xfer_bytes_n = state_xfer_bytes_r;
    message_xfer_bytes_n = message_xfer_bytes_r;
    scheduler_header_loaded_n = scheduler_header_loaded_r;
    phase_n = phase_r;
    var_row_count_n = var_row_count_r;
    fac_row_count_n = fac_row_count_r;
    rr_ptr_var_n = rr_ptr_var_r;
    rr_ptr_fac_n = rr_ptr_fac_r;
    phase_visit_count_n = phase_visit_count_r;
    var_cmd_accept_count_n = var_cmd_accept_count_r;
    fac_cmd_accept_count_n = fac_cmd_accept_count_r;
    phase_flip_count_n = phase_flip_count_r;
    epoch_count_n = epoch_count_r;
    txn_id_n = txn_id_r;
    compute_pending_n = compute_pending_r;
    compute_running_n = compute_running_r;
    msg_read_cnt_n = msg_read_cnt_r;
    cavity_accum_cnt_n = cavity_accum_cnt_r;
    current_meta_row_n = current_meta_row_r;
    scan_done_n = scan_done_r;
    last_meta_seq_n = last_meta_seq_r;
    // meta_issued_n will be computed after state machine to ensure correct ordering
    meta_parsed_n = meta_parsed_r;
    
    // Default interface outputs
    control_dispatch_if.valid = 1'b0;
    control_dispatch_if.mode = STREAM_META;
    control_dispatch_if.node_address = '0;
    control_dispatch_if.xfer_bytes = '0;
    control_dispatch_if.addr_step_bytes = STEP_BYTES_W'(BEAT_BYTES);
    control_dispatch_if.write = 1'b0;
    meta_consume_o = 1'b0;
    
    control_compute_if.start = 1'b0;
    control_compute_if.mode = 1'b0;
    // cmd_valid is now driven by register, assigned below
    control_compute_if.cmd_kind = is_factor_r;
    control_compute_if.cmd_node_idx = {9'd0, current_adj_r};
    control_compute_if.cmd_iter0 = (current_adj_r == 4'd0);
    control_compute_if.cmd_dofs = dofs_r;
    control_compute_if.cmd_adj_count = adj_count_r;
    control_compute_if.cmd_msg_count = adj_count_r;
    control_compute_if.cmd_txn_id = txn_id_r;
    control_compute_if.cmd_wr_addr = is_factor_r ? message_addr_r : state_addr_r;
    control_compute_if.cmd_wr_xfer_bytes = is_factor_r ? message_xfer_bytes_r : state_xfer_bytes_r;
    
    // State machine
    case (state_r)
      // ====================================================================
      // IDLE: Wait for new transaction trigger
      // ====================================================================
      S_IDLE: begin
        // Issue META read if not yet issued for this transaction (only when not in reset)
        if (!reset_i && !scan_done_r && !compute_running_r && !compute_pending_r && !meta_issued_r) begin
          control_dispatch_if.valid = 1'b1;
          control_dispatch_if.mode = STREAM_META;
          control_dispatch_if.node_address = META_BASE_ADDR_LP | meta_row_addr(current_meta_row_r);
          control_dispatch_if.xfer_bytes = META_XFER_BYTES_LP;
          
          if (issue_hs) begin
            meta_issued_n = 1'b1;
          end
        end
        
        // Parse META data when available (can happen same cycle as issue)
        if (meta_issued_r && meta_valid_sampled && !meta_parsed_r && (meta_seq_i != last_meta_seq_r)) begin
          meta_consume_o = 1'b1;
          last_meta_seq_n = meta_seq_i;
          if (!scheduler_header_loaded_r) begin
            scheduler_header_loaded_n = 1'b1;
            var_row_count_n = meta_word0[15:0];
            fac_row_count_n = meta_word0[31:16];
            rr_ptr_var_n = '0;
            rr_ptr_fac_n = '0;
            phase_visit_count_n = '0;
            phase_n = select_initial_phase(meta_word0[15:0], meta_word0[31:16]);
            if ((meta_word0[15:0] == 16'd0) && (meta_word0[31:16] == 16'd0)) begin
              scan_done_n = 1'b1;
              current_meta_row_n = '0;
            end else begin
              scan_done_n = 1'b0;
              current_meta_row_n = select_meta_row(
                  phase_n,
                  meta_word0[15:0],
                  '0,
                  meta_word0[31:16],
                  '0);
            end
            meta_issued_n = 1'b0;
            meta_parsed_n = 1'b0;
            state_n = S_IDLE;
          end else if (!meta_entry_valid_lo) begin
            $display("CTRL_SCAN_STOP %m row=%0d meta0=%08x meta1=%08x meta2=%08x meta3=%08x seq=%0d",
                     current_meta_row_r, meta_word0, meta_word1, meta_word2, meta_word3, meta_seq_i);
            scan_done_n = 1'b1;
            meta_issued_n = 1'b0;
            meta_parsed_n = 1'b0;
            state_n = S_IDLE;
          end else begin
            $display("CTRL_META_PARSE %m row=%0d meta0=%08x meta1=%08x meta2=%08x meta3=%08x seq=%0d",
                     current_meta_row_r, meta_word0, meta_word1, meta_word2, meta_word3, meta_seq_i);
            // Parse meta words
            txn_id_n = meta_word0[7:0];
            is_factor_n = meta_word0[8];
            dofs_n = meta_word0[11:9];
	            adj_count_n = meta_word0[15:12];
	            
	            // row0 为 scheduler header；普通节点从 row1 开始，word1 由 bank hint 指向真实 payload bank。
	            state_addr_n = apply_bank_hint(meta_word1[31:12], state_bank_hint_lo, 3'd3);
	            message_addr_n = advance_message_addr(meta_word2[31:12], 4'd0, meta_word3[15:0]);
	            factor_addr_n = apply_bank_hint(meta_word1[31:12], state_bank_hint_lo, 3'd1);
	            
	            state_xfer_bytes_n = meta_word3[31:16];
	            message_xfer_bytes_n = meta_word3[15:0];
            
            // Initialize counters
            msg_read_cnt_n = 4'd0;
            cavity_accum_cnt_n = 4'd0;
            current_adj_n = 4'd0;
            input_issue_done_n = 1'b0;
            
            meta_parsed_n = 1'b1;
            
            // Branch to appropriate path
            if (is_factor_n) begin
              state_n = S_FAC_READ_FACTOR;
            end else begin
              state_n = S_VAR_READ_STATE;
            end
          end
        end
        
        // Handle compute completion
        if (control_compute_if.rsp_done) begin
          compute_running_n = 1'b0;
        end
      end
      
      // ====================================================================
      // VARIABLE NODE PATH
      // ====================================================================
      
      S_VAR_READ_STATE: begin
        control_dispatch_if.valid = 1'b1;
        control_dispatch_if.mode = STREAM_VEC;
        control_dispatch_if.node_address = state_addr_r;
        control_dispatch_if.xfer_bytes = state_xfer_bytes_r;
        control_dispatch_if.addr_step_bytes = SPM_ROW_STEP_BYTES_LP;
        
        if (issue_hs) begin
          compute_pending_n = 1'b1;
          if (adj_count_r == 4'd0) begin
            state_n = S_VAR_COMPUTE;
            msg_read_cnt_n = 4'd0;
            input_issue_done_n = 1'b1;
          end else begin
            state_n = S_VAR_COMPUTE;
            msg_read_cnt_n = 4'd0;
            input_issue_done_n = 1'b0;
          end
        end
      end
      
      S_VAR_READ_MESSAGES: begin
        control_dispatch_if.valid = 1'b1;
        control_dispatch_if.mode = STREAM_MESSAGE;
        control_dispatch_if.node_address = advance_message_addr(message_addr_r, msg_read_cnt_r, message_xfer_bytes_r);
        control_dispatch_if.xfer_bytes = message_xfer_bytes_r;
        control_dispatch_if.addr_step_bytes = SPM_ROW_STEP_BYTES_LP;
        if (issue_hs) begin
          $display("CTRL_MSG_ISSUE %m row=%0d msg_idx=%0d addr=%h xfer=%0d adj=%0d",
                   current_meta_row_r, msg_read_cnt_r,
                   advance_message_addr(message_addr_r, msg_read_cnt_r, message_xfer_bytes_r),
                   message_xfer_bytes_r, adj_count_r);
          if (msg_read_cnt_r == adj_count_r - 4'd1) begin
            input_issue_done_n = 1'b1;
            state_n = compute_running_r ? S_VAR_WAIT_DONE : S_VAR_READ_MESSAGES;
          end
        end
      end
      
      S_VAR_COMPUTE: begin
        control_compute_if.start = 1'b1;
        control_compute_if.mode = 1'b0;
        
        if (control_compute_if.cmd_ready) begin
          compute_pending_n = 1'b0;
          compute_running_n = 1'b1;
          var_cmd_accept_count_n = var_cmd_accept_count_r + 32'd1;
          if (adj_count_r == 4'd0) begin
            state_n = S_VAR_WAIT_DONE;
          end else begin
            state_n = S_VAR_READ_MESSAGES;
          end
        end
      end
      
      S_VAR_WAIT_DONE: begin
        if (control_compute_if.rsp_done) begin
          compute_running_n = 1'b0;
          state_n = S_DONE;
        end
      end
      
      // ====================================================================
      // FACTOR NODE PATH
      // ====================================================================
      
      S_FAC_READ_FACTOR: begin
        control_dispatch_if.valid = 1'b1;
        control_dispatch_if.mode = STREAM_VEC;
        control_dispatch_if.node_address = factor_addr_r;
        control_dispatch_if.xfer_bytes = state_xfer_bytes_r;
        control_dispatch_if.addr_step_bytes = SPM_ROW_STEP_BYTES_LP;
        
        if (issue_hs) begin
          compute_pending_n = 1'b1;
          if (adj_count_r == 4'd0) begin
            input_issue_done_n = 1'b1;
            state_n = S_FAC_COMPUTE_MESSAGE;
          end else begin
            input_issue_done_n = 1'b0;
            state_n = S_FAC_COMPUTE_MESSAGE;
          end
          msg_read_cnt_n = 4'd0;
        end
      end
      
      S_FAC_READ_BELIEFS: begin
        control_dispatch_if.valid = 1'b1;
        control_dispatch_if.mode = STREAM_MESSAGE;
        control_dispatch_if.node_address = force_message_bank(message_addr_r, msg_read_cnt_r[1:0]);
        control_dispatch_if.xfer_bytes = message_xfer_bytes_r;
        control_dispatch_if.addr_step_bytes = SPM_ROW_STEP_BYTES_LP;
        
        if (issue_hs) begin
          $display("CTRL_FAC_MSG_ISSUE %m row=%0d msg_idx=%0d addr=%h xfer=%0d adj=%0d",
                   current_meta_row_r, msg_read_cnt_r,
                   force_message_bank(message_addr_r, msg_read_cnt_r[1:0]),
                   message_xfer_bytes_r, adj_count_r);
          if (msg_read_cnt_r == adj_count_r - 4'd1) begin
            input_issue_done_n = 1'b1;
            state_n = compute_running_r ? S_FAC_WAIT_DONE : S_FAC_READ_BELIEFS;
          end
        end
      end
      
      S_FAC_LOOP_INIT: begin
        current_adj_n = 4'd0;
        cavity_accum_cnt_n = 4'd0;
        state_n = S_FAC_CAVITY_ACCUM;
      end
      
      S_FAC_CAVITY_ACCUM: begin
        if (cavity_accum_cnt_r == adj_count_r - 4'd1) begin
          state_n = S_FAC_COMPUTE_MESSAGE;
          compute_pending_n = 1'b1;
        end else begin
          cavity_accum_cnt_n = cavity_accum_cnt_r + 4'd1;
        end
      end
      
      S_FAC_COMPUTE_MESSAGE: begin
        control_compute_if.start = 1'b1;
        control_compute_if.mode = 1'b1;
        
        if (control_compute_if.cmd_ready) begin
          compute_pending_n = 1'b0;
          compute_running_n = 1'b1;
          fac_cmd_accept_count_n = fac_cmd_accept_count_r + 32'd1;
          if (adj_count_r == 4'd0) begin
            state_n = S_FAC_WAIT_DONE;
          end else begin
            state_n = S_FAC_READ_BELIEFS;
          end
        end
      end
      
      S_FAC_WAIT_DONE: begin
        if (control_compute_if.rsp_done) begin
          compute_running_n = 1'b0;
          state_n = S_DONE;
        end
      end
      
      // ====================================================================
      // DONE: Transaction complete
      // ====================================================================
      S_DONE: begin
        meta_issued_n = 1'b0;
        meta_parsed_n = 1'b0;
        if (scheduler_header_loaded_r) begin
          if (is_factor_r) begin
            if ((fac_row_count_r != '0) && ((phase_visit_count_r + ROW_ADDR_W'(1)) >= fac_row_count_r)) begin
              rr_ptr_fac_n = '0;
              phase_visit_count_n = '0;
              if (var_row_count_r != '0) begin
                if (phase_r) begin
                  phase_flip_count_n = phase_flip_count_r + 32'd1;
                end
                phase_n = 1'b0;
              end
            end else begin
              rr_ptr_fac_n = rr_ptr_fac_r + ROW_ADDR_W'(1);
              phase_visit_count_n = phase_visit_count_r + ROW_ADDR_W'(1);
            end
          end else begin
            if ((var_row_count_r != '0) && ((phase_visit_count_r + ROW_ADDR_W'(1)) >= var_row_count_r)) begin
              rr_ptr_var_n = '0;
              phase_visit_count_n = '0;
              if (fac_row_count_r != '0) begin
                if (!phase_r) begin
                  phase_flip_count_n = phase_flip_count_r + 32'd1;
                end
                phase_n = 1'b1;
                epoch_count_n = epoch_count_r + 32'd1;
              end
            end else begin
              rr_ptr_var_n = rr_ptr_var_r + ROW_ADDR_W'(1);
              phase_visit_count_n = phase_visit_count_r + ROW_ADDR_W'(1);
            end
          end
          current_meta_row_n = select_meta_row(
              phase_n,
              var_row_count_r,
              rr_ptr_var_n,
              fac_row_count_r,
              rr_ptr_fac_n);
        end else begin
          current_meta_row_n = '0;
        end
        state_n = S_IDLE;
      end
      
      default: begin
        state_n = S_IDLE;
      end
    endcase
    
    // Compute meta_issued_n after state machine to ensure issue_hs is stable
    // Default: keep current value (clear in reset)
    meta_issued_n = reset_i ? 1'b0 : meta_issued_r;
    // In S_IDLE, set meta_issued when handshake completes (only when not in reset)
    if (!reset_i && state_r == S_IDLE && !compute_running_r && !compute_pending_r && !meta_issued_r) begin
      if (issue_hs) begin
        meta_issued_n = 1'b1;
      end
    end
    // In S_DONE, clear meta_issued
    if (!reset_i && state_r == S_DONE) begin
      meta_issued_n = 1'b0;
    end
    if (!reset_i
        && state_r == S_IDLE
        && !scheduler_header_loaded_r
        && meta_issued_r
        && meta_valid_sampled
        && !meta_parsed_r
        && (meta_seq_i != last_meta_seq_r)) begin
      meta_issued_n = 1'b0;
    end
    // 空 META 行会把 scan_done_n 拉高，这里必须保证 meta_issued 真正被清掉，
    // 否则控制器会卡在 meta_issued=1/meta_parsed=0 的悬空状态。
    if (!reset_i && scan_done_n) begin
      meta_issued_n = 1'b0;
    end
  end
  
  // Sequential logic
  // ========================================================================
  
  always @(posedge clk_i) begin
    if (reset_i) begin
      state_r <= S_IDLE;
      is_factor_r <= 1'b0;
      dofs_r <= 3'd2;
      adj_count_r <= 4'd0;
      current_adj_r <= 4'd0;
      state_addr_r <= '0;
      message_addr_r <= '0;
      factor_addr_r <= '0;
      state_xfer_bytes_r <= '0;
      message_xfer_bytes_r <= '0;
      scheduler_header_loaded_r <= 1'b0;
      phase_r <= 1'b0;
      var_row_count_r <= '0;
      fac_row_count_r <= '0;
      rr_ptr_var_r <= '0;
      rr_ptr_fac_r <= '0;
      phase_visit_count_r <= '0;
      var_cmd_accept_count_r <= '0;
      fac_cmd_accept_count_r <= '0;
      phase_flip_count_r <= '0;
      epoch_count_r <= '0;
      txn_id_r <= '0;
      compute_pending_r <= 1'b0;
      compute_running_r <= 1'b0;
      msg_read_cnt_r <= 4'd0;
      cavity_accum_cnt_r <= 4'd0;
      current_meta_row_r <= '0;
      scan_done_r <= 1'b0;
      last_meta_seq_r <= '0;
      input_issue_done_r <= 1'b0;
      meta_issued_r <= 1'b0;
      meta_parsed_r <= 1'b0;
      cmd_valid_r <= 1'b0;
      debug_compute_pending_o <= 1'b0;
    end else begin
      state_r <= state_n;
      is_factor_r <= is_factor_n;
      dofs_r <= dofs_n;
      adj_count_r <= adj_count_n;
      current_adj_r <= current_adj_n;
      state_addr_r <= state_addr_n;
      message_addr_r <= message_addr_n;
      factor_addr_r <= factor_addr_n;
      state_xfer_bytes_r <= state_xfer_bytes_n;
      message_xfer_bytes_r <= message_xfer_bytes_n;
      scheduler_header_loaded_r <= scheduler_header_loaded_n;
      phase_r <= phase_n;
      var_row_count_r <= var_row_count_n;
      fac_row_count_r <= fac_row_count_n;
      rr_ptr_var_r <= rr_ptr_var_n;
      rr_ptr_fac_r <= rr_ptr_fac_n;
      phase_visit_count_r <= phase_visit_count_n;
      var_cmd_accept_count_r <= var_cmd_accept_count_n;
      fac_cmd_accept_count_r <= fac_cmd_accept_count_n;
      phase_flip_count_r <= phase_flip_count_n;
      epoch_count_r <= epoch_count_n;
      txn_id_r <= txn_id_n;
      compute_pending_r <= compute_pending_n;
      compute_running_r <= compute_running_n;
      if ((state_r == S_VAR_READ_MESSAGES || state_r == S_FAC_READ_BELIEFS) && issue_hs) begin
        if (msg_read_cnt_r != adj_count_r - 4'd1)
          msg_read_cnt_r <= msg_read_cnt_r + 4'd1;
      end else if (state_r == S_VAR_READ_STATE || state_r == S_FAC_READ_FACTOR || state_r == S_DONE) begin
        msg_read_cnt_r <= 4'd0;
      end

      // Update cmd_valid_r
      cmd_valid_r <= compute_pending_r;
      debug_compute_pending_o <= cmd_valid_r;  // Direct assignment in sequential block
      cavity_accum_cnt_r <= cavity_accum_cnt_n;
      current_meta_row_r <= current_meta_row_n;
      scan_done_r <= scan_done_n;
      last_meta_seq_r <= last_meta_seq_n;
      input_issue_done_r <= input_issue_done_n;
      meta_issued_r <= meta_issued_n;
      meta_parsed_r <= meta_parsed_n;

    end
  end
  
  // ========================================================================
  // Debug outputs
  // ========================================================================
  
  // Force assignment via always block to work around Verilator optimization
  always @(*) begin
    control_compute_if.cmd_valid = cmd_valid_r;
  end
  
  // Removed separate always block for debugging
  
  assign debug_state_o = state_r[4:0];
  // debug_compute_pending_o is now assigned in sequential block
  assign debug_compute_running_o = compute_running_r;

endmodule
