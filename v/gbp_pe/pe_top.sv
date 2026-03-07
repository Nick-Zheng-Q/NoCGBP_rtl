module pe_top
  import gbp_pkg::*;
(
    input logic clk_i,
    input logic reset_i,

    output logic rd_req_valid_o,
    output logic [SPM_ADDR_W-1:0] rd_req_addr_o,
    output logic wr_req_valid_o,
    output logic [SPM_ADDR_W-1:0] wr_req_addr_o,
    output logic [31:0] wr_req_data_low_o,
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

  logic wr_desc_pending_r, wr_desc_pending_n;
  logic [SPM_ADDR_W-1:0] wr_base_addr_r, wr_base_addr_n;
  logic [TXN_ID_W-1:0] wr_txn_id_r, wr_txn_id_n;
  logic cmd_accept_lo;
  logic compute_cmd_ready_lo;
  logic compute_rsp_done_lo;
  logic compute_done_legacy_lo;

  logic compute_done_lo;
  logic [0:0][31:0] compute_data_a_lo;
  logic [0:0][31:0] compute_data_b_lo;
  logic [0:0][31:0] compute_data_o_lo;
  logic [1:0] compute_op_lo;
  logic compute_valid_i_lo;

  localparam logic [SPM_ADDR_W-1:0] MESSAGE_WRITE_BASE_LP = SPM_ADDR_W'('h80000);

  control_unit u_control_unit (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .control_dispatch_if(control_dispatch_if0),
      .control_compute_if(control_compute_if0),
      .stream_control_if_read(stream_control_if_read0),
      .stream_control_if_write(stream_control_if_write0)
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
      .if_stream_dispatcher_if_stream(stream_dispatcher_if_read0),
      .if_stream_if_stream(read_stream_if0),
      .if_stream_control_if_stream(stream_control_if_read0),
      .mic_to_spm_arbiter(rd_if[0])
  );

  write_stream_engine u_write_stream_engine (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .if_stream_control_if_stream(stream_control_if_write0),
      .if_stream_dispatcher_if_stream(stream_dispatcher_if_write0),
      .if_stream_if_stream(write_stream_if0),
      .mic_to_spm_arbiter(wr_if[0])
  );

  // Mapping contract: read payload bytes [7:0]/[15:8] feed compute lanes A/B; the
  // resulting 32-bit lane-0 value is forwarded as the write payload low word.
  assign compute_data_a_lo[0] = {24'h0, read_stream_if0.data[7:0]};
  assign compute_data_b_lo[0] = {24'h0, read_stream_if0.data[15:8]};
  assign compute_op_lo = 2'b00;
  assign compute_valid_i_lo = read_stream_if0.valid;

  compute_unit
    #(
      .GBP_CORE_PER_PE(1)
    ) u_compute_unit
    (
      .clk_i(clk_i)
      ,.reset_i(reset_i)
      ,.if_stream_if_stream(read_stream_if0)
      ,.if_write_stream_if_stream(write_stream_if0)
      ,.cmd_valid_i(control_compute_if0.cmd_valid)
      ,.cmd_kind_i(control_compute_if0.cmd_kind)
      ,.cmd_ready_o(compute_cmd_ready_lo)
      ,.rsp_done_o(compute_rsp_done_lo)
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
    for (gi = 1; gi < 2*NB; gi++) begin : g_tie_unused_ports
      assign rd_if[gi].spm_rd_req_valid = 1'b0;
      assign rd_if[gi].spm_rd_req_addr = '0;
      assign rd_if[gi].spm_rd_req_bytes = 1'b0;

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
      wr_txn_id_r <= '0;
    end else begin
      wr_desc_pending_r <= wr_desc_pending_n;
      wr_base_addr_r <= wr_base_addr_n;
      wr_txn_id_r <= wr_txn_id_n;
    end
  end

  always_comb begin
    wr_desc_pending_n = wr_desc_pending_r;
    wr_base_addr_n = wr_base_addr_r;
    wr_txn_id_n = wr_txn_id_r;

    cmd_accept_lo = control_compute_if0.cmd_valid & control_compute_if0.cmd_ready;

    if (cmd_accept_lo) begin
      wr_desc_pending_n = 1'b1;
      wr_base_addr_n = MESSAGE_WRITE_BASE_LP;
      wr_txn_id_n = control_compute_if0.cmd_txn_id;
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
      xfer_bytes: XFER_BYTES_W'(BEAT_BYTES),
      addr_step_bytes: STEP_BYTES_W'(BEAT_BYTES),
      operand_id: '0,
      wstrb_mode: WSTRB_FULL,
      dim: 1'b0,
      y_count: '0,
      y_stride_bytes: '0,
      addr_src: 1'b0
  };

  assign compute_done_legacy_lo = compute_rsp_done_lo;
  assign control_compute_if0.done = compute_done_legacy_lo;
  assign control_compute_if0.cmd_ready = compute_cmd_ready_lo;
  assign control_compute_if0.rsp_done = compute_rsp_done_lo;

  assign rd_req_valid_o = rd_if[0].spm_rd_req_valid;
  assign rd_req_addr_o = rd_if[0].spm_rd_req_addr;
  assign wr_req_valid_o = wr_if[0].spm_wr_req_valid;
  assign wr_req_addr_o = wr_if[0].spm_wr_req_addr;
  assign wr_req_data_low_o = wr_if[0].spm_wr_req_data[31:0];
  assign compute_start_o = control_compute_if0.start;
  assign compute_done_o = compute_done_legacy_lo;
  assign wr_txn_id_o = wr_txn_id_r;
  assign cmd_txn_id_o = control_compute_if0.cmd_txn_id;

 endmodule
