// gbp_pe_compute_subsystem.sv
// Encapsulates the descriptor-driven compute datapath:
//   compute_unit + read_stream_engine (STATE) + read_stream_engine (STAGING)
//   + write_stream_engine
//
// Exposes two read stream engine instances as separate SPM ports so that
// STATE and STAGING reads can be arbitrated independently by the memory
// subsystem.

module gbp_pe_compute_subsystem
  import gbp_pkg::*;
#(
    parameter int NODE_ID_W     = gbp_pkg::NODE_ID_W,
    parameter int SPM_ADDR_W    = gbp_pkg::SPM_ADDR_W,
    parameter int STATE_WORDS_W = gbp_pkg::STATE_WORDS_W,
    parameter int ADJ_COUNT_W   = gbp_pkg::ADJ_COUNT_W,
    parameter int DOF_W         = gbp_pkg::DOF_W,
    parameter int BEAT_BITS     = gbp_pkg::BEAT_BITS,
    parameter int FP32_W        = gbp_pkg::FP32_W
)(
    input  logic clk,
    input  logic rst_n,

    // Command from control subsystem
    input  logic                 cmd_valid_i,
    output logic                 cmd_ready_o,
    input  logic [NODE_ID_W-1:0] cmd_node_id_i,
    input  logic                 cmd_is_factor_i,
    input  logic [DOF_W-1:0]     cmd_dof_i,
    input  logic [ADJ_COUNT_W-1:0] cmd_adj_count_i,
    input  logic [STATE_WORDS_W-1:0] cmd_state_words_i,
    input  logic [SPM_ADDR_W-1:0]  cmd_state_base_i,
    input  logic [MAX_ADJ_COUNT-1:0][DOF_W-1:0] cmd_neighbor_dofs_i,

    // Neighbor state from accumulator
    input  logic                 ns_valid_i,
    output logic                 ns_ready_o,
    input  logic [FP32_W-1:0]    ns_data_i,
    input  logic                 ns_last_i,

    // SPM read port 0: STATE (to memory subsystem)
    output logic                 spm_rd0_valid_o,
    input  logic                 spm_rd0_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd0_addr_o,
    input  logic [BEAT_BITS-1:0]  spm_rd0_data_i,

    // SPM read port 1: STAGING (to memory subsystem)
    output logic                 spm_rd1_valid_o,
    input  logic                 spm_rd1_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_rd1_addr_o,
    input  logic [BEAT_BITS-1:0]  spm_rd1_data_i,

    // SPM write port (to memory subsystem)
    output logic                 spm_wr_valid_o,
    input  logic                 spm_wr_ready_i,
    output logic [SPM_ADDR_W-1:0] spm_wr_addr_o,
    output logic [BEAT_BITS-1:0]  spm_wr_data_o,
    output logic [BEAT_BITS/8-1:0] spm_wr_wstrb_o,

    // Completion
    output logic                 done_valid_o,
    output logic [NODE_ID_W-1:0] done_node_id_o,
    output logic                 done_is_factor_o,

    // Batch completion (to fetch subsystem / response collector)
    output logic                 batch_done_o
);

  // ========================================================================
  // Compute Unit
  // ========================================================================
  logic                 cu_rd_desc_valid;
  logic                 cu_rd_desc_ready;
  logic [SPM_ADDR_W-1:0] cu_rd_desc_base_addr;
  logic [15:0]          cu_rd_desc_word_count;
  logic                 cu_rd_desc_is_staging;

  logic                 cu_rd_beat_valid;
  logic                 cu_rd_beat_ready;
  logic [BEAT_BITS-1:0] cu_rd_beat_data;

  logic                 cu_wr_desc_valid;
  logic                 cu_wr_desc_ready;
  logic [SPM_ADDR_W-1:0] cu_wr_desc_base_addr;
  logic [15:0]          cu_wr_desc_word_count;

  logic                 cu_wr_word_valid;
  logic                 cu_wr_word_ready;
  logic [FP32_W-1:0]    cu_wr_word_data;

  compute_unit u_compute_unit (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.cmd_valid_i(cmd_valid_i)
    ,.cmd_ready_o(cmd_ready_o)
    ,.cmd_node_id_i(cmd_node_id_i)
    ,.cmd_is_factor_i(cmd_is_factor_i)
    ,.cmd_dof_i(cmd_dof_i)
    ,.cmd_adj_count_i(cmd_adj_count_i)
    ,.cmd_state_words_i(cmd_state_words_i)
    ,.cmd_state_base_i(cmd_state_base_i)
    ,.cmd_neighbor_dofs_i(cmd_neighbor_dofs_i)
    ,.ns_valid_i(ns_valid_i)
    ,.ns_ready_o(ns_ready_o)
    ,.ns_data_i(ns_data_i)
    ,.ns_last_i(ns_last_i)
    ,.rd_desc_valid_o(cu_rd_desc_valid)
    ,.rd_desc_ready_i(cu_rd_desc_ready)
    ,.rd_desc_base_addr_o(cu_rd_desc_base_addr)
    ,.rd_desc_word_count_o(cu_rd_desc_word_count)
    ,.rd_desc_is_staging_o(cu_rd_desc_is_staging)
    ,.rd_beat_valid_i(cu_rd_beat_valid)
    ,.rd_beat_ready_o(cu_rd_beat_ready)
    ,.rd_beat_data_i(cu_rd_beat_data)
    ,.wr_desc_valid_o(cu_wr_desc_valid)
    ,.wr_desc_ready_i(cu_wr_desc_ready)
    ,.wr_desc_base_addr_o(cu_wr_desc_base_addr)
    ,.wr_desc_word_count_o(cu_wr_desc_word_count)
    ,.wr_word_valid_o(cu_wr_word_valid)
    ,.wr_word_ready_i(cu_wr_word_ready)
    ,.wr_word_data_o(cu_wr_word_data)
    ,.done_valid_o(done_valid_o)
    ,.done_node_id_o(done_node_id_o)
    ,.done_is_factor_o(done_is_factor_o)
    ,.batch_done_o(batch_done_o)
  );

  // ========================================================================
  // Read Stream Engine 0: STATE
  // ========================================================================
  logic                 rse0_desc_valid;
  logic                 rse0_desc_ready;
  logic [SPM_ADDR_W-1:0] rse0_desc_base_addr;
  logic [15:0]          rse0_desc_word_count;
  logic                 rse0_desc_is_staging;

  logic                 rse0_beat_valid;
  logic                 rse0_beat_ready;
  logic [BEAT_BITS-1:0] rse0_beat_data;

  read_stream_engine u_rse_state (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.desc_valid_i(rse0_desc_valid)
    ,.desc_ready_o(rse0_desc_ready)
    ,.desc_base_addr_i(rse0_desc_base_addr)
    ,.desc_word_count_i(rse0_desc_word_count)
    ,.desc_is_staging_i(rse0_desc_is_staging)
    ,.beat_valid_o(rse0_beat_valid)
    ,.beat_ready_i(rse0_beat_ready)
    ,.beat_data_o(rse0_beat_data)
    ,.spm_rd_valid_o(spm_rd0_valid_o)
    ,.spm_rd_ready_i(spm_rd0_ready_i)
    ,.spm_rd_addr_o(spm_rd0_addr_o)
    ,.spm_rd_data_i(spm_rd0_data_i)
  );

  // ========================================================================
  // Read Stream Engine 1: STAGING — tied off until batched staging used
  // ========================================================================
  logic                 rse1_desc_valid;
  logic                 rse1_desc_ready;
  logic [SPM_ADDR_W-1:0] rse1_desc_base_addr;
  logic [15:0]          rse1_desc_word_count;
  logic                 rse1_desc_is_staging;

  logic                 rse1_beat_valid;
  logic                 rse1_beat_ready;
  logic [BEAT_BITS-1:0] rse1_beat_data;

  read_stream_engine u_rse_staging (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.desc_valid_i(rse1_desc_valid)
    ,.desc_ready_o(rse1_desc_ready)
    ,.desc_base_addr_i(rse1_desc_base_addr)
    ,.desc_word_count_i(rse1_desc_word_count)
    ,.desc_is_staging_i(rse1_desc_is_staging)
    ,.beat_valid_o(rse1_beat_valid)
    ,.beat_ready_i(rse1_beat_ready)
    ,.beat_data_o(rse1_beat_data)
    ,.spm_rd_valid_o(spm_rd1_valid_o)
    ,.spm_rd_ready_i(spm_rd1_ready_i)
    ,.spm_rd_addr_o(spm_rd1_addr_o)
    ,.spm_rd_data_i(spm_rd1_data_i)
  );

  assign rse1_desc_valid = 1'b0;
  assign rse1_desc_base_addr = '0;
  assign rse1_desc_word_count = '0;
  assign rse1_desc_is_staging = 1'b0;
  assign rse1_beat_ready = 1'b0;

  // ========================================================================
  // Write Stream Engine
  // ========================================================================
  write_stream_engine u_wse (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.desc_valid_i(cu_wr_desc_valid)
    ,.desc_ready_o(cu_wr_desc_ready)
    ,.desc_base_addr_i(cu_wr_desc_base_addr)
    ,.desc_word_count_i(cu_wr_desc_word_count)
    ,.word_valid_i(cu_wr_word_valid)
    ,.word_ready_o(cu_wr_word_ready)
    ,.word_data_i(cu_wr_word_data)
    ,.spm_wr_valid_o(spm_wr_valid_o)
    ,.spm_wr_ready_i(spm_wr_ready_i)
    ,.spm_wr_addr_o(spm_wr_addr_o)
    ,.spm_wr_data_o(spm_wr_data_o)
    ,.spm_wr_wstrb_o(spm_wr_wstrb_o)
  );

  // ========================================================================
  // Descriptor/beat mux: is_staging selects RSE0 vs RSE1
  // ========================================================================
  // For now compute_unit only issues one read descriptor. Future batched mode
  // will interleave STATE and STAGING descriptors.
  assign rse0_desc_valid      = cu_rd_desc_valid && !cu_rd_desc_is_staging;
  assign rse1_desc_valid      = cu_rd_desc_valid && cu_rd_desc_is_staging;
  assign cu_rd_desc_ready     = cu_rd_desc_is_staging ? rse1_desc_ready : rse0_desc_ready;

  assign rse0_desc_base_addr  = cu_rd_desc_base_addr;
  assign rse0_desc_word_count = cu_rd_desc_word_count;
  assign rse0_desc_is_staging = cu_rd_desc_is_staging;

  assign rse1_desc_base_addr  = cu_rd_desc_base_addr;
  assign rse1_desc_word_count = cu_rd_desc_word_count;
  assign rse1_desc_is_staging = cu_rd_desc_is_staging;

  assign cu_rd_beat_valid     = cu_rd_desc_is_staging ? rse1_beat_valid : rse0_beat_valid;
  assign rse0_beat_ready      = !cu_rd_desc_is_staging && cu_rd_beat_ready;
  assign rse1_beat_ready      = cu_rd_desc_is_staging && cu_rd_beat_ready;
  assign cu_rd_beat_data      = cu_rd_desc_is_staging ? rse1_beat_data : rse0_beat_data;

endmodule
