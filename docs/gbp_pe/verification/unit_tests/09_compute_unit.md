# Compute Unit Unit Test (GBP Compute Core v0.7)

> **Status:** 🟡 **Spec updated for v0.7 architecture, implementation pending**
>
> **Architecture change:** The compute unit is now split into `gbp_compute_core` (arithmetic) + `compute_unit_wrapper` (stream framing + writeback). This test spec covers the new v0.7 architecture defined in `08_NEW_COMPUTE_UNIT.md`.
>
> **Key differences from the legacy matmul-based compute engine (pre-v0.7):**
> - Streaming operand model (no batch array)
> - `cavity_builder` is a stream accumulator, not a batch module
> - `belief_operand_unpacker` converts stream beats into typed structs
> - `writeback_packer` lives inside `compute_unit_wrapper`
> - V0 no automatic retry on pivot fail

## 1. Test Objective

Verify that the GBP Compute Core correctly:
- Receives compute commands via `cmd_valid/ready` handshake
- Consumes operand streams via `operand_valid/ready` handshake
- Produces responses via `rsp_valid/ready` handshake
- Performs factor-to-variable message update (`OP_MSG_F2V`)
- Performs variable belief update (`OP_BELIEF`)
- Reports status (fail, regularized, nan_guard, degree_mismatch, stream_error)

---

## 2. Module Under Test

### 2.1 `gbp_compute_core`

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

### 2.2 `compute_unit_wrapper`

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

> **Note (v0.7):** All command, operand-beat, and response structs now carry a 1-bit `ctx_id` field. In V0 this is tied to `0` and is reserved for future `CTX_DEPTH>1` expansion. The port lists above omit `ctx_id` for readability.

---

## 3. Preconditions

- Clock: 100MHz (10ns period)
- Reset: Active-high synchronous (`reset_i`)
- Initial state: IDLE
- All inputs driven to default (valid=0, ready=0)

---

## 4. Test Cases

### 4.1 Test Case 1: `OP_MSG_F2V` — Scalar Factor (dim=1)

**Scenario**: Scalar factor message update. Simplest case.

**Command**:
```systemverilog
cmd.op           = OP_MSG_F2V
cmd.factor_type  = FACTOR_SCALAR
cmd.dim_i        = DIM_1
cmd.dim_o        = DIM_1
cmd.direction    = 0
cmd.damping      = 0.3
cmd.diag_lambda  = 1e-9
cmd.pivot_eps    = 1e-12
cmd.regularize_en = 1
```

**Operand stream** (per Section 5.5 packing order):
```
OST_MSG_STATIC beat 0:
  data[0] = eta_i[0]       (target eta)
  data[1] = L_ii_packed[0] (target Lambda)
  data[2] = L_io_dense[0]  (cross block)
  data[3] = old_msg_eta[0] (old target message eta)
  data[4] = old_msg_L[0]   (old target message Lambda)

OST_CAV_FACTOR_O beat 0:
  data[0] = eta_o[0]       (other factor eta)
  data[1] = L_oo_packed[0] (other factor Lambda)

OST_CAV_BELIEF_O beat 0:
  data[0] = belief_eta_o[0]
  data[1] = belief_L_o[0]

OST_CAV_OLD_TO_O beat 0:
  data[0] = old_msg_eta_to_o[0]
  data[1] = old_msg_L_to_o[0]
```

**Expected response**:
```
rsp.op = OP_MSG_F2V
rsp.msg_result.dim = DIM_1
rsp.msg_result.eta[0] = computed value
rsp.msg_result.L_packed[0] = computed value
rsp.fail = 0
```

**Pass criteria**:
- [ ] `cmd_ready_o` asserts when `cmd_valid_i` is high
- [ ] `operand_ready_o` asserts for each beat
- [ ] All 4 operand streams consumed in correct order
- [ ] `rsp_valid_o` asserts after computation
- [ ] Numerical result matches golden reference (within FP32 tolerance)

---

### 4.2 Test Case 2: `OP_MSG_F2V` — SE(2) Factor (dim=3)

**Scenario**: SE(2) factor message update. Tests 3×3 matrix operations.

