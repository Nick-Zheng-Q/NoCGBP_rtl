# NoCBP GBP Compute Core RTL Interface Specification

Version: v0.7
Scope: PE-local canonical Gaussian block compute core
Target: NoCBP PE / SPM / stream-engine integration

---

## 1. Design Goal

The GBP compute core is a PE-local arithmetic block for executing canonical Gaussian belief propagation primitives. It does not understand high-level application semantics such as bundle adjustment projection, SE(3) Lie logarithms, g2o factors, or global nonlinear optimization. Its only responsibility is to consume already-linearized canonical Gaussian blocks and produce updated messages, beliefs, residuals, and status flags.

The core should be integrated behind the PE read-stream engines and before the write-stream engine:

```text
resident_scheduler
    -> read_stream_engine / compute_unit_wrapper
        -> gbp_compute_core
            -> write_stream_engine
                -> spm_subsystem
```

The compute core must not generate SPM addresses by itself. Address generation, metadata traversal, adjacency traversal, remote fetch, and NoC handling belong to the scheduler, AGU, stream engines, and SPM subsystem.

### 1.1 Design Principles

**Single-issue / pipeline-ready**: V0 is single-issue (`CTX_DEPTH=1`), one operation in the core at a time. But all interfaces use ready/valid handshakes, and all stage payloads carry `op_id`/`ctx_id` handles. This allows expanding to `CTX_DEPTH>1` without rewriting module interfaces.

```systemverilog
parameter int MSG_CTX_DEPTH      = 1;
parameter int BELIEF_CTX_DEPTH   = 1;
parameter int SOLVE_REQ_FIFO_DP  = 1;
parameter int SOLVE_RSP_FIFO_DP  = 1;
parameter bit ENABLE_CORE_PIPE_P = 1'b0;
```

**Context table, not global registers**: `operand_window` and intermediate storage use `msg_context_t msg_ctx[MSG_CTX_DEPTH]` style, not a single global register. V0 has one entry; expanding to 2/4 contexts is a parameter change.

**Stage tokens, not bulk data**: Inter-stage FIFOs carry small handles (`op_id`, `ctx_id`, `op`), not full operand payloads. Large data stays in the context table.

**Result ownership**: The compute core only commits results to the local SPM. Non-local effects are emitted as notify events through the NI. Remote payload writeback is not supported in V0.

Ownership is determined from the command's `dst_addr` by the `compute_unit_wrapper` (local SPM address vs. non-local). V0 does not carry a separate `result_owner_pe` field; if needed in V1 it can be added to `gbp_core_req_t` / `cu_cmd_t`.

```text
result_owner_pe == this_pe:
  core_rsp -> local_writeback_packer -> write_stream_engine -> local SPM

result_owner_pe != this_pe:
  V0: notify event through NI (no payload, just dirty/updated signal)
  V1: notify event through NI; payload writeback may be added later
```

**Pipeline cut points** (V1-ready, not implemented in V0):

```text
Cut 1: solve_req_fifo   — after frontend (cavity+rhs), before solver
Cut 2: solve_rsp_fifo   — after solver, before schur/backend
Cut 3: schur_fifo       — optional, after schur_update, before damping
Cut 4: rsp_fifo         — before wrapper result_router
```

These are the only places where FIFO depth > 0 is meaningful. Cavity and rhs_builder are combined into one frontend macro-stage; they should not be separated.

**FP pipeline awareness**: All FP operations use hardfloat 3-stage pipeline (`bsg_fpu_add_sub`, `bsg_fpu_mul`). Latency = 3 cycles per operation, throughput = 1 result/cycle after pipeline fills. The spec's latency model accounts for this.

---

## 2. Supported Mathematical Primitives

### 2.1 Factor-to-variable message

For a binary factor connecting target variable `i` and other variable `o`, the message update is:

```text
A_cav = Lambda_oo_factor + Lambda_belief_o - Lambda_old_a_to_o
b_cav = eta_o_factor    + eta_belief_o    - eta_old_a_to_o

X_L = solve(A_cav, Lambda_oi)
X_e = solve(A_cav, b_cav)

Lambda_msg_raw = Lambda_ii_factor - Lambda_io * X_L
eta_msg_raw    = eta_i_factor     - Lambda_io * X_e

message_new = (1 - damping) * message_raw + damping * message_old_a_to_i
```

### 2.2 Variable belief update

For variable `i` with degree `k`:

```text
Lambda_belief_i = Lambda_prior_i + sum incoming Lambda_msg_a_to_i
eta_belief_i    = eta_prior_i    + sum incoming eta_msg_a_to_i
mu_i            = solve(Lambda_belief_i, eta_belief_i)
residual_i      = ||mu_i - mu_i_old||^2
```

### 2.3 Optional side operations

V1 may add:

```text
relinearization_check: ||mu_current - x_linearization||^2 > beta^2
robust_rescale:        Lambda_factor *= weight, eta_factor *= weight
```

---

## 3. Supported Dimensions and Factor Types

### 3.1 Variable dimensions

```text
dim = 1: scalar linear variable
dim = 3: SE(2) pose or BA landmark
dim = 6: SE(3) pose or BA camera
```

### 3.2 Binary factor dimensions

```text
scalar factor: 1 + 1
SE(2) factor:  3 + 3
BA factor:     6 + 3
SE(3) factor:  6 + 6
```

### 3.3 Packed Gaussian message size

Each Gaussian message stores `eta` plus packed upper-triangular `Lambda`.

| dim | eta entries | packed Lambda entries | total scalar entries |
| --: | ----------: | --------------------: | -------------------: |
|   1 |           1 |                     1 |                    2 |
|   3 |           3 |                     6 |                    9 |
|   6 |           6 |                    21 |                   27 |

Define:

```text
E(d) = d + d(d+1)/2       (compact message words)
P(d) = d(d+1)/2           (packed upper-triangular words)
E(1) = 2,  P(1) = 1
E(3) = 9,  P(3) = 6
E(6) = 27, P(6) = 21
```

### 3.4 Direction semantics

A factor always has two endpoints:

```text
endpoint0 = var0
endpoint1 = var1
```

`direction` selects the target endpoint:

```text
direction = 0: compute factor -> endpoint0 (target = var0, other = var1)
direction = 1: compute factor -> endpoint1 (target = var1, other = var0)
```

BA factor storage convention is fixed:

```text
endpoint0 = camera,   dim = 6
endpoint1 = landmark, dim = 3
```

BA mapping:

```text
BA direction=0:
  target = camera   (dim_i = 6)
  other  = landmark (dim_o = 3)
  Lambda_io = Lambda_cl

BA direction=1:
  target = landmark (dim_i = 3)
  other  = camera   (dim_o = 6)
  Lambda_io = transpose(Lambda_cl)
```

**Key rule**: The operand stream producer (scheduler/loader/operand_assembler) is responsible for target-major normalization. For BA `direction=1`, `OST_MSG_STATIC` must already contain `Lambda_io` in landmark-by-camera (transposed) row-major order. `compute_unit_wrapper` only forwards/fragments streams; it does not transpose matrix blocks. The compute core operates on `dim_i` and `dim_o` and never needs to know the original camera/landmark storage direction.

---

## 4. Global Interface Convention

All submodules use ready/valid style interfaces unless explicitly marked as purely combinational.

### 4.1 Common handshake

```systemverilog
input  logic valid_i;
output logic ready_o;
output logic valid_o;
input  logic ready_i;
```

A request is accepted when:

```systemverilog
valid_i && ready_o
```

A response is consumed when:

```systemverilog
valid_o && ready_i
```

### 4.2 Numeric type

V0 assumes FP32 internally.

```systemverilog
typedef logic [31:0] fp32_t;
```

Storage may later be FP16/BF16/SDF, but conversion should happen at the stream-engine or operand-assembler boundary, not inside the first version of the compute core.

