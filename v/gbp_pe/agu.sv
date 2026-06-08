// agu
// Address Generation Unit: sequential word-address generator.
// Produces addr_valid/addr for word_count cycles starting from base_addr.
// Advances only when addr_ready is asserted.

module agu #(
    parameter int SPM_ADDR_W = gbp_pkg::SPM_ADDR_W
)(
    input  logic clk_i,
    input  logic rst_n_i,

    input  logic start_i,
    input  logic [SPM_ADDR_W-1:0] base_addr_i,
    input  logic [15:0] word_count_i,

    output logic addr_valid_o,
    input  logic addr_ready_i,
    output logic [SPM_ADDR_W-1:0] addr_o,
    output logic last_addr_o
);

  logic active_r;
  logic [15:0] cnt_r;

  assign addr_valid_o = active_r;
  assign last_addr_o = active_r && (cnt_r == word_count_i - 1);

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      active_r <= 1'b0;
      cnt_r <= '0;
      addr_o <= '0;
    end else begin
      if (start_i && !active_r) begin
        active_r <= 1'b1;
        cnt_r <= '0;
        addr_o <= base_addr_i;
      end else if (active_r && addr_ready_i) begin
        if (cnt_r + 1 == word_count_i) begin
          active_r <= 1'b0;
        end else begin
          cnt_r <= cnt_r + 1;
          addr_o <= addr_o + 1'b1;
        end
      end
    end
  end

endmodule
