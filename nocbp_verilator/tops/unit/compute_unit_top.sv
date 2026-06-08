module compute_unit_top;
  import gbp_pkg::*;

  logic clk, rst_n;

  // Command
  logic                 cmd_valid;
  logic                 cmd_ready;
  logic [NODE_ID_W-1:0] cmd_node_id;
  logic                 cmd_is_factor;
  logic [DOF_W-1:0]     cmd_dof;
  logic [ADJ_COUNT_W-1:0] cmd_adj_count;
  logic [STATE_WORDS_W-1:0] cmd_state_words;

  // Neighbor state
  logic                 ns_valid;
  logic                 ns_ready;
  logic [FP32_W-1:0]    ns_data;
  logic                 ns_last;

  // Read Stream Engine
  logic                 rd_desc_valid;
  logic                 rd_desc_ready;
  logic [SPM_ADDR_W-1:0] rd_desc_base_addr;
  logic [15:0]          rd_desc_word_count;
  logic                 rd_desc_is_staging;

  logic                 rd_beat_valid;
  logic                 rd_beat_ready;
  logic [BEAT_BITS-1:0] rd_beat_data;

  // Write Stream Engine
  logic                 wr_desc_valid;
  logic                 wr_desc_ready;
  logic [SPM_ADDR_W-1:0] wr_desc_base_addr;
  logic [15:0]          wr_desc_word_count;

  logic                 wr_word_valid;
  logic                 wr_word_ready;
  logic [FP32_W-1:0]    wr_word_data;

  // Completion
  logic                 done_valid;
  logic [NODE_ID_W-1:0] done_node_id;
  logic                 done_is_factor;
  logic                 batch_done;

  compute_unit u_dut (
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
    .rd_desc_valid(rd_desc_valid),
    .rd_desc_ready(rd_desc_ready),
    .rd_desc_base_addr(rd_desc_base_addr),
    .rd_desc_word_count(rd_desc_word_count),
    .rd_desc_is_staging(rd_desc_is_staging),
    .rd_beat_valid(rd_beat_valid),
    .rd_beat_ready(rd_beat_ready),
    .rd_beat_data(rd_beat_data),
    .wr_desc_valid(wr_desc_valid),
    .wr_desc_ready(wr_desc_ready),
    .wr_desc_base_addr(wr_desc_base_addr),
    .wr_desc_word_count(wr_desc_word_count),
    .wr_word_valid(wr_word_valid),
    .wr_word_ready(wr_word_ready),
    .wr_word_data(wr_word_data),
    .done_valid(done_valid),
    .done_node_id(done_node_id),
    .done_is_factor(done_is_factor),
    .batch_done(batch_done)
  );

endmodule
