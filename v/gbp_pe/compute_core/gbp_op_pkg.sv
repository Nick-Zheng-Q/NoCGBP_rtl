// gbp_op_pkg.sv
// GBP Compute Core v0.7 — types and parameters
// Source: docs/gbp_pe/08_NEW_COMPUTE_UNIT.md §6-§8, §22, §24

package gbp_op_pkg;

  // ============================================================
  // Global dimension parameters
  // ============================================================
  parameter int GBP_MAX_VAR_DIM    = 6;
  parameter int GBP_MAX_PACKED_VAR = 21;
  parameter int GBP_MAX_MSG_SCALAR = 27;
  parameter int GBP_MAX_FACTOR_DIM = 12;
  parameter int GBP_MAX_PACKED_FAC = 78;
  parameter int GBP_MAX_RHS        = 7;
  parameter int GBP_MAX_WB_SCALARS = 36;
  parameter int DEGREE_WIDTH       = 16;
  parameter int OPERAND_STREAM_WIDTH = 16;
  parameter bit ENABLE_RELIN_P  = 0;
  parameter bit ENABLE_ROBUST_P = 0;

  // ============================================================
  // Base FP type
  // ============================================================
  typedef logic [31:0] fp32_t;

  // ============================================================
  // Enum types
  // ============================================================
  typedef enum logic [1:0] {
    DIM_1 = 2'd0,
    DIM_3 = 2'd1,
    DIM_6 = 2'd2
  } gbp_dim_e;

  typedef enum logic [3:0] {
    OP_MSG_F2V      = 4'd0,
    OP_BELIEF       = 4'd1,
    OP_RELIN_CHECK  = 4'd2,
    OP_ROBUST_SCALE = 4'd3
  } gbp_op_e;

  typedef enum logic [1:0] {
    FACTOR_SCALAR = 2'd0,
    FACTOR_SE2    = 2'd1,
    FACTOR_BA     = 2'd2,
    FACTOR_SE3    = 2'd3
  } gbp_factor_type_e;

  typedef enum logic [3:0] {
    OST_MSG_STATIC       = 4'd0,
    OST_CAV_FACTOR_O     = 4'd1,
    OST_CAV_BELIEF_O     = 4'd2,
    OST_CAV_OLD_TO_O     = 4'd3,
    OST_BELIEF_PRIOR     = 4'd4,
    OST_BELIEF_MSG       = 4'd5,
    OST_RELIN_VECTOR     = 4'd6,
    OST_ROBUST_FACTOR    = 4'd7
  } operand_stream_kind_e;

  typedef enum logic [3:0] {
    WB_MSG    = 4'd0,
    WB_BELIEF = 4'd1,
    WB_RELIN  = 4'd2,
    WB_ROBUST = 4'd3
  } wb_kind_e;

  // ============================================================
  // Helper functions
  // ============================================================
  function automatic int E(input int d);
    E = d + d * (d + 1) / 2;
  endfunction

  function automatic int P(input int d);
    P = d * (d + 1) / 2;
  endfunction

  function automatic int dim_to_val(input gbp_dim_e d);
    case (d)
      DIM_1: dim_to_val = 1;
      DIM_3: dim_to_val = 3;
      DIM_6: dim_to_val = 6;
      default: dim_to_val = 0;
    endcase
  endfunction

  function automatic gbp_dim_e val_to_dim(input int v);
    case (v)
      1: val_to_dim = DIM_1;
      3: val_to_dim = DIM_3;
      6: val_to_dim = DIM_6;
      default: val_to_dim = DIM_1;
    endcase
  endfunction

  // ============================================================
  // Shared data types (Section 6)
  // ============================================================
  typedef struct {
    gbp_dim_e dim;
    fp32_t    eta      [GBP_MAX_VAR_DIM];
    fp32_t    L_packed [GBP_MAX_PACKED_VAR];
  } gaussian_msg_t;

  typedef struct {
    logic [2:0] dim_i;
    logic [2:0] dim_o;
    fp32_t      L_io [GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM];
  } cross_block_t;

  typedef struct {
    gbp_dim_e dim;
    fp32_t    eta      [GBP_MAX_VAR_DIM];
    fp32_t    L_packed [GBP_MAX_PACKED_VAR];
    logic     fail;
    logic     regularized;
    logic     nan_guard;
    fp32_t    min_pivot;
  } msg_result_t;

  typedef struct {
    gbp_dim_e dim;
    fp32_t    eta      [GBP_MAX_VAR_DIM];
    fp32_t    L_packed [GBP_MAX_PACKED_VAR];
    fp32_t    mu       [GBP_MAX_VAR_DIM];
    fp32_t    residual;
    logic     fail;
    logic     regularized;
    logic     nan_guard;
    logic     degree_mismatch;
    fp32_t    min_pivot;
  } belief_result_t;

  typedef struct {
    logic     need_refresh;
    fp32_t    delta_norm_sq;
  } relin_result_t;

  typedef struct {
    gbp_factor_type_e factor_type;
    gbp_dim_e dim0;
    gbp_dim_e dim1;
    fp32_t    weight;
    fp32_t    eta0      [GBP_MAX_VAR_DIM];
    fp32_t    eta1      [GBP_MAX_VAR_DIM];
    fp32_t    L00_packed [GBP_MAX_PACKED_VAR];
    fp32_t    L01_dense  [GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM];
    fp32_t    L11_packed [GBP_MAX_PACKED_VAR];
  } robust_result_t;

  // ============================================================
  // Operand stream types (Section 7)
  // ============================================================
  typedef struct {
    operand_stream_kind_e kind;
    logic [0:0]           ctx_id;
    logic [31:0]          op_id;
    logic [15:0]          beat_idx;
    logic                 last;
    fp32_t                data [OPERAND_STREAM_WIDTH];
  } operand_stream_beat_t;

  typedef struct {
    gbp_dim_e dim_i;
    gbp_dim_e dim_o;

    fp32_t eta_i       [GBP_MAX_VAR_DIM];
    fp32_t L_ii_packed [GBP_MAX_PACKED_VAR];
    fp32_t L_io_dense  [GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM];

    fp32_t old_msg_eta_to_i [GBP_MAX_VAR_DIM];
    fp32_t old_msg_L_to_i   [GBP_MAX_PACKED_VAR];
  } msg_static_window_t;

  // ============================================================
  // Core request / response (Section 8)
  // ============================================================
  typedef struct {
    gbp_op_e           op;
    gbp_factor_type_e  factor_type;

    gbp_dim_e          dim_i;
    gbp_dim_e          dim_o;
    logic              direction;

    logic [0:0]        ctx_id;
    logic [31:0]       op_id;
    logic [31:0]       node_id;
    logic [31:0]       factor_id;
    logic [31:0]       dst_addr;
    logic [31:0]       aux_addr;

    fp32_t             damping;
    fp32_t             diag_lambda;
    fp32_t             pivot_eps;
    logic              regularize_en;

    logic [15:0]       degree;
  } gbp_core_req_t;

  typedef struct {
    gbp_op_e      op;
    logic [0:0]   ctx_id;
    logic [31:0]  op_id;
    logic [31:0]  dst_addr;
    logic [31:0]  aux_addr;
    logic [31:0]  node_id;
    logic [31:0]  factor_id;

    msg_result_t     msg_result;
    belief_result_t  belief_result;
    relin_result_t   relin_result;
    robust_result_t  robust_result;

    logic         fail;
    logic         regularized;
    logic         nan_guard;
    logic         degree_mismatch;
    logic         stream_error;
    fp32_t        min_pivot;
  } gbp_core_rsp_t;

  // ============================================================
  // Writeback record (Section 22)
  // ============================================================
  typedef struct {
    logic [31:0] addr;
    logic [15:0] nwords;
    wb_kind_e    kind;

    fp32_t       payload [GBP_MAX_WB_SCALARS];

    logic        fail;
    logic        regularized;
    logic        nan_guard;
  } writeback_record_t;

  // ============================================================
  // Wrapper command / read request / done (Section 24)
  // ============================================================
  typedef struct {
    logic                 valid;
    operand_stream_kind_e kind;
    logic [31:0]          base_addr;
    logic [15:0]          nbeats;
  } cu_operand_desc_t;

  typedef struct {
    gbp_op_e           op;
    gbp_factor_type_e  factor_type;
    gbp_dim_e          dim_i;
    gbp_dim_e          dim_o;
    logic              direction;

    logic [0:0]        ctx_id;
    logic [31:0]       op_id;
    logic [31:0]       node_id;
    logic [31:0]       factor_id;
    logic [31:0]       dst_addr;
    logic [31:0]       aux_addr;

    logic [15:0]       degree;
    fp32_t             damping;
    fp32_t             diag_lambda;
    fp32_t             pivot_eps;
    logic              regularize_en;

    cu_operand_desc_t  operand_desc [8];
  } cu_cmd_t;

  typedef struct {
    logic [31:0]          op_id;
    gbp_op_e              op;
    logic [31:0]          base_addr;
    logic [15:0]          nbeats;
    operand_stream_kind_e kind;
  } cu_rd_req_t;

  typedef struct {
    logic [31:0] node_id;
    logic [31:0] factor_id;
    gbp_op_e     op;

    logic [0:0]  ctx_id;
    logic        success;
    logic        fail;
    logic        regularized;
    logic        nan_guard;
    logic        degree_mismatch;
    logic        stream_error;

    fp32_t       residual;
    fp32_t       min_pivot;
  } cu_done_t;

endpackage
