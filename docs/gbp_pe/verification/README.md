# GBP PE Verification Documentation

## 1. Overview

This directory contains verification documents for the GBP PE design. Verification is divided into five levels:

1. **Unit Tests**: Test individual modules in isolation
2. **Subsystem Tests**: Test medium-sized groups of connected modules (compute, memory, fetch, control, NoC)
3. **Integration Tests**: Test end-to-end data/control flows across multiple subsystems
4. **System Tests**: Test the complete PE top-level

## 2. Directory Structure

```
verification/
├── README.md                    (this file)
├── unit_tests/
│   ├── 01_phase_controller.md
│   ├── 02_node_scheduler.md
│   ├── 03_metadata_scanner.md
│   ├── 04_scoreboard_prefetcher.md
│   ├── 05_pull_client.md
│   ├── 06_pull_server.md
│   ├── 07_response_collector.md
│   ├── 08_neighbor_state_accumulator.md
│   ├── 09_compute_unit.md
│   ├── 10_writeback_controller.md
│   ├── 11_spm_arbiter.md
│   ├── 12_noc_adapter.md
│   ├── 13_gbp_pe.md                 (Partial: RTL complete, test TBD)
│   ├── 14_pe_top.md                 (DEPRECATED: replaced by gbp_pe)
│   ├── 15_spm_subsystem.md          (DEPRECATED: replaced by memory_subsystem)
│   ├── 16_spm_bank.md
│   ├── 17_spm_bank_array.md
│   ├── 18_gbp_pe_noc_bridge.md      (DEPRECATED)
│   ├── 19_gbp_pe_endpoint_adapter.md (DEPRECATED)
│   ├── 20_read_stream_engine.md
│   ├── 21_write_stream_engine.md
│   └── 22_agu.md
│
├── subsystem_tests/
│   ├── 01_compute_subsystem.md      (CU + RSE + WSE + AGU)
│   ├── 02_memory_subsystem.md       (SPM Arbiter + Banks + Stream Engines)
│   ├── 03_fetch_subsystem.md        (Scoreboard + Pull Client + Response Collector)
│   ├── 04_control_subsystem.md      (Phase Controller + Node Scheduler + Metadata Scanner)
│   └── 05_noc_subsystem.md          (NoC Adapter + Pull Server + Writeback Controller)
│
├── integration_tests/
│   ├── 01_notification_flow.md
│   ├── 02_fetch_request_flow.md
│   ├── 03_fetch_response_flow.md
│   ├── 04_full_pull_cycle.md
│   ├── 05_phase_scheduling.md
│   └── 06_multi_node_concurrent.md
│
└── system_tests/
    ├── 01_mesh_2x2_gbp_interconnect.md   (Direction B: 2×2 manycore mesh, 4 GBP PEs)
    └── 02_gbp_algorithm_golden_reference.md (Direction C: numerical correctness vs Python reference)
```

### Status

#### Unit Tests (Core Modules)

| Document | Status | Notes |
|----------|--------|-------|
| 01_phase_controller.md | ✅ Complete | 3 tests PASS |
| 02_node_scheduler.md | ✅ Complete | 3 tests PASS |
| 03_metadata_scanner.md | ✅ Complete | 1 test PASS |
| 04_scoreboard_prefetcher.md | ✅ Complete | 3 tests PASS |
| 05_pull_client.md | ✅ Complete | 2 tests PASS |
| 06_pull_server.md | ✅ Complete | 2 tests PASS |
| 07_response_collector.md | ✅ Complete | 2 tests PASS |
| 08_neighbor_state_accumulator.md | ✅ Complete | 4 tests PASS |
| 09_compute_unit.md | ✅ **Complete** | 1 test PASS |
| 10_writeback_controller.md | ✅ Complete | 3 tests PASS |
| 11_spm_arbiter.md | ✅ **Complete** | 1 test PASS |
| 12_noc_adapter.md | ✅ Complete | 4 tests PASS |

#### Unit Tests (Infrastructure)