### 4.3 Reset

All modules use **active-high synchronous reset** (`reset_i`). `rst_n_i` is converted to `reset_i` at the top boundary.

### 4.4 Dimension encoding

```systemverilog
typedef enum logic [1:0] {
  DIM_1 = 2'd0,
  DIM_3 = 2'd1,
  DIM_6 = 2'd2
} gbp_dim_e;
```

### 4.5 Operation encoding

```systemverilog
typedef enum logic [3:0] {
  OP_MSG_F2V      = 4'd0,
  OP_BELIEF       = 4'd1,
  OP_RELIN_CHECK  = 4'd2,
  OP_ROBUST_SCALE = 4'd3
} gbp_op_e;
```

### 4.6 Factor type encoding

```systemverilog
typedef enum logic [1:0] {
  FACTOR_SCALAR = 2'd0,
  FACTOR_SE2    = 2'd1,
  FACTOR_BA     = 2'd2,
  FACTOR_SE3    = 2'd3
} gbp_factor_type_e;
```

### 4.7 Global dimension parameters

```systemverilog
parameter int GBP_MAX_VAR_DIM      = 6;
parameter int GBP_MAX_PACKED_VAR   = 21;   // P(6) = 6*7/2
parameter int GBP_MAX_MSG_SCALAR   = 27;   // E(6)
parameter int GBP_MAX_FACTOR_DIM   = 12;   // 6+6
parameter int GBP_MAX_PACKED_FAC   = 78;   // P(12) = 12*13/2; V0 unused, reserved for V1 full-factor storage
parameter int GBP_MAX_RHS          = 7;    // dim_i + 1
parameter int GBP_MAX_WB_SCALARS   = 36;   // V0: message/belief writeback
parameter int DEGREE_WIDTH         = 16;

parameter bit ENABLE_RELIN_P       = 0;    // V0: disabled
parameter bit ENABLE_ROBUST_P      = 0;    // V0: disabled
```

### 4.8 Single-issue rule

V0 Core-Lite is single-issue. The core accepts a new command only when idle. No two operations are interleaved inside one `gbp_compute_core`.

---

## 5. Canonical Scalar Packing Order

All operand streams use the following packing order within each beat's `data[]` array.

### 5.1 Packed symmetric upper-triangular (Lambda)

For dimension `d`, the upper-triangular entries are packed row-major:

```text
PackedCount(d) = d(d+1)/2        // total packed entries for dim d
PackedOffset(r, d) = r*d - r*(r-1)/2  // row offset in packed array

L_packed[idx] where idx = PackedOffset(r, d) + (c - r)
  for r in 0..d-1:
    for c in r..d-1:
      emit L[r,c]

Example d=3:
  PackedCount(3) = 6
  L[0,0] L[0,1] L[0,2] L[1,1] L[1,2] L[2,2]
  idx:    0      1      2      3      4      5
```

Note: `PackedCount(d)` is used throughout this document as `P(d)` for brevity. `PackedOffset(r, d)` is only used in this section for clarity.

### 5.2 Dense cross block (Lambda_io)

Row-major, `dim_i` rows x `dim_o` columns:

```text
L_io_dense[idx] where idx = r * dim_o + c
  for r in 0..dim_i-1:
    for c in 0..dim_o-1
```

### 5.3 Gaussian message (compact form)

```text
GaussianMessage(d):
  eta[0], eta[1], ..., eta[d-1], L_packed[0], L_packed[1], ..., L_packed[P(d)-1]
  total = E(d) = d + P(d) scalars
```

### 5.4 Stream beat packing

Each beat carries up to 16 FP32 scalars in `data[0..15]`. Multiple logical fields are concatenated sequentially within the beat array. If a field spans a beat boundary, it continues at `data[0]` of the next beat.

### 5.5 Per-stream-field packing order

**OST_MSG_STATIC** (target-side + cross + old target msg):

```text
offset0 = 0
offset1 = E(d_i)
offset2 = E(d_i) + d_i * d_o
offset3 = 2 * E(d_i) + d_i * d_o

scalar[offset0 .. offset1-1]: eta_i + L_ii_packed
scalar[offset1 .. offset2-1]: L_io_dense
scalar[offset2 .. offset3-1]: old_msg_eta_to_i + old_msg_L_to_i
```

**OST_CAV_FACTOR_O**:

```text
data[0..E(d_o)-1]: eta_o + L_oo_packed
```

**OST_CAV_BELIEF_O**:

```text
data[0..E(d_o)-1]: belief_eta_o + belief_L_o
```

**OST_CAV_OLD_TO_O**:

```text
data[0..E(d_o)-1]: old_msg_eta_to_o + old_msg_L_to_o
```

**OST_BELIEF_PRIOR**:

```text
data[0..E(d_v)-1]:              prior_eta + prior_L_packed
data[E(d_v)..E(d_v)+d_v-1]:    old_mu
```

where `d_v` is the variable dimension (`dim_i` for belief update).

**OST_BELIEF_MSG**:

```text
data[0..E(d)-1]: msg_eta + msg_L_packed
```

---

## 6. Operand and Result Type Definitions

All types use **unpacked structs** because SystemVerilog `struct packed` does not support unpacked arrays.

### 6.1 Shared Gaussian message type

```systemverilog
typedef struct {
  gbp_dim_e dim;
  fp32_t    eta      [GBP_MAX_VAR_DIM];
  fp32_t    L_packed [GBP_MAX_PACKED_VAR];
} gaussian_msg_t;
```

### 6.2 Cross block (dense, target x other)

```systemverilog
typedef struct {
  logic [2:0] dim_i;
  logic [2:0] dim_o;
  fp32_t      L_io [GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM];
} cross_block_t;
```

### 6.3 Message result

```systemverilog
typedef struct {
  gbp_dim_e dim;
  fp32_t    eta      [GBP_MAX_VAR_DIM];
  fp32_t    L_packed [GBP_MAX_PACKED_VAR];
  logic     fail;
  logic     regularized;
  logic     nan_guard;
  fp32_t    min_pivot;
} msg_result_t;
```

### 6.4 Belief result

```systemverilog
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
```

### 6.5 Relin result

```systemverilog
typedef struct {
  logic     need_refresh;
  fp32_t    delta_norm_sq;
} relin_result_t;
```

### 6.6 Robust result (V1 only, block-wise)

```systemverilog
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
```

---

## 7. Operand Stream Format

### 7.1 Stream beat type

```systemverilog
typedef enum logic [3:0] {
  OST_MSG_STATIC       = 4'd0,   // target-side + cross + old target msg
  OST_CAV_FACTOR_O     = 4'd1,   // other-side factor eta/Lambda
  OST_CAV_BELIEF_O     = 4'd2,   // other-side belief eta/Lambda
  OST_CAV_OLD_TO_O     = 4'd3,   // other-side old msg eta/Lambda
  OST_BELIEF_PRIOR     = 4'd4,   // belief prior + old_mu
  OST_BELIEF_MSG       = 4'd5,   // incoming message (streamed, one per edge)
  OST_RELIN_VECTOR     = 4'd6,
  OST_ROBUST_FACTOR    = 4'd7
} operand_stream_kind_e;

typedef struct {
  operand_stream_kind_e kind;
  logic [0:0]           ctx_id;     // V0 tied to 0; reserved for CTX_DEPTH>1
  logic [31:0]          op_id;
  logic [15:0]          beat_idx;
  logic                 last;
  fp32_t                data [16];  // 16 FP32 scalars per beat
} operand_stream_beat_t;
```

`op_id` and `ctx_id` must match across all beats belonging to the same operation. V0 does not allow interleaved `op_id`/`ctx_id` streams within one `compute_unit_wrapper`.

### 7.2 Message operand window

