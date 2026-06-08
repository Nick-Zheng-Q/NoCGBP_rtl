// agu_top.sv
// Unit test wrapper for agu

module agu_top #(
    parameter int SPM_ADDR_W = 18
) (
    input  logic clk,
    input  logic rst_n,
    input  logic start_i,
    input  logic [SPM_ADDR_W-1:0] base_addr_i,
    input  logic [15:0] word_count_i,
    output logic addr_valid_o,
    input  logic addr_ready_i,
    output logic [SPM_ADDR_W-1:0] addr_o,
    output logic last_addr_o
);

  agu #(
    .SPM_ADDR_W(SPM_ADDR_W)
  ) dut (
    .clk_i(clk),
    .rst_n_i(rst_n),
    .start_i(start_i),
    .base_addr_i(base_addr_i),
    .word_count_i(word_count_i),
    .addr_valid_o(addr_valid_o),
    .addr_ready_i(addr_ready_i),
    .addr_o(addr_o),
    .last_addr_o(last_addr_o)
  );

endmodule
