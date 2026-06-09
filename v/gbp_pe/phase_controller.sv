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
    , input  logic rst_n_i

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

  logic rst_i;
  assign rst_i = ~rst_n_i;

  logic phase_factor_r;
  logic [NUM_NODES-1:0] visited_mask_r;
  logic no_schedulable_nodes_r;

  // phase_switch_pulse_o stays high for polling compatibility;
  // phase_factor_r toggles only on the rising edge.
  assign phase_switch_pulse_o = no_schedulable_nodes_i;

  // Registered: phase toggles on next clock edge
  assign phase_factor_first_o = phase_factor_r;
  assign visited_mask_o = visited_mask_r;

  always_ff @(posedge clk_i) begin
    if (rst_i) begin
      phase_factor_r <= 1'b1;  // start in FACTOR_PHASE
      visited_mask_r <= '0;
      no_schedulable_nodes_r <= 1'b0;
    end else begin
      no_schedulable_nodes_r <= no_schedulable_nodes_i;

      // Clear visited mask whenever no_schedulable_nodes is asserted
      // (matches original behavior; ensures mask is cleared even if
      // sched_valid_i is still high from the previous cycle).
      if (no_schedulable_nodes_i) begin
        visited_mask_r <= '0;
      end

      // Toggle phase only on rising edge of no_schedulable_nodes
      if (no_schedulable_nodes_i && !no_schedulable_nodes_r) begin
        phase_factor_r <= ~phase_factor_r;
      end

      // Track visited nodes (overrides the clear for the scheduled node)
      if (sched_valid_i) begin
        visited_mask_r[sched_node_id_i] <= 1'b1;
      end
    end
  end

endmodule
