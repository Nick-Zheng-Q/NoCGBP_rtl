module gbp_pe (
  input logic clk,
  input logic rst_n,
  input logic [31:0] data_i,
  input logic [31:0] data_b_i,
  input logic [1:0] op_i,
  input logic length_i,
  output logic [31:0] data_o
);

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      data_o <= '0;
    end else if (length_i) begin
      unique case (op_i)
        2'b00: data_o <= data_i + data_b_i;
        2'b01: data_o <= data_i - data_b_i;
        2'b10: data_o <= data_i ^ data_b_i;
        default: data_o <= data_i * data_b_i;
      endcase
    end
  end

endmodule
