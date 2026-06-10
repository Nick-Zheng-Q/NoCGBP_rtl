# GBP Algorithm Golden Reference Test

> **Status**: 🔴 **REQUIRED** — document complete; Python golden reference model complete. **RTL testbench implementation is blocked by missing factor node compute path.**
>
> **Priority**: A (must pass for algorithmic correctness).

## 1. Test Objective

Verify that GBP PE hardware produces **numerically correct belief values** matching a floating-point golden-reference implementation within acceptable tolerance.

This test bridges "PE hardware works" (Directions A/B) to "PE computes correct GBP results". It is the final gate before declaring the GBP PE algorithmically correct.

Specifically validates:
1. **Variable node belief computation**: `belief_eta = prior_eta + Σ(msg_eta)`, `belief_lam = prior_lam + Σ(msg_lam)`, `mu = inv(lam) * eta`.
2. **Factor node message computation**: Schur-complement-based message extraction with damping.
3. **Message passing schedule**: Round-robin scheduling order produces convergent beliefs.
4. **FP32 numerical fidelity**: Hardware FP32 results match software FP32 reference (not double-precision) to within rounding-error tolerance.

---

## 2. Golden Reference Model

### 2.1 Reference Implementation

Language: Python 3 + NumPy. All computations use **FP32** (`numpy.float32`) to match hardware precision.

```python
import numpy as np

def update_variable_belief(prior_eta, prior_lam, messages_eta, messages_lam):
    """Variable node: accumulate messages into belief."""
    belief_eta = prior_eta + np.sum(messages_eta, axis=0)
    belief_lam = prior_lam + np.sum(messages_lam, axis=0)
    sigma = np.linalg.inv(belief_lam)      # FP32 inverse
    mu = sigma @ belief_eta                # FP32 matmul
    return mu, sigma, belief_eta, belief_lam

def compute_factor_messages(factor_eta, factor_lam, beliefs_eta, beliefs_lam,
                            old_messages_eta, old_messages_lam, damping):
    """Factor node: Schur-complement message extraction with damping."""
    # 1. Assemble joint precision matrix and information vector
    #    (layout depends on factor type; see Section 2.2)
    # 2. For each adjacent variable:
    #    - Compute cavity: joint excluding current variable
    #    - Extract Schur complement → new message (eta_msg, lam_msg)
    #    - Apply damping:
    #        eta_damped = damping * eta_old + (1-damping) * eta_new
    #        lam_damped = damping * lam_old + (1-damping) * lam_new
    # 3. Return damped messages
    ...
```

### 2.2 Algorithm Parameters (match RTL defaults)

| Parameter | Value | RTL Source |
|-----------|-------|------------|
| Damping factor | `0.3` (FP32 `0x3E99999A`) | `compute_unit.sv`, `gbp_compute_engine` port |
| Max iterations | `10` | Convergence check in testbench |
| Convergence threshold | `|delta_mu| < 1e-4` per DOF | Test-defined |
| Message format | Information form (`eta`, `lam`) | `gbp_compute_engine` spec |

### 2.3 Data Layout (Compact Payload)

For a node with `DOF = d`:

| Word Offset | Content | Count |
|-------------|---------|-------|
| 0 .. d-1 | `eta` (FP32 vector) | d |
| d .. d + d*(d+1)/2 - 1 | `lam` upper-triangular (FP32, row-major) | d(d+1)/2 |
| **Total** | — | **d + d(d+1)/2** |

Example: `d = 2` → 2 eta + 3 lam = **5 words**.

For `d = 6` → 6 eta + 21 lam = **27 words**.

---

## 3. Test Graph Structures

### 3.1 Case 1: 4-Node Chain (Sanity Check)

```
F0 ──► V1 ──► F2 ──► V3
```

- **F0** (factor, DOF=2, node_id=0): connects to V1
- **V1** (variable, DOF=2, node_id=1): connects to F0 and F2
- **F2** (factor, DOF=2, node_id=2): connects to V1 and V3
- **V3** (variable, DOF=2, node_id=3): connects to F2

**Purpose**: Linear chain is hand-verifiable. One forward pass (F0→V1→F2→V3) and one backward pass (V3→F2→V1→F0) should converge in 2 iterations.

**SPM Initialization (per node, 5 words each)**:
```
F0 prior: eta=[0.0, 0.0], lam=I₂  (identity)
V1 prior: eta=[1.0, 2.0], lam=I₂
F2 prior: eta=[0.0, 0.0], lam=I₂
V3 prior: eta=[3.0, 4.0], lam=I₂
```

**Golden reference expected output (after 2 iterations)**:
```
V1 belief_mu ≈ [0.5, 1.0]   (average of F0 and V1 priors)
V3 belief_mu ≈ [1.5, 2.0]   (average of F2 and V3 priors)
```

---

### 3.2 Case 2: 4-Node Mesh (Same as Direction B Topology)

