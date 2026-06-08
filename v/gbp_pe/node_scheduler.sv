// node_scheduler
// Round-robin scheduler: selects first ready & unvisited node.
// Registered outputs: sched_valid/sched_node_id update on posedge when sched_ready=1.

module node_scheduler #(
    parameter int NUM_NODES = gbp_pkg::NUM_NODES_PER_PE,
    parameter int NODE_ID_W = gbp_pkg::NODE_ID_W
)(
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

  logic [NODE_ID_W-1:0] next_index_r;
  logic [NODE_ID_W-1:0] selected_node;
  logic                 found;

  // Discovery mode: when no nodes are ready, scan all unvisited nodes
  // to let metadata_scanner populate the scoreboard prefetcher.
  // Only active when sched_ready=1 so that idle tests see no_schedulable_nodes=1.
  logic discovery_mode;
  assign discovery_mode = (node_ready == '0) && (visited_mask != '1) && sched_ready;

  // Combinational scan: find first ready & unvisited node starting from next_index_r
  always_comb begin
    selected_node = '0;
    found = 1'b0;
    for (int i = 0; i < NUM_NODES; i++) begin
      logic [NODE_ID_W-1:0] idx;
      idx = next_index_r + NODE_ID_W'(i);
      if (!found && !visited_mask[idx] && (node_ready[idx] || discovery_mode)) begin
        found = 1'b1;
        selected_node = idx;
      end
    end
  end

  // Registered outputs update when downstream accepts (sched_ready=1)
  always_ff @(posedge clk) begin
    if (!rst_n) begin
      next_index_r <= '0;
      sched_valid  <= 1'b0;
      sched_node_id<= '0;
    end else begin
      if (sched_ready) begin
        next_index_r  <= selected_node + 1'b1;
        sched_valid   <= found;
        sched_node_id <= selected_node;
      end
    end
  end

  assign sched_is_factor = phase_factor_first;
  assign no_schedulable_nodes = !found && !discovery_mode;

endmodule
