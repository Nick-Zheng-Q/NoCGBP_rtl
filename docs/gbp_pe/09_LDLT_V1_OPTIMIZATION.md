# LDLT Solve Core V1 Optimization

## 1. Overview

V0 is a correctness-first sequential implementation: one FP operation at a time, generic N×N loop, no pipelining. V0 dim=6 nrhs=7 takes ~1000 cycles.

V1 targets **200-400 cycles** through four incremental optimizations.

## 2. Current V0 Performance Analysis

### 2.1 Cycle breakdown (dim=6, nrhs=7)

From TC4 trace:

| Phase | Cycles | States |
|-------|--------|--------|
| Factorization (S1) | ~537 | ST_FACTOR_D_*, ST_FACTOR_L_* |
| Forward solve (S2) | ~234 | ST_FORWARD_* |
| Diagonal scale (S3) | ~140 | ST_DIAG_* |
| Backward solve (S4) | ~103 | ST_BACKWARD_* |
| **Total LDLT** | **~914** | |

### 2.2 Operation counts

| Operation | Count | Cycles/op (V0) | Total cycles |
|-----------|-------|----------------|--------------|
| Division (pivot) | 6 | ~15 | ~90 |
| Division (L update) | 15 | ~15 | ~225 |
| Division (forward RHS) | 42 | ~15 | ~630 |
| Division (backward RHS) | 42 | ~15 | ~630 |
| Multiply | ~200 | ~3 | ~600 |
| Add/Sub | ~200 | ~3 | ~600 |

**Total divisions: ~105** (dominant bottleneck, ~15 cycles each)

### 2.3 Key bottlenecks

1. **Division latency**: Each `/ D[k]` takes ~15 cycles. 105 divisions = ~1575 cycles of pure division time.
2. **Sequential RHS**: Forward/backward solve processes RHS columns one at a time (nrhs=7 → 7x overhead).
3. **Generic loop overhead**: FSM iterates through k, i, j loops with per-iteration control overhead.
4. **No FP pipeline utilization**: Each FP op waits for completion before issuing next.

## 3. V1 Optimization Plan

### 3.1 Priority 1: Reciprocal Reuse

**Problem**: Division by `D[k]` is repeated many times for the same `k`.

**Solution**: Compute `invD[k] = 1 / D[k]` once per pivot, then use multiply.

**Before (V0)**:
```
// Factorization
D[k] = A[k][k] - sum
L[i][k] = numerator / D[k]          // division per (i,k)

// Forward solve
y[k][r] = (b[k][r] - sum) / D[k]    // division per (k,r)

// Backward solve
x[k][r] = (z[k][r] - sum) / D[k]    // division per (k,r)
```

**After (V1)**:
```
// Factorization
D[k] = A[k][k] - sum
invD[k] = recip(D[k])                // 1 reciprocal per k
L[i][k] = numerator * invD[k]        // multiply per (i,k)

// Forward solve
y[k][r] = (b[k][r] - sum) * invD[k] // multiply per (k,r)

// Backward solve
x[k][r] = (z[k][r] - sum) * invD[k] // multiply per (k,r)
```

**Impact**:

| | V0 divisions | V1 reciprocals | V1 multiplies |
|---|---|---|---|
| Factorization | 21 | 6 | 15 |
| Forward | 42 | 0 | 42 |
| Backward | 42 | 0 | 42 |
| **Total** | **105** | **6** | **99** |

Division (~15 cycles) → Reciprocal (~15 cycles) + Multiply (~3 cycles)

Estimated saving: ~1000 cycles → ~300-400 cycles.

### 3.2 Priority 2: RHS-Parallel Triangular Solve

**Problem**: Forward/backward solve iterates over RHS columns sequentially:
```
for r in 0..nrhs-1:    // nrhs = 7 for dim=6
  for i in 0..d-1:
    y[i][r] = b[i][r] - sum(L[i][j] * y[j][r])
```

**Solution**: Process all RHS columns in parallel:
```
for i in 0..d-1:
  for r in 0..nrhs-1:    // all RHS in parallel
    y[i][r] = b[i][r] - sum(L[i][j] * y[j][r])
```

**Implementation**: Use vector FMA lanes. For nrhs=7, pack 7 RHS values into a vector register and process simultaneously.

**Impact**: Forward/backward solve latency reduced by ~7x (from ~234+140=374 cycles to ~55+20=75 cycles).

### 3.3 Priority 3: Fixed Dim Microcode

**Problem**: Generic FSM with `for k, for i, for j` loops has per-iteration overhead (counter increment, branch, state transition).

**Solution**: Use fixed schedules for dim=1, 3, 6. Eliminate loop counters and branches.

