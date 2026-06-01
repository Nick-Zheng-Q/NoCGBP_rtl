import gbp_pkg::*;
module pe_top
(
    input logic clk_i,
    input logic reset_i,

    input logic cmd_valid_i,
    input logic [1:0] cmd_kind_i,
    input logic [TXN_ID_W-1:0] cmd_txn_id_i,
    input logic force_persistence_stall_i,
    output logic cmd_ready_o,
    output logic rsp_done_o,
    output logic rsp_error_o,

    input logic ingress_wr_valid_i,
    input logic [SPM_ADDR_W-1:0] ingress_wr_addr_i,
    input logic [BEAT_BITS-1:0] ingress_wr_data_i,
    output logic ingress_wr_ready_o,

    output logic rd_req_valid_o,
    output logic [SPM_ADDR_W-1:0] rd_req_addr_o,
    output logic wr_req_valid_o,
    output logic [SPM_ADDR_W-1:0] wr_req_addr_o,
    output logic [31:0] wr_req_data_low_o,
    output logic ingress_wr_req_valid_o,
    output logic [SPM_ADDR_W-1:0] ingress_wr_req_addr_o,
    output logic [31:0] ingress_wr_req_data_low_o,
    output logic compute_start_o,
    output logic compute_done_o,
    output logic [TXN_ID_W-1:0] wr_txn_id_o,
    output logic [TXN_ID_W-1:0] cmd_txn_id_o
);

  control_dispatch_if control_dispatch_if0();
  control_compute_if control_compute_if0();
  stream_control_if stream_control_if_read0();
  stream_control_if stream_control_if_write0();

  stream_dispatcher_if stream_dispatcher_if_read0();
  stream_dispatcher_if stream_dispatcher_if_write_unused0();
  stream_dispatcher_if stream_dispatcher_if_write0();

  read_stream_if read_stream_if0();
  write_stream_if write_stream_if0();

  mic_spm_arbiter_if rd_if[2*NB]();
  mic_spm_arbiter_wr_if wr_if[2*NB]();
  


  logic ingress_addr_fifo_ready_lo;
  logic ingress_addr_fifo_valid_lo;
  logic [SPM_ADDR_W-1:0] ingress_addr_fifo_data_lo;
  logic ingress_addr_fifo_unqueue_lo;

  logic ingress_data_fifo_ready_lo;
  logic ingress_data_fifo_valid_lo;
  logic [BEAT_BYTES-1:0] ingress_data_fifo_data_lo;
  logic ingress_data_fifo_unqueue_lo;
  logic [7:0] ingress_data_fifo_occ_lo;
  logic ingress_data_fifo_afull_lo;

  logic [BEAT_BITS-1:0] ingress_data_to_mic_lo;
  logic ingress_wr_enqueue_lo;

  logic wr_desc_pending_r, wr_desc_pending_n;
  logic [SPM_ADDR_W-1:0] wr_base_addr_r, wr_base_addr_n;
  logic [XFER_BYTES_W-1:0] wr_xfer_bytes_r, wr_xfer_bytes_n;
  logic [TXN_ID_W-1:0] wr_txn_id_r, wr_txn_id_n;
  localparam logic [STEP_BYTES_W-1:0] SPM_ROW_STEP_BYTES_LP =
      STEP_BYTES_W'(1 << (BYTE_OFF_W + WORD_OFF_W + BANK_ID_W));
  logic cmd_accept_lo;
  logic compute_cmd_ready_lo;
  logic compute_rsp_done_lo;
  logic compute_cmd_ready_masked_lo;
  logic compute_rsp_done_masked_lo;
  logic compute_done_legacy_lo;
  logic compute_done_raw_lo;
  logic compute_cmd_kind_lo;
  logic [2:0] compute_cmd_dofs_lo;
  logic [3:0] compute_cmd_adj_count_lo;
  logic [3:0] compute_cmd_msg_count_lo;
  logic [XFER_BYTES_W-1:0] compute_cmd_wr_xfer_bytes_lo;
  logic [TXN_ID_W-1:0] compute_cmd_txn_id_lo;
  logic cmd_valid_from_ctrl;
  // Use explicit signal from control_unit to work around Verilator interface issue
  wire compute_cmd_valid_lo = cmd_valid_i ? ~sideband_cmd_unsupported_lo : cmd_valid_from_ctrl;
  logic sideband_cmd_active_lo;
  logic sideband_cmd_unsupported_lo;
  logic sideband_inflight_r, sideband_inflight_n;
  logic sideband_unsupported_rsp_r, sideband_unsupported_rsp_n;
  logic sideband_unsupported_hold_r, sideband_unsupported_hold_n;

  logic compute_done_lo;
  logic [0:0][31:0] compute_data_a_lo;
  logic [0:0][31:0] compute_data_b_lo;
  logic [0:0][31:0] compute_data_o_lo;
  logic [1:0] compute_op_lo;
  logic compute_valid_i_lo;
  wire rse_meta_valid_lo;  // Explicit connection for meta_valid - use wire to force continuous assignment
  logic rse_meta_consume_lo;
  logic [BEAT_BITS-1:0] rse_meta_data_lo;
  logic [15:0] rse_meta_seq_lo;

  localparam logic [SPM_ADDR_W-1:0] MESSAGE_WRITE_BASE_LP = SPM_ADDR_W'('h80000);

  control_unit_gbp u_control_unit (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .control_dispatch_if(control_dispatch_if0),
      .control_compute_if(control_compute_if0),
      .stream_control_if_read(stream_control_if_read0),
      .stream_control_if_write(stream_control_if_write0),
      .debug_state_o(),
      .debug_compute_pending_o(cmd_valid_from_ctrl),
      .debug_compute_running_o(),
      .meta_consume_o(rse_meta_consume_lo),
      .meta_valid_i(rse_meta_valid_lo),
      .meta_data_i(rse_meta_data_lo),
      .meta_seq_i(rse_meta_seq_lo)
  );

  stream_dispatcher u_stream_dispatcher (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .control_dispatch_if(control_dispatch_if0),
      .stream_dispatcher_if_read(stream_dispatcher_if_read0),
      .stream_dispatcher_if_write(stream_dispatcher_if_write_unused0)
  );

  read_stream_engine u_read_stream_engine (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .meta_consume_i(rse_meta_consume_lo),
      .if_stream_dispatcher_if_stream(stream_dispatcher_if_read0),
      .if_stream_if_stream(read_stream_if0),
      .if_stream_control_if_stream(stream_control_if_read0),
      .mic_to_spm_arbiter(rd_if[0]),
      .meta_valid_o(rse_meta_valid_lo),
      .meta_data_o(rse_meta_data_lo),
      .meta_seq_o(rse_meta_seq_lo)
  );

  write_stream_engine u_write_stream_engine (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .if_stream_control_if_stream(stream_control_if_write0),
      .if_stream_dispatcher_if_stream(stream_dispatcher_if_write0),
      .if_stream_if_stream(write_stream_if0),
      .mic_to_spm_arbiter(wr_if[0])
  );

  assign ingress_wr_ready_o = ingress_addr_fifo_ready_lo & ingress_data_fifo_ready_lo;
  assign ingress_wr_enqueue_lo = ingress_wr_valid_i & ingress_wr_ready_o;

  addr_fifo u_ingress_addr_fifo (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .data_i(ingress_wr_addr_i),
      .valid_i(ingress_wr_enqueue_lo),
      .ready_o(ingress_addr_fifo_ready_lo),
      .valid_o(ingress_addr_fifo_valid_lo),
      .data_o(ingress_addr_fifo_data_lo),
      .unqueue_i(ingress_addr_fifo_unqueue_lo)
  );

  data_fifo #(
      .width_p(BEAT_BYTES)
  ) u_ingress_data_fifo (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .data_i(ingress_wr_data_i[BEAT_BYTES-1:0]),
      .valid_i(ingress_wr_enqueue_lo),
      .ready_o(ingress_data_fifo_ready_lo),
      .valid_o(ingress_data_fifo_valid_lo),
      .data_o(ingress_data_fifo_data_lo),
      .unqueue_i(ingress_data_fifo_unqueue_lo),
      .occ(ingress_data_fifo_occ_lo),
      .afull(ingress_data_fifo_afull_lo)
  );

  assign ingress_data_to_mic_lo = {{(BEAT_BITS-BEAT_BYTES){1'b0}}, ingress_data_fifo_data_lo};

  mic_write u_ingress_mic_write (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .addr_valid_i(ingress_addr_fifo_valid_lo),
      .addr_data_i(ingress_addr_fifo_data_lo),
      .addr_unqueue_o(ingress_addr_fifo_unqueue_lo),
      .data_valid_i(ingress_data_fifo_valid_lo),
      .data_data_i(ingress_data_to_mic_lo),
      .data_unqueue_o(ingress_data_fifo_unqueue_lo),
      .mic_to_spm_arbiter(wr_if[1])
  );

  // Mapping contract: read payload bytes [7:0]/[15:8] feed compute lanes A/B; the
  // resulting 32-bit lane-0 value is forwarded as the write payload low word.
  assign compute_data_a_lo[0] = {24'h0, read_stream_if0.data[7:0]};
  assign compute_data_b_lo[0] = {24'h0, read_stream_if0.data[15:8]};
  assign compute_op_lo = 2'b00;
  assign compute_valid_i_lo = read_stream_if0.valid;

  compute_unit_wrapper
    #(
      .GBP_CORE_PER_PE(1)
    ) u_compute_unit
    (
      .clk_i(clk_i)
      ,.reset_i(reset_i)
      ,.if_stream_if_stream(read_stream_if0)
      ,.if_write_stream_if_stream(write_stream_if0)
      ,.cmd_valid_i(compute_cmd_valid_lo)
      ,.cmd_kind_i(compute_cmd_kind_lo)
      ,.cmd_dofs_i(compute_cmd_dofs_lo)
      ,.cmd_adj_count_i(compute_cmd_adj_count_lo)
      ,.cmd_msg_count_i(compute_cmd_msg_count_lo)
      ,.cmd_wr_xfer_bytes_i(compute_cmd_wr_xfer_bytes_lo)
      ,.cmd_ready_o(compute_cmd_ready_lo)
      ,.compute_done_o(compute_done_raw_lo)
      ,.rsp_done_o(compute_rsp_done_lo)
      ,.force_persistence_stall_i(force_persistence_stall_i)
      ,.data_a_i(compute_data_a_lo)
      ,.data_b_i(compute_data_b_lo)
      ,.op_i(compute_op_lo)
      ,.valid_i(compute_valid_i_lo)
      ,.data_o(compute_data_o_lo)
      ,.valid_o(compute_done_lo)
    );

  spm_subsystem u_spm_subsystem (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .rd_if(rd_if),
      .wr_if(wr_if)
  );

  genvar gi;
  generate
    for (gi = 1; gi < 2*NB; gi++) begin : g_tie_unused_rd_ports
      assign rd_if[gi].spm_rd_req_valid = 1'b0;
      assign rd_if[gi].spm_rd_req_addr = '0;
      assign rd_if[gi].spm_rd_req_bytes = 1'b0;
    end

    for (gi = 2; gi < 2*NB; gi++) begin : g_tie_unused_wr_ports

      assign wr_if[gi].spm_wr_req_valid = 1'b0;
      assign wr_if[gi].spm_wr_req_addr = '0;
      assign wr_if[gi].spm_wr_req_data = '0;
      assign wr_if[gi].spm_wr_req_wstrb = '0;
    end
  endgenerate

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      wr_desc_pending_r <= 1'b0;
      wr_base_addr_r <= '0;
      wr_xfer_bytes_r <= '0;
      wr_txn_id_r <= '0;
      sideband_inflight_r <= 1'b0;
      sideband_unsupported_rsp_r <= 1'b0;
      sideband_unsupported_hold_r <= 1'b0;
    end else begin
      wr_desc_pending_r <= wr_desc_pending_n;
      wr_base_addr_r <= wr_base_addr_n;
      wr_xfer_bytes_r <= wr_xfer_bytes_n;
      wr_txn_id_r <= wr_txn_id_n;
      sideband_inflight_r <= sideband_inflight_n;
      sideband_unsupported_rsp_r <= sideband_unsupported_rsp_n;
      sideband_unsupported_hold_r <= sideband_unsupported_hold_n;
    end
  end

  always_comb begin
    wr_desc_pending_n = wr_desc_pending_r;
    wr_base_addr_n = wr_base_addr_r;
    wr_xfer_bytes_n = wr_xfer_bytes_r;
    wr_txn_id_n = wr_txn_id_r;
    sideband_inflight_n = sideband_inflight_r;
    sideband_unsupported_rsp_n = sideband_unsupported_rsp_r;
    sideband_unsupported_hold_n = sideband_unsupported_hold_r;

    sideband_cmd_active_lo = cmd_valid_i | sideband_inflight_r;
    sideband_cmd_unsupported_lo = cmd_valid_i & cmd_kind_i[1];

    // compute_cmd_valid_lo is now assigned via wire with continuous assignment
    if (cmd_valid_i) begin
      compute_cmd_kind_lo = cmd_kind_i[0];
      // sideband 直发命令当前没有 META，上游也未提供真实 GBP 参数；
      // 保守保持旧 whitebox 默认值，避免接口扩展后未驱动。
      compute_cmd_dofs_lo = 3'd2;
      compute_cmd_adj_count_lo = cmd_kind_i[0] ? 4'd2 : 4'd1;
      compute_cmd_msg_count_lo = compute_cmd_adj_count_lo;
      compute_cmd_wr_xfer_bytes_lo = XFER_BYTES_W'(BEAT_BYTES);
      compute_cmd_txn_id_lo = cmd_txn_id_i;
    end else begin
      compute_cmd_kind_lo = control_compute_if0.cmd_kind;
      compute_cmd_dofs_lo = control_compute_if0.cmd_dofs;
      compute_cmd_adj_count_lo = control_compute_if0.cmd_adj_count;
      compute_cmd_msg_count_lo = control_compute_if0.cmd_msg_count;
      compute_cmd_wr_xfer_bytes_lo = control_compute_if0.cmd_wr_xfer_bytes;
      compute_cmd_txn_id_lo = control_compute_if0.cmd_txn_id;
    end

    cmd_ready_o = sideband_cmd_unsupported_lo ? 1'b1 : compute_cmd_ready_lo;
    rsp_done_o = sideband_unsupported_rsp_r | (sideband_inflight_r & compute_rsp_done_lo);
    rsp_error_o = sideband_unsupported_rsp_r;

    if (cmd_valid_i && sideband_cmd_unsupported_lo && cmd_ready_o) begin
      sideband_unsupported_rsp_n = 1'b1;
      sideband_unsupported_hold_n = 1'b1;
    end else if (sideband_unsupported_rsp_r && sideband_unsupported_hold_r) begin
      sideband_unsupported_hold_n = 1'b0;
    end else if (sideband_unsupported_rsp_r) begin
      sideband_unsupported_rsp_n = 1'b0;
    end

    if (cmd_valid_i && ~sideband_cmd_unsupported_lo && compute_cmd_ready_lo) begin
      sideband_inflight_n = 1'b1;
    end else if (sideband_inflight_r && compute_rsp_done_lo) begin
      sideband_inflight_n = 1'b0;
    end

    compute_cmd_ready_masked_lo = sideband_cmd_active_lo ? 1'b0 : compute_cmd_ready_lo;
    compute_rsp_done_masked_lo = sideband_cmd_active_lo ? 1'b0 : compute_rsp_done_lo;

    cmd_accept_lo = compute_cmd_valid_lo & compute_cmd_ready_lo;

    if (cmd_accept_lo) begin
      // 先把写描述符发出去，避免 compute 输出阶段因下游没有地址描述符而反压自锁。
      wr_base_addr_n = control_compute_if0.cmd_wr_addr;
      wr_xfer_bytes_n = compute_cmd_wr_xfer_bytes_lo;
      wr_txn_id_n = compute_cmd_txn_id_lo;
      wr_desc_pending_n = 1'b1;
    end

    if (wr_desc_pending_r && stream_dispatcher_if_write0.ready) begin
      wr_desc_pending_n = 1'b0;
    end

  end

  assign stream_dispatcher_if_write0.valid = wr_desc_pending_r;
  assign stream_dispatcher_if_write0.data = '{
      op: OP_WRITE,
      txn_id: wr_txn_id_r,
      start: 1'b1,
      base_addr: wr_base_addr_r,
      xfer_bytes: wr_xfer_bytes_r,
      addr_step_bytes: SPM_ROW_STEP_BYTES_LP,
      operand_id: '0,
      wstrb_mode: WSTRB_FULL,
      dim: 1'b0,
      y_count: '0,
      y_stride_bytes: '0,
      addr_src: 1'b0
  };

  assign compute_done_legacy_lo = compute_rsp_done_lo;
  assign control_compute_if0.done = compute_done_raw_lo;
  
  assign control_compute_if0.cmd_ready = compute_cmd_ready_masked_lo;
  assign control_compute_if0.rsp_done = compute_rsp_done_masked_lo;

  assign rd_req_valid_o = rd_if[0].spm_rd_req_valid;
  assign rd_req_addr_o = rd_if[0].spm_rd_req_addr;
  assign wr_req_valid_o = wr_if[0].spm_wr_req_valid;
  assign wr_req_addr_o = wr_if[0].spm_wr_req_addr;
  assign wr_req_data_low_o = wr_if[0].spm_wr_req_data[31:0];
  assign ingress_wr_req_valid_o = wr_if[1].spm_wr_req_valid;
  assign ingress_wr_req_addr_o = wr_if[1].spm_wr_req_addr;
  assign ingress_wr_req_data_low_o = wr_if[1].spm_wr_req_data[31:0];
  assign compute_start_o = control_compute_if0.start;
  assign compute_done_o = compute_done_raw_lo;
  assign wr_txn_id_o = wr_txn_id_r;
  assign cmd_txn_id_o = compute_cmd_txn_id_lo;

 endmodule