**Command**:
```systemverilog
cmd.op           = OP_MSG_F2V
cmd.factor_type  = FACTOR_SE2
cmd.dim_i        = DIM_3
cmd.dim_o        = DIM_3
cmd.direction    = 0
cmd.damping      = 0.4
cmd.diag_lambda  = 1e-9
cmd.pivot_eps    = 1e-12
cmd.regularize_en = 1
```

**Operand stream**:
```
OST_MSG_STATIC: 2*E(3) + 3*3 = 2*9 + 9 = 27 scalars
OST_CAV_FACTOR_O: E(3) = 9 scalars
OST_CAV_BELIEF_O: E(3) = 9 scalars
OST_CAV_OLD_TO_O: E(3) = 9 scalars
```

**Expected**: 27 scalars output (compact message).

**Pass criteria**:
- [ ] All operand streams consumed
- [ ] Cavity computation correct: `A_cav = L_oo + belief_L - old_msg_L`
- [ ] LDLT solve produces correct X
- [ ] Schur complement correct: `msg_L = L_ii - L_io * X_Lambda`
- [ ] Damping applied: `msg_new = (1-0.4)*msg_raw + 0.4*msg_old`
- [ ] Output matches golden reference

---

### 4.3 Test Case 3: `OP_MSG_F2V` — SE(3) Factor (dim=6)

**Scenario**: SE(3) factor message update. Largest dimension.

**Command**:
```systemverilog
cmd.op           = OP_MSG_F2V
cmd.factor_type  = FACTOR_SE3
cmd.dim_i        = DIM_6
cmd.dim_o        = DIM_6
cmd.direction    = 0
cmd.damping      = 0.5
```

**Operand stream**:
```
OST_MSG_STATIC: 2*E(6) + 6*6 = 2*27 + 36 = 90 scalars (6 beats)
OST_CAV_FACTOR_O: E(6) = 27 scalars (2 beats)
OST_CAV_BELIEF_O: E(6) = 27 scalars (2 beats)
OST_CAV_OLD_TO_O: E(6) = 27 scalars (2 beats)
```

**Expected**: 27 scalars output (compact message).

**Pass criteria**:
- [ ] Multi-beat streams handled correctly
- [ ] 6×6 matrix inversion works
- [ ] 6×6 Schur complement correct
- [ ] Output matches golden reference

---

### 4.4 Test Case 4: `OP_MSG_F2V` — BA Factor (direction=0, dim_i=6, dim_o=3)

**Scenario**: BA factor with camera target, landmark other.

**Command**:
```systemverilog
cmd.op           = OP_MSG_F2V
cmd.factor_type  = FACTOR_BA
cmd.dim_i        = DIM_6   // camera
cmd.dim_o        = DIM_3   // landmark
cmd.direction    = 0
```

**Pass criteria**:
- [ ] Cross block L_io is 6×3 (18 entries)
- [ ] Cavity uses dim_o=3
- [ ] RHS builder produces correct B matrix
- [ ] Output is E(6)=27 scalars

---

### 4.5 Test Case 5: `OP_BELIEF` — Scalar Variable (dim=1, degree=2)

**Scenario**: Scalar variable belief update with 2 incoming messages.

**Command**:
```systemverilog
cmd.op           = OP_BELIEF
cmd.dim_i        = DIM_1
cmd.degree       = 2
cmd.damping      = 0.0  // not used for belief
cmd.regularize_en = 0
```

**Operand stream**:
```
OST_BELIEF_PRIOR beat 0:
  data[0] = prior_eta[0]
  data[1] = prior_L_packed[0]
  data[2] = old_mu[0]

OST_BELIEF_MSG beat 0 (message 0):
  data[0] = msg0_eta[0]
  data[1] = msg0_L_packed[0]

OST_BELIEF_MSG beat 1 (message 1):
  data[0] = msg1_eta[0]
  data[1] = msg1_L_packed[0]
  last = 1
```

**Expected response**:
```
rsp.op = OP_BELIEF
rsp.belief_result.dim = DIM_1
rsp.belief_result.eta[0] = prior_eta + msg0_eta + msg1_eta
rsp.belief_result.L_packed[0] = prior_L + msg0_L + msg1_L
rsp.belief_result.mu[0] = solve(belief_L, belief_eta)
rsp.belief_result.residual = (mu[0] - old_mu[0])^2
rsp.belief_result.degree_mismatch = 0
```

