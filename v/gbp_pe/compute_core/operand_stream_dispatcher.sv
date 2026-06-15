// operand_stream_dispatcher.sv
// Small descriptor FIFO that lets compute_unit_wrapper issue all of its
// operand descriptors while a single operand_stream_assembler processes them
// one at a time.

module operand_stream_dispatcher
  import gbp_pkg::*;
  import gbp_op_pkg::*;
#(
    parameter int SPM_ADDR_W = gbp_pkg::SPM_ADDR_W,
    parameter int DEPTH      = 8
) (
    input logic clk_i,
    input logic rst_n_i,

    // -- Read-request descriptors from compute_unit_wrapper
    input  logic                                  req_valid_i,
    output logic                                  req_ready_o,
    input  logic                 [          31:0] req_op_id_i,
    input  operand_stream_kind_e                  req_kind_i,
    input  logic                 [SPM_ADDR_W-1:0] req_base_addr_i,
    input  logic                 [          15:0] req_nbeats_i,

    // -- Descriptors to operand_stream_assembler
    output logic                                  desc_valid_o,
    input  logic                                  desc_ready_i,
    output logic                 [          31:0] desc_op_id_o,
    output operand_stream_kind_e                  desc_kind_o,
    output logic                 [SPM_ADDR_W-1:0] desc_base_addr_o,
    output logic                 [          15:0] desc_nbeats_o
);

  localparam int PTR_W = $clog2(DEPTH);

  typedef struct packed {
    logic [31:0]           op_id;
    operand_stream_kind_e  kind;
    logic [SPM_ADDR_W-1:0] base_addr;
    logic [15:0]           nbeats;
  } desc_entry_t;

  desc_entry_t           fifo   [DEPTH];

  logic        [PTR_W:0] wr_ptr;
  logic        [PTR_W:0] rd_ptr;
  logic                  empty;
  logic                  full;

  assign empty = (wr_ptr == rd_ptr);
  assign full = (wr_ptr[PTR_W] != rd_ptr[PTR_W]) && (wr_ptr[PTR_W-1:0] == rd_ptr[PTR_W-1:0]);

  assign req_ready_o = ~full;

  // ---------- write side (unchanged) ----------
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      wr_ptr <= '0;
    end else if (req_valid_i && req_ready_o) begin
      fifo[wr_ptr[PTR_W-1:0]] <= '{
          op_id: req_op_id_i,
          kind: req_kind_i,
          base_addr: req_base_addr_i,
          nbeats: req_nbeats_i
      };
      wr_ptr <= wr_ptr + 1'b1;
    end
  end

  // ---------- read side: registered output ----------
  desc_entry_t desc_out;
  logic        desc_valid_r;

  assign desc_valid_o     = desc_valid_r;
  assign desc_op_id_o     = desc_out.op_id;
  assign desc_kind_o      = desc_out.kind;
  assign desc_base_addr_o = desc_out.base_addr;
  assign desc_nbeats_o    = desc_out.nbeats;

  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      rd_ptr       <= '0;
      desc_out     <= '0;
      desc_valid_r <= 1'b0;
    end else begin
      if (desc_valid_r && desc_ready_i) begin
        rd_ptr       <= rd_ptr + 1'b1;
        desc_valid_r <= 1'b0;
      end
      if (!desc_valid_r && !empty) begin
        desc_out     <= fifo[rd_ptr[PTR_W-1:0]];
        desc_valid_r <= 1'b1;
      end
    end
  end

endmodule
