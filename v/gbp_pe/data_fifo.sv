module data_fifo
  import gbp_pkg::*;
(
    input logic clk_i,
    input logic reset_i,
    // in 
    input logic [BEAT_BYTES-1:0] data_i,
    input logic valid_i,
    output logic ready_o,
    // out
    output logic valid_o,
    output logic [BEAT_BYTES-1:0] data_o,
    input logic unqueue_i,
    // status
    output logic [7:0] occ,
    output logic afull
);

  logic [DATA_ELS_DEPTH-1:0] count;

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
  assign afull = (count >= DATA_ELS_DEPTH - 1);

  bsg_fifo_1r1w_large #(
      .width_p(BEAT_BYTES),
      .els_p  (DATA_ELS_DEPTH)
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
