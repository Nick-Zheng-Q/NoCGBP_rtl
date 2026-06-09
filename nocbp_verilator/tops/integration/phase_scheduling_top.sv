// phase_scheduling_top.sv
// Integration test top for phase scheduling:
// control_subsystem -> compute_subsystem loopback.

module phase_scheduling_top (
    input logic clk,
    input logic rst_n,

    // Test controls node readiness
    input logic [gbp_pkg::NUM_NODES_PER_PE-1:0] node_ready_i,

    // Compute neighbor state stream
    input logic        ns_valid_i,
    output logic       ns_ready_o,
    input logic [gbp_pkg::FP32_W-1:0] ns_data_i,
    input logic        ns_last_i,

    // Observations
    output logic       phase_factor_first_o,
    output logic       phase_switch_pulse_o,
    output logic       sched_valid_o,
    output logic [gbp_pkg::NODE_ID_W-1:0] sched_node_id_o,
    output logic       comp_cmd_valid_o,
    output logic [gbp_pkg::NODE_ID_W-1:0] comp_cmd_node_id_o,
    output logic       done_valid_o,
    output logic [gbp_pkg::NODE_ID_W-1:0] done_node_id_o,
    output logic       batch_done_o,
    output logic [gbp_pkg::NUM_NODES_PER_PE-1:0] visited_mask_o
);

  // -------------------------------------------------------------------------
  // Control Subsystem Fake SPM
  // -------------------------------------------------------------------------
  logic                  ctrl_spm_rd_valid;
  logic [gbp_pkg::SPM_ADDR_W-1:0] ctrl_spm_rd_addr;
  logic                  ctrl_spm_rd_ready;
  logic [gbp_pkg::BEAT_BITS-1:0]  ctrl_spm_rd_data;

  assign ctrl_spm_rd_ready = 1'b1;
  always_comb begin
    ctrl_spm_rd_data = '0;
    case (ctrl_spm_rd_addr[3:0])
      // Node headers at addr = node_id * HEADER_WORDS (HEADER_WORDS=2)
      4'd0: begin
        // NodeHeader[0]: node_id=1, dof=1, adj_count=1, adj_base=8, state_base=8, state_words=8
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W-1:0] = gbp_pkg::NODE_ID_W'(1);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W-1:gbp_pkg::NODE_ID_W] = gbp_pkg::DOF_W'(1);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W] = gbp_pkg::ADJ_COUNT_W'(1);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+gbp_pkg::SPM_ADDR_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W] = gbp_pkg::SPM_ADDR_W'(8);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+gbp_pkg::SPM_ADDR_W] = gbp_pkg::SPM_ADDR_W'(8);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W+gbp_pkg::STATE_WORDS_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W] = gbp_pkg::STATE_WORDS_W'(8);
      end
      4'd2: begin
        // NodeHeader[1]: node_id=2, dof=1, adj_count=1, adj_base=8, state_base=8, state_words=8
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W-1:0] = gbp_pkg::NODE_ID_W'(2);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W-1:gbp_pkg::NODE_ID_W] = gbp_pkg::DOF_W'(1);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W] = gbp_pkg::ADJ_COUNT_W'(1);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+gbp_pkg::SPM_ADDR_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W] = gbp_pkg::SPM_ADDR_W'(8);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+gbp_pkg::SPM_ADDR_W] = gbp_pkg::SPM_ADDR_W'(8);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W+gbp_pkg::STATE_WORDS_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W] = gbp_pkg::STATE_WORDS_W'(8);
      end
      4'd4: begin
        // NodeHeader[2]: node_id=3, dof=1, adj_count=1, adj_base=8, state_base=8, state_words=8
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W-1:0] = gbp_pkg::NODE_ID_W'(3);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W-1:gbp_pkg::NODE_ID_W] = gbp_pkg::DOF_W'(1);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W] = gbp_pkg::ADJ_COUNT_W'(1);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+gbp_pkg::SPM_ADDR_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W] = gbp_pkg::SPM_ADDR_W'(8);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+gbp_pkg::SPM_ADDR_W] = gbp_pkg::SPM_ADDR_W'(8);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W+gbp_pkg::STATE_WORDS_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W] = gbp_pkg::STATE_WORDS_W'(8);
      end
      4'd8: begin
        // AdjEntry[0]: neighbor_id=2, x=2, y=3 (remote)
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W-1:0] = gbp_pkg::NODE_ID_W'(2);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::X_CORD_W-1:gbp_pkg::NODE_ID_W] = gbp_pkg::X_CORD_W'(2);
        ctrl_spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::X_CORD_W+gbp_pkg::Y_CORD_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::X_CORD_W] = gbp_pkg::Y_CORD_W'(3);
      end
      default: ctrl_spm_rd_data = '0;
    endcase
  end

  // -------------------------------------------------------------------------
  // Control Subsystem
  // -------------------------------------------------------------------------
  logic ctrl_cmd_valid;
  logic ctrl_cmd_ready;
  logic [gbp_pkg::NODE_ID_W-1:0] ctrl_cmd_node_id;
  logic ctrl_cmd_is_factor;
  logic [gbp_pkg::DOF_W-1:0] ctrl_cmd_dof;
  logic [gbp_pkg::ADJ_COUNT_W-1:0] ctrl_cmd_adj_count;
  logic [gbp_pkg::STATE_WORDS_W-1:0] ctrl_cmd_state_words;
  logic [gbp_pkg::SPM_ADDR_W-1:0] ctrl_cmd_state_base;
  logic [gbp_pkg::ADJ_COUNT_W-1:0] ctrl_wb_adj_count;

  gbp_pe_control_subsystem u_control (
    .clk(clk)
    ,.rst_n(rst_n)
    ,.node_ready_i(node_ready_i)
    ,.wb_done_i(done_valid_o)
    ,.cmd_valid_o(ctrl_cmd_valid)
    ,.cmd_ready_i(ctrl_cmd_ready)
    ,.cmd_node_id_o(ctrl_cmd_node_id)
    ,.cmd_is_factor_o(ctrl_cmd_is_factor)
    ,.cmd_dof_o(ctrl_cmd_dof)
    ,.cmd_adj_count_o(ctrl_cmd_adj_count)
    ,.cmd_state_words_o(ctrl_cmd_state_words)
    ,.cmd_state_base_o(ctrl_cmd_state_base)
    ,.wb_adj_count_o(ctrl_wb_adj_count)
    ,.wb_adj_neighbor_ids_o()
    ,.wb_adj_neighbor_xs_o()
    ,.wb_adj_neighbor_ys_o()
    ,.wb_adj_is_local_o()
    ,.adj_valid_o()
    ,.adj_ready_i(1'b1)
    ,.adj_neighbor_id_o()
    ,.adj_neighbor_x_o()
    ,.adj_neighbor_y_o()
    ,.adj_is_local_o()
    ,.adj_last_o()
    ,.adj_edge_idx_o()
    ,.reset_valid_i(1'b0)
    ,.reset_node_id_i('0)
    ,.reset_is_factor_i(1'b0)
    ,.spm_rd_valid_o(ctrl_spm_rd_valid)
    ,.spm_rd_ready_i(ctrl_spm_rd_ready)
    ,.spm_rd_addr_o(ctrl_spm_rd_addr)
    ,.spm_rd_data_i(ctrl_spm_rd_data)
    ,.local_valid_o()
    ,.local_ready_i(1'b1)
    ,.local_data_o()
    ,.local_last_o()
    ,.affected_valid_i(1'b0)
    ,.affected_local_id_i('0)
    ,.my_x_i(gbp_pkg::X_CORD_W'(1))
    ,.my_y_i(gbp_pkg::Y_CORD_W'(0))
  );

  assign phase_factor_first_o = u_control.u_phase_controller.phase_factor_first_o;
  assign phase_switch_pulse_o = u_control.u_phase_controller.phase_switch_pulse_o;
  assign visited_mask_o       = u_control.u_phase_controller.visited_mask_o;
  assign sched_valid_o        = u_control.u_node_scheduler.sched_valid_o;
  assign sched_node_id_o      = u_control.u_node_scheduler.sched_node_id_o;

  // -------------------------------------------------------------------------
  // Compute Subsystem
  // -------------------------------------------------------------------------
  logic comp_spm_rd0_valid, comp_spm_rd1_valid;
  logic [gbp_pkg::SPM_ADDR_W-1:0] comp_spm_rd0_addr, comp_spm_rd1_addr;
  logic [gbp_pkg::BEAT_BITS-1:0]  comp_spm_rd0_data, comp_spm_rd1_data;
  logic comp_spm_wr_valid;
  logic [gbp_pkg::SPM_ADDR_W-1:0] comp_spm_wr_addr;
  logic [gbp_pkg::BEAT_BITS-1:0]  comp_spm_wr_data;
  logic [gbp_pkg::WSTRB_W-1:0]   comp_spm_wr_wstrb;

  assign comp_spm_rd0_data = {32'd0, 32'(comp_spm_rd0_addr)};
  assign comp_spm_rd1_data = {32'd0, 32'(comp_spm_rd1_addr)};

  gbp_pe_compute_subsystem u_compute (
    .clk(clk)
    ,.rst_n(rst_n)
    ,.cmd_valid_i(ctrl_cmd_valid)
    ,.cmd_ready_o(ctrl_cmd_ready)
    ,.cmd_node_id_i(ctrl_cmd_node_id)
    ,.cmd_is_factor_i(ctrl_cmd_is_factor)
    ,.cmd_dof_i(ctrl_cmd_dof)
    ,.cmd_adj_count_i(ctrl_cmd_adj_count)
    ,.cmd_state_words_i(ctrl_cmd_state_words)
    ,.cmd_state_base_i(ctrl_cmd_state_base)
    ,.ns_valid_i(ns_valid_i)
    ,.ns_ready_o(ns_ready_o)
    ,.ns_data_i(ns_data_i)
    ,.ns_last_i(ns_last_i)
    ,.spm_rd0_valid_o(comp_spm_rd0_valid)
    ,.spm_rd0_ready_i(1'b1)
    ,.spm_rd0_addr_o(comp_spm_rd0_addr)
    ,.spm_rd0_data_i(comp_spm_rd0_data)
    ,.spm_rd1_valid_o(comp_spm_rd1_valid)
    ,.spm_rd1_ready_i(1'b1)
    ,.spm_rd1_addr_o(comp_spm_rd1_addr)
    ,.spm_rd1_data_i(comp_spm_rd1_data)
    ,.spm_wr_valid_o(comp_spm_wr_valid)
    ,.spm_wr_ready_i(1'b1)
    ,.spm_wr_addr_o(comp_spm_wr_addr)
    ,.spm_wr_data_o(comp_spm_wr_data)
    ,.spm_wr_wstrb_o(comp_spm_wr_wstrb)
    ,.done_valid_o(done_valid_o)
    ,.done_node_id_o(done_node_id_o)
    ,.done_is_factor_o()
    ,.batch_done_o(batch_done_o)
  );

  assign comp_cmd_valid_o = ctrl_cmd_valid;
  assign comp_cmd_node_id_o = ctrl_cmd_node_id;

endmodule