V0 uses a **streaming operand window**, not a full operand register file. The compute core receives operands as typed streams from `compute_unit_wrapper`.

For `OP_MSG_F2V`, the core keeps only the target-side + cross + old target msg (loaded from `OST_MSG_STATIC`). The cavity-side streams (`OST_CAV_*`) are consumed on-the-fly by `cavity_builder` and discarded.

### 7.3 Message static window

```systemverilog
typedef struct {
  gbp_dim_e dim_i;
  gbp_dim_e dim_o;

  fp32_t eta_i       [GBP_MAX_VAR_DIM];
  fp32_t L_ii_packed [GBP_MAX_PACKED_VAR];
  fp32_t L_io_dense  [GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM];

  fp32_t old_msg_eta_to_i [GBP_MAX_VAR_DIM];
  fp32_t old_msg_L_to_i   [GBP_MAX_PACKED_VAR];
} msg_static_window_t;
```

Loaded from `OST_MSG_STATIC` beats. Packed order defined in Section 5.5.

### 7.4 Message operand storage estimate

For one directional message with target dimension `d_i` and other dimension `d_o`:

```text
MsgWindowWords = 2*E(d_i) + d_i*d_o
SolverWords    = P(d_o) + d_o*(d_i+1)
```

V0 solver overwrites B with X, so no separate X buffer is needed.

| message | `d_i,d_o` | message window words | solver words | peak live words |
|---|---:|---:|---:|---:|
| scalar | 1,1 | 5 | 3 | 8 |
| SE(2) | 3,3 | 27 | 18 | 45 |
| BA -> camera | 6,3 | 72 | 27 | 99 |
| BA -> landmark | 3,6 | 36 | 45 | 81 |
| SE(3) | 6,6 | 90 | 63 | 153 |

### 7.5 Belief operand handling

Belief update is fully streaming:

```systemverilog
typedef struct {
  gbp_dim_e     dim;       // filled from config (gbp_core_req_t.dim_i), not stream payload
  logic [15:0]  degree;    // filled from config (gbp_core_req_t.degree), not stream payload
  fp32_t        prior_eta      [GBP_MAX_VAR_DIM];
  fp32_t        prior_L_packed [GBP_MAX_PACKED_VAR];
  fp32_t        old_mu         [GBP_MAX_VAR_DIM];
} belief_start_t;
```

Incoming messages arrive as `OST_BELIEF_MSG` beats via `belief_operand_unpacker`. `packed_accumulator` consumes them directly. There is no `MAX_DEGREE` buffer.

```systemverilog
typedef struct {
  gbp_dim_e dim;            // filled from config (gbp_core_req_t.dim_i)
  fp32_t    msg_eta      [GBP_MAX_VAR_DIM];
  fp32_t    msg_L_packed [GBP_MAX_PACKED_VAR];
  logic     last;
} belief_msg_stream_t;
```

**V0 layout requirement**: For `OP_BELIEF`, all incoming messages for a variable must be laid out contiguously in SPM as a single `OST_BELIEF_MSG` stream. The scheduler/loader is responsible for constructing this layout before issuing the command. `operand_desc[OST_BELIEF_MSG]` describes this contiguous region with `base_addr` and `nbeats`.

### 7.6 Relin and robust operands (V1 only)

```systemverilog
typedef struct {
  logic [3:0] total_dim;
  fp32_t      mu_current       [GBP_MAX_FACTOR_DIM];
  fp32_t      x_linearization  [GBP_MAX_FACTOR_DIM];
  fp32_t      beta_sq;
} relin_operand_t;

typedef struct {
  gbp_factor_type_e factor_type;
  gbp_dim_e dim0;
  gbp_dim_e dim1;
  fp32_t    weight;
  fp32_t    eta0       [GBP_MAX_VAR_DIM];
  fp32_t    eta1       [GBP_MAX_VAR_DIM];
  fp32_t    L00_packed [GBP_MAX_PACKED_VAR];
  fp32_t    L01_dense  [GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM];
  fp32_t    L11_packed [GBP_MAX_PACKED_VAR];
} robust_operand_t;
```

---

## 8. Top-Level Module: `gbp_compute_core`

### 8.1 Role

`gbp_compute_core` receives fully assembled operand packets and produces fully packed result packets. It contains no SPM address-generation logic.

### 8.2 Ports

```systemverilog
module gbp_compute_core #(
  parameter int FP_WIDTH = 32
)(
  input  logic clk_i,
  input  logic reset_i,

  // Command channel
  input  logic          cmd_valid_i,
  output logic          cmd_ready_o,
  input  gbp_core_req_t cmd_i,

  // Operand stream channel
  input  logic                 operand_valid_i,
  output logic                 operand_ready_o,
  input  operand_stream_beat_t operand_i,

  // Response channel
  output logic          rsp_valid_o,
  input  logic          rsp_ready_i,
  output gbp_core_rsp_t rsp_o
);
```

### 8.3 Command packet

```systemverilog
typedef struct {
  gbp_op_e           op;
  gbp_factor_type_e  factor_type;

  gbp_dim_e          dim_i;
  gbp_dim_e          dim_o;
  logic              direction;

  logic [0:0]        ctx_id;       // V0 tied to 0; reserved for CTX_DEPTH>1
  logic [31:0]       op_id;
  logic [31:0]       node_id;
  logic [31:0]       factor_id;
  logic [31:0]       dst_addr;
  logic [31:0]       aux_addr;     // V0: unused, must be zero

  fp32_t             damping;
  fp32_t             diag_lambda;  // additive diagonal loading amount
  fp32_t             pivot_eps;    // minimum acceptable pivot
  logic              regularize_en;

  logic [15:0]       degree;       // OP_BELIEF only: number of incoming messages
} gbp_core_req_t;
```

`degree` usage:
- `OP_MSG_F2V`: ignored, must be zero.
- `OP_BELIEF`: number of incoming messages to accumulate.

### 8.4 Output packet

```systemverilog
typedef struct {
  gbp_op_e      op;
  logic [0:0]   ctx_id;       // echoes req.ctx_id
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
```

### 8.5 Internal routing

`gbp_compute_core` produces a `gbp_core_rsp_t` response. `writeback_packer` lives inside `compute_unit_wrapper`, not inside the core.

```text
OP_MSG_F2V      -> operand_window -> cavity_builder -> rhs_builder -> ldlt_solve_core -> schur_update_unit -> damping_unit -> rsp_assembler
OP_BELIEF       -> belief_operand_unpacker -> packed_accumulator -> belief_solve_adapter -> ldlt_solve_core -> belief_result_builder -> rsp_assembler
OP_RELIN_CHECK  -> relin_check_unit -> rsp_assembler
OP_ROBUST_SCALE -> robust_scale_unit -> rsp_assembler

compute_unit_wrapper:
  gbp_core_rsp_t -> writeback_packer -> write_stream_engine
```

---

## 9. Module: `op_decoder`

### 9.1 Role

Validates `op`, factor type, dimensions, direction, and required operand fields. Generates internal control signals for the datapath.

### 9.2 Inputs

```systemverilog
input gbp_core_req_t cmd_i;
```

### 9.3 Outputs

```systemverilog
output logic          is_msg_o;
output logic          is_belief_o;
output logic          is_relin_o;
output logic          is_robust_o;

output logic [2:0]    dim_i_val_o;     // 1, 3, or 6
output logic [2:0]    dim_o_val_o;     // 1, 3, or 6
output logic [4:0]    e_i_o;           // E(dim_i)
output logic [4:0]    e_o_o;           // E(dim_o)
output logic [4:0]    p_i_o;           // P(dim_i)
output logic [4:0]    p_o_o;           // P(dim_o)
output logic [2:0]    nrhs_o;          // dim_i + 1

output logic          legal_o;
output logic          illegal_dim_o;
output logic          illegal_factor_o;
output logic          illegal_op_o;
```

