// phase_controller.sv
// Manages factor/variable phase alternation.
// phase_switch_pulse = combinational (same cycle as no_schedulable_nodes)
// phase_factor_first = registered (changes next cycle after pulse)

module phase_controller
  import gbp_pkg::*;
#(
    parameter int NUM_NODES = gbp_pkg::NUM_NODES_PER_PE
) (
    input  logic clk_i
    , input  logic rst_i

    // Node Scheduler feedback
    , input  logic sched_valid_i           // scheduler selected a node this cycle
    , input  logic [NODE_ID_W-1:0] sched_node_id_i  // which node was selected
    , input  logic no_schedulable_nodes_i  // current phase has no ready nodes

    // Writeback Controller feedback
    , input  logic wb_done_i               // node update complete

    // Phase output
    , output logic phase_factor_first_o     // 1 = factor phase, 0 = variable phase
    , output logic phase_switch_pulse_o     // combinational pulse, same cycle as no_sched
    , output logic [NUM_NODES-1:0] visited_mask_o  // nodes already computed this phase
);

  logic phase_factor_r;
  logic [NUM_NODES-1:0] visited_mask_r;

  // Combinational: pulse when no_schedulable_nodes is asserted
  assign phase_switch_pulse_o = no_schedulable_nodes_i;

  // Registered: phase toggles on next clock edge
  assign phase_factor_first_o = phase_factor_r;
  assign visited_mask_o = visited_mask_r;

  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      phase_factor_r <= 1'b1;  // start in FACTOR_PHASE
      visited_mask_r <= '0;
    end else begin
      // Toggle phase when no_schedulable_nodes is asserted
      if (no_schedulable_nodes_i) begin
        phase_factor_r <= ~phase_factor_r;
        visited_mask_r <= '0;  // reset on phase switch
      end

      // Track visited nodes
      if (sched_valid_i) begin
        visited_mask_r[sched_node_id_i] <= 1'b1;
      end
    end
  end

endmodule
