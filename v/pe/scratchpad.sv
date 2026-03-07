// ScratchPad memory template (1R1W, sync read)
module scratchpad #(
    parameter int data_width_p = 32
    , parameter int addr_width_p = 10
    , parameter int num_banks_p = 1
    , localparam int els_lp = (1 << addr_width_p)
    , localparam int lg_banks_lp = (num_banks_p > 1) ? $clog2(num_banks_p) : 1
    , localparam int bank_addr_width_lp = addr_width_p - ((num_banks_p > 1) ? $clog2(
        num_banks_p
    ) : 0)
    , localparam int els_per_bank_lp = (1 << bank_addr_width_lp)
) (
      input logic clk_i
    , input logic reset_i

    // write port
    , input logic w_v_i
    , input logic [addr_width_p-1:0] w_addr_i
    , input logic [data_width_p-1:0] w_data_i

    // read port (1-cycle latency)
    , input logic r_v_i
    , input logic [addr_width_p-1:0] r_addr_i
    , output logic [data_width_p-1:0] r_data_o
    , output logic r_v_o
);

  // synthesis translate_off
  initial begin
    if (num_banks_p < 1) begin
      $error("scratchpad: num_banks_p must be >= 1");
      $finish();
    end
    if ((num_banks_p & (num_banks_p - 1)) != 0) begin
      $error("scratchpad: num_banks_p must be power of two");
      $finish();
    end
    if (addr_width_p <= $clog2(num_banks_p)) begin
      $error("scratchpad: addr_width_p too small for num_banks_p");
      $finish();
    end
  end
  // synthesis translate_on

  generate
    if (num_banks_p == 1) begin : gen_single_bank
      logic [data_width_p-1:0] mem[0:els_lp-1];

      always_ff @(posedge clk_i) begin
        if (w_v_i) begin
          mem[w_addr_i] <= w_data_i;
        end
      end

      always_ff @(posedge clk_i) begin
        if (reset_i) begin
          r_data_o <= '0;
          r_v_o <= 1'b0;
        end else begin
          r_v_o <= r_v_i;
          if (r_v_i) begin
            r_data_o <= mem[r_addr_i];
          end
        end
      end
    end else begin : gen_multi_bank
      logic [data_width_p-1:0] mem[0:num_banks_p-1][0:els_per_bank_lp-1];
      logic [lg_banks_lp-1:0] r_bank_sel;
      logic [lg_banks_lp-1:0] w_bank_sel;
      logic [bank_addr_width_lp-1:0] r_bank_addr;
      logic [bank_addr_width_lp-1:0] w_bank_addr;

      assign r_bank_sel  = r_addr_i[0+:lg_banks_lp];
      assign w_bank_sel  = w_addr_i[0+:lg_banks_lp];
      assign r_bank_addr = r_addr_i[lg_banks_lp+:bank_addr_width_lp];
      assign w_bank_addr = w_addr_i[lg_banks_lp+:bank_addr_width_lp];

      always_ff @(posedge clk_i) begin
        if (w_v_i) begin
          mem[w_bank_sel][w_bank_addr] <= w_data_i;
        end
      end

      always_ff @(posedge clk_i) begin
        if (reset_i) begin
          r_data_o <= '0;
          r_v_o <= 1'b0;
        end else begin
          r_v_o <= r_v_i;
          if (r_v_i) begin
            r_data_o <= mem[r_bank_sel][r_bank_addr];
          end
        end
      end
    end
  endgenerate

endmodule