### 9.4 Validation rules

```text
FACTOR_SCALAR: dim_i/dim_o must be 1/1
FACTOR_SE2:    dim_i/dim_o must be 3/3
FACTOR_BA:     dim pair must be 6/3 or 3/6 depending on direction
FACTOR_SE3:    dim_i/dim_o must be 6/6

OP_BELIEF:     dim_i must be 1/3/6; dim_o/direction/factor_type ignored (software should zero them)
OP_RELIN_CHECK:  illegal if !ENABLE_RELIN_P
OP_ROBUST_SCALE: illegal if !ENABLE_ROBUST_P
```

---

## 10. Module: `operand_window`

### 10.1 Role

Buffers the static operand window (target-side + cross + old target msg) for multi-cycle solver and Schur stages. Cavity-side streams are consumed directly by `cavity_builder` and are not stored here.

### 10.2 Ports

```systemverilog
module operand_window (
  input  logic clk_i,
  input  logic reset_i,

  // Configuration (must be stable before first load beat)
  input  gbp_dim_e dim_i_i,
  input  gbp_dim_e dim_o_i,
  input  logic     start_i,

  // Load from operand stream
  input  logic                 load_valid_i,
  output logic                 load_ready_o,
  input  operand_stream_kind_e load_kind_i,
  input  fp32_t                load_data_i [16],
  input  logic                 load_last_i,

  // Downstream read (combinational)
  output msg_static_window_t   msg_static_o,
  output logic                 msg_static_valid_o,

  // Clear on operation complete
  input  logic                 clear_i
);
```

**Timing**: `dim_i_i`/`dim_o_i` must be stable before the first load beat. `start_i` is a single-cycle pulse that tells `operand_window` to prepare for a new operation; beats are actually transferred via `load_valid_i && load_ready_o`.

### 10.3 Stored fields

```systemverilog
typedef struct {
  gbp_dim_e dim_i;
  gbp_dim_e dim_o;

  fp32_t eta_i       [GBP_MAX_VAR_DIM];
  fp32_t L_ii_packed [GBP_MAX_PACKED_VAR];
  fp32_t L_io_dense  [GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM];

  fp32_t old_msg_eta_to_i [GBP_MAX_VAR_DIM];
  fp32_t old_msg_L_to_i   [GBP_MAX_PACKED_VAR];
} msg_static_window_t;
```

Packed order defined in Section 5.5.

### 10.4 Solver-local buffers (inside `ldlt_solve_core`)

```text
A_cav:  P(d_o) words
B/X:    d_o * (d_i + 1) words (B is overwritten by X in V0)
```

These are solver-internal and not part of the operand window.

---

## 11. Module: `cavity_builder`

### 11.1 Role

Constructs the cavity precision matrix/vector for the non-target variable. Consumes three operand streams on-the-fly:

```text
OST_CAV_FACTOR_O:  factor_eta_o, factor_L_oo    -> acc += factor_o
OST_CAV_BELIEF_O:  belief_eta_o, belief_L_o      -> acc += belief_o
OST_CAV_OLD_TO_O:  old_msg_eta_to_o, old_msg_L_to_o -> acc -= old_msg_to_o
```

After `A_cav` and `eta_cav` are built, the source streams are discarded.

### 11.2 Ports

```systemverilog
module cavity_builder (
  input  logic                 clk_i,
  input  logic                 reset_i,

  input  logic                 start_valid_i,
  output logic                 start_ready_o,
  input  gbp_dim_e             dim_o_i,

  // Operand stream input (shared with operand_window)
  input  logic                 beat_valid_i,
  output logic                 beat_ready_o,
  input  operand_stream_kind_e beat_kind_i,
  input  fp32_t                beat_data_i [16],
  input  logic                 beat_last_i,

  output logic                 cav_valid_o,
  input  logic                 cav_ready_i,
  output fp32_t                cav_eta_o [GBP_MAX_VAR_DIM],
  output fp32_t                cav_L_o   [GBP_MAX_PACKED_VAR],

  output logic                 stream_error_o
);
```

**Timing**: `start_valid_i && start_ready_o` is the operation-level handshake that arms the accumulator. After it fires, the module expects `OST_CAV_FACTOR_O`, then `OST_CAV_BELIEF_O`, then `OST_CAV_OLD_TO_O` beats on the shared `beat_valid_i` interface. Each stream is delimited by `beat_kind_i` and `beat_last_i`.

### 11.3 Computation

```text
On start: acc_eta = 0, acc_L = 0

For each OST_CAV_FACTOR_O beat:
  acc_eta += factor_eta_o
  acc_L   += factor_L_oo

For each OST_CAV_BELIEF_O beat:
  acc_eta += belief_eta_o
  acc_L   += belief_L_o

For each OST_CAV_OLD_TO_O beat:
  acc_eta -= old_msg_eta_to_o
  acc_L   -= old_msg_L_to_o

After all three streams consumed:
  cav_eta_o = acc_eta
  cav_L_o   = acc_L
  cav_valid_o = 1
```

### 11.4 Stream error

`stream_error_o` is asserted if an unexpected `beat_kind_i` arrives or streams arrive in the wrong order.

---

## 12. Module: `rhs_builder_for_message`

### 12.1 Role

Builds the multi-RHS matrix for factor-message Schur complement.

```text
A = Lambda_cav_o
B = [Lambda_oi, eta_cav_o]
```

where `Lambda_oi = transpose(Lambda_io)`.

### 12.2 Inputs

```systemverilog
input gbp_dim_e dim_i_i;
input gbp_dim_e dim_o_i;
input fp32_t    L_io_dense_i [GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM];
input fp32_t    cav_eta_o_i  [GBP_MAX_VAR_DIM];
```

### 12.3 Outputs

```systemverilog
output logic [2:0] nrhs_o;       // dim_i + 1
output fp32_t      B_o [GBP_MAX_VAR_DIM] [GBP_MAX_RHS];
```

### 12.4 Layout rule

```text
B[:, 0 : dim_i-1] = transpose(L_io)
B[:, dim_i]       = cav_eta_o
```

Unused rows/columns are zeroed.

---

## 13. Module: `ldlt_solve_core`

### 13.1 Role

Solves multiple right-hand sides for SPD or regularized-SPD matrices of size 1, 3, or 6.

Shared by:

```text
factor message: solve(A_cav, [Lambda_oi, eta_cav])
belief update:  solve(Lambda_belief, eta_belief)
```

### 13.2 Request

```systemverilog
typedef struct {
  logic [0:0] ctx_id;       // V0 tied to 0; reserved for CTX_DEPTH>1
  gbp_dim_e dim;
  logic [2:0] nrhs;
  fp32_t A_packed [GBP_MAX_PACKED_VAR];
  fp32_t B [GBP_MAX_VAR_DIM] [GBP_MAX_RHS];
  fp32_t diag_lambda;
  fp32_t pivot_eps;
  logic  regularize_en;
} solve_req_t;
```

### 13.3 Response

```systemverilog
typedef struct {
  logic [0:0] ctx_id;       // echoes req.ctx_id
  gbp_dim_e dim;
  logic [2:0] nrhs;
  fp32_t X [GBP_MAX_VAR_DIM] [GBP_MAX_RHS];

  logic  fail;
  logic  regularized;
  logic  nan_guard;
  fp32_t min_pivot;
} solve_rsp_t;
```

### 13.4 Ports

```systemverilog
input  logic       req_valid_i;
output logic       req_ready_o;
input  solve_req_t req_i;

output logic       rsp_valid_o;
input  logic       rsp_ready_i;
output solve_rsp_t rsp_o;
```

### 13.5 Internal stages

