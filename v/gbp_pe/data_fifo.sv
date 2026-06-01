import gbp_pkg::*;
module data_fifo
#(
    parameter int unsigned width_p = BEAT_BITS,
    parameter int unsigned depth_p = DATA_ELS_DEPTH
)
(
    input logic clk_i,
    input logic reset_i,
    // in 
    input logic [width_p-1:0] data_i,
    input logic valid_i,
    output logic ready_o,
    // out
    output logic valid_o,
    output logic [width_p-1:0] data_o,
    input logic unqueue_i,
    // status
    output logic [7:0] occ,
    output logic afull
);

  localparam int unsigned COUNT_W = (depth_p <= 1) ? 1 : $clog2(depth_p + 1);
  logic [COUNT_W-1:0] count;

  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      count <= '0;
    end else begin
      case ({
        valid_i & ready_o, unqueue_i
      })
        2'b10:   count <= count + 1'b1;  // enqueue
        2'b01:   count <= count - 1'b1;  // dequeue
        2'b11:   count <= count;  // simultaneous enqueue/dequeue
        default: count <= count;  // no change
      endcase
    end
  end

  assign occ   = count;
  assign afull = (count >= depth_p - 1);

  bsg_fifo_1r1w_large #(
      .width_p(width_p),
      .els_p  (depth_p)
  ) data_fifo (
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
