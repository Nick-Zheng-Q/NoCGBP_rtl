// gbp_compute_defines.svh
// Common definitions for GBP compute engine

`ifndef GBP_COMPUTE_DEFINES_SVH
`define GBP_COMPUTE_DEFINES_SVH

// Operation codes for matrix_fsm
`define GBP_OP_MAT_ADD     3'd0
`define GBP_OP_MAT_SUB     3'd1
`define GBP_OP_MAT_MUL     3'd2
`define GBP_OP_MAT_INV     3'd3
`define GBP_OP_MAT_VEC_MUL 3'd4

// Data source selection for simd_array
`define GBP_SRC_BUFFER_A   2'b00
`define GBP_SRC_BUFFER_B   2'b01
`define GBP_SRC_ACC        2'b10
`define GBP_SRC_CONST      2'b11

// Maximum dimensions
`define GBP_MAX_DOFS       6
`define GBP_MAX_ADJACENT   8
`define GBP_MAX_LAM_SIZE   36  // 6*6

// Staging buffer layout (for dofs=6)
// Address 0-5:   prior_eta
// Address 6-41:  prior_lam (full 6x6 matrix)
// Address 42-47: msg0_eta
// Address 48-83: msg0_lam
// ... (repeat for each message)
`define GBP_ADDR_PRIOR_ETA    6'd0
`define GBP_ADDR_PRIOR_LAM    6'd6
`define GBP_ADDR_MSG_BASE     6'd42

// Message size in floats
// dofs=2: 5 floats, dofs=3: 9 floats, dofs=6: 27 floats
function automatic int msg_size(int dofs);
  return dofs + (dofs * (dofs + 1)) / 2;
endfunction

// Full matrix size (for computation)
function automatic int full_matrix_size(int dofs);
  return dofs * dofs;
endfunction

`endif // GBP_COMPUTE_DEFINES_SVH