```text
S0: unpack A_packed
    if regularize_en: A_reg[k,k] = A[k,k] + diag_lambda
S1: LDLT factorization
S2: forward solve L y = B
S3: diagonal scale z = D^{-1} y
S4: backward solve L^T x = z
S5: pack X and status
```

### 13.6 Pivot check behavior

```text
if D_k <= pivot_eps:
  fail = 1
```

The solver does **not** silently modify the matrix on pivot failure. Regularization (diagonal loading) was already applied in S0 before factorization. The solver only reports whether the resulting factorization had acceptable pivots.

The `regularized` output flag indicates whether the solver actually used the modified matrix `A + diag_lambda * I`:

```systemverilog
regularized_o = regularize_en_i && (diag_lambda_i != 0);
```

It does **not** mean "a small pivot was detected and regularization saved the solve"; it simply reports that the input requested non-zero diagonal loading.

V0 has no automatic retry inside `ldlt_solve_core` or `compute_unit_wrapper`. On pivot failure, the solver sets `fail=1` and reports `min_pivot`. The scheduler may issue a new command with a larger `diag_lambda` if retry is desired.

---

## 14. Module: `schur_update_unit`

### 14.1 Role

Computes raw message parameters after the solve result is available.

### 14.2 Inputs

```systemverilog
input  logic      valid_i;
output logic      ready_o;

input  gbp_dim_e  dim_i_i;
input  gbp_dim_e  dim_o_i;

input  fp32_t     factor_eta_i_i [GBP_MAX_VAR_DIM];
input  fp32_t     factor_L_ii_i  [GBP_MAX_PACKED_VAR];
input  fp32_t     L_io_dense_i   [GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM];
input  fp32_t     solve_X_i      [GBP_MAX_VAR_DIM] [GBP_MAX_RHS];
```

### 14.3 Outputs

```systemverilog
output logic      valid_o;
input  logic      ready_i;

output fp32_t     msg_eta_raw_o [GBP_MAX_VAR_DIM];
output fp32_t     msg_L_raw_o   [GBP_MAX_PACKED_VAR];
```

### 14.4 Computation

```text
X_Lambda = solve_X[:, 0 : dim_i-1]
X_eta    = solve_X[:, dim_i]

msg_L_raw[p,q] = factor_L_ii[p,q] - dot(L_io[p,:], X_Lambda[:,q])
msg_eta_raw[p] = factor_eta_i[p]  - dot(L_io[p,:], X_eta[:])
```

Only the upper triangle of `msg_L_raw` is generated.

### 14.5 Suggested datapath

```text
NUM_DOT = 9
MAX_DOT_LEN = 6
```

3D message: 9 scalar outputs in one Schur cycle. 6D message: 27 scalar outputs in three Schur cycles.

---

## 15. Module: `damping_unit`

### 15.1 Role

Applies message damping for factor-message operations.

### 15.2 Inputs

```systemverilog
input  logic     valid_i;
output logic     ready_o;
input  gbp_dim_e dim_i_i;
input  fp32_t    damping_i;

input  fp32_t    msg_eta_raw_i      [GBP_MAX_VAR_DIM];
input  fp32_t    msg_L_raw_i        [GBP_MAX_PACKED_VAR];
input  fp32_t    old_msg_eta_to_i_i [GBP_MAX_VAR_DIM];
input  fp32_t    old_msg_L_to_i_i   [GBP_MAX_PACKED_VAR];
```

### 15.3 Outputs

```systemverilog
output logic     valid_o;
input  logic     ready_i;
output fp32_t    msg_eta_o    [GBP_MAX_VAR_DIM];
output fp32_t    msg_L_o      [GBP_MAX_PACKED_VAR];
```

### 15.4 Computation

```text
msg_new = (1 - damping) * msg_raw + damping * old_msg
```

---

## 16. Module: `packed_accumulator`

### 16.1 Role

Accumulates prior and incoming factor-to-variable messages for a variable belief update. Operates in streaming mode.

### 16.2 Inputs

```systemverilog
input  logic          start_valid_i;
output logic          start_ready_o;
input  gbp_dim_e      dim_i;
input  fp32_t         prior_eta_i   [GBP_MAX_VAR_DIM];
input  fp32_t         prior_L_i     [GBP_MAX_PACKED_VAR];
input  logic [15:0]   degree_i;

input  logic          msg_valid_i;
output logic          msg_ready_o;
input  fp32_t         msg_eta_i     [GBP_MAX_VAR_DIM];
input  fp32_t         msg_L_i       [GBP_MAX_PACKED_VAR];
input  logic          msg_last_i;
```

### 16.3 Outputs

```systemverilog
output logic          acc_valid_o;
input  logic          acc_ready_i;
output gbp_dim_e      dim_o;
output fp32_t         acc_eta_o     [GBP_MAX_VAR_DIM];
output fp32_t         acc_L_o       [GBP_MAX_PACKED_VAR];
output logic [15:0]   msg_count_o;
output logic          degree_mismatch_o;
```

### 16.4 Computation

```text
acc_eta = prior_eta
acc_L   = prior_L
for each incoming message:
  acc_eta += msg_eta
  acc_L   += msg_L
```

### 16.5 Termination

V0 termination rule: **`msg_count == degree_i`**.

`msg_last_i` is checked for debug/status only:

```text
degree_mismatch_o = (msg_last_i && msg_count + 1 != degree_i)
                 || (msg_count == degree_i && !msg_last_i)
```

`degree_mismatch_o` does not affect computation; it is reported in `cu_done_t`.

> **Programming rule**: The scheduler/compiler must guarantee that the number of `OST_BELIEF_MSG` beats matches `degree_i`. If they differ, behavior is undefined and may cause the accumulator to wait indefinitely.

### 16.6 Implementation note

Use a 16-lane FP32 add array. For `dim=6`, 27 scalar entries require two cycles per incoming message if using 16 lanes.

---

## 17. Module: `belief_operand_unpacker`

### 17.1 Role

Converts `operand_stream_beat_t` beats into typed `belief_start_t` and `belief_msg_stream_t` structs for `packed_accumulator`. This module understands the stream beat layout; `packed_accumulator` only understands the mathematical accumulation.

### 17.2 Ports

```systemverilog
module belief_operand_unpacker (
  input  logic                 clk_i,
  input  logic                 reset_i,

  // Configuration (must be stable before first beat)
  input  gbp_dim_e             dim_i,
  input  logic [15:0]          degree_i,
  input  logic [31:0]          op_id_i,

  // Operand stream input
  input  logic                 beat_valid_i,
  output logic                 beat_ready_o,
  input  operand_stream_beat_t beat_i,

  // Packed accumulator interface
  output logic                 prior_valid_o,
  input  logic                 prior_ready_i,
  output belief_start_t        prior_o,

  output logic                 msg_valid_o,
  input  logic                 msg_ready_i,
  output belief_msg_stream_t   msg_o,

  output logic                 stream_error_o
);
```

### 17.3 Behavior

```text
Configuration fields:
  dim_i    -> fills prior_o.dim and msg_o.dim
  degree_i -> fills prior_o.degree (not encoded in stream payload)
  op_id_i  -> used to validate beat.op_id

On OST_BELIEF_PRIOR beats:
  Unpack into belief_start_t fields per Section 5.5 packing order.
  prior_o.dim = dim_i, prior_o.degree = degree_i (from config, not stream).
  Assert prior_valid_o when all fields received.

On OST_BELIEF_MSG beats:
  Unpack into belief_msg_stream_t fields per Section 5.5 packing order.
  msg_o.dim = dim_i.
  Assert msg_valid_o for each complete message.
  Forward beat.last to msg_o.last.

On unexpected kind or op_id mismatch: assert stream_error_o.
```

---

## 18. Module: `belief_solve_adapter`

### 18.1 Role

Converts accumulated belief parameters into a solve request for `ldlt_solve_core`.

