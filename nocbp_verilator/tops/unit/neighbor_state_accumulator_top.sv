// neighbor_state_accumulator_top.sv
// Unit test wrapper for neighbor_state_accumulator

module neighbor_state_accumulator_top (
    input  logic        clk
    , input  logic        rst_n
    // Local input
    , input  logic        local_valid_i
    , input  logic [31:0] local_data_i
    , input  logic        local_last_i
    // Remote input
    , input  logic        remote_valid_i
    , input  logic [31:0] remote_data_i
    , input  logic        remote_last_i
    // Output
    , output logic        out_valid_o
    , input  logic        out_ready_i
    , output logic [31:0] out_data_o
    , output logic        out_last_o
    // Pipeline control
    , input  logic        start_i
    , input  logic        has_remote_i
    // Status
    , output logic        accumulator_done_o
    , output logic        local_ready_o
    , output logic        remote_ready_o
);

  neighbor_state_accumulator dut (
    .clk_i(clk)
    ,.rst_n_i(rst_n)
    ,.local_valid_i(local_valid_i)
    ,.local_ready_o(local_ready_o)
    ,.local_data_i(local_data_i)
    ,.local_last_i(local_last_i)
    ,.remote_valid_i(remote_valid_i)
    ,.remote_ready_o(remote_ready_o)
    ,.remote_data_i(remote_data_i)
    ,.remote_last_i(remote_last_i)
    ,.out_valid_o(out_valid_o)
    ,.out_ready_i(out_ready_i)
    ,.out_data_o(out_data_o)
    ,.out_last_o(out_last_o)
    ,.start_i(start_i)
    ,.has_remote_i(has_remote_i)
    ,.accumulator_done_o(accumulator_done_o)
  );

endmodule