Identical to `01_mesh_2x2_gbp_interconnect.md` §2.1:

```
    N0(factor) ──► N1(variable)
         │
         └────────► N3(variable)
    
    N2(factor) ──► N3(variable)
```

- N0 (factor, DOF=2) on PE(0,0)
- N1 (variable, DOF=2) on PE(1,0)
- N2 (factor, DOF=2) on PE(0,1)
- N3 (variable, DOF=2) on PE(1,1)

**Purpose**: Validates that beliefs computed across the mesh (with real NoC transfers) match the golden reference. This is the **integration point** between Direction B (functional) and Direction C (numerical).

**Execution**: Run Direction B mesh simulation, then read final STATE regions from all 4 PEs and compare against Python reference.

---

### 3.3 Case 3: Single Node Self-Loop (Degenerate Sanity)

```
F0 ──► V0   (both on same PE)
```

- **F0** (factor, DOF=2): connects only to V0
- **V0** (variable, DOF=2): connects only to F0

**Purpose**: Minimal graph. After one factor→variable message and one variable→factor message, beliefs should equal the priors (no external information).

**Expected**: `belief_mu == prior_mu`, `belief_lam == prior_lam`.

---

## 4. Data Preparation

### 4.1 From Graph to SPM Words

For each node, translate the Python `eta` (length-d vector) and `lam` (d×d matrix) into the compact payload word sequence:

```python
def encode_node_state(eta, lam):
    """Encode (eta, lam) into SPM words matching RTL layout."""
    d = len(eta)
    words = []
    # eta vector
    for i in range(d):
        words.append(float_to_hex32(eta[i]))
    # lam upper-triangular, row-major
    for i in range(d):
        for j in range(i, d):
            words.append(float_to_hex32(lam[i, j]))
    return words
```

### 4.2 Concrete Hex Values (Case 1 — F0)

```
eta = [0.0f, 0.0f]        → 0x00000000, 0x00000000
lam = [[1.0f, 0.0f],
       [0.0f, 1.0f]]     → 0x3F800000, 0x00000000, 0x3F800000

F0 STATE words @ SPM[0x200]:
  0x200: 0x00000000   # eta[0]
  0x201: 0x00000000   # eta[1]
  0x202: 0x3F800000   # lam[0,0]
  0x203: 0x00000000   # lam[0,1]
  0x204: 0x3F800000   # lam[1,1]
```

### 4.3 Metadata Header

Each NodeHeader follows `02_SPM_AND_METADATA.md`:

```
NodeHeader:
  [9:0]   node_id
  [13:10] dof
  [17:14] adj_count
  [35:18] adj_words
  [53:36] state_base
  [59:54] state_words
```

For Case 1 F0 (`dof=2`, `adj_count=1`, `state_words=5`):
```
NodeHeader[0] = {state_words=5, state_base=0x200, adj_words=4,
                  adj_count=1, dof=2, node_id=0}
```

---

## 5. Simulation Procedure

### 5.1 Single-PE Mode (Case 1 & 3)

Use Direction A testbench (`gbp_pe_top.sv`) with `GBP_WHITEBOX_TEST`:

| Step | Action |
|------|--------|
| 1 | Initialize SPM via backdoor (NodeHeaders, AdjEntries, STATE words). |
| 2 | Release `rst_n` (active-low, i.e. `reset_i` → 0). |
| 3 | **Iteration loop** (repeat up to 10 times): |
| 3a | For each factor node: inject whitebox cmd (`is_factor=1`). Wait for `done_valid_o`. |
| 3b | For each variable node: inject whitebox cmd (`is_factor=0`). Wait for `done_valid_o`. |
| 3c | Read updated STATE region via backdoor. |
| 3d | Compare against golden reference. If converged, break. |
| 4 | Final comparison: all beliefs within tolerance. |

### 5.2 Mesh Mode (Case 2)

Use Direction B testbench (`mesh_2x2_gbp_top.sv`):

| Step | Action |
|------|--------|
| 1 | Initialize SPM in all 4 PEs via backdoor. |
| 2 | Release `rst_n` on all PEs (active-low). |
| 3 | Wait for self-scheduling convergence (no external stimulus needed after init). |
| 4 | Read final STATE regions from all 4 PEs. |
| 5 | Compare against Python golden reference running the same graph. |

> **Note**: In mesh mode, PEs self-schedule via phase_controller. The convergence iteration count may differ from the single-PE reference by ±1 due to scheduling order variations. Only belief values are compared, not iteration count.

---

## 6. Numerical Comparison Method

### 6.1 Tolerances

| Check | Formula | Tolerance | Rationale |
|-------|---------|-----------|-----------|
| Absolute error | `|hw_mu[i] - ref_mu[i]|` | `< 1e-4` | FP32 rounding across ~20 operations |
| Relative error | `|hw - ref| / |ref|` | `< 1e-3` | For non-zero values > 1e-3 |
| Lam diagonal | `|hw_lam[i,i] - ref_lam[i,i]|` | `< 1e-3` | Precision matrix sensitive to rounding |