### 18.2 Inputs

```systemverilog
input  logic      valid_i,
output logic      ready_o,
input  gbp_dim_e  dim_i,
input  fp32_t     acc_eta_i      [GBP_MAX_VAR_DIM],
input  fp32_t     acc_L_i        [GBP_MAX_PACKED_VAR],
input  fp32_t     diag_lambda_i,
input  fp32_t     pivot_eps_i,
input  logic      regularize_en_i
```

### 18.3 Outputs

```systemverilog
output logic       solve_req_valid_o,
input  logic       solve_req_ready_i,
output solve_req_t solve_req_o
```

### 18.4 Mapping

```text
A = acc_L
B[:,0] = acc_eta
nrhs = 1
diag_lambda = diag_lambda_i
pivot_eps = pivot_eps_i
regularize_en = regularize_en_i
```

---

## 19. Module: `belief_result_builder`

### 19.1 Role

Builds final belief result from accumulated `eta/Lambda`, solver output `mu`, and old `mu`. Also computes residual internally.

### 19.2 Inputs

```systemverilog
input  logic       valid_i,
output logic       ready_o,
input  gbp_dim_e   dim_i,
input  fp32_t      acc_eta_i [GBP_MAX_VAR_DIM],
input  fp32_t      acc_L_i   [GBP_MAX_PACKED_VAR],
input  fp32_t      mu_old_i  [GBP_MAX_VAR_DIM],
input  solve_rsp_t solve_rsp_i
```

### 19.3 Outputs

```systemverilog
output logic            valid_o,
input  logic            ready_i,
output belief_result_t  result_o
```

### 19.4 Internal computation

```text
result_o.eta = acc_eta_i
result_o.L_packed = acc_L_i
result_o.mu = solve_rsp_i.X[:,0]
result_o.residual = sum_j (mu[j] - mu_old[j])^2
result_o.fail = solve_rsp_i.fail
result_o.regularized = solve_rsp_i.regularized
result_o.nan_guard = solve_rsp_i.nan_guard
result_o.min_pivot = solve_rsp_i.min_pivot
```

No separate `belief_residual_unit` or `WB_RESIDUAL` in V0. Residual is included in `WB_BELIEF`.

---

## 20. Module: `relin_check_unit` (V1)

### 20.1 Role

Checks whether a factor should be relinearized.

### 20.2 Inputs

```systemverilog
input  logic       valid_i,
output logic       ready_o,
input  logic [3:0] total_dim_i,
input  fp32_t      mu_current_i       [GBP_MAX_FACTOR_DIM],
input  fp32_t      x_linearization_i  [GBP_MAX_FACTOR_DIM],
input  fp32_t      beta_sq_i
```

### 20.3 Outputs

```systemverilog
output logic       valid_o,
input  logic       ready_i,
output logic       need_refresh_o,
output fp32_t      delta_norm_sq_o
```

### 20.4 Computation

```text
delta_norm_sq = sum_j (mu_current[j] - x_linearization[j])^2
need_refresh  = delta_norm_sq > beta_sq
```

---

## 21. Module: `robust_scale_unit` (V1, block-wise)

### 21.1 Role

Applies a scalar robust weight to canonical factor parameters in block form.

### 21.2 Inputs

```systemverilog
input  logic              valid_i,
output logic              ready_o,
input  gbp_factor_type_e  factor_type_i,
input  gbp_dim_e          dim0_i,
input  gbp_dim_e          dim1_i,
input  fp32_t             weight_i,
input  fp32_t             eta0_i       [GBP_MAX_VAR_DIM],
input  fp32_t             eta1_i       [GBP_MAX_VAR_DIM],
input  fp32_t             L00_packed_i [GBP_MAX_PACKED_VAR],
input  fp32_t             L01_dense_i  [GBP_MAX_VAR_DIM * GBP_MAX_VAR_DIM],
input  fp32_t             L11_packed_i [GBP_MAX_PACKED_VAR]
```

### 21.3 Outputs

```systemverilog
output logic              valid_o,
input  logic              ready_i,
output robust_result_t    result_o;
```

### 21.4 Computation

```text
result_o.eta0 = weight * eta0
result_o.eta1 = weight * eta1
result_o.L00  = weight * L00
result_o.L01  = weight * L01
result_o.L11  = weight * L11
```

---

## 22. Module: `writeback_packer`

### 22.1 Role

Converts internal results into write-stream records.

### 22.2 Inputs

```systemverilog
input  logic          valid_i,
output logic          ready_o,
input  gbp_core_rsp_t core_rsp_i
```

### 22.3 Outputs

```systemverilog
output logic               wb_valid_o,
input  logic               wb_ready_i,
output writeback_record_t  wb_o
```

### 22.4 Writeback record

```systemverilog
typedef struct {
  logic [31:0] addr;
  logic [15:0] nwords;
  wb_kind_e    kind;

  fp32_t       payload [GBP_MAX_WB_SCALARS];

  logic        fail;
  logic        regularized;
  logic        nan_guard;
} writeback_record_t;
```

### 22.5 Writeback kinds

```systemverilog
typedef enum logic [3:0] {
  WB_MSG       = 4'd0,
  WB_BELIEF    = 4'd1,
  WB_RELIN     = 4'd2,
  WB_ROBUST    = 4'd3
} wb_kind_e;
```

V0: `WB_BELIEF` payload includes eta, Lambda, mu, and residual. No separate `WB_RESIDUAL`.

### 22.6 `WB_BELIEF` payload order

For a belief result with dimension `d`, the `WB_BELIEF` payload is written to SPM in compact form:

```text
payload[0 .. E(d)-1]:          eta + Lambda_packed
payload[E(d) .. E(d)+d-1]:     mu
payload[E(d)+d]:               residual
```

This matches the `OST_BELIEF_PRIOR` read layout for the first `E(d)+d` words. The trailing residual word is appended by `belief_result_builder` and is also reported to the scheduler via `cu_done_t.residual`; it is not part of the Variable Node STATE block defined in the SPM metadata spec.

### 22.7 Writeback payload sizes

```text
WB_MSG_D6    = E(6) = 27 words
WB_BELIEF_D6 = E(6) + 6 + 1 = 34 words
```

V0 `GBP_MAX_WB_SCALARS = 36` covers message and belief for all dimensions.

V1 note: If `ENABLE_ROBUST_P=1`, `WB_ROBUST_SE3` requires up to 90 words. In that case, set `GBP_MAX_WB_SCALARS = 96`.

---

## 24. Module: `compute_unit_wrapper`

### 24.1 Role

Integration block between PE stream engines and `gbp_compute_core`. Responsibilities:

```text
1. receive compute commands from resident_scheduler
2. issue read requests based on operand_desc in command
3. frame operand stream beats into operation-local streams
4. feed gbp_compute_core
5. convert gbp_core_rsp_t to writeback_record_t via writeback_packer
6. emit writeback records to write_stream_engine
7. report done/error/status to scheduler
```

### 24.2 Top-level ports

```systemverilog
module compute_unit_wrapper #(
  parameter int FP_WIDTH = 32,
  parameter int OPERAND_STREAM_WIDTH = 16,
  parameter int WB_STREAM_WIDTH = 8
)(
  input  logic clk_i,
  input  logic reset_i,

  input  logic         cmd_valid_i,
  output logic         cmd_ready_o,
  input  cu_cmd_t      cmd_i,

  output logic         rd_req_valid_o,
  input  logic         rd_req_ready_i,
  output cu_rd_req_t   rd_req_o,

  input  logic                 operand_valid_i,
  output logic                 operand_ready_o,
  input  operand_stream_beat_t operand_i,

  output logic                wb_valid_o,
  input  logic                wb_ready_i,
  output writeback_record_t   wb_o,

  output logic                done_valid_o,
  input  logic                done_ready_i,
  output cu_done_t            done_o
);
```