**Pass criteria**:
- [ ] `belief_operand_unpacker` correctly unpacks prior and messages
- [ ] `packed_accumulator` accumulates 2 messages
- [ ] `belief_solve_adapter` creates correct solve request
- [ ] `ldlt_solve_core` produces correct mu
- [ ] `belief_result_builder` computes residual
- [ ] Output matches golden reference

---

### 4.6 Test Case 6: `OP_BELIEF` — SE(3) Variable (dim=6, degree=10)

**Scenario**: SE(3) variable with 10 incoming messages.

**Command**:
```systemverilog
cmd.op           = OP_BELIEF
cmd.dim_i        = DIM_6
cmd.degree       = 10
```

**Operand stream**:
```
OST_BELIEF_PRIOR: E(6) + 6 = 33 scalars (3 beats)
OST_BELIEF_MSG × 10: each E(6) = 27 scalars (2 beats each)
```

**Pass criteria**:
- [ ] 10 messages accumulated correctly
- [ ] 6×6 matrix inversion works
- [ ] Output matches golden reference

---

### 4.7 Test Case 7: Pivot Failure Reporting

**Scenario**: Singular matrix triggers pivot failure.

**Command**:
```systemverilog
cmd.op           = OP_MSG_F2V
cmd.dim_i        = DIM_3
cmd.dim_o        = DIM_3
cmd.regularize_en = 0  // no regularization
cmd.pivot_eps    = 1e-6
```

**Operand stream**: Construct `A_cav` to be near-singular.

**Expected**:
```
rsp.fail = 1
rsp.min_pivot < pivot_eps
```

**Pass criteria**:
- [ ] Solver reports fail=1
- [ ] min_pivot is correct
- [ ] No NaN propagation

---

### 4.8 Test Case 8: `degree_mismatch` Detection

**Scenario**: Send fewer messages than `cmd.degree`.

**Command**:
```systemverilog
cmd.op      = OP_BELIEF
cmd.dim_i   = DIM_3
cmd.degree  = 5
```

**Operand stream**: Send only 3 `OST_BELIEF_MSG` beats with `last=1` on beat 3.

**Expected**:
```
rsp.belief_result.degree_mismatch = 1
```

> **Programming rule (v0.7):** The scheduler/compiler must guarantee that the number of `OST_BELIEF_MSG` beats matches `degree_i`. If they differ, behavior is undefined and the accumulator may wait indefinitely.

**Pass criteria**:
- [ ] `degree_mismatch` asserted
- [ ] This condition is flagged as a programming error; subsequent behavior is undefined and the test may hang

---

### 4.9 Test Case 9: `stream_error` Detection

**Scenario**: Send unexpected `operand_stream_kind`.

**Operand stream**: Send `OST_CAV_FACTOR_O` when `OST_MSG_STATIC` is expected.

**Expected**:
```
rsp.stream_error = 1
rsp.fail = 1
```

**Pass criteria**:
- [ ] `stream_error` asserted
- [ ] `fail` asserted
- [ ] Core returns to IDLE

---

### 4.10 Test Case 10: Backpressure Handling

**Scenario**: Response channel applies backpressure.

**Stimulus**:
```
T+0: cmd_valid = 1, rsp_ready = 0
T+1: operand stream starts
T+N: computation completes, rsp_valid = 1, but rsp_ready = 0
T+N+M: rsp_ready = 1
```

**Pass criteria**:
- [ ] Core holds response until `rsp_ready` asserted
- [ ] No data loss
- [ ] No deadlock

---

### 4.11 Test Case 11: `compute_unit_wrapper` Integration

**Scenario**: Full wrapper integration with read/write stream engines.

**Stimulus**: Issue `cu_cmd_t` command, verify:
1. Wrapper issues read requests from `operand_desc[]`
2. Wrapper forwards operand beats to `gbp_compute_core`
3. Wrapper converts `gbp_core_rsp_t` to `writeback_record_t`
4. Wrapper asserts `done_valid_o` with `cu_done_t`

**Pass criteria**:
- [ ] Read requests match `operand_desc[]`
- [ ] Operand beats forwarded correctly
- [ ] Writeback record has correct `nwords`, `kind`, `payload`
- [ ] `cu_done_t` has correct `node_id`, `factor_id`, `op`, `success`, `fail`

