// gbp_pe_control_subsystem_top.sv
// Unit-test wrapper for control subsystem.
// Provides a fake SPM that returns a simple NodeHeader + AdjEntries.

module gbp_pe_control_subsystem_top (
    input logic clk
    , input logic rst_n

    // Backdoor controls
    , input logic        wb_done_i
    , input logic        comp_cmd_ready_i
    , input logic        adj_ready_i
    , input logic        local_ready_i

    // Node readiness from fetch subsystem (test-controlled)
    , input logic [gbp_pkg::NUM_NODES_PER_PE-1:0] node_ready_i

    // Outputs to observe
    , output logic       comp_cmd_valid_o
    , output logic [gbp_pkg::NODE_ID_W-1:0] comp_cmd_node_id_o
    , output logic       comp_cmd_is_factor_o
    , output logic [gbp_pkg::DOF_W-1:0]     comp_cmd_dof_o
    , output logic [gbp_pkg::ADJ_COUNT_W-1:0] comp_cmd_adj_count_o
    , output logic [gbp_pkg::STATE_WORDS_W-1:0] comp_cmd_state_words_o
    , output logic [gbp_pkg::SPM_ADDR_W-1:0]  comp_cmd_state_base_o

    , output logic       adj_valid_o
    , output logic [gbp_pkg::NODE_ID_W-1:0] adj_neighbor_id_o
    , output logic [gbp_pkg::X_CORD_W-1:0]  adj_neighbor_x_o
    , output logic [gbp_pkg::Y_CORD_W-1:0]  adj_neighbor_y_o
    , output logic       adj_is_local_o
    , output logic       adj_last_o
    , output logic [gbp_pkg::ADJ_COUNT_W-1:0] adj_edge_idx_o

    , output logic       local_valid_o
    , output logic [gbp_pkg::FP32_W-1:0]   local_data_o
    , output logic       local_last_o

    , output logic [gbp_pkg::MAX_ADJ_COUNT-1:0][gbp_pkg::DOF_W-1:0] comp_cmd_neighbor_dofs_o
    , output logic [gbp_pkg::ADJ_COUNT_W-1:0] wb_adj_count_o
    , output logic       phase_factor_first_o
    , output logic       phase_switch_pulse_o
    , output logic       no_schedulable_nodes_o
    , output logic [gbp_pkg::NUM_NODES_PER_PE-1:0] visited_mask_o
    , output logic [gbp_pkg::SPM_ADDR_W-1:0] debug_spm_rd_addr_o
    , output logic [gbp_pkg::NODE_ID_W-1:0] debug_sched_node_id_o
);

  // Fake SPM: combinational responses based on address
  logic                  spm_rd_valid;
  logic [gbp_pkg::SPM_ADDR_W-1:0] spm_rd_addr;
  logic                  spm_rd_ready;
  logic [gbp_pkg::BEAT_BITS-1:0]  spm_rd_data;

  assign spm_rd_ready = 1'b1; // SPM always ready for reads
  always_comb begin
    spm_rd_data = '0;
    case (spm_rd_addr[3:0])
      4'd0: begin
        // NodeHeader: node_id=1, dof=3, adj_count=2, adj_base=4, state_base=8, state_words=2
        spm_rd_data[gbp_pkg::NODE_ID_W-1:0] = gbp_pkg::NODE_ID_W'(1);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W-1:gbp_pkg::NODE_ID_W] = gbp_pkg::DOF_W'(3);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W] = gbp_pkg::ADJ_COUNT_W'(2);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+gbp_pkg::SPM_ADDR_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W] = gbp_pkg::SPM_ADDR_W'(4);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+gbp_pkg::SPM_ADDR_W] = gbp_pkg::SPM_ADDR_W'(8);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W+gbp_pkg::STATE_WORDS_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W] = gbp_pkg::STATE_WORDS_W'(2);
      end
      4'd4: begin
        // AdjEntry[0]: neighbor_id=2, x=1, y=0 (local)
        spm_rd_data[gbp_pkg::NODE_ID_W-1:0] = gbp_pkg::NODE_ID_W'(2);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::X_CORD_W-1:gbp_pkg::NODE_ID_W] = gbp_pkg::X_CORD_W'(1);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::X_CORD_W+gbp_pkg::Y_CORD_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::X_CORD_W] = gbp_pkg::Y_CORD_W'(0);
      end
      4'd5: begin
        // AdjEntry[1]: neighbor_id=3, x=2, y=1 (remote)
        spm_rd_data[gbp_pkg::NODE_ID_W-1:0] = gbp_pkg::NODE_ID_W'(3);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::X_CORD_W-1:gbp_pkg::NODE_ID_W] = gbp_pkg::X_CORD_W'(2);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::X_CORD_W+gbp_pkg::Y_CORD_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::X_CORD_W] = gbp_pkg::Y_CORD_W'(1);
      end
      4'd8, 4'd9: begin
        // State words (lower 32b = FP32 value = address)
        spm_rd_data[31:0] = 32'(spm_rd_addr);
      end
      4'd2: begin
        // Neighbor NodeHeader for local neighbor id=2: state_base=8, state_words=2
        spm_rd_data[gbp_pkg::NODE_ID_W-1:0] = gbp_pkg::NODE_ID_W'(2);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+gbp_pkg::SPM_ADDR_W] = gbp_pkg::SPM_ADDR_W'(8);
        spm_rd_data[gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W+gbp_pkg::STATE_WORDS_W-1:gbp_pkg::NODE_ID_W+gbp_pkg::DOF_W+gbp_pkg::ADJ_COUNT_W+2*gbp_pkg::SPM_ADDR_W] = gbp_pkg::STATE_WORDS_W'(2);
      end
      default: spm_rd_data = '0;
    endcase
  end

  gbp_pe_control_subsystem u_dut (
    .clk(clk)
    ,.rst_n(rst_n)
    ,.node_ready_i(node_ready_i)
    ,.wb_done_i(wb_done_i)
    ,.cmd_valid_o(comp_cmd_valid_o)
    ,.cmd_ready_i(comp_cmd_ready_i)
    ,.cmd_node_id_o(comp_cmd_node_id_o)
    ,.cmd_is_factor_o(comp_cmd_is_factor_o)
    ,.cmd_dof_o(comp_cmd_dof_o)
    ,.cmd_adj_count_o(comp_cmd_adj_count_o)
    ,.cmd_state_words_o(comp_cmd_state_words_o)
    ,.cmd_state_base_o(comp_cmd_state_base_o)
    ,.cmd_neighbor_dofs_o(comp_cmd_neighbor_dofs_o)
    ,.wb_adj_count_o(wb_adj_count_o)
    ,.wb_adj_neighbor_ids_o()
    ,.wb_adj_neighbor_xs_o()
    ,.wb_adj_neighbor_ys_o()
    ,.wb_adj_is_local_o()
    ,.adj_valid_o(adj_valid_o)
    ,.adj_ready_i(adj_ready_i)
    ,.adj_neighbor_id_o(adj_neighbor_id_o)
    ,.adj_neighbor_x_o(adj_neighbor_x_o)
    ,.adj_neighbor_y_o(adj_neighbor_y_o)
    ,.adj_is_local_o(adj_is_local_o)
    ,.adj_last_o(adj_last_o)
    ,.adj_edge_idx_o(adj_edge_idx_o)
    ,.affected_valid_i(1'b0)
    ,.affected_local_id_i('0)
    ,.reset_valid_i(1'b0)
    ,.reset_node_id_i('0)
    ,.reset_is_factor_i(1'b0)
    ,.spm_rd_valid_o(spm_rd_valid)
    ,.spm_rd_ready_i(spm_rd_ready)
    ,.spm_rd_addr_o(spm_rd_addr)
    ,.spm_rd_data_i(spm_rd_data)
    ,.local_valid_o(local_valid_o)
    ,.local_ready_i(local_ready_i)
    ,.local_data_o(local_data_o)
    ,.local_last_o(local_last_o)
    ,.my_x_i(gbp_pkg::X_CORD_W'(1))
    ,.my_y_i(gbp_pkg::Y_CORD_W'(0))
  );

  assign phase_factor_first_o = u_dut.u_phase_controller.phase_factor_first_o;
  assign phase_switch_pulse_o = u_dut.u_phase_controller.phase_switch_pulse_o;
  assign no_schedulable_nodes_o = u_dut.u_node_scheduler.no_schedulable_nodes_o;
  assign visited_mask_o = u_dut.u_phase_controller.visited_mask_o;
  assign debug_spm_rd_addr_o = spm_rd_addr;
  assign debug_sched_node_id_o = u_dut.u_node_scheduler.sched_node_id_o;

endmodule
