module read_stream_engine
  import gbp_pkg::*;
(
    input logic clk_i,
    input logic reset_i,
    stream_dispatcher_if.slave if_stream_dispatcher_if_stream,
    read_stream_if.slave if_stream_if_stream,
    stream_control_if.master if_stream_control_if_stream,
    mic_spm_arbiter_if.master mic_to_spm_arbiter
);

  desc_t desc_r;
  desc_t desc_to_agu;
  logic  desc_active_r;
  logic  desc_active_n;
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

  logic                  data_fifo_ready_lo;
  logic                  data_fifo_valid_lo;
  logic [BEAT_BYTES-1:0] data_fifo_data_lo;
  logic [7:0]            data_fifo_occ_lo;
  logic                  data_fifo_afull_lo;
  logic                  data_fifo_unqueue_lo;
  logic                  meta_capture_valid_lo;
  logic [BEAT_BITS-1:0]  meta_capture_data_lo;

  logic desc_accept;

  assign desc_accept = if_stream_dispatcher_if_stream.valid & if_stream_dispatcher_if_stream.ready;

  assign if_stream_dispatcher_if_stream.ready = ~desc_active_r;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      desc_r <= '0;
      desc_active_r <= 1'b0;
      agu_start_r <= 1'b0;
    end else begin
      desc_active_r <= desc_active_n;
      agu_start_r <= agu_start_n;
      if (desc_accept) begin
        desc_r <= if_stream_dispatcher_if_stream.data;
      end
    end
  end

  always_comb begin
    desc_active_n = desc_active_r;
    agu_start_n = agu_start_r;

    if (desc_accept) begin
      desc_active_n = 1'b1;
      agu_start_n = 1'b1;
    end else begin
      if (agu_start_r & addr_fifo_ready_lo) begin
        agu_start_n = 1'b0;
      end
      if (desc_active_r & agu_next_desc_lo) begin
        desc_active_n = 1'b0;
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
      .valid_i(desc_active_r & agu_valid_lo),
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
      .data_o(mic_data_lo)
  );

  assign data_fifo_unqueue_lo = data_fifo_valid_lo & if_stream_if_stream.ready;

  data_fifo u_data_fifo (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .data_i(mic_data_lo[BEAT_BYTES-1:0]),
      .valid_i(mic_data_valid_lo),
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
  assign meta_capture_valid_lo = mic_data_valid_lo & (desc_r.operand_id[1:0] == STREAM_META);
  assign meta_capture_data_lo = mic_data_lo;
  assign if_stream_control_if_stream.meta_valid = meta_capture_valid_lo;
  assign if_stream_control_if_stream.meta_data = meta_capture_data_lo;

endmodule