| Document | Status | Notes |
|----------|--------|-------|
| 13_gbp_pe.md | 🟡 **Planned** | Direction A detailed; RTL testbench (`gbp_pe_top.sv` + `gbp_pe.cc`) TBD |
| 14_pe_top.md | ❌ **DEPRECATED** | `pe_top.sv` deleted; replaced by `gbp_pe.sv` |
| 15_spm_subsystem.md | ❌ **DEPRECATED** | `spm_subsystem.sv` deleted; replaced by `gbp_pe_memory_subsystem.sv` |
| 16_spm_bank.md | ✅ Complete | Updated for BEAT_BITS=64 |
| 17_spm_bank_array.md | ✅ Complete | |
| 18_gbp_pe_noc_bridge.md | ❌ DEPRECATED | Replaced by noc_adapter |
| 19_gbp_pe_endpoint_adapter.md | ❌ DEPRECATED | Replaced by noc_adapter |
| 18_read_stream_engine.md | ✅ **Complete** | 1 test PASS |
| 19_write_stream_engine.md | ✅ **Complete** | 1 test PASS |
| 20_agu.md | ✅ Complete | 3 tests PASS |
| 21_gbp_compute_engine_test.md | ✅ Complete | 27 tests PASS |

#### Subsystem Tests

| Document | Status | Implemented / Total |
|----------|--------|---------------------|
| 01_compute_subsystem.md | 🟡 **Partial** | 2 / ~8 (missing factor, multi-DOF, msg_count>1) |
| 02_memory_subsystem.md | 🟡 **Partial** | 4 / ~10 (missing write-then-read, zero-wstrb, all-clients) |
| 03_fetch_subsystem.md | 🟡 **Partial** | 3 / ~10 (missing response full path, dedup, scoreboard-full) |
| 04_control_subsystem.md | 🟡 **Partial** | 3 / ~8 (missing multi-edge, adj_last, local-state-reader) |
| 05_noc_subsystem.md | ⚠️ TODO | No standalone wrapper; tested via noc_adapter + pull_server + writeback_controller unit tests |

#### Integration Tests

| Document | Status |
|----------|--------|
| 01_notification_flow.md | ✅ Complete |
| 02_fetch_request_flow.md | ✅ Complete |
| 03_fetch_response_flow.md | ✅ Complete |
| 04_full_pull_cycle.md | ✅ Complete |
| 05_phase_scheduling.md | ✅ Complete |
| 06_multi_node_concurrent.md | ✅ Complete |

#### System Tests

| Document | Status |
|----------|--------|
| 01_mesh_2x2_gbp_interconnect.md | 🟡 **Planned** | Direction B framework complete; awaiting `mesh_2x2_gbp_top.sv` |
| 02_gbp_algorithm_golden_reference.md | 🟡 **Planned** | Direction C document complete; Python reference + RTL integration TBD |

## 3. Verification Hierarchy

```
Level 5: Chip Tests
    └── Full chip with manycore mesh + GBP PEs + host interface

Level 4: System Tests
    ├── Direction A: gbp_pe whitebox (single full PE end-to-end)
    │   └── Validates integrated pipeline: schedule → scan → fetch → compute → writeback
    ├── Direction B: mesh_2x2_gbp_interconnect (2×2 manycore mesh with 4 GBP PEs)
    │   └── Validates NoC routing, multi-PE concurrency, functional handshake
    └── Direction C: gbp_algorithm_golden_reference (numerical correctness)
        └── Validates belief values match Python FP32 reference within tolerance

> **Dependencies**: A → B → C. Single PE must work before mesh; mesh must work before numerical comparison.

Level 3: Integration Tests
    └── End-to-end flows (notification → fetch → response → compute → writeback)

Level 2: Subsystem Tests
    ├── Compute Subsystem (`gbp_pe_compute_subsystem` = compute_unit + read_stream_engine + write_stream_engine + agu)
    ├── Memory Subsystem (`gbp_pe_memory_subsystem` = spm_arbiter + spm_bank_array)
    ├── Fetch Subsystem (`gbp_pe_fetch_subsystem` = scoreboard_prefetcher + pull_client + response_collector)
    ├── Control Subsystem (`gbp_pe_control_subsystem` = phase_controller + node_scheduler + metadata_scanner)
    └── NoC Subsystem (noc_adapter + pull_server + writeback_controller)

Level 1: Unit Tests
    └── Individual leaf modules

**Relationship to RTL**: Each of the first four subsystem tests directly exercises one `gbp_pe_*_subsystem` wrapper. The NoC subsystem test exercises the NoC adapter and the two modules that talk directly to it (`pull_server`, `writeback_controller`).
```

