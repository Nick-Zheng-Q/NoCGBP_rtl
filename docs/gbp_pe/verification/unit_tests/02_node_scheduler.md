# Node Scheduler Unit Test

## 1. Test Objective

Verify that the Node Scheduler correctly:
- Selects nodes based on the current phase (factor/variable)
- Applies scheduling policies (round-robin, dirty-age, etc.)
- Reports when no schedulable nodes are available


---

## 2. Preconditions

- Module: `node_scheduler`
- Parameters: `NUM_NODES = 1024`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: No nodes ready


---

## 3. Test Stimulus

### 3.1 Test Case 1: Round-Robin Scheduling

**Scenario**: Multiple nodes ready in factor phase, verify round-robin selection.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | phase_factor_first | 1 | Factor phase |
| T+1   | node_ready | 0x000F | Nodes 0-3 ready |
| T+1   | visited_mask | 0x0000 | No nodes visited |
| T+2   | node_ready | 0x000F | Still ready |
| T+2   | visited_mask | 0x0001 | Node 0 visited |
| T+3   | node_ready | 0x000F | Still ready |
| T+3   | visited_mask | 0x0003 | Nodes 0-1 visited |

### 3.2 Test Case 2: Phase Switch

**Scenario**: No factor nodes ready, should report `no_schedulable_nodes`.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | phase_factor_first | 1 | Factor phase |
| T+1   | node_ready | 0xF000 | Only variable nodes ready |
| T+1   | visited_mask | 0x0000 | No nodes visited |


---

## 4. Expected Output

### 4.1 Test Case 1: Round-Robin Scheduling

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | sched_valid | 1 | Node selected |
| T+1   | sched_node_id | 0 | Node 0 selected |
| T+1   | sched_is_factor | 1 | Factor node |
| T+1   | no_schedulable_nodes | 0 | Nodes available |
| T+2   | sched_valid | 1 | Node selected |
| T+2   | sched_node_id | 1 | Node 1 selected |
| T+3   | sched_valid | 1 | Node selected |
| T+3   | sched_node_id | 2 | Node 2 selected |

### 4.2 Test Case 2: Phase Switch

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | sched_valid | 0 | No factor nodes selected |
| T+1   | no_schedulable_nodes | 1 | No schedulable nodes |


---

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___
         _____
rst_n        |___________________________________
              ________    ________    ________
ready     ___|0x000F |__|0x000F |__|0x000F |____
              ________    ________    ________
valid     ___|        |__|        |__|        |__
              ________    ________    ________
node_id   XXXX|  0   |XX|  1   |XX|  2   |XXXXXX
```


---

## 6. Pass/Fail Criteria

- [ ] Round-robin selects nodes in order
- [ ] `no_schedulable_nodes` asserted when no nodes ready
- [ ] `sched_is_factor` matches current phase
- [ ] `visited_mask` updated correctly after each selection
- [ ] Node ID within valid range (0 to NUM_NODES-1)


---

## 7. Corner Cases

1. **All nodes ready**: Verify round-robin wraps around
2. **No nodes ready**: Verify `no_schedulable_nodes` asserted
3. **Single node ready**: Verify correct selection
4. **Phase change during selection**: Verify clean transition

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
