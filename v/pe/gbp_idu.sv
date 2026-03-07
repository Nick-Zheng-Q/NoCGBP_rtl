typedef enum logic [2:0] {
  UOP_CLASS_VEC    = 3'b000,
  UOP_CLASS_REDUCE = 3'b001,
  UOP_CLASS_MAT    = 3'b010,
  UOP_CLASS_SOLVE  = 3'b011,
  UOP_CLASS_GBP    = 3'b100
} gbp_uop_class_e;

typedef enum logic [1:0] {
  MEM_NONE   = 2'b00,
  MEM_STREAM = 2'b01,
  MEM_STRIDE = 2'b10,
  MEM_GATHER = 2'b11
} gbp_mem_mode_e;

typedef struct packed {
  gbp_uop_class_e uop_class;
  logic [5:0] uop_op;
  logic [4:0] rd;
  logic [4:0] rs1;
  logic [4:0] rs2;
  logic [4:0] rs3;
  logic [15:0] imm;
  gbp_mem_mode_e mem_mode;
  logic mem_write;
  logic [7:0] mem_len;
  logic use_imm;
} gbp_uop_s_t;

module gbp_idu #(
      parameter int INST_WIDTH_P   = 32
    , parameter int OPCODE_WIDTH_P = 6
) (
    input logic clk_i,
    input logic reset_i,

    input logic inst_v_i,
    input logic [INST_WIDTH_P-1:0] inst_i,
    input logic issue_ready_i,

    output logic inst_ready_o,
    output logic uop_v_o,
    output logic illegal_o,
    output gbp_uop_s uop_o
);

  localparam logic [OPCODE_WIDTH_P-1:0] OP_VEC = 6'h00;
  localparam logic [OPCODE_WIDTH_P-1:0] OP_REDUCE = 6'h01;
  localparam logic [OPCODE_WIDTH_P-1:0] OP_MAT = 6'h02;
  localparam logic [OPCODE_WIDTH_P-1:0] OP_SOLVE = 6'h03;
  localparam logic [OPCODE_WIDTH_P-1:0] OP_GBP = 6'h04;

  logic [OPCODE_WIDTH_P-1:0] opcode;
  localparam int reg_addr_width_lp = 5;
  localparam int subop_width_lp = 6;
  localparam int imm_width_lp = 16;

  logic [reg_addr_width_lp-1:0] rd;
  logic [reg_addr_width_lp-1:0] rs1;
  logic [reg_addr_width_lp-1:0] rs2;
  logic [reg_addr_width_lp-1:0] rs3;
  logic [subop_width_lp-1:0] func;
  logic [imm_width_lp-1:0] imm;

  assign opcode = inst_i[INST_WIDTH_P-1-:OPCODE_WIDTH_P];
  assign rd = inst_i[25:21];
  assign rs1 = inst_i[20:16];
  assign rs2 = inst_i[15:11];
  assign rs3 = inst_i[10:6];
  assign func = inst_i[5:0];
  assign imm = inst_i[15:0];

  assign inst_ready_o = issue_ready_i;
  assign uop_v_o = inst_v_i & issue_ready_i;

  always_comb begin
    uop_o = '0;
    illegal_o = 1'b0;

    uop_o.rd = rd;
    uop_o.rs1 = rs1;
    uop_o.rs2 = rs2;
    uop_o.rs3 = rs3;
    uop_o.imm = imm;
    uop_o.uop_op = func;
    uop_o.mem_mode = gbp_mem_mode_e'(inst_i[10:9]);
    uop_o.mem_write = inst_i[8];
    uop_o.mem_len = inst_i[7:0];
    uop_o.use_imm = 1'b0;

    unique case (opcode)
      OP_VEC: begin
        uop_o.uop_class = UOP_CLASS_VEC;
      end
      OP_REDUCE: begin
        uop_o.uop_class = UOP_CLASS_REDUCE;
      end
      OP_MAT: begin
        uop_o.uop_class = UOP_CLASS_MAT;
      end
      OP_SOLVE: begin
        uop_o.uop_class = UOP_CLASS_SOLVE;
      end
      OP_GBP: begin
        uop_o.uop_class = UOP_CLASS_GBP;
      end
      default: begin
        illegal_o = inst_v_i;
      end
    endcase
  end

endmodule