**Example for dim=6 factorization**:
```
// k=0
RECIP  D0
MUL    L10 = num * invD0
MUL    L20 = num * invD0
MUL    L30 = num * invD0
MUL    L40 = num * invD0
MUL    L50 = num * invD0

// k=1
RECIP  D1
MUL    L21 = num * invD1
MUL    L31 = num * invD1
MUL    L41 = num * invD1
MUL    L51 = num * invD1

// ... etc
```

**Implementation**: Micro-op ROM with fixed instruction sequence. Each instruction specifies operation type, source registers, destination register.

**Impact**: Eliminates ~2-3 cycles per iteration of loop overhead. For ~71 iterations, saves ~150-200 cycles.

### 3.4 Priority 4: FMA Pipeline (II=1)

**Problem**: Current implementation waits for each FP operation to complete before issuing the next:
```
MUL a, b → wait 3 cycles → result ready
ADD c, d → wait 3 cycles → result ready
```

**Solution**: Use pipelined FMA with Initiation Interval (II=1):
```
MUL a, b → issue (cycle 0)
ADD c, d → issue (cycle 1)  // no wait, pipeline accepts new op
MUL e, f → issue (cycle 2)
...
```

**Implementation**: Add pipeline hazard detection. If result of op N is needed by op N+1, stall 1 cycle. Otherwise, issue continuously.

**Impact**: For independent operations, throughput becomes 1 op/cycle instead of 1 op/3-5 cycles.

## 4. Estimated Performance

| Optimization | Estimated cycles | Speedup |
|-------------|-----------------|---------|
| V0 baseline | ~1000 | 1x |
| + Reciprocal reuse | ~300-400 | 2.5-3x |
| + RHS parallel | ~100-150 | 7-10x |
| + Fixed microcode | ~80-120 | 8-12x |
| + FMA pipeline | ~60-80 | 12-17x |

**V1 target: 200-400 cycles** (Priority 1 + 2 only).

## 5. Implementation Strategy

### 5.1 Combined Phase 1+2

Implement reciprocal reuse and RHS parallel together in one pass:

1. Add 6 `bsg_fpu_mul` units (for RHS parallel multiply)
2. Add 6 `bsg_fpu_add_sub` units (for RHS parallel add/sub)
3. Modify FSM to use reciprocal reuse
4. Modify forward/backward solve to process all RHS in parallel

### 5.2 Hardware changes

New units (total: 7 add + 7 mul + 1 div):

```systemverilog
// Existing units
bsg_fpu_add_sub u_add      // add/sub unit 0
bsg_fpu_mul     u_mul      // mul unit 0
divSqrtFN       u_div      // div/recip unit

// New units for RHS parallel
bsg_fpu_add_sub u_add_vec [6]  // add/sub units 1-6
bsg_fpu_mul     u_mul_vec [6]  // mul units 1-6
```

### 5.3 Verification

- Reuse existing TC1-TC4 tests (same numerical results)
- Add cycle count regression: assert LDLT cycles < threshold
- Compare against golden model (ldlt_golden.hpp)

## 6. RTL Changes Required

### 6.1 New signals

```systemverilog
fp32_t invD [GBP_MAX_VAR_DIM];  // reciprocal of D[k]
logic  invD_valid;               // reciprocal result ready

// Vector RHS registers
fp32_t B_vec [GBP_MAX_VAR_DIM] [GBP_MAX_RHS];  // all RHS columns
fp32_t Y_vec [GBP_MAX_VAR_DIM] [GBP_MAX_RHS];  // forward solve result
fp32_t Z_vec [GBP_MAX_VAR_DIM] [GBP_MAX_RHS];  // diagonal scale result
fp32_t X_vec [GBP_MAX_VAR_DIM] [GBP_MAX_RHS];  // backward solve result

// Vector FPU control
logic [6:0] mul_v_i_vec, mul_ready_vec, mul_v_o_vec;
logic [6:0] add_v_i_vec, add_ready_vec, add_v_o_vec;
```

### 6.2 Modified states

- `ST_FACTOR_D_FINISH`: Store `invD[k] = 1/D[k]` instead of just `D[k]`
- `ST_FACTOR_L_*`: Use `invD[k]` instead of `/ D[k]`
- `ST_FORWARD_*`: Process all RHS columns in parallel using vector units
- `ST_BACKWARD_*`: Process all RHS columns in parallel using vector units

### 6.3 New reciprocal unit

Reuse existing HardFloat `divSqrtRecFN` in reciprocal mode, or add dedicated `bsg_fpu_recip` module.

## 7. Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Numerical precision | Compare against golden model with tolerance |
| Reciprocal accuracy | Use HardFloat reciprocal (IEEE 754 compliant) |
| Area increase | 6 add + 6 mul units are small (~10% of core area) |
| Timing closure | Pipeline vector FPU units if needed |
