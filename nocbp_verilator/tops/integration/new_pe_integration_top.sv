module new_pe_integration_top (
  input  logic clk_i,
  input  logic rst_n_i,

  // Command
  input  logic                 cmd_valid_i,
  output logic                 cmd_ready_o,
  input  logic [gbp_pkg::NODE_ID_W-1:0] cmd_node_id_i,
  input  logic                 cmd_is_factor_i,
  input  logic [gbp_pkg::DOF_W-1:0]     cmd_dof_i,
  input  logic [gbp_pkg::ADJ_COUNT_W-1:0] cmd_adj_count_i,
  input  logic [gbp_pkg::STATE_WORDS_W-1:0] cmd_state_words_i,

  // Neighbor state
  input  logic                 ns_valid_i,
  output logic                 ns_ready_o,
  input  logic [gbp_pkg::FP32_W-1:0]    ns_data_i,
  input  logic                 ns_last_i,

  // Completion
  output logic                 done_valid_o,
  output logic [gbp_pkg::NODE_ID_W-1:0] done_node_id_o,
  output logic                 done_is_factor_o,
  output logic                 batch_done_o
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
    .clk_i(clk_i),
    .rst_n_i(rst_n_i),
    .desc_valid_i(cu_rd_desc_valid),
    .desc_ready_o(cu_rd_desc_ready),
    .desc_base_addr_i(cu_rd_desc_base_addr),
    .desc_word_count_i(cu_rd_desc_word_count),
    .desc_is_staging_i(cu_rd_desc_is_staging),
    .beat_valid_o(cu_rd_beat_valid),
    .beat_ready_i(cu_rd_beat_ready),
    .beat_data_o(cu_rd_beat_data),
    .spm_rd_valid_o(rd_valid[0]),
    .spm_rd_ready_i(rd_ready[0]),
    .spm_rd_addr_o(rd_addr[0]),
    .spm_rd_data_i(rd_data[0])
  );

  write_stream_engine u_wse (
    .clk_i(clk_i),
    .rst_n_i(rst_n_i),
    .desc_valid_i(cu_wr_desc_valid),
    .desc_ready_o(cu_wr_desc_ready),
    .desc_base_addr_i(cu_wr_desc_base_addr),
    .desc_word_count_i(cu_wr_desc_word_count),
    .word_valid_i(cu_wr_word_valid),
    .word_ready_o(cu_wr_word_ready),
    .word_data_i(cu_wr_word_data),
    .spm_wr_valid_o(wr_valid[0]),
    .spm_wr_ready_i(wr_ready[0]),
    .spm_wr_addr_o(wr_addr[0]),
    .spm_wr_data_o(wr_data[0]),
    .spm_wr_wstrb_o(wr_wstrb[0])
  );

  compute_unit u_cu (
    .clk_i(clk_i),
    .rst_n_i(rst_n_i),
    .cmd_valid_i(cmd_valid_i),
    .cmd_ready_o(cmd_ready_o),
    .cmd_node_id_i(cmd_node_id_i),
    .cmd_is_factor_i(cmd_is_factor_i),
    .cmd_dof_i(cmd_dof_i),
    .cmd_adj_count_i(cmd_adj_count_i),
    .cmd_state_words_i(cmd_state_words_i),
    .cmd_neighbor_dofs_i({8{gbp_pkg::DOF_W'(2)}}),
    .ns_valid_i(ns_valid_i),
    .ns_ready_o(ns_ready_o),
    .ns_data_i(ns_data_i),
    .ns_last_i(ns_last_i),
    .rd_desc_valid_o(cu_rd_desc_valid),
    .rd_desc_ready_i(cu_rd_desc_ready),
    .rd_desc_base_addr_o(cu_rd_desc_base_addr),
    .rd_desc_word_count_o(cu_rd_desc_word_count),
    .rd_desc_is_staging_o(cu_rd_desc_is_staging),
    .rd_beat_valid_i(cu_rd_beat_valid),
    .rd_beat_ready_o(cu_rd_beat_ready),
    .rd_beat_data_i(cu_rd_beat_data),
    .wr_desc_valid_o(cu_wr_desc_valid),
    .wr_desc_ready_i(cu_wr_desc_ready),
    .wr_desc_base_addr_o(cu_wr_desc_base_addr),
    .wr_desc_word_count_o(cu_wr_desc_word_count),
    .wr_word_valid_o(cu_wr_word_valid),
    .wr_word_ready_i(cu_wr_word_ready),
    .wr_word_data_o(cu_wr_word_data),
    .done_valid_o(done_valid_o),
    .done_node_id_o(done_node_id_o),
    .done_is_factor_o(done_is_factor_o),
    .batch_done_o(batch_done_o)
  );

  spm_arbiter #(
    .NUM_BANKS(NUM_BANKS),
    .NUM_CLIENTS(NUM_ARB_CLIENTS),
    .SPM_ADDR_W(SPM_ADDR_W),
    .BEAT_BITS(BEAT_BITS),
    .ROW_ADDR_W(ROW_ADDR_W),
    .WSTRB_W(WSTRB_W)
  ) u_arbiter (
    .clk_i(clk_i),
    .rst_n_i(rst_n_i),
    .rd_valid_i(rd_valid),
    .rd_ready_o(rd_ready),
    .rd_addr_i(rd_addr),
    .rd_data_o(rd_data),
    .wr_valid_i(wr_valid),
    .wr_ready_o(wr_ready),
    .wr_addr_i(wr_addr),
    .wr_data_i(wr_data),
    .wr_wstrb_i(wr_wstrb),
    .bank_rd_en_o(bank_rd_en),
    .bank_rd_addr_o(bank_rd_addr),
    .bank_rd_data_i(bank_rd_data),
    .bank_wr_en_o(bank_wr_en),
    .bank_wr_addr_o(bank_wr_addr),
    .bank_wr_data_o(bank_wr_data),
    .bank_wr_wstrb_o(bank_wr_wstrb)
  );

  for (genvar b = 0; b < NUM_BANKS; b++) begin : banks
    spm_bank #(.BANK_ID(b)) u_bank (
      .clk_i(clk_i),
      .rst_n_i(rst_n_i),
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
