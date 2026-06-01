# GBP IDU Instruction Set (Draft, 64-bit, SPM-only)

## Scope
This document defines a **64-bit** instruction set for `gbp_idu.sv` with a
**SPM-only** operand model. There is **no architectural register file**.
All operands and results live in ScratchPad Memory (SPM), and instruction
fields select **SPM block IDs**.

Key goals:
- Compute directly on SPM-resident data (no explicit load/store ISA).
- Use fixed-width 64-bit encoding with four 11-bit SPM operands.
- Support vector, reduction, small-matrix, solve, and GBP graph ops.

## 1. Encoding Overview (64-bit)

### 1.1 Base Format (SPM operand format)

```
63          58 57          52 51        41 40        30 29        19 18        8 7      0
+-------------+--------------+------------+------------+------------+------------+--------+
|   opcode    |     func     |    dst     |   srcA     |   srcB     |   srcC     |  imm8  |
+-------------+--------------+------------+------------+------------+------------+--------+
```

- `opcode` : 6-bit major class
- `func`   : 6-bit sub-op inside the class
- `dst`    : destination **SPM block ID** (11-bit)
- `srcA/B/C` : source **SPM block IDs** (11-bit each)
- `imm8`   : small flags/override field (optional; see 1.3)

### 1.2 Block Descriptor Model (replaces memory overlay)
Access patterns (length/stride/shape/layout) are **not encoded in the
instruction**. Each SPM block has a **descriptor** that supplies:

- length (element count)
- shape (M/N/K for matrices)
- layout (row/col major)
- stride/offset rules

The PE uses the descriptor associated with each block ID to execute the op.

### 1.3 Immediate Usage (imm8)
`imm8` provides small overrides or mode flags when needed, such as:

- `imm8` as a small stride override (if the block descriptor is generic)
- `imm8` as a tiny constant for scale or threshold operations
- `imm8` as a mode selector (e.g., alternate layout or rounding mode)

If not needed, `imm8` is ignored.

## 2. Operand Model (SPM-only)

### 2.1 SPM Block IDs
- `dst/srcA/srcB/srcC` select **SPM blocks**, not registers.
- Each block is a fixed-size, contiguous region in SPM.
- Each block has a descriptor used by the PE to interpret its contents.

**SPM address mapping (draft):**
```
spm_addr = block_id * BLOCK_STRIDE + block_offset
```

### 2.2 Access Modes (descriptor-driven)
Descriptors can specify one of the following access modes:

- **CONTIG**: contiguous block access
- **STREAM**: sequential access across multiple blocks
- **STRIDE**: fixed stride within a block
- **GATHER**: indexed access using an index vector block

The instruction does not carry these mode bits; they are inferred from the
descriptor tied to the block ID.

## 3. Instruction Classes (SPM semantics)

### 3.1 VEC Class (`opcode=0x00`)

All VEC ops operate over the length from the block descriptors.

| func | Mnemonic | Semantics (SPM) |
| --- | --- | --- |
| `0x00` | VADD  | `SPM[dst] = SPM[srcA] + SPM[srcB]` |
| `0x01` | VSUB  | `SPM[dst] = SPM[srcA] - SPM[srcB]` |
| `0x02` | VMUL  | `SPM[dst] = SPM[srcA] * SPM[srcB]` |
| `0x03` | VDIV  | `SPM[dst] = SPM[srcA] / SPM[srcB]` |
| `0x04` | VFMA  | `SPM[dst] = SPM[srcA] * SPM[srcB] + SPM[srcC]` |
| `0x05` | VABS  | `SPM[dst] = abs(SPM[srcA])` |
| `0x06` | VSQR  | `SPM[dst] = SPM[srcA] * SPM[srcA]` |

### 3.2 REDUCE Class (`opcode=0x01`)

Reduce ops write a single scalar (first element) into `dst`.

| func | Mnemonic | Semantics (SPM) |
| --- | --- | --- |
| `0x00` | RSUM   | `SPM[dst][0] = sum(SPM[srcA])` |
| `0x01` | RDOT   | `SPM[dst][0] = dot(SPM[srcA], SPM[srcB])` |
| `0x02` | RNORM2 | `SPM[dst][0] = sqrt(dot(SPM[srcA], SPM[srcA]))` |
| `0x03` | RMAX   | `SPM[dst][0] = max(SPM[srcA])` |
| `0x04` | RMIN   | `SPM[dst][0] = min(SPM[srcA])` |

