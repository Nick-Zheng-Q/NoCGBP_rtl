# Phase Controller Unit Test

## 1. Test Objective

Verify that the Phase Controller correctly alternates between factor and variable phases based on node availability.


---

## 2. Preconditions

- Module: `phase_controller`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: `FACTOR_PHASE`


---

## 3. Test Stimulus

### 3.1 Test Case 1: Normal Phase Switch

**Scenario**: Factor phase completes (no schedulable nodes), switch to variable phase.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | no_schedulable_nodes | 1 | No more factor nodes ready |
| T+2   | no_schedulable_nodes | 0 | Clear signal |
| T+3   | no_schedulable_nodes | 1 | No more variable nodes ready |
| T+4   | no_schedulable_nodes | 0 | Clear signal |

### 3.2 Test Case 2: Continuous Scheduling

**Scenario**: Multiple nodes schedulable in same phase, no switch until exhausted.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | sched_valid | 1 | Scheduler selected a node |
| T+2   | sched_valid | 1 | Another node selected |
| T+3   | sched_valid | 0 | No more nodes selected |
| T+4   | no_schedulable_nodes | 1 | Phase exhausted |


---

## 4. Expected Output

### 4.1 Test Case 1: Normal Phase Switch

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+0   | phase_factor_first | 1 | Factor phase |
| T+1   | phase_switch_pulse | 0 | No switch yet |
| T+2   | phase_switch_pulse | 1 | Switch pulse (1 cycle) |
| T+3   | phase_factor_first | 0 | Variable phase |
| T+4   | phase_switch_pulse | 1 | Switch pulse (1 cycle) |
| T+5   | phase_factor_first | 1 | Back to factor phase |

### 4.2 Test Case 2: Continuous Scheduling

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+0   | phase_factor_first | 1 | Factor phase |
| T+1   | phase_switch_pulse | 0 | No switch |
| T+2   | phase_switch_pulse | 0 | No switch |
| T+3   | phase_switch_pulse | 0 | No switch |
| T+4   | phase_switch_pulse | 0 | No switch |
| T+5   | phase_switch_pulse | 1 | Switch after exhaustion |


---

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___
         _____
rst_n         |_______________________________________________
              ________
no_sched     |        |______________________________________
                           ________
switch      _____________|        |__________________________
             ___________                                 _____
factor     |           |_________________________________|   
```


---

## 6. Pass/Fail Criteria

- [ ] Phase switches only when `no_schedulable_nodes` is asserted
- [ ] `phase_switch_pulse` is exactly 1 cycle wide
- [ ] `phase_factor_first` toggles on each switch
- [ ] No switch occurs while `sched_valid` is asserted


---

## 7. Corner Cases

1. **Reset during switch**: Verify clean reset to FACTOR_PHASE
2. **Immediate exhaustion**: Phase with 0 schedulable nodes at start
3. **Continuous back-to-back switches**: No gap between phases

---


---

## 8. Related Documents

| Document | Content |
|----------|---------|
| `../../00_WRITING_GUIDE.md` | How to write architecture documents |
| `../../01_ARCHITECTURE.md` | Design goals, core rules, overall data flow |
| `../../02_SPM_AND_METADATA.md` | SPM layout, metadata structures |
| `../../03_NOC_PROTOCOL.md` | NoC adaptation layer, mailbox encoding |
| `../../04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters |
| `../../05_INTERFACES.md` | Port-level interfaces, state machines |
| `../../06_PE_CONTROL_FLOW.md` | PE-level control flow, pipeline stages |
| `../README.md` | Verification documentation index |