---

## 5. Golden Reference

For each test case, compute expected results using Python:

```python
import numpy as np

def compact_to_matrix(eta, L_packed, dim):
    """Convert compact form to full matrix."""
    Lambda = np.zeros((dim, dim))
    idx = 0
    for i in range(dim):
        for j in range(i, dim):
            Lambda[i, j] = L_packed[idx]
            if i != j:
                Lambda[j, i] = L_packed[idx]
            idx += 1
    return eta, Lambda

def solve(Lambda, eta):
    """Solve Lambda * mu = eta."""
    return np.linalg.solve(Lambda, eta)

def msg_update(eta_i, L_ii, L_io, eta_o, L_oo, belief_eta_o, belief_L_o,
               old_msg_eta, old_msg_L, damping):
    """Factor-to-variable message update."""
    # Cavity
    A_cav = L_oo + belief_L_o - old_msg_L
    b_cav = eta_o + belief_eta_o - old_msg_eta
    
    # Solve
    X_L = np.linalg.solve(A_cav, L_io.T)
    X_e = np.linalg.solve(A_cav, b_cav)
    
    # Schur complement
    L_raw = L_ii - L_io @ X_L
    eta_raw = eta_i - L_io @ X_e
    
    # Damping
    eta_new = (1 - damping) * eta_raw + damping * old_msg_eta
    L_new = (1 - damping) * L_raw + damping * old_msg_L
    
    return eta_new, L_new
```

---

## 6. Timing Diagram

```
OP_MSG_F2V flow:
           ___     ___     ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___|   |___|   |___
              ________
cmd       ___|        |_________________________________________________
                      ________    ________    ________    ________
operand   _________|  STATIC  |__| CAV_F  |__| CAV_B  |__| CAV_O  |____
                                                      ________________
rsp       _______________________________________|     result     |________
                                                              ________
done      ___________________________________________________|        |____
```

---

## 7. Pass/Fail Criteria Summary

### OP_MSG_F2V
- [ ] Command accepted when `cmd_valid && cmd_ready`
- [ ] All 4 operand streams consumed in correct order
- [ ] Cavity computation correct
- [ ] LDLT solve correct
- [ ] Schur complement correct
- [ ] Damping applied correctly
- [ ] `rsp_valid_o` asserts after computation
- [ ] Numerical result matches golden reference

### OP_BELIEF
- [ ] `belief_operand_unpacker` correctly unpacks prior and messages
- [ ] `packed_accumulator` accumulates correct number of messages
- [ ] `belief_solve_adapter` creates correct solve request
- [ ] `ldlt_solve_core` produces correct mu
- [ ] `belief_result_builder` computes residual
- [ ] `degree_mismatch` detected when msg_count != degree

### Error handling
- [ ] Pivot failure reported (fail=1, min_pivot)
- [ ] Stream error reported (stream_error=1, fail=1)
- [ ] NaN guard works

### Backpressure
- [ ] Response channel backpressure handled
- [ ] No deadlock under sustained backpressure

---

## 8. Corner Cases

1. **Reset during compute**: Verify clean state after reset
2. **Maximum DOF**: dim=6, largest matrix operations
3. **Zero degree**: `OP_BELIEF` with degree=0 (no messages, just prior)
4. **Single message**: `OP_BELIEF` with degree=1
5. **Large degree**: `OP_BELIEF` with degree=20
6. **BA direction=1**: Verify transpose handled by operand stream producer
7. **Diagonal loading**: Verify `diag_lambda` applied when `regularize_en=1`
8. **Pivot eps boundary**: Verify fail triggers at exact boundary

---

## 9. Related Documents

| Document | Content |
|----------|---------|
| `../../08_NEW_COMPUTE_UNIT.md` | v0.7 compute core spec |
| `../../04_PE_MICROARCHITECTURE.md` §2.9 | Compute Unit architecture |
| `../../05_INTERFACES.md` §2.10-2.13 | Compute Unit, Stream Engines, AGU ports |
| `../../06_PE_CONTROL_FLOW.md` §3.5 | Compute stage flow |
| `../subsystem_tests/01_compute_subsystem.md` | Compute subsystem integration test |
| `../README.md` | Verification documentation index |
