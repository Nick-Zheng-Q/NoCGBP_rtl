module addr_fifo
  import gbp_pkg::*;
(
    input logic clk_i,
    input logic reset_i,
    // in 
    input logic [SPM_ADDR_W-1:0] data_i,
    input logic valid_i,
    output logic ready_o,
    // out
    output logic valid_o,
    output logic [SPM_ADDR_W-1:0] data_o,
    input logic unqueue_i
);

  bsg_fifo_1r1w_large #(
      .width_p(SPM_ADDR_W),
      .els_p  (ADDR_ELS_DEPTH)
  ) addr_fifo (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .data_i(data_i),
      .v_i(valid_i),
      .ready_and_o(ready_o),
      .v_o(valid_o),
      .data_o(data_o),
      .yumi_i(unqueue_i)
  );

endmodule
