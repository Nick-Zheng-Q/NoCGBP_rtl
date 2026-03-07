module spm_subsystem
  import gbp_pkg::*;
(
    input logic clk_i,
    input logic reset_i,

    mic_spm_arbiter_if.slave rd_if[2*NB],
    mic_spm_arbiter_wr_if.slave wr_if[2*NB]
);

  localparam int unsigned ROW_BYTES_LG = BYTE_OFF_W + WORD_OFF_W;

  spm_bank_if bank_if[NB]();

  logic [NB-1:0][1:0] rd_req_by_bank;
  logic [NB-1:0][1:0][ROW_ADDR_W-1:0] rd_row_by_bank;
  logic [NB-1:0][1:0] rd_ready_by_bank;
  logic [NB-1:0][1:0] rd_rsp_valid_by_bank;
  logic [NB-1:0][1:0][BEAT_BITS-1:0] rd_rsp_data_by_bank;

  logic [NB-1:0][1:0] wr_req_by_bank;
  logic [NB-1:0][1:0][ROW_ADDR_W-1:0] wr_row_by_bank;
  logic [NB-1:0][1:0][BEAT_BITS-1:0] wr_data_by_bank;
  logic [NB-1:0][1:0][WSTRB_W-1:0] wr_wstrb_by_bank;
  logic [NB-1:0][1:0] wr_ready_by_bank;

  logic [NB-1:0][1:0] rd_req_bytes_unused;

  for (genvar b = 0; b < NB; b++) begin : per_bank
    assign rd_req_bytes_unused[b][0] = rd_if[2*b].spm_rd_req_bytes;
    assign rd_req_bytes_unused[b][1] = rd_if[2*b+1].spm_rd_req_bytes;

    assign rd_req_by_bank[b][0] = rd_if[2*b].spm_rd_req_valid;
    assign rd_row_by_bank[b][0] = rd_if[2*b].spm_rd_req_addr[(ROW_BYTES_LG + BANK_ID_W) +: ROW_ADDR_W];
    assign rd_req_by_bank[b][1] = rd_if[2*b+1].spm_rd_req_valid;
    assign rd_row_by_bank[b][1] = rd_if[2*b+1].spm_rd_req_addr[(ROW_BYTES_LG + BANK_ID_W) +: ROW_ADDR_W];

    assign wr_req_by_bank[b][0] = wr_if[2*b].spm_wr_req_valid;
    assign wr_row_by_bank[b][0] = wr_if[2*b].spm_wr_req_addr[(ROW_BYTES_LG + BANK_ID_W) +: ROW_ADDR_W];
    assign wr_data_by_bank[b][0] = wr_if[2*b].spm_wr_req_data;
    assign wr_wstrb_by_bank[b][0] = wr_if[2*b].spm_wr_req_wstrb;
    assign wr_req_by_bank[b][1] = wr_if[2*b+1].spm_wr_req_valid;
    assign wr_row_by_bank[b][1] = wr_if[2*b+1].spm_wr_req_addr[(ROW_BYTES_LG + BANK_ID_W) +: ROW_ADDR_W];
    assign wr_data_by_bank[b][1] = wr_if[2*b+1].spm_wr_req_data;
    assign wr_wstrb_by_bank[b][1] = wr_if[2*b+1].spm_wr_req_wstrb;

    spm_rd_arbiter u_rd_arbiter (
        .clk_i(clk_i),
        .reset_i(reset_i),
        .req_i(rd_req_by_bank[b]),
        .row_i(rd_row_by_bank[b]),
        .ready_o(rd_ready_by_bank[b]),
        .bank_rd_en_o(bank_if[b].bank_rd_en),
        .bank_rd_row_o(bank_if[b].bank_rd_addr),
        .bank_rd_data_i(bank_if[b].bank_rd_data),
        .rsp_valid_o(rd_rsp_valid_by_bank[b]),
        .rsp_data_o(rd_rsp_data_by_bank[b])
    );

    spm_wr_arbiter u_wr_arbiter (
        .clk_i(clk_i),
        .reset_i(reset_i),
        .req_i(wr_req_by_bank[b]),
        .row_i(wr_row_by_bank[b]),
        .data_i(wr_data_by_bank[b]),
        .wstrb_i(wr_wstrb_by_bank[b]),
        .ready_o(wr_ready_by_bank[b]),
        .bank_wr_en_o(bank_if[b].bank_wr_en),
        .bank_wr_row_o(bank_if[b].bank_wr_addr),
        .bank_wr_data_o(bank_if[b].bank_wr_data),
        .bank_wr_wstrb_o(bank_if[b].bank_wr_wstrb)
    );

    assign bank_if[b].bank_rd_bank = BANK_ID_W'(b);
    assign bank_if[b].bank_wr_bank = BANK_ID_W'(b);

    assign rd_if[2*b].spm_rd_rsp_valid = rd_rsp_valid_by_bank[b][0];
    assign rd_if[2*b].spm_rd_rsp_data = rd_rsp_data_by_bank[b][0];
    assign rd_if[2*b+1].spm_rd_rsp_valid = rd_rsp_valid_by_bank[b][1];
    assign rd_if[2*b+1].spm_rd_rsp_data = rd_rsp_data_by_bank[b][1];

    assign wr_if[2*b].spm_wr_req_ready = wr_ready_by_bank[b][0];
    assign wr_if[2*b+1].spm_wr_req_ready = wr_ready_by_bank[b][1];
  end

  spm_bank_array u_spm_bank_array (
      .clk_i(clk_i),
      .reset_i(reset_i),
      .bank_if(bank_if)
  );

endmodule