### 24.3 Operand descriptor

```systemverilog
typedef struct {
  logic                 valid;
  operand_stream_kind_e kind;
  logic [31:0]          base_addr;
  logic [15:0]          nbeats;
} cu_operand_desc_t;
```

### 24.4 Command struct

```systemverilog
typedef struct {
  gbp_op_e           op;
  gbp_factor_type_e  factor_type;
  gbp_dim_e          dim_i;
  gbp_dim_e          dim_o;
  logic              direction;

  logic [0:0]        ctx_id;       // V0 tied to 0; reserved for CTX_DEPTH>1
  logic [31:0]       op_id;
  logic [31:0]       node_id;
  logic [31:0]       factor_id;
  logic [31:0]       dst_addr;
  logic [31:0]       aux_addr;     // V0: unused, must be zero

  logic [15:0]       degree;
  fp32_t             damping;
  fp32_t             diag_lambda;
  fp32_t             pivot_eps;
  logic              regularize_en;

  cu_operand_desc_t  operand_desc [8];
} cu_cmd_t;
```

`operand_desc` provides the read plan for each operand stream. The wrapper issues read requests based on these descriptors; it does not interpret `node_id`/`factor_id` to derive addresses.

**Required `operand_desc[]` order** (indices 0-7):

```text
OP_MSG_F2V:
  [0] OST_MSG_STATIC       (required)
  [1] OST_CAV_FACTOR_O     (required)
  [2] OST_CAV_BELIEF_O     (required)
  [3] OST_CAV_OLD_TO_O     (required)
  [4..7] unused (valid=0)

OP_BELIEF:
  [0] OST_BELIEF_PRIOR     (required)
  [1] OST_BELIEF_MSG       (required, contiguous in SPM)
  [2..7] unused (valid=0)
```

Streams must be issued in index order. `cavity_builder` and `belief_operand_unpacker` check `beat_kind_i` against the expected sequence.

### 24.5 Read request struct

```systemverilog
typedef struct {
  logic [31:0]          op_id;
  gbp_op_e              op;
  logic [31:0]          base_addr;
  logic [15:0]          nbeats;
  operand_stream_kind_e kind;
} cu_rd_req_t;
```

### 24.6 Done struct

```systemverilog
typedef struct {
  logic [31:0] node_id;
  logic [31:0] factor_id;
  gbp_op_e     op;

  logic [0:0]  ctx_id;       // echoes cmd.ctx_id
  logic        success;
  logic        fail;
  logic        regularized;
  logic        nan_guard;
  logic        degree_mismatch;
  logic        stream_error;

  fp32_t       residual;
  fp32_t       min_pivot;
} cu_done_t;
```

`residual` is valid only for `OP_BELIEF`; for `OP_MSG_F2V` it is driven to 0 and should be ignored by the scheduler.

### 24.7 Wrapper FSM

```text
W_IDLE:
  wait for cmd_valid_i

W_ISSUE_READS:
  issue read requests from cmd.operand_desc[]

W_LOAD_STATIC:
  load OST_MSG_STATIC or OST_BELIEF_PRIOR

W_STREAM_OPERANDS:
  forward cavity/message streams to compute core

W_WAIT_CORE:
  wait for gbp_compute_core response
  V0: report fail/min_pivot to scheduler via cu_done_t; no automatic retry

W_EMIT_WB:
  emit writeback_record_t to write_stream_engine

W_DONE:
  emit cu_done_t to scheduler
```

### 24.8 Wrapper counters

```systemverilog
typedef struct packed {
  logic [63:0] cnt_cmd_accept;
  logic [63:0] cnt_done;
  logic [63:0] cnt_operand_beats;
  logic [63:0] cnt_wb_beats;

  logic [63:0] cnt_wait_operand;
  logic [63:0] cnt_wait_core;
  logic [63:0] cnt_wait_wb;
  logic [63:0] cnt_wait_done_ready;

  logic [63:0] cnt_degree_mismatch;
  logic [63:0] cnt_core_fail;
} cu_wrapper_perf_t;
```

---

## 25. Latency Model

### 25.1 Assumptions

```text
OPERAND_STREAM_WIDTH = 16 FP32 scalars/cycle
WB_STREAM_WIDTH      = 8 FP32 scalars/cycle
one LDLT solver
one Schur update unit
NUM_DOT = 9 dot engines
FP32 internal arithmetic (hardfloat, 3-stage pipeline)
local SPM hit, no NoC miss
```

**FP pipeline characteristics** (hardfloat `bsg_fpu_add_sub` / `bsg_fpu_mul`):

```text
Pipeline depth: 3 cycles per operation
Throughput:     1 result/cycle (pipelined)
Backpressure:   ready_and_o = ~stall & en_i
Output:         v_o = v_3_r, consumed by yumi_i
```

**Multi-operation chains** (e.g., damping = sub → mul → add):
- Pipeline latency: 3 + 3 + 3 = 9 cycles
- Throughput: 1 element/cycle after pipeline fills
- With 16 lanes: ceil(E(d)/16) batches

> **Latency-model assumption**: The cycle estimates in Sections 25.2–25.6 use a conservative model in which adjacent batches of the same operation are **not overlapped**. For example, `B` batches of a single FP op are counted as `B * 3` cycles, and `B` batches of a damping chain as `B * 9` cycles. If the implementation pipelines across batches, the actual latency will be lower (`B + 2` and `B + 8`, respectively).

### 25.2 Solver latency

```text
C_solve(1) = 5
C_solve(3) = 16
C_solve(6) = 31
```

### 25.3 Message update latency

For `OP_MSG_F2V` with `d_i`, `d_o`:

```text
M0 decode          = 1
M1 load_static     = ceil((2E(d_i) + d_i*d_o) / 16)
M2 cavity_stream   = ceil(3E(d_o) / 16) * 3  // FP add/sub, 3 cycles per batch
M3 rhs_build       = ceil(d_o*(d_i+1) / 16)
M4 solve           = C_solve(d_o)
M5 schur           = ceil(E(d_i) / 9) * 3    // FP mul+add, 3 cycles per batch
M6 damp            = ceil(E(d_i) / 16) * 9   // sub→mul→add chain, 9 cycles per batch
M7 pack_writeback  = ceil(E(d_i) / 8)
```

| message | `d_i,d_o` | M0 | M1 | M2 | M3 | M4 | M5 | M6 | M7 | total |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| scalar | 1,1 | 1 | 1 | 3 | 1 | 5 | 3 | 9 | 1 | 24 |
| SE(2) | 3,3 | 1 | 2 | 6 | 1 | 16 | 6 | 9 | 2 | 40 |
| BA -> camera | 6,3 | 1 | 5 | 6 | 2 | 16 | 9 | 18 | 4 | 61 |
| BA -> landmark | 3,6 | 1 | 3 | 18 | 2 | 31 | 3 | 9 | 2 | 69 |
| SE(3) | 6,6 | 1 | 6 | 18 | 3 | 31 | 9 | 18 | 4 | 90 |

### 25.4 Full binary factor latency

Core-Lite (one directional engine):

| factor | latency |
|---|---:|
| scalar | 48 |
| SE(2) | 80 |
| BA | 130 |
| SE(3) | 180 |

### 25.5 Belief update latency

For `OP_BELIEF` with dimension `d` and degree `k`:

```text
B0 decode          = 1
B1 load_prior_mu   = ceil((E(d) + d) / 16) + 1
B2 accumulate_msgs = ceil(k * E(d) / 16) * 3  // FP add, 3 cycles per batch
B3 solve_adapter   = 1
B4 solve           = C_solve(d)
B5 result_build    = 1
B6 pack_writeback  = ceil((E(d) + d + 1) / 8)
```

