// phase_controller_top.sv
// Unit test wrapper for phase_controller

module phase_controller_top #(
    parameter NUM_NODES = 1024
) (
    input  logic clk
    , input  logic rst_n
    , input  logic sched_valid_i
    , input  logic [12:0] sched_node_id_i
    , input  logic no_schedulable_nodes_i
    , input  logic wb_done_i
    , output logic phase_factor_first_o
    , output logic phase_switch_pulse_o
    , output logic [NUM_NODES-1:0] visited_mask_o
);

  phase_controller #(
    .NUM_NODES(NUM_NODES)
  ) dut (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.sched_valid_i(sched_valid_i)
    ,.sched_node_id_i(sched_node_id_i)
    ,.no_schedulable_nodes_i(no_schedulable_nodes_i)
    ,.wb_done_i(wb_done_i)
    ,.phase_factor_first_o(phase_factor_first_o)
    ,.phase_switch_pulse_o(phase_switch_pulse_o)
    ,.visited_mask_o(visited_mask_o)
  );

endmodule
