module read_stream_engine
  import gbp_pkg::*;
(
    input logic clk_i,
    input logic reset_i,
    input logic meta_consume_i,
    stream_dispatcher_if.slave if_stream_dispatcher_if_stream,
    read_stream_if.slave if_stream_if_stream,
    stream_control_if.master if_stream_control_if_stream,
    mic_spm_arbiter_if.master mic_to_spm_arbiter,
    output logic meta_valid_o,  // Explicit output for meta_valid to work around Verilator issue
    output logic [BEAT_BITS-1:0] meta_data_o,
    output logic [15:0] meta_seq_o
);
  localparam int unsigned READ_DATA_FIFO_DEPTH_LP = 64;
  desc_t desc_r;
  desc_t desc_to_agu;
  logic  desc_loaded_r;
  logic  desc_loaded_n;
  logic  desc_inflight_r;
  logic  desc_inflight_n;
  logic  agu_start_r;
  logic  agu_start_n;

  logic [SPM_ADDR_W-1:0] agu_addr_lo;
  logic                  agu_valid_lo;
  logic                  agu_next_desc_lo;

  logic                  addr_fifo_ready_lo;
  logic                  addr_fifo_valid_lo;
  logic [SPM_ADDR_W-1:0] addr_fifo_data_lo;
  logic                  addr_fifo_unqueue_lo;

  logic                  mic_data_valid_lo;
  logic [BEAT_BITS-1:0]  mic_data_lo;
  logic                  mic_busy_lo;

  logic                  data_fifo_ready_lo;
  logic                  data_fifo_valid_lo;
  logic [ BEAT_BITS-1:0] data_fifo_data_lo;
  logic [7:0]            data_fifo_occ_lo;
  logic                  data_fifo_afull_lo;
  logic                  data_fifo_unqueue_lo;
  logic                  meta_capture_valid_lo;
  logic [BEAT_BITS-1:0]  meta_capture_data_lo;
  logic                  data_fifo_push_valid_lo;
  logic                  meta_hold_valid_r;
  logic [BEAT_BITS-1:0]  meta_hold_data_r;
  logic [15:0]           meta_seq_r;
  logic                  meta_desc_pending_r;

  logic desc_accept;

  assign desc_accept = if_stream_dispatcher_if_stream.valid & if_stream_dispatcher_if_stream.ready;

  // 必须等上一条 descriptor 的地址/响应/数据路径都完全排空，才能接受下一条。
  // 否则前一条非 META 的返回 beat 会在 meta_desc_pending 置位后被错误采样成 META。
  assign if_stream_dispatcher_if_stream.ready =
      ~desc_loaded_r & ~desc_inflight_r & ~addr_fifo_valid_lo & ~mic_busy_lo;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      desc_r <= '0;
      desc_loaded_r <= 1'b0;
      desc_inflight_r <= 1'b0;
      agu_start_r <= 1'b0;
      meta_hold_valid_r <= 1'b0;
      meta_hold_data_r <= '0;
      meta_seq_r <= '0;
      meta_desc_pending_r <= 1'b0;
    end else begin
      desc_loaded_r <= desc_loaded_n;
      desc_inflight_r <= desc_inflight_n;
      agu_start_r <= agu_start_n;
      if (desc_accept) begin
        desc_r <= if_stream_dispatcher_if_stream.data;
        $display("RSE_DESC_ACCEPT %m mode=%0d base=%h xfer=%0d",
                 if_stream_dispatcher_if_stream.data.operand_id[1:0],
                 if_stream_dispatcher_if_stream.data.base_addr,
                 if_stream_dispatcher_if_stream.data.xfer_bytes);
        // 新 descriptor 被接受时，上一条 META 快照失效。
        meta_hold_valid_r <= 1'b0;
        meta_desc_pending_r <= (if_stream_dispatcher_if_stream.data.operand_id[1:0] == STREAM_META);
        if (if_stream_dispatcher_if_stream.data.operand_id[1:0] == STREAM_META) begin
          $display("RSE_META_REQ %m addr=%h xfer=%0d seq=%0d",
                   if_stream_dispatcher_if_stream.data.base_addr,
                   if_stream_dispatcher_if_stream.data.xfer_bytes,
                   meta_seq_r);
        end
      end else if (meta_consume_i) begin
        // control_unit 已完成对当前 META 的消费，释放 sticky 标志，等待下一条真正的新 META。
        meta_hold_valid_r <= 1'b0;
        $display("RSE_META_CONSUME %m seq=%0d data=%h", meta_seq_r, meta_hold_data_r);
      end
      if (meta_capture_valid_lo) begin
        meta_hold_valid_r <= 1'b1;
        meta_hold_data_r <= mic_data_lo;
        meta_seq_r <= meta_seq_r + 16'd1;
        meta_desc_pending_r <= 1'b0;
        $display("RSE_META_CAPTURE %m seq_next=%0d data=%h",
                 meta_seq_r + 16'd1, mic_data_lo);
      end
    end
  end

  always_comb begin
    desc_loaded_n = desc_loaded_r;
    desc_inflight_n = desc_inflight_r;
    agu_start_n = agu_start_r;

    if (desc_accept) begin
      desc_loaded_n = 1'b1;
      agu_start_n = 1'b1;
    end else begin
      if (agu_start_r & addr_fifo_ready_lo) begin
        agu_start_n = 1'b0;
        desc_inflight_n = 1'b1;
      end
      if (desc_inflight_r & agu_next_desc_lo) begin
        desc_loaded_n = 1'b0;
        desc_inflight_n = 1'b0;
        agu_start_n = 1'b0;
      end
    end
  end

  always_comb begin
    desc_to_agu = desc_r;
    desc_to_agu.start = agu_start_r;
  end

  agu u_agu (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .descriptor_i(desc_to_agu),
      .ready_i(addr_fifo_ready_lo),
      .agu_addr(agu_addr_lo),
      .valid_o(agu_valid_lo),
      .next_desc_o(agu_next_desc_lo)
  );

  addr_fifo u_addr_fifo (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .data_i(agu_addr_lo),
      .valid_i(desc_inflight_r & agu_valid_lo),
      .ready_o(addr_fifo_ready_lo),
      .valid_o(addr_fifo_valid_lo),
      .data_o(addr_fifo_data_lo),
      .unqueue_i(addr_fifo_unqueue_lo)
  );

  mic_read u_mic_read (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .valid_i(addr_fifo_valid_lo),
      .data_i(addr_fifo_data_lo),
      .unqueue_o(addr_fifo_unqueue_lo),
      .mic_to_spm_arbiter(mic_to_spm_arbiter),
      .ready_i(data_fifo_ready_lo),
      .valid_o(mic_data_valid_lo),
      .data_o(mic_data_lo),
      .busy_o(mic_busy_lo)
  );

  assign data_fifo_unqueue_lo = data_fifo_valid_lo & if_stream_if_stream.ready;

  assign data_fifo_push_valid_lo = mic_data_valid_lo & ~meta_capture_valid_lo;

  data_fifo #(
      .width_p(BEAT_BITS),
      .depth_p(READ_DATA_FIFO_DEPTH_LP)
  ) u_data_fifo (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .data_i(mic_data_lo),
      .valid_i(data_fifo_push_valid_lo),
      .ready_o(data_fifo_ready_lo),
      .valid_o(data_fifo_valid_lo),
      .data_o(data_fifo_data_lo),
      .unqueue_i(data_fifo_unqueue_lo),
      .occ(data_fifo_occ_lo),
      .afull(data_fifo_afull_lo)
  );

  assign if_stream_if_stream.valid = data_fifo_valid_lo;
  assign if_stream_if_stream.data = data_fifo_data_lo;

  assign if_stream_control_if_stream.occ = data_fifo_occ_lo;
  assign if_stream_control_if_stream.afull = data_fifo_afull_lo;
  assign meta_capture_valid_lo = mic_data_valid_lo & meta_desc_pending_r;
  
  assign meta_capture_data_lo = mic_data_lo;
  
  // Combine assignments to work around Verilator optimization issue
  assign meta_valid_o = meta_hold_valid_r;
  assign meta_data_o = meta_hold_data_r;
  assign meta_seq_o = meta_seq_r;
  assign if_stream_control_if_stream.meta_valid = meta_valid_o;
  assign if_stream_control_if_stream.meta_data = meta_hold_data_r;
  
endmodule
