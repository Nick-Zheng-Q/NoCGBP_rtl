// agu_top.sv
// Unit test wrapper for agu

module agu_top #(
    parameter int SPM_ADDR_W = 18
) (
    input  logic clk,
    input  logic rst_n,
    input  logic start,
    input  logic [SPM_ADDR_W-1:0] base_addr,
    input  logic [15:0] word_count,
    output logic addr_valid,
    input  logic addr_ready,
    output logic [SPM_ADDR_W-1:0] addr,
    output logic last_addr
);

  agu #(
    .SPM_ADDR_W(SPM_ADDR_W)
  ) dut (
    .clk(clk),
    .rst_n(rst_n),
    .start(start),
    .base_addr(base_addr),
    .word_count(word_count),
    .addr_valid(addr_valid),
    .addr_ready(addr_ready),
    .addr(addr),
    .last_addr(last_addr)
  );

endmodule
