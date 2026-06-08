module new_pe_integration_top (
  input  logic clk,
  input  logic rst_n,

  // Command
  input  logic                 cmd_valid,
  output logic                 cmd_ready,
  input  logic [gbp_pkg::NODE_ID_W-1:0] cmd_node_id,
  input  logic                 cmd_is_factor,
  input  logic [gbp_pkg::DOF_W-1:0]     cmd_dof,
  input  logic [gbp_pkg::ADJ_COUNT_W-1:0] cmd_adj_count,
  input  logic [gbp_pkg::STATE_WORDS_W-1:0] cmd_state_words,

  // Neighbor state
  input  logic                 ns_valid,
  output logic                 ns_ready,
  input  logic [gbp_pkg::FP32_W-1:0]    ns_data,
  input  logic                 ns_last,

  // Completion
  output logic                 done_valid,
  output logic [gbp_pkg::NODE_ID_W-1:0] done_node_id,
  output logic                 done_is_factor,
  output logic                 batch_done
);

  import gbp_pkg::*;

  // -------------------------------------------------------------------
  // Compute Unit <-> Read Stream Engine
  // -------------------------------------------------------------------
  logic                 cu_rd_desc_valid;
  logic                 cu_rd_desc_ready;
  logic [SPM_ADDR_W-1:0] cu_rd_desc_base_addr;
  logic [15:0]          cu_rd_desc_word_count;
  logic                 cu_rd_desc_is_staging;

  logic                 cu_rd_beat_valid;
  logic                 cu_rd_beat_ready;
  logic [BEAT_BITS-1:0] cu_rd_beat_data;

  // -------------------------------------------------------------------
  // Compute Unit <-> Write Stream Engine
  // -------------------------------------------------------------------
  logic                 cu_wr_desc_valid;
  logic                 cu_wr_desc_ready;
  logic [SPM_ADDR_W-1:0] cu_wr_desc_base_addr;
  logic [15:0]          cu_wr_desc_word_count;

  logic                 cu_wr_word_valid;
  logic                 cu_wr_word_ready;
  logic [FP32_W-1:0]    cu_wr_word_data;

  // -------------------------------------------------------------------
  // SPM Arbiter <-> Banks
  // -------------------------------------------------------------------
  logic [NUM_BANKS-1:0]                bank_rd_en;
  logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0] bank_rd_addr;
  logic [NUM_BANKS-1:0][BEAT_BITS-1:0] bank_rd_data;

  logic [NUM_BANKS-1:0]                bank_wr_en;
  logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0] bank_wr_addr;
  logic [NUM_BANKS-1:0][BEAT_BITS-1:0] bank_wr_data;
  logic [NUM_BANKS-1:0][WSTRB_W-1:0]   bank_wr_wstrb;

  // -------------------------------------------------------------------
  // SPM Arbiter client ports (7 clients)
  // -------------------------------------------------------------------
  localparam int NUM_ARB_CLIENTS = 7;
  logic [NUM_ARB_CLIENTS-1:0]              rd_valid;
  logic [NUM_ARB_CLIENTS-1:0]              rd_ready;
  logic [NUM_ARB_CLIENTS-1:0][SPM_ADDR_W-1:0] rd_addr;
  logic [NUM_ARB_CLIENTS-1:0][BEAT_BITS-1:0] rd_data;

  logic [NUM_ARB_CLIENTS-1:0]              wr_valid;
  logic [NUM_ARB_CLIENTS-1:0]              wr_ready;
  logic [NUM_ARB_CLIENTS-1:0][SPM_ADDR_W-1:0] wr_addr;
  logic [NUM_ARB_CLIENTS-1:0][BEAT_BITS-1:0] wr_data;
  logic [NUM_ARB_CLIENTS-1:0][WSTRB_W-1:0]   wr_wstrb;

  assign rd_valid[2:1] = '0;
  assign rd_addr[2:1]  = '0;
  assign wr_valid[2:1] = '0;
  assign wr_addr[2:1]  = '0;
  assign wr_data[2:1]  = '0;
  assign wr_wstrb[2:1] = '0;

  assign rd_valid[6:3] = '0;
  assign rd_addr[6:3]  = '0;
  assign wr_valid[6:3] = '0;
  assign wr_addr[6:3]  = '0;
  assign wr_data[6:3]  = '0;
  assign wr_wstrb[6:3] = '0;

  read_stream_engine u_rse (
    .clk(clk),
    .rst_n(rst_n),
    .desc_valid(cu_rd_desc_valid),
    .desc_ready(cu_rd_desc_ready),
    .desc_base_addr(cu_rd_desc_base_addr),
    .desc_word_count(cu_rd_desc_word_count),
    .desc_is_staging(cu_rd_desc_is_staging),
    .beat_valid(cu_rd_beat_valid),
    .beat_ready(cu_rd_beat_ready),
    .beat_data(cu_rd_beat_data),
    .spm_rd_valid(rd_valid[0]),
    .spm_rd_ready(rd_ready[0]),
    .spm_rd_addr(rd_addr[0]),
    .spm_rd_data(rd_data[0])
  );

  write_stream_engine u_wse (
    .clk(clk),
    .rst_n(rst_n),
    .desc_valid(cu_wr_desc_valid),
    .desc_ready(cu_wr_desc_ready),
    .desc_base_addr(cu_wr_desc_base_addr),
    .desc_word_count(cu_wr_desc_word_count),
    .word_valid(cu_wr_word_valid),
    .word_ready(cu_wr_word_ready),
    .word_data(cu_wr_word_data),
    .spm_wr_valid(wr_valid[0]),
    .spm_wr_ready(wr_ready[0]),
    .spm_wr_addr(wr_addr[0]),
    .spm_wr_data(wr_data[0]),
    .spm_wr_wstrb(wr_wstrb[0])
  );

  compute_unit u_cu (
    .clk(clk),
    .rst_n(rst_n),
    .cmd_valid(cmd_valid),
    .cmd_ready(cmd_ready),
    .cmd_node_id(cmd_node_id),
    .cmd_is_factor(cmd_is_factor),
    .cmd_dof(cmd_dof),
    .cmd_adj_count(cmd_adj_count),
    .cmd_state_words(cmd_state_words),
    .ns_valid(ns_valid),
    .ns_ready(ns_ready),
    .ns_data(ns_data),
    .ns_last(ns_last),
    .rd_desc_valid(cu_rd_desc_valid),
    .rd_desc_ready(cu_rd_desc_ready),
    .rd_desc_base_addr(cu_rd_desc_base_addr),
    .rd_desc_word_count(cu_rd_desc_word_count),
    .rd_desc_is_staging(cu_rd_desc_is_staging),
    .rd_beat_valid(cu_rd_beat_valid),
    .rd_beat_ready(cu_rd_beat_ready),
    .rd_beat_data(cu_rd_beat_data),
    .wr_desc_valid(cu_wr_desc_valid),
    .wr_desc_ready(cu_wr_desc_ready),
    .wr_desc_base_addr(cu_wr_desc_base_addr),
    .wr_desc_word_count(cu_wr_desc_word_count),
    .wr_word_valid(cu_wr_word_valid),
    .wr_word_ready(cu_wr_word_ready),
    .wr_word_data(cu_wr_word_data),
    .done_valid(done_valid),
    .done_node_id(done_node_id),
    .done_is_factor(done_is_factor),
    .batch_done(batch_done)
  );

  spm_arbiter #(
    .NUM_BANKS(NUM_BANKS),
    .NUM_CLIENTS(NUM_ARB_CLIENTS),
    .SPM_ADDR_W(SPM_ADDR_W),
    .BEAT_BITS(BEAT_BITS),
    .ROW_ADDR_W(ROW_ADDR_W),
    .WSTRB_W(WSTRB_W)
  ) u_arbiter (
    .clk(clk),
    .rst_n(rst_n),
    .rd_valid(rd_valid),
    .rd_ready(rd_ready),
    .rd_addr(rd_addr),
    .rd_data(rd_data),
    .wr_valid(wr_valid),
    .wr_ready(wr_ready),
    .wr_addr(wr_addr),
    .wr_data(wr_data),
    .wr_wstrb(wr_wstrb),
    .bank_rd_en(bank_rd_en),
    .bank_rd_addr(bank_rd_addr),
    .bank_rd_data(bank_rd_data),
    .bank_wr_en(bank_wr_en),
    .bank_wr_addr(bank_wr_addr),
    .bank_wr_data(bank_wr_data),
    .bank_wr_wstrb(bank_wr_wstrb)
  );

  for (genvar b = 0; b < NUM_BANKS; b++) begin : banks
    spm_bank #(.BANK_ID(b)) u_bank (
      .clk_i(clk),
      .reset_i(~rst_n),
      .bank_rd_en(bank_rd_en[b]),
      .bank_rd_addr(bank_rd_addr[b]),
      .bank_rd_data(bank_rd_data[b]),
      .bank_wr_en(bank_wr_en[b]),
      .bank_wr_addr(bank_wr_addr[b]),
      .bank_wr_data(bank_wr_data[b]),
      .bank_wr_wstrb(bank_wr_wstrb[b])
    );
  end

endmodule
