# Phase Scheduling Integration Test

## 1. Test Objective

Verify the phase-based scheduling across multiple modules:
1. Phase Controller manages factor/variable phase alternation
2. Node Scheduler selects nodes within current phase
3. ScoreboardPrefetcher reports node readiness
4. Metadata Scanner provides node information
5. Compute Unit executes node update

## 2. Preconditions

- System: Single PE with multiple nodes
- Nodes: 4 factor nodes (0-3), 4 variable nodes (4-7)
- Initial state: Factor phase, nodes 0 and 1 ready

## 3. Test Stimulus

### Phase 1: Factor Phase

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | phase_factor_first | 1 | Factor phase |
| T+1   | node_ready | 0x0003 | Nodes 0-1 ready |
| T+2   | sched_valid | 1 | Node 0 selected |
| T+2   | sched_node_id | 0 | Node 0 |
| T+3   | compute_done_valid | 1 | Node 0 complete |
| T+3   | compute_done_node_id | 0 | Node 0 |
| T+4   | sched_valid | 1 | Node 1 selected |
| T+4   | sched_node_id | 1 | Node 1 |
| T+5   | compute_done_valid | 1 | Node 1 complete |
| T+5   | compute_done_node_id | 1 | Node 1 |
| T+6   | no_schedulable_nodes | 1 | No more factor nodes |

### Phase 2: Variable Phase

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+7   | phase_factor_first | 0 | Variable phase |
| T+7   | node_ready | 0x00F0 | Nodes 4-7 ready |
| T+8   | sched_valid | 1 | Node 4 selected |
| T+8   | sched_node_id | 4 | Node 4 |
| T+9   | compute_done_valid | 1 | Node 4 complete |
| T+9   | compute_done_node_id | 4 | Node 4 |
| T+10  | sched_valid | 1 | Node 5 selected |
| T+10  | sched_node_id | 5 | Node 5 |
| T+11  | compute_done_valid | 1 | Node 5 complete |
| T+11  | compute_done_node_id | 5 | Node 5 |
| T+12  | no_schedulable_nodes | 1 | No more variable nodes |

### Phase 3: Back to Factor Phase

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+13  | phase_factor_first | 1 | Factor phase |
| T+13  | node_ready | 0x000C | Nodes 2-3 ready |
| T+14  | sched_valid | 1 | Node 2 selected |
| T+14  | sched_node_id | 2 | Node 2 |

## 4. Expected Output

### Phase 1: Factor Phase

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | phase_switch_pulse | 0 | No switch |
| T+2   | sched_is_factor | 1 | Factor node |
| T+3   | reset_valid | 1 | Reset edges |
| T+3   | reset_node_id | 0 | Node 0 |
| T+4   | sched_is_factor | 1 | Factor node |
| T+5   | reset_valid | 1 | Reset edges |
| T+5   | reset_node_id | 1 | Node 1 |
| T+6   | phase_switch_pulse | 1 | Switch to variable |
| T+7   | phase_factor_first | 0 | Variable phase |

### Phase 2: Variable Phase

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+7   | phase_switch_pulse | 0 | No switch |
| T+8   | sched_is_factor | 0 | Variable node |
| T+9   | reset_valid | 1 | Reset edges |
| T+9   | reset_node_id | 4 | Node 4 |
| T+10  | sched_is_factor | 0 | Variable node |
| T+11  | reset_valid | 1 | Reset edges |
| T+11  | reset_node_id | 5 | Node 5 |
| T+12  | phase_switch_pulse | 1 | Switch to factor |
| T+13  | phase_factor_first | 1 | Factor phase |

### Phase 3: Back to Factor Phase

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+13  | phase_switch_pulse | 0 | No switch |
| T+14  | sched_is_factor | 1 | Factor node |

## 5. Timing Diagram

```
           ___     ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___|   |___
         _____
rst_n        |___________________________________________________
              _______________________________________________
factor   ____|                                               |___
                                  ___________________________
var      _______________________|                               |___
              ________    ________                    ________
sched     ___|   0   |__|   1   |__________________|   4   |______
                      ________                    ________
done      _______|   0   |__________________|   4   |______________
                                  ________
switch    _______________________|        |________________________
```

## 6. Pass/Fail Criteria

- [ ] Phase alternates correctly (factor → variable → factor)
- [ ] `phase_switch_pulse` is 1 cycle wide
- [ ] Nodes selected only in correct phase
- [ ] `sched_is_factor` matches current phase
- [ ] `reset_valid` asserted after compute completion
- [ ] Round-robin within phase works correctly
- [ ] `no_schedulable_nodes` triggers phase switch

## 7. Corner Cases

1. **Empty phase**: Phase with 0 ready nodes
2. **Single node phase**: Phase with only 1 ready node
3. **Continuous phase**: No switch needed (always nodes ready)
4. **Reset during phase**: Verify clean recovery
5. **Simultaneous ready nodes**: Multiple nodes ready same cycle
