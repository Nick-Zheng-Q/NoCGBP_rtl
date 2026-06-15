// operand_stream_assembler_top.sv
// Unit-test wrapper for operand_stream_assembler.
// Includes a small synchronous SPM memory model to supply 64-bit beats.

module operand_stream_assembler_top
  import gbp_pkg::*;
  import gbp_op_pkg::*;
#(
    parameter int MEM_WORDS = 64
)(
  input  logic clk_i,
  input  logic reset_i,

  // Descriptor input
  input  logic                       desc_valid_i,
  output logic                       desc_ready_o,
  input  logic [3:0]                 desc_kind_i,
  input  logic [31:0]                desc_op_id_i,
  input  logic [SPM_ADDR_W-1:0]      desc_base_addr_i,
  input  logic [15:0]                desc_nbeats_i,

  // Operand stream output (flattened)
  output logic                       operand_valid_o,
  input  logic                       operand_ready_i,
  output logic [3:0]                 operand_kind_o,
  output logic                       operand_ctx_id_o,
  output logic [31:0]                operand_op_id_o,
  output logic [15:0]                operand_beat_idx_o,
  output logic                       operand_last_o,
  output logic [OPERAND_STREAM_WIDTH*32-1:0] operand_data_flat_o
);

  // ------------------------------------------------------------------
  // DUT
  // ------------------------------------------------------------------
  operand_stream_beat_t operand;
  logic                 spm_rd_valid;
  logic [SPM_ADDR_W-1:0] spm_rd_addr;
  logic [BEAT_BITS-1:0]  spm_rd_data;

  operand_stream_assembler u_dut (
    .clk_i,
    .rst_n_i (~reset_i),
    .desc_valid_i,
    .desc_ready_o,
    .desc_kind_i (operand_stream_kind_e'(desc_kind_i)),
    .desc_op_id_i,
    .desc_base_addr_i,
    .desc_nbeats_i,
    .operand_valid_o,
    .operand_ready_i,
    .operand_o (operand),
    .spm_rd_valid_o (spm_rd_valid),
    .spm_rd_ready_i (1'b1),
    .spm_rd_addr_o  (spm_rd_addr),
    .spm_rd_data_i  (spm_rd_data)
  );

  // Flatten operand output
  assign operand_kind_o     = operand.kind;
  assign operand_ctx_id_o   = operand.ctx_id;
  assign operand_op_id_o    = operand.op_id;
  assign operand_beat_idx_o = operand.beat_idx;
  assign operand_last_o     = operand.last;
  generate
    genvar g_i;
    for (g_i = 0; g_i < OPERAND_STREAM_WIDTH; g_i++) begin : gen_data
      assign operand_data_flat_o[g_i*32 +: 32] = operand.data[g_i];
    end
  endgenerate

  // ------------------------------------------------------------------
  // Small synchronous SPM memory model
  // ------------------------------------------------------------------
  logic [BEAT_BITS-1:0] mem [0:MEM_WORDS-1];
  logic [$clog2(MEM_WORDS)-1:0] rd_idx_r;
  logic                         rd_valid_r;

  initial begin
    for (int i = 0; i < MEM_WORDS; i++) begin
      mem[i] = {32'(i*2 + 1), 32'(i*2)};  // {upper_word, lower_word}
    end
  end

  // Registered read: read_stream_engine expects data one cycle after issue.
  always_ff @(posedge clk_i) begin
    if (reset_i) begin
      rd_valid_r <= 1'b0;
      rd_idx_r   <= '0;
    end else begin
      rd_valid_r <= spm_rd_valid;
      if (spm_rd_valid) begin
        // spm_rd_addr is a 32-bit word address; beat index is addr[ADDR_W-1:1].
        rd_idx_r <= spm_rd_addr[$clog2(MEM_WORDS):1];
      end
    end
  end

  assign spm_rd_data = rd_valid_r ? mem[rd_idx_r] : '0;

endmodule
