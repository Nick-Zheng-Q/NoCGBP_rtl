// node_scheduler_top.sv
// Unit test wrapper for node_scheduler

module node_scheduler_top #(
    parameter int NUM_NODES = 1024,
    parameter int NODE_ID_W = 10
) (
    input  logic clk,
    input  logic rst_n,
    input  logic phase_factor_first,
    input  logic [NUM_NODES-1:0] node_ready,
    input  logic [NUM_NODES-1:0] visited_mask,
    input  logic sched_ready,
    output logic sched_valid,
    output logic [NODE_ID_W-1:0] sched_node_id,
    output logic sched_is_factor,
    output logic no_schedulable_nodes
);

  node_scheduler #(
    .NUM_NODES(NUM_NODES),
    .NODE_ID_W(NODE_ID_W)
  ) dut (
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

endmodule
