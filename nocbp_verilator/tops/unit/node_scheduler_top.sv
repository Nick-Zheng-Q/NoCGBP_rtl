// node_scheduler_top.sv
// Unit test wrapper for node_scheduler

module node_scheduler_top #(
    parameter int NUM_NODES = 1024,
    parameter int NODE_ID_W = 10
) (
    input  logic clk,
    input  logic rst_n,
    input  logic phase_factor_first_i,
    input  logic [NUM_NODES-1:0] node_ready_i,
    input  logic [NUM_NODES-1:0] visited_mask_i,
    input  logic sched_ready_i,
    output logic sched_valid_o,
    output logic [NODE_ID_W-1:0] sched_node_id_o,
    output logic sched_is_factor_o,
    output logic no_schedulable_nodes_o
);

  node_scheduler #(
    .NUM_NODES(NUM_NODES),
    .NODE_ID_W(NODE_ID_W)
  ) dut (
    .clk_i(clk),
    .rst_n_i(rst_n),
    .phase_factor_first_i(phase_factor_first_i),
    .node_ready_i(node_ready_i),
    .visited_mask_i(visited_mask_i),
    .sched_ready_i(sched_ready_i),
    .sched_valid_o(sched_valid_o),
    .sched_node_id_o(sched_node_id_o),
    .sched_is_factor_o(sched_is_factor_o),
    .no_schedulable_nodes_o(no_schedulable_nodes_o)
  );

endmodule
