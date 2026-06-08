// agu
// Address Generation Unit: sequential word-address generator.
// Produces addr_valid/addr for word_count cycles starting from base_addr.
// Advances only when addr_ready is asserted.

module agu #(
    parameter int SPM_ADDR_W = gbp_pkg::SPM_ADDR_W
)(
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

  logic active_r;
  logic [15:0] cnt_r;

  assign addr_valid = active_r;
  assign last_addr = active_r && (cnt_r == word_count - 1);

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      active_r <= 1'b0;
      cnt_r <= '0;
      addr <= '0;
    end else begin
      if (start && !active_r) begin
        active_r <= 1'b1;
        cnt_r <= '0;
        addr <= base_addr;
      end else if (active_r && addr_ready) begin
        if (cnt_r + 1 == word_count) begin
          active_r <= 1'b0;
        end else begin
          cnt_r <= cnt_r + 1;
          addr <= addr + 1'b1;
        end
      end
    end
  end

endmodule
