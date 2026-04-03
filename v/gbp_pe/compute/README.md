# GBP Compute Engine RTL Implementation

## Overview

This directory contains the RTL implementation of the GBP (Gaussian Belief Propagation) compute engine, designed to replace the existing simple `compute_unit` with full GBP computation capabilities.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    gbp_compute_engine                        │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  SIMD Array  │  │   Matrix     │  │   GBP        │      │
│  │  (16 lanes)  │  │   FSM        │  │   Control    │      │
│  │              │  │              │  │   FSM        │      │
│  │ • FP Add/Sub │  │ • MatAdd     │  │ • Var Node   │      │
│  │ • FP Mul/Div │  │ • MatMul     │  │ • Factor Node│      │
│  │ • MAC        │  │ • MatInv     │  │ • Sequencing │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                 │                 │              │
│  ┌──────┴─────────────────┴─────────────────┴──────┐       │
│  │              Staging Buffer (128x32b)           │       │
│  └─────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────┘
```

## Module Descriptions

### 1. staging_buffer.sv
- **Purpose**: Data staging area between SPM and compute units
- **Features**:
  - 128-entry x 32-bit register file
  - Supports 8-float (256-bit) stream transfers
  - Parallel SIMD access (16 independent read ports)
  - Per-lane write enables

### 2. simd_array.sv
- **Purpose**: SIMD FP arithmetic array
- **Features**:
  - 16 parallel FP lanes
  - Operations: ADD, SUB, MUL, DIV, MAC (multiply-accumulate)
  - Flexible data source selection (buffer_a, buffer_b, accumulator, constant)
  - Accumulator support for reduction operations
  - Based on HardFloat library for IEEE 754 compliance

### 3. matrix_fsm.sv
- **Purpose**: Matrix operation state machine
- **Supported Operations**:
  - MatAdd/MatSub: Element-wise matrix addition/subtraction
  - MatMul: Matrix multiplication C = A × B
  - MatInv: Matrix inversion (via Gauss-Jordan)
  - MatVecMul: Matrix-vector multiplication

### 4. gbp_control_fsm.sv
- **Purpose**: GBP node computation orchestration
- **Variable Node Path**:
  1. Load prior + inbound messages
  2. Accumulate: belief = prior + Σ(messages)
  3. Invert: sigma = inverse(lam)
  4. Multiply: mu = sigma × eta
  5. Store result
  
- **Factor Node Path** (Simplified):
  1. Load factor + beliefs + old messages
  2. For each adjacent variable:
     - Compute cavity (excluding current variable)
     - Extract Schur complement blocks
     - Invert lnono
     - Compute new message via Schur complement
     - Apply damping
     - Store message

### 5. gbp_compute_engine.sv
- **Purpose**: Top-level integration module
- **Interface**: Compatible with existing compute_unit interface
- **Parameters**:
  - LANES: SIMD width (default 16)
  - MAX_DOFS: Maximum dimension (default 6)
  - MAX_ADJACENT: Max adjacent nodes (default 8)
  - STAGING_DEPTH: Buffer depth (default 128)

## Data Formats

### Message Payload
| dofs | eta (floats) | lam (floats) | Total |
|------|-------------|--------------|-------|
| 2    | 2           | 3 (upper-tri)| 5     |
| 3    | 3           | 6 (upper-tri)| 9     |
| 6    | 6           | 21 (upper-tri)| 27   |

### Staging Buffer Layout (dofs=6 example)
```
Address 0-5:   prior_eta (6 floats)
Address 6-41:  prior_lam (36 floats, full matrix)
Address 42-...: messages (27 floats each)
```

## Usage

### Integration with Existing System

Replace the existing `compute_unit` instantiation in `pe_top.sv`:

```systemverilog
// Old:
compute_unit #(...) u_compute_unit (...);

// New:
gbp_compute_engine #(
  .LANES(16),
  .MAX_DOFS(6),
  .MAX_ADJACENT(8),
  .STAGING_DEPTH(128)
) u_compute_engine (
  .clk_i(clk_i),
  .reset_i(reset_i),
  .cmd_valid_i(cmd_valid),
  .cmd_is_factor_i(cmd_is_factor),  // New: 0=variable, 1=factor
  .cmd_node_idx_i(cmd_node_idx),
  .cmd_dofs_i(cmd_dofs),            // New: 2, 3, or 6
  .cmd_adj_count_i(cmd_adj_count),  // New: number of adjacent nodes
  .cmd_ready_o(cmd_ready),
  .compute_done_o(compute_done),
  .rsp_done_o(rsp_done),
  .stream_in_ready(...),
  .stream_in_valid(...),
  .stream_in_data(...),
  .stream_out_ready(...),
  .stream_out_valid(...),
  .stream_out_data(...),
  .damping_factor_i(32'h3E99999A)   // 0.3 in FP32
);
```

### Testing

```bash
# Run lint check
cd v/gbp_pe/compute
make lint

# Build unit test (requires verilator)
make test
```

## Performance Estimates

### Variable Node (dofs=6, 4 adjacent)
| Operation | Cycles |
|-----------|--------|
| Load data | ~10    |
| Accumulate| ~11    |
| Invert    | ~100   |
| MVMul     | ~3     |
| Store     | ~5     |
| **Total** | **~130** |

### Factor Node (binary, dofs=6+6)
| Operation | Cycles (per adjacent) |
|-----------|----------------------|
| Cavity    | ~6                   |
| Invert    | ~100                 |
| Schur     | ~12                  |
| Damping   | ~1                   |
| **Total** | **~280** (for 2 adjacents) |

## Implementation Status

### Completed
- [x] staging_buffer: Data staging with parallel access
- [x] simd_array: SIMD FP array with accumulator
- [x] matrix_fsm: Basic matrix operations
- [x] gbp_control_fsm: Variable node path (partial)
- [x] gbp_compute_engine: Top-level integration

### TODO
- [ ] Complete Factor Node compute_messages path
- [ ] Implement full Schur complement computation
- [ ] Add robustify/relinearization support
- [ ] Integrate with existing control_unit
- [ ] Unit tests for each module
- [ ] Integration tests with reference model
- [ ] Performance optimization

## Notes

1. **Matrix Inversion**: Currently using Gauss-Jordan elimination. Cholesky decomposition may be added later for SPD matrices.

2. **Factor Node Complexity**: The full factor node computation requires multiple matrix operations per adjacent variable. Current implementation is simplified.

3. **Damping**: Configurable via `damping_factor_i` input (FP32 value, e.g., 0.3-0.5).

4. **Data Movement**: Heavy data movement between staging buffer and SPM. Consider DMA optimizations in future revisions.
