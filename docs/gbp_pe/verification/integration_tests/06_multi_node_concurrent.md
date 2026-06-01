# Multi-Node Concurrent Integration Test

## 1. Test Objective

Verify concurrent operation of multiple nodes:
1. Multiple nodes on same PE with different edges
2. Concurrent notifications and fetches
3. Scoreboard tracking multiple in-flight fetches
4. Phase scheduling with multiple ready nodes
5. Compute Unit processing nodes sequentially

## 2. Preconditions

- System: 3 PEs (PE_A at (0,0), PE_B at (1,0), PE_C at (0,1))
- Nodes on PE_A: N1 (factor), N2 (variable)
- Nodes on PE_B: M1 (variable, adjacent to N1)
- Nodes on PE_C: M2 (variable, adjacent to N2)
- All edges initially IDLE

## 3. Test Stimulus

### Phase 1: Multiple Notifications Arrive

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | rx_notif_valid | 1 | Notification for N1 |
| T+1   | rx_notif_source_node_id | 0x30 | Source node M1 |
| T+1   | rx_notif_target_node_id | 0x10 | Target node N1 |
| T+1   | rx_notif_source_pe | 0x01 | PE_B |
| T+2   | rx_notif_valid | 0 | Clear |
| T+3   | rx_notif_valid | 1 | Notification for N2 |
| T+3   | rx_notif_source_node_id | 0x40 | Source node M2 |
| T+3   | rx_notif_target_node_id | 0x20 | Target node N2 |
| T+3   | rx_notif_source_pe | 0x02 | PE_C |
| T+4   | rx_notif_valid | 0 | Clear |

### Phase 2: Concurrent Fetch Requests

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+5   | tx_fetch_req_valid | 1 | Fetch for N1 |
| T+5   | tx_fetch_req_target_node_id | 0x30 | Target M1 |
| T+5   | tx_fetch_req_consumer_node_id | 0x10 | Consumer N1 |
| T+5   | tx_fetch_req_target_pe | 0x01 | PE_B |
| T+6   | tx_fetch_req_valid | 1 | Fetch for N2 |
| T+6   | tx_fetch_req_target_node_id | 0x40 | Target M2 |
| T+6   | tx_fetch_req_consumer_node_id | 0x20 | Consumer N2 |
| T+6   | tx_fetch_req_target_pe | 0x02 | PE_C |
| T+7   | tx_fetch_req_valid | 0 | Clear |

### Phase 3: Responses Arrive (Different Timing)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+15  | rx_fetch_resp_done_valid | 1 | Response for N1 complete |
| T+15  | resp_producer_node_id | 0x30 | Producer M1 |
| T+15  | resp_consumer_node_id | 0x10 | Consumer N1 |
| T+16  | rx_fetch_resp_done_valid | 0 | Clear |
| T+20  | rx_fetch_resp_done_valid | 1 | Response for N2 complete |
| T+20  | resp_producer_node_id | 0x40 | Producer M2 |
| T+20  | resp_consumer_node_id | 0x20 | Consumer N2 |
| T+21  | rx_fetch_resp_done_valid | 0 | Clear |

### Phase 4: Phase Scheduling

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+16  | node_ready | 0x0001 | N1 ready (factor) |
| T+17  | sched_valid | 1 | N1 selected |
| T+17  | sched_node_id | 0x10 | N1 |
| T+17  | sched_is_factor | 1 | Factor |
| T+18  | compute_done_valid | 1 | N1 complete |
| T+18  | compute_done_node_id | 0x10 | N1 |
| T+21  | node_ready | 0x0002 | N2 ready (variable) |
| T+22  | phase_factor_first | 0 | Variable phase |
| T+23  | sched_valid | 1 | N2 selected |
| T+23  | sched_node_id | 0x20 | N2 |
| T+23  | sched_is_factor | 0 | Variable |

## 4. Expected Output

### Phase 1: Scoreboard State

| Cycle | PE_A Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+2   | scoreboard_occupancy | 0 | No entries (notified only) |
| T+4   | scoreboard_occupancy | 0 | No entries (notified only) |

### Phase 2: Fetch Requests Sent

| Cycle | PE_A Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+5   | scoreboard_occupancy | 1 | Entry for N1 |
| T+6   | scoreboard_occupancy | 2 | Entry for N2 |

### Phase 3: Node Readiness

| Cycle | PE_A Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+15  | node_ready[10] | 1 | N1 ready |
| T+15  | node_ready[20] | 0 | N2 not ready |
| T+16  | scoreboard_occupancy | 1 | N1 entry removed |
| T+20  | node_ready[20] | 1 | N2 ready |
| T+21  | scoreboard_occupancy | 0 | All entries removed |

### Phase 4: Scheduling

| Cycle | PE_A Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+17  | sched_is_factor | 1 | Factor node |
| T+18  | reset_valid | 1 | Reset N1 edges |
| T+18  | reset_node_id | 0x10 | N1 |
| T+23  | sched_is_factor | 0 | Variable node |
| T+24  | reset_valid | 1 | Reset N2 edges |
| T+24  | reset_node_id | 0x20 | N2 |

## 5. Timing Diagram

```
           ___     ___     ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___|   |___|   |___
              ________            ________
notif     ___|   N1  |__________|   N2  |________________________________
                  ________    ________
fetch     _______|   N1  |__|   N2  |____________________________________
                                      ________                ________
complete  ___________________________|   N1  |______________|   N2  |____
                                          ________                ________
ready     ___________________________|  0x01  |______________|  0x02  |____
                                              ________              ________
sched     ___________________________________|   N1  |____________|   N2  |____
```

## 6. Pass/Fail Criteria

- [ ] Multiple notifications handled correctly
- [ ] Scoreboard tracks multiple in-flight fetches
- [ ] Fetch requests issued concurrently
- [ ] Responses arrive and complete independently
- [ ] Node readiness updates correctly
- [ ] Phase scheduling respects node types
- [ ] Edges reset after compute completion
- [ ] No interference between concurrent operations

## 7. Corner Cases

1. **All edges ready same cycle**: Multiple nodes schedulable
2. **Response out-of-order**: N2 completes before N1
3. **Scoreboard full**: Many concurrent fetches
4. **Phase switch during fetch**: Verify clean transition
5. **Reset during concurrent operations**: Verify clean recovery
6. **Maximum concurrent nodes**: All nodes active
