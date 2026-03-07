module spm_bank_array
  import gbp_pkg::*;
(
    input logic clk_i,
    input logic reset_i,

    spm_bank_if.slave bank_if[NB]
);

  for (genvar b = 0; b < NB; b++) begin : banks
    spm_bank u_spm_bank (
        .clk_i(clk_i),
        .reset_i(reset_i),
        .bank_rd_en(bank_if[b].bank_rd_en),
        .bank_rd_addr(bank_if[b].bank_rd_addr),
        .bank_rd_data(bank_if[b].bank_rd_data),
        .bank_wr_en(bank_if[b].bank_wr_en),
        .bank_wr_addr(bank_if[b].bank_wr_addr),
        .bank_wr_data(bank_if[b].bank_wr_data),
        .bank_wr_wstrb(bank_if[b].bank_wr_wstrb)
    );
  end

endmodule
