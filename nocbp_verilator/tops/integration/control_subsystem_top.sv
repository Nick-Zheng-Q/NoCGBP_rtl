// control_subsystem_top.sv
// Integration test top for Control Subsystem:
// phase_controller + node_scheduler + metadata_scanner

module control_subsystem_top #(
    parameter int NUM_NODES = 64,
    parameter int NODE_ID_W = 6
) (
    input  logic clk,
    input  logic rst_n,

    // Mocked inputs
    input  logic [NUM_NODES-1:0] node_ready,
    input  logic wb_done,
    input  logic spm_rd_ready,
    input  logic [63:0] spm_rd_data,
    input  logic adj_ready,
    input  logic [5:0] my_x,
    input  logic [4:0] my_y,

    // Outputs
    output logic spm_rd_valid,
    output logic [17:0] spm_rd_addr,
    output logic adj_valid,
    output logic [9:0] adj_neighbor_id,
    output logic [5:0] adj_neighbor_x,
    output logic [4:0] adj_neighbor_y,
    output logic adj_is_local,
    output logic adj_last,
    output logic [3:0] adj_edge_idx,
    output logic info_valid,
    output logic [3:0] info_dof,
    output logic [3:0] info_adj_count,
    output logic [17:0] info_state_base,
    output logic [5:0] info_state_words,
    output logic phase_factor_first,
    output logic phase_switch_pulse,
    output logic [NUM_NODES-1:0] visited_mask,
    output logic sched_valid,
    output logic [NODE_ID_W-1:0] sched_node_id,
    output logic sched_is_factor,
    output logic no_schedulable_nodes
);

  logic rst_i;
  assign rst_i = ~rst_n;

  // Internal signals
  logic sched_ready;

  // Phase Controller
  phase_controller #(
    .NUM_NODES(NUM_NODES)
  ) u_phase_controller (
    .clk_i(clk),
    .rst_i(rst_i),
    .sched_valid_i(sched_valid),
    .sched_node_id_i(sched_node_id),
    .no_schedulable_nodes_i(no_schedulable_nodes),
    .wb_done_i(wb_done),
    .phase_factor_first_o(phase_factor_first),
    .phase_switch_pulse_o(phase_switch_pulse),
    .visited_mask_o(visited_mask)
  );

  // Node Scheduler
  node_scheduler #(
    .NUM_NODES(NUM_NODES),
    .NODE_ID_W(NODE_ID_W)
  ) u_node_scheduler (
    .clk(clk),
    .rst_n(rst_n),
    .phase_factor_first(phase_factor_first),
    .node_ready(node_ready),
    .visited_mask(visited_mask),
    .sched_ready(sched_ready),
    .sched_valid(sched_valid),
    .sched_node_id(sched_node_id),
    .sched_is_factor(sched_is_factor),
    .no_schedulable_nodes(no_schedulable_nodes)
  );

  // Metadata Scanner
  metadata_scanner u_metadata_scanner (
    .clk_i(clk),
    .rst_i(rst_i),
    .cmd_valid_i(sched_valid),
    .cmd_node_id_i(sched_node_id),
    .cmd_is_factor_i(sched_is_factor),
    .cmd_ready_o(sched_ready),
    .spm_rd_valid_o(spm_rd_valid),
    .spm_rd_addr_o(spm_rd_addr),
    .spm_rd_ready_i(spm_rd_ready),
    .spm_rd_data_i(spm_rd_data),
    .adj_valid_o(adj_valid),
    .adj_ready_i(adj_ready),
    .adj_neighbor_id_o(adj_neighbor_id),
    .adj_neighbor_x_o(adj_neighbor_x),
    .adj_neighbor_y_o(adj_neighbor_y),
    .adj_is_local_o(adj_is_local),
    .adj_last_o(adj_last),
    .adj_edge_idx_o(adj_edge_idx),
    .info_valid_o(info_valid),
    .info_dof_o(info_dof),
    .info_adj_count_o(info_adj_count),
    .info_state_base_o(info_state_base),
    .info_state_words_o(info_state_words),
    .my_x_i(my_x),
    .my_y_i(my_y)
  );

endmodule
