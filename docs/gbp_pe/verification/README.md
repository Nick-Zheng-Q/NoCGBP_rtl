# GBP PE Verification Documentation

## 1. Overview

This directory contains verification documents for the GBP PE design. Verification is divided into two levels:

1. **Unit Tests**: Test individual modules in isolation
2. **Integration Tests**: Test multiple modules working together

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
│   ├── 13_gbp_pe.md
│   ├── 14_pe_top.md
│   ├── 15_spm_subsystem.md
│   ├── 16_spm_bank.md
│   ├── 17_spm_bank_array.md
│   ├── 18_gbp_pe_noc_bridge.md
│   └── 19_gbp_pe_endpoint_adapter.md
│
└── integration_tests/
    ├── 01_notification_flow.md
    ├── 02_fetch_request_flow.md
    ├── 03_fetch_response_flow.md
    ├── 04_full_pull_cycle.md
    ├── 05_phase_scheduling.md
    └── 06_multi_node_concurrent.md
```

### Status

| Document | Status |
|----------|--------|
| **Unit Tests (Core Modules)** | |
| 01_phase_controller.md | ✅ Complete |
| 02_node_scheduler.md | ✅ Complete |
| 03_metadata_scanner.md | ✅ Complete |
| 04_scoreboard_prefetcher.md | ✅ Complete |
| 05_pull_client.md | ✅ Complete |
| 06_pull_server.md | ✅ Complete |
| 07_response_collector.md | ✅ Complete |
| 08_neighbor_state_accumulator.md | ✅ Complete |
| 09_compute_unit.md | ✅ Complete |
| 10_writeback_controller.md | ✅ Complete |
| 11_spm_arbiter.md | ✅ Complete |
| 12_noc_adapter.md | ✅ Complete |
| **Unit Tests (System Modules)** | |
| 13_gbp_pe.md | ✅ Complete |
| 14_pe_top.md | ✅ Complete |
| 15_spm_subsystem.md | ✅ Complete |
| 16_spm_bank.md | ✅ Complete |
| 17_spm_bank_array.md | ✅ Complete |
| 18_gbp_pe_noc_bridge.md | ✅ Complete |
| 19_gbp_pe_endpoint_adapter.md | ✅ Complete |
| **Integration Tests** | |
| 01_notification_flow.md | ✅ Complete |
| 02_fetch_request_flow.md | ✅ Complete |
| 03_fetch_response_flow.md | ✅ Complete |
| 04_full_pull_cycle.md | ✅ Complete |
| 05_phase_scheduling.md | ✅ Complete |
| 06_multi_node_concurrent.md | ✅ Complete |

## 3. Document Template

Each test document follows this structure:

```markdown
# [Module Name] Unit Test / [Test Name] Integration Test

## 1. Test Objective
What is being verified.

## 2. Preconditions
- Initial state of the module/system
- Required configurations
- Clock/reset conditions

## 3. Test Stimulus
Step-by-step stimulus sequence with timing:

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | ...    | ...   | ...         |

## 4. Expected Output
Expected signal values and state transitions:

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+0   | ...    | ...            | ...         |

## 5. Timing Diagram
ASCII or Mermaid timing diagram showing signal relationships.

## 6. Pass/Fail Criteria
- [ ] Criterion 1
- [ ] Criterion 2

## 7. Corner Cases
Edge cases to consider.
```

## 4. Verification Approach

### 4.1 Unit Tests

Each module is tested in isolation with:
- **Happy path**: Normal operation
- **Edge cases**: Boundary conditions
- **Error handling**: Invalid inputs (if applicable)
- **Backpressure**: Ready/valid handshake stress

### 4.2 Integration Tests

Multi-module tests verify:
- **Data flow**: Correct data propagation between modules
- **Control flow**: Correct handshake sequencing
- **Timing**: Correct cycle-by-cycle behavior
- **Concurrency**: Multiple outstanding transactions

## 5. Related Documents

| Document | Content |
|----------|---------|
| `../01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `../02_SPM_AND_METADATA.md` | SPM layout, metadata structures |
| `../03_NOC_PROTOCOL.md` | NoC adaptation layer, mailbox encoding |
| `../04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters |
| `../05_INTERFACES.md` | Port-level interfaces, state machines |
| `../06_PE_CONTROL_FLOW.md` | PE-level control flow, pipeline stages, module handshakes |