**Why system tests?**

- `mesh_2x2_gbp_interconnect` is the first test that exercises **real NoC routing** between PEs.
- Single-PE tests use loopback or direct module connections. Mesh tests validate:
  - XY routing through manycore mesh nodes
  - Credit-based flow control across link boundaries
  - Concurrent bidirectional traffic (east-west + north-south)
  - Multi-hop latency (diagonal PE-to-PE)
- This is the gate between "PE works in isolation" and "PE works in the chip".

**Scope limitation**: Only **single-layer mesh** is required. No pods, no ruche links, no heterogeneous tiles (vanilla core + GBP accelerator). The test wrapper uses a homogeneous 2×2 tile array where every tile is a GBP PE.

**Why subsystems?**

- Compute Subsystem validates the descriptor-driven streaming datapath end-to-end without needing full SPM or NoC.
- Memory Subsystem validates bank arbitration and 64-bit beat integrity without compute logic.
- Fetch Subsystem validates the pull/response loop without compute or control overhead.
- Control Subsystem validates phase scheduling and metadata scanning without data movement.
- NoC Subsystem validates all NoC message types and TX arbitration.

## 4. Document Template

Each test document follows this structure:

```markdown
# [Module/Subsystem Name] Test

## 1. Test Objective
What is being verified.

## 2. Architecture (for subsystem tests)
ASCII block diagram showing modules and connections.

## 3. Preconditions
- Initial state of the module/system
- Required configurations
- Clock/reset conditions

## 4. Test Stimulus
Step-by-step stimulus sequence with timing:

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | ...    | ...   | ...         |

## 5. Expected Output
Expected signal values and state transitions:

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+0   | ...    | ...            | ...         |

## 6. Timing Diagram
ASCII or Mermaid timing diagram showing signal relationships.

## 7. Pass/Fail Criteria
- [ ] Criterion 1
- [ ] Criterion 2

## 8. Corner Cases
Edge cases to consider.

## 9. Related Documents
```

## 5. Verification Approach

### 5.1 Unit Tests

Each module is tested in isolation with:
- **Happy path**: Normal operation
- **Edge cases**: Boundary conditions
- **Error handling**: Invalid inputs (if applicable)
- **Backpressure**: Ready/valid handshake stress

### 5.2 Subsystem Tests

Multi-module tests verify:
- **Interface compatibility**: Modules connect correctly
- **Data flow**: Correct propagation through the subsystem
- **Control flow**: Correct handshake sequencing
- **Backpressure propagation**: Stall propagates correctly upstream
- **Concurrency**: Multiple outstanding transactions

### 5.3 Integration Tests

End-to-end tests verify:
- **Cross-subsystem flows**: Data moves correctly between subsystems
- **Timing**: Correct cycle-by-cycle behavior across boundaries
- **Resource sharing**: SPM Arbiter, NoC Adapter shared correctly

### 5.4 System Tests

Full PE tests verify:
- **Functional correctness**: End-to-end GBP algorithm execution
- **Performance**: Throughput and latency under load
- **Stability**: Long-running correctness

## 6. Related Documents

| Document | Content |
|----------|---------|
| `../01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `../02_SPM_AND_METADATA.md` | SPM layout, metadata structures |
| `../03_NOC_PROTOCOL.md` | NoC adaptation layer, mailbox encoding |
| `../04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters |
| `../05_INTERFACES.md` | Port-level interfaces, state machines |
| `../06_PE_CONTROL_FLOW.md` | PE-level control flow, pipeline stages, module handshakes |
