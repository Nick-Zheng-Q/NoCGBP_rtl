// spm_arbiter
// Multi-client SPM bank arbiter with round-robin per-bank grant.
// Supports simultaneous reads to different banks and simultaneous read+write
// to the same bank. Multiple requests to the same bank are serialized
// round-robin across cycles.

module spm_arbiter #(
    parameter int NUM_BANKS   = gbp_pkg::NUM_BANKS,
    parameter int NUM_CLIENTS = 7,
    parameter int SPM_ADDR_W  = gbp_pkg::SPM_ADDR_W,
    parameter int BEAT_BITS   = gbp_pkg::BEAT_BITS,
    parameter int ROW_ADDR_W  = gbp_pkg::ROW_ADDR_W,
    parameter int WSTRB_W     = gbp_pkg::WSTRB_W
)(
    input  logic clk,
    input  logic rst_n,

    // Client read ports
    input  logic [NUM_CLIENTS-1:0]              rd_valid,
    output logic [NUM_CLIENTS-1:0]              rd_ready,
    input  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] rd_addr,
    output logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0]   rd_data,

    // Client write ports
    input  logic [NUM_CLIENTS-1:0]              wr_valid,
    output logic [NUM_CLIENTS-1:0]              wr_ready,
    input  logic [NUM_CLIENTS-1:0][SPM_ADDR_W-1:0] wr_addr,
    input  logic [NUM_CLIENTS-1:0][BEAT_BITS-1:0]   wr_data,
    input  logic [NUM_CLIENTS-1:0][WSTRB_W-1:0] wr_wstrb,

    // Bank read ports (1-cycle latency)
    output logic [NUM_BANKS-1:0] bank_rd_en,
    output logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0] bank_rd_addr,
    input  logic [NUM_BANKS-1:0][BEAT_BITS-1:0] bank_rd_data,

    // Bank write ports (combinational)
    output logic [NUM_BANKS-1:0] bank_wr_en,
    output logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0] bank_wr_addr,
    output logic [NUM_BANKS-1:0][BEAT_BITS-1:0] bank_wr_data,
    output logic [NUM_BANKS-1:0][WSTRB_W-1:0] bank_wr_wstrb
);

  localparam int BANK_ID_W = $clog2(NUM_BANKS);
  localparam int WORD_OFF_W_LP = $clog2(BEAT_BITS / 32);  // = 1 for 64-bit beat
  localparam int ROW_LSB_LP = WORD_OFF_W_LP + BANK_ID_W;  // = 4 for 64-bit beat

  // Round-robin base pointer: advances every cycle to prevent starvation
  logic [$clog2(NUM_CLIENTS)-1:0] rr_base_r;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      rr_base_r <= '0;
    end else begin
      rr_base_r <= rr_base_r + 1'b1;
    end
  end

  // -------------------------------------------------------------------------
  // Read arbitration and bank drive (round-robin)
  // -------------------------------------------------------------------------
  logic [NUM_CLIENTS-1:0] rd_grant_oh;
  logic [NUM_BANKS-1:0] bank_rd_en_tmp;
  logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0] bank_rd_addr_tmp;

  always_comb begin
    rd_grant_oh = '0;
    bank_rd_en_tmp = '0;
    bank_rd_addr_tmp = '0;
    for (int bi = 0; bi < NUM_CLIENTS; bi++) begin
      logic [$clog2(NUM_CLIENTS)-1:0] c;
      logic [BANK_ID_W-1:0] b;
      c = rr_base_r + $clog2(NUM_CLIENTS)'(bi);
      b = rd_addr[c][WORD_OFF_W_LP +: BANK_ID_W];
      if (rd_valid[c] && !rd_grant_oh[c] && !bank_rd_en_tmp[b]) begin
        bank_rd_en_tmp[b] = 1'b1;
        bank_rd_addr_tmp[b] = rd_addr[c][ROW_LSB_LP +: ROW_ADDR_W];
        rd_grant_oh[c] = 1'b1;
      end
    end
  end

  assign bank_rd_en   = bank_rd_en_tmp;
  assign bank_rd_addr = bank_rd_addr_tmp;

  // -------------------------------------------------------------------------
  // Read response pipeline (1-cycle latency)
  // -------------------------------------------------------------------------
  logic [NUM_CLIENTS-1:0] rd_grant_r;
  logic [NUM_CLIENTS-1:0][BANK_ID_W-1:0] rd_client_bank_r;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      rd_grant_r <= '0;
    end else begin
      rd_grant_r <= rd_grant_oh;
      for (int c = 0; c < NUM_CLIENTS; c++) begin
        if (rd_grant_oh[c]) begin
          rd_client_bank_r[c] <= rd_addr[c][WORD_OFF_W_LP +: BANK_ID_W];
        end
      end
    end
  end

  for (genvar c = 0; c < NUM_CLIENTS; c++) begin : gen_rd_resp
    assign rd_ready[c] = rd_grant_r[c];
    assign rd_data[c]  = bank_rd_data[rd_client_bank_r[c]];
  end

  // -------------------------------------------------------------------------
  // Write arbitration and bank drive (round-robin)
  // -------------------------------------------------------------------------
  logic [NUM_CLIENTS-1:0] wr_grant_oh;
  logic [NUM_BANKS-1:0] bank_wr_en_tmp;
  logic [NUM_BANKS-1:0][ROW_ADDR_W-1:0] bank_wr_addr_tmp;
  logic [NUM_BANKS-1:0][BEAT_BITS-1:0] bank_wr_data_tmp;
  logic [NUM_BANKS-1:0][WSTRB_W-1:0] bank_wr_wstrb_tmp;

  always_comb begin
    wr_grant_oh = '0;
    bank_wr_en_tmp = '0;
    bank_wr_addr_tmp = '0;
    bank_wr_data_tmp = '0;
    bank_wr_wstrb_tmp = '0;
    for (int bi = 0; bi < NUM_CLIENTS; bi++) begin
      logic [$clog2(NUM_CLIENTS)-1:0] c;
      logic [BANK_ID_W-1:0] b;
      c = rr_base_r + $clog2(NUM_CLIENTS)'(bi);
      b = wr_addr[c][WORD_OFF_W_LP +: BANK_ID_W];
      if (wr_valid[c] && !wr_grant_oh[c] && !bank_wr_en_tmp[b]) begin
        bank_wr_en_tmp[b] = 1'b1;
        bank_wr_addr_tmp[b] = wr_addr[c][ROW_LSB_LP +: ROW_ADDR_W];
        bank_wr_data_tmp[b] = wr_data[c];
        bank_wr_wstrb_tmp[b] = wr_wstrb[c];
        wr_grant_oh[c] = 1'b1;
      end
    end
  end

  assign bank_wr_en   = bank_wr_en_tmp;
  assign bank_wr_addr = bank_wr_addr_tmp;
  assign bank_wr_data = bank_wr_data_tmp;
  assign bank_wr_wstrb = bank_wr_wstrb_tmp;

  for (genvar c = 0; c < NUM_CLIENTS; c++) begin : gen_wr_ready
    assign wr_ready[c] = wr_grant_oh[c];
  end

endmodule