### 3.3 MAT Class (`opcode=0x02`)

Matrix ops use descriptor-provided shapes (M/N/K) and layout.

| func | Mnemonic | Semantics (SPM) |
| --- | --- | --- |
| `0x00` | MMUL | `SPM[dst] = SPM[srcA](MxK) * SPM[srcB](KxN)` |
| `0x01` | MVEC | `SPM[dst] = SPM[srcA](MxN) * SPM[srcB](Nx1)` |
| `0x02` | MTR  | `SPM[dst] = transpose(SPM[srcA])` |
| `0x03` | OUTER| `SPM[dst] = SPM[srcA] * SPM[srcB]^T` |

### 3.4 SOLVE Class (`opcode=0x03`)

Solve ops read from SPM and write results to `dst`. In-place is allowed by
setting `dst == srcA`.

| func | Mnemonic | Semantics (SPM) |
| --- | --- | --- |
| `0x00` | CHOL  | `SPM[dst] = chol(SPM[srcA])` |
| `0x01` | LDLT  | `SPM[dst] = ldlt(SPM[srcA])` |
| `0x02` | TRIS_L| `SPM[dst] = solve(L=SPM[srcA], b=SPM[srcB])` |
| `0x03` | TRIS_U| `SPM[dst] = solve(U=SPM[srcA], b=SPM[srcB])` |
| `0x04` | INV   | `SPM[dst] = inverse(SPM[srcA])` |
| `0x05` | SCHUR | `SPM[dst] = A - B*C^-1*D` (A/B/C/D via srcA/srcB/srcC/imm8)` |

### 3.5 GBP Class (`opcode=0x04`)

GBP ops consume and produce **structured SPM blocks**. The exact in-block
layout is a contract between microcode and the compiler/scheduler.

| func | Mnemonic | Semantics (SPM) |
| --- | --- | --- |
| `0x00` | GBP_ROBUST | update noise using residuals; in/out in SPM blocks |
| `0x01` | GBP_RELIN  | update linpoint if needed; in/out in SPM blocks |
| `0x02` | GBP_FACT   | compute factor (J^T J, J^T r) into SPM |
| `0x03` | GBP_MSG    | compute messages (Schur complement) into SPM |
| `0x04` | GBP_BELIEF | update belief (prior + inbound messages) into SPM |
| `0x05` | GBP_PACK   | pack message block to NoC format buffer |
| `0x06` | GBP_UNPACK | unpack NoC format buffer to message block |

**GBP block convention (draft):**
- `srcA` = base of factor/belief structure
- `srcB` = base of message list or adjacency list
- `srcC` = metadata block (dims, ids, damping)
- `dst`  = output block

## 4. Notes and Known Constraints

- The PE relies on **SPM block descriptors** to provide sizes and access modes.
- `imm8` is optional; leave zero if not used.
- All operands are **SPM blocks**, not registers.

## 5. Next Steps

1) Define SPM block descriptor layout (length/shape/stride/layout).
2) Align `gbp_idu.sv` decode with 64-bit fields and 11-bit operands.
3) Decide which ops require `imm8` and document their usage.

---

## 6. Compute Function Analysis (SPM-based mapping)

This section summarizes core GBP compute functions and the SPM-based instruction
requirements. Micro-ops assume **SPM reads/writes** and only small local
accumulators for reductions.

### 6.1 `compute_factor`

Required instruction capabilities (SPM-based):
- Matrix inverse/factorization on SPM blocks
- Matrix-vector multiply on SPM blocks
- Matrix transpose and matrix-matrix multiply on SPM blocks
- Vector add/sub/scale on SPM blocks
- Optional quantize/round step

### 6.2 `robustify_loss`

Required instruction capabilities (SPM-based):
- Measurement-model eval on SPM vectors
- Residual norm (dot + sqrt) on SPM vectors
- Scalar divide and threshold compare
- Vector/matrix scale on SPM blocks

### 6.3 `compute_messages`

Required instruction capabilities (SPM-based):
- Sub-matrix block extraction from SPM
- Matrix inverse/factorization on SPM blocks
- Chained matrix-matrix multiply for Schur complement
- Vector arithmetic for damping

### 6.4 `update_belief`

Required instruction capabilities (SPM-based):
- Vector/matrix accumulation across SPM message blocks
- Matrix inverse/factorization
- Matrix-vector multiply on SPM blocks
