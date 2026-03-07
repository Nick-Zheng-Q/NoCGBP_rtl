module example_top (
  input  logic       clk,
  input  logic       rst_n,
  output logic [7:0] out
);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      out <= '0;
    end
    else begin
      out <= out + 8'd1;
    end
  end
endmodule