| dim | degree=2 | degree=5 | degree=10 | degree=20 |
|---:|---:|---:|---:|---:|
| 1 | 14 | 14 | 17 | 20 |
| 3 | 29 | 32 | 41 | 59 |
| 6 | 55 | 70 | 94 | 145 |

### 25.6 Scheduler-visible latency recommendation

```text
COST_MSG_SCALAR       = 24
COST_MSG_SE2          = 40
COST_MSG_BA_CAMERA    = 61
COST_MSG_BA_LANDMARK  = 69
COST_MSG_SE3          = 90

COST_BELIEF_D1(k)     = 11 + ceil(2k/16)*3
COST_BELIEF_D3(k)     = 23 + ceil(9k/16)*3
COST_BELIEF_D6(k)     = 43 + ceil(27k/16)*3
```

### 25.7 Pipeline utilization analysis

For SE(3) message update (total ~90 cycles):

| Stage | Cycles | % of total |
|-------|-------:|----------:|
| Frontend (M0-M3) | 28 | 31% |
| Solver (M4) | 31 | 34% |
| Backend (M5-M7) | 31 | 34% |

The three macro-stages are roughly balanced. The solver is the bottleneck for larger dimensions.

**FP unit utilization** (damping, SE3):
- 27 elements, 16 lanes → 2 batches
- Each batch: 9 cycles (sub→mul→add pipeline)
- FP busy: 18 cycles out of ~90 total = 20% utilization

The FP units are idle most of the time during stream loading/unloading. This is expected for V0 single-issue. V1 pipeline would increase utilization by overlapping frontend/backend of consecutive operations.

---

## 26. V0 Message Datapath Sequence

```text
M0: op_decoder validates dimensions and direction
M1: operand_window captures msg_static_window_t from OST_MSG_STATIC
M2: cavity_builder consumes OST_CAV_FACTOR_O / OST_CAV_BELIEF_O / OST_CAV_OLD_TO_O
M3: rhs_builder_for_message builds B=[Lambda_oi, eta_cav]
M4: ldlt_solve_core solves A_cav X = B (with diag_lambda if regularize_en)
M5: schur_update_unit computes raw eta/Lambda message
M6: damping_unit applies damping against old target message
M7: rsp_assembler emits gbp_core_rsp_t; wrapper writeback_packer emits WB_MSG
```

---

## 27. V0 Belief Datapath Sequence

```text
B0: op_decoder validates dim
B1: belief_operand_unpacker unpacks OST_BELIEF_PRIOR into belief_start_t
B2: belief_operand_unpacker unpacks OST_BELIEF_MSG beats; packed_accumulator consumes them
B3: belief_solve_adapter creates solve request
B4: ldlt_solve_core solves mu = Lambda^{-1} eta (with diag_lambda if regularize_en)
B5: belief_result_builder computes eta/Lambda/mu/residual/status
B6: rsp_assembler emits gbp_core_rsp_t; wrapper writeback_packer emits WB_BELIEF
```

---

## 28. V0 / V1 / V2 Implementation Boundary

### V0: required

```text
OP_MSG_F2V
OP_BELIEF
LDLT solve for dim 1/3/6
message damping
belief residual (inside belief_result_builder)
additive diagonal loading regularization (inside solver)
streaming operand window (no MAX_DEGREE buffer)
```

### V1: optional but recommended

```text
OP_RELIN_CHECK (guarded by ENABLE_RELIN_P)
OP_ROBUST_SCALE (guarded by ENABLE_ROBUST_P, block-wise)
more detailed numerical status
```

### V2: performance extension

```text
two directional message lanes
optional second LDLT solver
wider packed accumulator
FP16/BF16/SDF storage conversion at boundary
```

---

## 29. Verification Hooks

### 29.1 Unit-level tests

```text
cavity_builder: compare A_cav/eta_cav against Python reference
ldlt_solve_core: compare solve result for random SPD 1x1/3x3/6x6 matrices
schur_update_unit: compare raw message against Python Schur complement
packed_accumulator: compare accumulated belief against reference
writeback_packer: compare layout and payload order
```

### 29.2 Core-level tests

```text
OP_MSG_F2V scalar, SE2, BA camera, BA landmark, SE3
OP_BELIEF d1, d3, d6 with degree sweep
near-singular regularization cases (diag_lambda sweep)
damping alpha = 0, 0.5, 1
NaN/Inf guard cases
pivot fail reporting cases
```

### 29.3 Integration tests

```text
same SPM image + same op sequence -> same writeback trace as software trace model
```

---

## 30. Key Design Decisions

1. The compute core consumes decoded operands as typed streams, not addresses.
2. The compute core implements canonical Gaussian algebra, not application-specific nonlinear functions.
3. Factor message and belief update share the same LDLT solve core in V0.
4. Directional factor messages are serial in Core-Lite and parallel only in Core-Perf.
5. Old factor-to-variable messages are still required to construct cavity and apply damping.
6. Regularization uses additive diagonal loading inside the solver; pivot failure is reported, not silently corrected.
7. `cavity_builder` is a stream accumulator, not a batch array module.
8. `compute_unit_wrapper` receives a read plan (`operand_desc[]`) from the scheduler; it does not interpret metadata.
9. V0 is single-issue (`CTX_DEPTH=1`); no interleaved operations. But all interfaces use ready/valid and carry `op_id`/`ctx_id` for future pipeline expansion.
10. Residual is computed inside `belief_result_builder`; no separate residual unit or writeback record.
11. `writeback_packer` lives inside `compute_unit_wrapper`, not inside `gbp_compute_core`. The core outputs `gbp_core_rsp_t`.
12. `belief_operand_unpacker` converts stream beats into typed structs for `packed_accumulator`. The accumulator only does math, not stream parsing.
13. V0 does not automatically retry on pivot fail. The wrapper reports fail/min_pivot to the scheduler.
14. `gbp_core_req_t.degree` carries the message count for `OP_BELIEF`. It is zero for `OP_MSG_F2V`.
15. Target-major normalization (BA `direction=1` transpose) is the responsibility of the operand stream producer, not the wrapper or core.
16. `stream_error` from `cavity_builder` and `belief_operand_unpacker` propagates through `gbp_core_rsp_t` and `cu_done_t`.
17. **Result ownership**: V0 only commits local-owned results to local SPM. Non-local effects are notify events through NI. Remote payload writeback is not supported.
18. **Pipeline-ready**: All stage boundaries use valid/ready handshakes. Inter-stage payloads carry `op_id`/`ctx_id` handles, not bulk data. Context tables use `msg_ctx[CTX_DEPTH]` style (V0 depth=1).
19. **Pipeline cut points**: solver input FIFO, solver output FIFO, optional schur-to-damp FIFO, rsp FIFO before wrapper. Cavity+rhs are one frontend macro-stage.
20. **FP pipeline awareness**: All FP ops use hardfloat 3-stage pipeline. Latency = 3 cycles/op, throughput = 1 result/cycle after fill.

---

## 31. Minimal RTL Bring-up Order

```text
 1. gbp_op_pkg.sv (all types and parameters from Sections 4-7)
 2. packed layout helper functions
 3. ldlt_solve_core: dim=1, then dim=3, then dim=6
 4. operand_window
 5. cavity_builder (stream accumulator)
 6. rhs_builder_for_message
 7. schur_update_unit
 8. damping_unit
 9. belief_operand_unpacker
10. packed_accumulator (streaming mode)
11. belief_solve_adapter + belief_result_builder
12. op_decoder
13. gbp_compute_core top-level FSM (outputs gbp_core_rsp_t)
14. writeback_packer (inside compute_unit_wrapper)
15. compute_unit_wrapper skeleton (defines stream framing for unit tests)
16. compute_unit_wrapper full integration
17. PE/SPM trace-level integration tests
```