### 6.2 Special Values

- **Zero**: If `|ref| < 1e-6`, use absolute tolerance only (relative tolerance diverges).
- **Denormal / NaN / Inf**: Any non-finite hardware value is an immediate failure, regardless of reference.
- **Negative eigenvalue in `lam`**: If hardware produces a `lam` matrix with negative eigenvalue, the belief is invalid (precision matrix must be SPD). This is a **failure** even if the reference also has numerical issues.

### 6.3 Message Comparison (optional debug)

For deep debugging, intermediate messages can be compared. However, because the RTL and reference may use different matrix inversion algorithms (Cholesky vs. LU vs. direct inverse), intermediate messages may diverge more than final beliefs. **Final beliefs are the authoritative pass/fail metric**.

---

## 7. Pass/Fail Criteria

### 7.1 Mandatory

- [ ] All final `mu` (belief mean) vectors within absolute + relative tolerance.
- [ ] All final `lam` (belief precision) diagonal entries within tolerance.
- [ ] No NaN / Inf / denormal values in hardware output.
- [ ] All `lam` matrices are symmetric positive definite (eigenvalues > 0).

### 7.2 Optional (informational)

- [ ] Convergence iteration count within ±2 of reference.
- [ ] Intermediate messages within 10× tolerance (looser check for debugging).
- [ ] Damping visibly slows convergence when `damping=0.9` vs `damping=0.0`.

---

## 8. Corner Cases

1. **Zero-degree node**: Node with `adj_count=0`. Belief should equal prior (no messages to accumulate). Verify `mu == prior_mu`.
2. **Very large DOF (d=6)**: Tests AGU capacity and accumulator vector length. All 27 words per message must transfer correctly.
3. **Damping extremes**:
   - `damping=0.0`: No damping, fastest convergence, most oscillation-prone.
   - `damping=0.9`: Heavy damping, slow convergence, most stable.
   - Both must converge for the same graph.
4. **Ill-conditioned precision matrix**: Prior `lam` with condition number > 1e4. Tests numerical stability of matrix inversion. May require higher tolerance or be marked as "expected numerical difficulty".
5. **Identical priors**: All nodes have same prior. Beliefs should remain equal after convergence (symmetry check).
6. **Message overwrite race**: If a new NOTIFICATION arrives while a previous fetch for the same edge is still in flight, verify scoreboard handles deduplication or serialization correctly.

---

## 9. Relationship to Directions A & B

| Direction | Validates | This Test Adds |
|-----------|-----------|----------------|
| **A** (`gbp_pe` whitebox) | Single-PE pipeline works end-to-end | Numerical correctness of the arithmetic inside that pipeline |
| **B** (2×2 mesh) | Multi-PE NoC routing and functional handshake | Belief values computed across the mesh match mathematical expectation |
| **C** (this document) | — | Golden-reference numerical validation, the final algorithmic correctness gate |

**Execution order**:
1. Direction A passes → single PE pipeline is functionally correct.
2. Direction B passes → mesh routing and multi-PE handshake work.
3. Direction C passes → the arithmetic inside Compute Unit (`gbp_compute_engine`) produces correct GBP beliefs.

---

## 10. Implementation Roadmap

| Step | Deliverable | Notes |
|------|-------------|-------|
| 1 | `golden_ref/gbp_reference.py` | Python model: `update_variable_belief()`, `compute_factor_messages()` |
| 2 | `golden_ref/test_cases.py` | Case 1/2/3 graph definitions + expected outputs |
| 3 | `nocbp_verilator/tests/system/gbp_golden_top.cc` | C++ testbench that reads SPM backdoor and calls Python via `system()` or embeds expected values |
| 4 | Integration with Direction B | After mesh simulation, auto-extract STATE and run `gbp_reference.py` for comparison |

---

## 11. Related Documents

| Document | Content |
|----------|---------|
| `../../01_ARCHITECTURE.md` | GBP design goals and data flow |
| `../../02_SPM_AND_METADATA.md` | SPM layout, NodeHeader/AdjEntry formats |
| `../../05_INTERFACES.md` | Compute Unit interface and message payload layout |
| `../../06_PE_CONTROL_FLOW.md` | PE-level scheduling and phase switching |
| `../unit_tests/13_gbp_pe.md` | Direction A: single-PE top-level whitebox test |
| `01_mesh_2x2_gbp_interconnect.md` | Direction B: 2×2 mesh functional test |
| `../../compute/README.md` | `gbp_compute_engine` internal architecture and data formats |
| `../../CONTROL_COMPUTE_ABSTRACTION_DRAFT.md` | Exact GBP math (variable update, factor message, damping) |
