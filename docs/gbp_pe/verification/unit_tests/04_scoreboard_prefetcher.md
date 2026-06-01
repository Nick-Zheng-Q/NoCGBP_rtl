# ScoreboardPrefetcher Unit Test

## 1. Test Objective

Verify that the ScoreboardPrefetcher correctly:
- Tracks edge states (IDLE → NOTIFIED → IN_FLIGHT → READY)
- Issues FETCH_REQUEST on notification
- Reports node readiness when all edges are ready
- Resets edges after compute

## 2. Preconditions

- Module: `scoreboard_prefetcher`
- Parameters: `SCOREBOARD_DEPTH = 64`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: All edges IDLE, scoreboard empty

## 3. Test Stimulus

### 3.1 Test Case 1: Single Edge Lifecycle

**Scenario**: One remote edge receives notification, fetch completes, node becomes ready.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | rx_notif_valid | 1 | Notification received |
| T+1   | rx_notif_source_node_id | 0x10 | Producer node ID |
| T+1   | rx_notif_target_node_id | 0x20 | Consumer node ID |
| T+1   | rx_notif_source_pe | 0x02 | Producer PE ID |
| T+2   | rx_notif_valid | 0 | Clear notification |
| T+3   | fetch_req_ready | 1 | Pull Client ready |
| T+5   | complete_valid | 1 | Fetch response complete |
| T+5   | resp_producer_node_id | 0x10 | Match producer |
| T+5   | resp_consumer_node_id | 0x20 | Match consumer |
| T+6   | complete_valid | 0 | Clear completion |

### 3.2 Test Case 2: Multiple Edges, Node Readiness

**Scenario**: Node with 2 remote edges, both must complete before node is ready.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | rx_notif_valid | 1 | Notification for edge 1 |
| T+1   | rx_notif_source_node_id | 0x10 | Producer 1 |
| T+1   | rx_notif_target_node_id | 0x20 | Consumer (same) |
| T+3   | rx_notif_valid | 1 | Notification for edge 2 |
| T+3   | rx_notif_source_node_id | 0x11 | Producer 2 |
| T+3   | rx_notif_target_node_id | 0x20 | Consumer (same) |
| T+5   | complete_valid | 1 | Edge 1 complete |
| T+5   | resp_producer_node_id | 0x10 | Match producer 1 |
| T+7   | complete_valid | 1 | Edge 2 complete |
| T+7   | resp_producer_node_id | 0x11 | Match producer 2 |

### 3.3 Test Case 3: Scoreboard Full

**Scenario**: Scoreboard reaches capacity, new fetches stalled.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1..T+64 | rx_notif_valid | 1 | Fill scoreboard |
| T+65  | rx_notif_valid | 1 | Notification when full |
| T+65  | scoreboard_full | 1 | Scoreboard at capacity |

## 4. Expected Output

### 4.1 Test Case 1: Single Edge Lifecycle

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | scoreboard_occupancy | 0 | No entries yet |
| T+2   | fetch_req_valid | 1 | Fetch request issued |
| T+2   | fetch_req_target_node_id | 0x10 | Correct target |
| T+2   | fetch_req_consumer_node_id | 0x20 | Correct consumer |
| T+2   | scoreboard_occupancy | 1 | One entry added |
| T+5   | scoreboard_occupancy | 0 | Entry removed |
| T+6   | node_ready[20] | 1 | Node 0x20 ready |

### 4.2 Test Case 2: Multiple Edges, Node Readiness

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+2   | fetch_req_valid | 1 | Fetch for edge 1 |
| T+4   | fetch_req_valid | 1 | Fetch for edge 2 |
| T+5   | node_ready[20] | 0 | Not ready (edge 2 pending) |
| T+6   | scoreboard_occupancy | 1 | One entry remaining |
| T+7   | scoreboard_occupancy | 0 | All entries cleared |
| T+8   | node_ready[20] | 1 | Node ready (all edges done) |

### 4.3 Test Case 3: Scoreboard Full

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+64  | scoreboard_occupancy | 64 | Full |
| T+65  | scoreboard_full | 1 | Full flag asserted |
| T+65  | fetch_req_valid | 0 | No new fetches issued |

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___
              ________
notif     ___|        |_____________________________________
                  ____
fetch     _______|    |_____________________________________
                                        ________
complete  _____________________________|        |___________
                                   ____
ready     ________________________|    |____________________
```

## 6. Pass/Fail Criteria

- [ ] Edge transitions: IDLE → NOTIFIED → IN_FLIGHT → READY → IDLE
- [ ] Fetch issued within 1 cycle of notification
- [ ] Node ready only when ALL edges are READY
- [ ] Scoreboard occupancy tracks correctly
- [ ] Scoreboard full blocks new fetches
- [ ] Edges reset after `reset_valid` assertion

## 7. Corner Cases

1. **Reset during in-flight**: Verify clean state after reset
2. **Duplicate notification**: Same edge notified while IN_FLIGHT
3. **Out-of-order responses**: Responses arrive in different order than requests
4. **Local edges**: Always READY, never leave READY state
