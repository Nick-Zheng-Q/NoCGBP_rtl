// simd_array.sv
// SIMD FP array for GBP matrix operations
// Extended from compute_unit with accumulator support and flexible data sources

`include "bsg_defines.sv"
`include "bsg_fpu_defines.svh"
`include "HardFloat_consts.vi"
`include "HardFloat_specialize.vi"

module simd_array #(
    parameter int LANES = 16,
    parameter int FP_EXP_WIDTH_P = 8,
    parameter int FP_MANT_WIDTH_P = 23,
    parameter int DIV_BITS_PER_ITER_P = 1
)(
    input  logic clk_i,
    input  logic rst_n_i,
    
    // Operation control (per lane)
    input  logic [LANES-1:0]        op_add_en,      // Enable ADD
    input  logic [LANES-1:0]        op_sub_en,      // Enable SUB
    input  logic [LANES-1:0]        op_mul_en,      // Enable MUL
    input  logic [LANES-1:0]        op_div_en,      // Enable DIV
    input  logic [LANES-1:0]        op_mac_en,      // Enable MAC (multiply-accumulate)
    
    // Data source selection (per lane)
    // 00: data_a_i (from buffer)
    // 01: data_b_i (from buffer)
    // 10: accumulator (feedback)
    // 11: const_val
    input  logic [LANES-1:0][1:0]   src_a_sel,
    input  logic [LANES-1:0][1:0]   src_b_sel,
    input  logic [LANES-1:0][31:0]  const_val,      // Per-lane constant value
    
    // Input data from staging buffer
    input  logic [LANES-1:0][31:0]  data_a_i,
    input  logic [LANES-1:0][31:0]  data_b_i,
    
    // Output
    output logic [LANES-1:0][31:0]  result_o,
    output logic [LANES-1:0]        valid_o,
    output logic                    busy_o,
    
    // Accumulator interface
    input  logic [LANES-1:0]        acc_clear,      // Clear accumulator
    input  logic [LANES-1:0]        acc_load,       // Load result to accumulator
    output logic [LANES-1:0][31:0]  acc_value_o     // Current accumulator value
);

  logic reset_i;
  assign reset_i = ~rst_n_i;

  localparam logic [1:0] SRC_BUFFER_A = 2'b00;
  localparam logic [1:0] SRC_BUFFER_B = 2'b01;
  localparam logic [1:0] SRC_ACC      = 2'b10;
  localparam logic [1:0] SRC_CONST    = 2'b11;
  
  localparam logic [1:0] OP_ADD = 2'b00;
  localparam logic [1:0] OP_SUB = 2'b01;
  localparam logic [1:0] OP_MUL = 2'b10;
  localparam logic [1:0] OP_DIV = 2'b11;
  
  localparam int REC_W = FP_EXP_WIDTH_P + FP_MANT_WIDTH_P + 2;
  
  // ============================================================
  // Input multiplexers
  // ============================================================
  logic [LANES-1:0][31:0] src_a;
  logic [LANES-1:0][31:0] src_b;
  logic [LANES-1:0][31:0] accumulator_r;
  
  for (genvar i = 0; i < LANES; i++) begin : g_input_mux
    always_comb begin
      unique case (src_a_sel[i])
        SRC_BUFFER_A: src_a[i] = data_a_i[i];
        SRC_BUFFER_B: src_a[i] = data_b_i[i];
        SRC_ACC:      src_a[i] = accumulator_r[i];
        SRC_CONST:    src_a[i] = const_val[i];
        default:      src_a[i] = data_a_i[i];
      endcase
      
      unique case (src_b_sel[i])
        SRC_BUFFER_A: src_b[i] = data_a_i[i];
        SRC_BUFFER_B: src_b[i] = data_b_i[i];
        SRC_ACC:      src_b[i] = accumulator_r[i];
        SRC_CONST:    src_b[i] = const_val[i];
        default:      src_b[i] = data_b_i[i];
      endcase
    end
  end
  
  // ============================================================
  // FP datapath (per lane)
  // ============================================================
  logic [LANES-1:0][REC_W-1:0] rec_a;
  logic [LANES-1:0][REC_W-1:0] rec_b;
  logic [LANES-1:0][REC_W-1:0] rec_acc;  // Accumulator in recoded format
  logic [LANES-1:0][REC_W-1:0] add_rec_z;
  logic [LANES-1:0][REC_W-1:0] sub_rec_z;
  logic [LANES-1:0][REC_W-1:0] mul_rec_z;
  logic [LANES-1:0][REC_W-1:0] mac_rec_z; // MAC result
  
  logic [LANES-1:0][31:0] add_z;
  logic [LANES-1:0][31:0] mac_z;
  logic [LANES-1:0][31:0] sub_z;
  logic [LANES-1:0][31:0] mul_z;
  logic [LANES-1:0][31:0] div_z;
  
  logic [LANES-1:0] div_v;
  logic [LANES-1:0] div_active_r;
  logic [LANES-1:0] div_done_r;
  logic [LANES-1:0][31:0] div_result_r;
  
  logic [LANES-1:0] lane_busy;
  logic [LANES-1:0][1:0] op_select;  // Selected operation per lane
  
  // Operation priority: DIV > MAC > MUL > SUB > ADD
  for (genvar i = 0; i < LANES; i++) begin : g_op_sel
    always_comb begin
      if (op_div_en[i])      op_select[i] = OP_DIV;
      else if (op_mac_en[i]) op_select[i] = OP_ADD;  // MAC uses ADD after MUL
      else if (op_mul_en[i]) op_select[i] = OP_MUL;
      else if (op_sub_en[i]) op_select[i] = OP_SUB;
      else if (op_add_en[i]) op_select[i] = OP_ADD;
      else                   op_select[i] = OP_ADD;  // Default
    end
  end
  
  // Generate FP units for each lane
  for (genvar i = 0; i < LANES; i++) begin : g_lanes
    logic add_invalid_lo, sub_invalid_lo, mul_invalid_lo;
    logic [4:0] add_fflags_lo, sub_fflags_lo, mul_fflags_lo;
    logic div_ready_lo;
    logic [4:0] div_fflags_lo;
    logic mac_mul_valid;
    
    // Convert to recoded format
    fNToRecFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    a_to_rec ( .in(src_a[i]), .out(rec_a[i]) );
    
    fNToRecFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    b_to_rec ( .in(src_b[i]), .out(rec_b[i]) );
    
    // FP operations
    addRecFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    add_op ( .control(`flControl_default), .subOp(1'b0),
             .a(rec_a[i]), .b(rec_b[i]), .roundingMode(3'b000),
             .out(add_rec_z[i]), .exceptionFlags(add_fflags_lo) );
    
    addRecFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    sub_op ( .control(`flControl_default), .subOp(1'b1),
             .a(rec_a[i]), .b(rec_b[i]), .roundingMode(3'b000),
             .out(sub_rec_z[i]), .exceptionFlags(sub_fflags_lo) );
    
    mulRecFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    mul_op ( .control(`flControl_default),
             .a(rec_a[i]), .b(rec_b[i]), .roundingMode(3'b000),
             .out(mul_rec_z[i]), .exceptionFlags(mul_fflags_lo) );
    
    // MAC: multiply-accumulate (accumulator + a*b)
    // For MAC, we need: acc + (a * b)
    addRecFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    mac_add ( .control(`flControl_default), .subOp(1'b0),
              .a(rec_acc[i]), .b(mul_rec_z[i]), .roundingMode(3'b000),
              .out(mac_rec_z[i]), .exceptionFlags() );
    
    // Divider (multi-cycle)
    divSqrtFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1),
                 .bits_per_iter_p(DIV_BITS_PER_ITER_P) )
    div ( .nReset(~reset_i), .clock(clk_i), .control(`flControl_default),
          .inReady(div_ready_lo),
          .inValid(op_div_en[i] & ~div_active_r[i] & ~lane_busy[i]),
          .sqrtOp(1'b0), .a(src_a[i]), .b(src_b[i]), .roundingMode(3'b000),
          .outValid(div_v[i]), .sqrtOpOut(), .out(div_z[i]),
          .exceptionFlags(div_fflags_lo) );
    
    // Convert back to normal format
    recFNToFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    add_to_fn ( .in(add_rec_z[i]), .out(add_z[i]) );

    recFNToFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    mac_to_fn ( .in(mac_rec_z[i]), .out(mac_z[i]) );
    
    recFNToFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    sub_to_fn ( .in(sub_rec_z[i]), .out(sub_z[i]) );
    
    recFNToFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    mul_to_fn ( .in(mul_rec_z[i]), .out(mul_z[i]) );
    
    // Lane busy detection
    assign lane_busy[i] = div_active_r[i];
    
    // Sequential logic per lane
    always_ff @(posedge clk_i) begin
      if (reset_i) begin
        div_active_r[i] <= 1'b0;
        div_done_r[i] <= 1'b0;
        div_result_r[i] <= '0;
        accumulator_r[i] <= '0;
      end else begin
        // Divider state machine
        if (op_div_en[i] && ~div_active_r[i] && ~lane_busy[i]) begin
          div_active_r[i] <= 1'b1;
          div_done_r[i] <= 1'b0;
        end else if (div_active_r[i]) begin
          if (div_v[i]) begin
            div_done_r[i] <= 1'b1;
            div_result_r[i] <= div_z[i];
          end
          if (div_done_r[i]) begin
            div_active_r[i] <= 1'b0;
            div_done_r[i] <= 1'b0;
          end
        end
        
        // Accumulator update
        if (acc_clear[i]) begin
          accumulator_r[i] <= '0;
        end else if (acc_load[i]) begin
          // Load operation result to accumulator
          case (op_select[i])
            OP_ADD: accumulator_r[i] <= add_z[i];
            OP_SUB: accumulator_r[i] <= sub_z[i];
            OP_MUL: accumulator_r[i] <= mul_z[i];
            OP_DIV: accumulator_r[i] <= div_v[i] ? div_z[i] : div_result_r[i];
          endcase
        end else if (op_mac_en[i]) begin
          // MAC: accumulator = accumulator + (a * b)
          // HardFloat 的 mac_rec_z 是 recoded 格式，必须先转回 IEEE-754 再写回 32b 累加器。
          accumulator_r[i] <= mac_z[i];
        end
      end
    end
    
    // Convert accumulator to recoded for MAC
    fNToRecFN #( .expWidth(FP_EXP_WIDTH_P), .sigWidth(FP_MANT_WIDTH_P + 1) )
    acc_to_rec ( .in(accumulator_r[i]), .out(rec_acc[i]) );
    
    // Result selection
    always_comb begin
      if (div_active_r[i] || div_done_r[i]) begin
        result_o[i] = div_v[i] ? div_z[i] : div_result_r[i];
      end else if (op_mac_en[i]) begin
        result_o[i] = mac_z[i];
      end else begin
        unique case (op_select[i])
          OP_ADD: result_o[i] = add_z[i];
          OP_SUB: result_o[i] = sub_z[i];
          OP_MUL: result_o[i] = mul_z[i];
          OP_DIV: result_o[i] = div_z[i];
          default: result_o[i] = add_z[i];
        endcase
      end
    end
    
    // Valid output
    assign valid_o[i] = ~div_active_r[i] && ~lane_busy[i] && 
                        (op_add_en[i] || op_sub_en[i] || op_mul_en[i] || op_mac_en[i]) ||
                        (div_done_r[i] || (div_active_r[i] && div_v[i]));
    
    assign acc_value_o[i] = accumulator_r[i];
  end
  
  // Global busy signal
  assign busy_o = |lane_busy || |div_active_r;
  
  // Debug
  always_ff @(posedge clk_i) begin
    if (|op_add_en)
      $display("SIMD_DBG: op_add=%b data_a[0]=%h (%f) data_b[0]=%h (%f) result[0]=%h (%f)",
               op_add_en, data_a_i[0], $bitstoreal(data_a_i[0]),
               data_b_i[0], $bitstoreal(data_b_i[0]),
               result_o[0], $bitstoreal(result_o[0]));
  end

endmodule
