// gbp_pe_compute_subsystem_top.sv
// Unit-test wrapper for compute subsystem.
// Ties off unused ports and provides a simple descriptor.

module gbp_pe_compute_subsystem_top (
    input logic clk
    , input logic rst_n

    // Command
    , input logic        cmd_valid_i
    , input logic [gbp_pkg::NODE_ID_W-1:0] cmd_node_id_i
    , input logic        cmd_is_factor_i
    , input logic [gbp_pkg::DOF_W-1:0]     cmd_dof_i
    , input logic [gbp_pkg::ADJ_COUNT_W-1:0] cmd_adj_count_i
    , input logic [gbp_pkg::STATE_WORDS_W-1:0] cmd_state_words_i
    , input logic [gbp_pkg::SPM_ADDR_W-1:0]  cmd_state_base_i
    , input logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::DOF_W-1:0] cmd_neighbor_dofs_i
    , output logic       cmd_ready_o

    // Neighbor state stream
    , input logic        ns_valid_i
    , output logic       ns_ready_o
    , input logic [gbp_pkg::FP32_W-1:0]    ns_data_i
    , input logic        ns_last_i

    // SPM backdoor controls
    , input logic        spm_rd0_ready_i

    // Done
    , output logic       done_valid_o
    , output logic [gbp_pkg::NODE_ID_W-1:0] done_node_id_o
    , output logic       done_is_factor_o
    , output logic       batch_done_o
);

  // Fake SPM memory (combinational read, accept-all write)
  logic                  spm_rd0_valid, spm_rd1_valid;
  logic [gbp_pkg::SPM_ADDR_W-1:0] spm_rd0_addr, spm_rd1_addr;
  logic [gbp_pkg::BEAT_BITS-1:0]  spm_rd0_data, spm_rd1_data;

  logic                  spm_wr_valid;
  logic [gbp_pkg::SPM_ADDR_W-1:0] spm_wr_addr;
  logic [gbp_pkg::BEAT_BITS-1:0]  spm_wr_data;
  logic [gbp_pkg::WSTRB_W-1:0]   spm_wr_wstrb;

  assign spm_rd0_data = {32'd0, 32'(spm_rd0_addr)};
  assign spm_rd1_data = {32'd0, 32'(spm_rd1_addr)};

  gbp_pe_compute_subsystem u_dut (
    .clk(clk)
    ,.rst_n(rst_n)
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
    ,.spm_rd0_valid_o(spm_rd0_valid)
    ,.spm_rd0_ready_i(spm_rd0_ready_i)
    ,.spm_rd0_addr_o(spm_rd0_addr)
    ,.spm_rd0_data_i(spm_rd0_data)
    ,.spm_rd1_valid_o(spm_rd1_valid)
    ,.spm_rd1_ready_i(1'b1)
    ,.spm_rd1_addr_o(spm_rd1_addr)
    ,.spm_rd1_data_i(spm_rd1_data)
    ,.spm_wr_valid_o(spm_wr_valid)
    ,.spm_wr_ready_i(1'b1)
    ,.spm_wr_addr_o(spm_wr_addr)
    ,.spm_wr_data_o(spm_wr_data)
    ,.spm_wr_wstrb_o(spm_wr_wstrb)
    ,.done_valid_o(done_valid_o)
    ,.done_node_id_o(done_node_id_o)
    ,.done_is_factor_o(done_is_factor_o)
    ,.batch_done_o(batch_done_o)
  );

endmodule
