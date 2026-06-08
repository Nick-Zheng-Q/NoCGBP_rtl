# Writeback Controller Unit Test

## 1. Test Objective

Verify that the Writeback Controller correctly:
- Receives compute completion from Compute Unit
- Reads adjacency information from latched Metadata Scanner data
- Sends NOTIFICATION to all remote consuming neighbors
- Triggers Scoreboard reset for completed node
- Signals completion to Phase Controller


---

## 2. Preconditions

- Module: `writeback_controller`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE
- Adjacency info latched from Metadata Scanner


---

## 3. Test Stimulus

### 3.1 Test Case 1: Node with 2 Remote Neighbors

**Scenario**: Compute complete for node with 2 remote neighbors.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | done_valid | 1 | Compute complete |
| T+1   | done_node_id | 0x10 | Node ID |
| T+1   | done_is_factor | 0 | Variable node |
| T+1   | adj_count | 2 | 2 neighbors |
| T+1   | adj_neighbor_ids[0] | 0x20 | Neighbor 0 ID |
| T+1   | adj_neighbor_pes[0] | 0x02 | Neighbor 0 PE |
| T+1   | adj_is_local[0] | 0 | Remote |
| T+1   | adj_neighbor_ids[1] | 0x30 | Neighbor 1 ID |
| T+1   | adj_neighbor_pes[1] | 0x03 | Neighbor 1 PE |
| T+1   | adj_is_local[1] | 0 | Remote |
| T+2   | done_valid | 0 | Clear |

### 3.2 Test Case 2: Mixed Local and Remote Neighbors

**Scenario**: Node with 1 local and 1 remote neighbor.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | done_valid | 1 | Compute complete |
| T+1   | done_node_id | 0x10 | Node ID |
| T+1   | adj_count | 2 | 2 neighbors |
| T+1   | adj_neighbor_ids[0] | 0x20 | Neighbor 0 ID |
| T+1   | adj_neighbor_pes[0] | 0x01 | Neighbor 0 PE (self) |
| T+1   | adj_is_local[0] | 1 | Local |
| T+1   | adj_neighbor_ids[1] | 0x30 | Neighbor 1 ID |
| T+1   | adj_neighbor_pes[1] | 0x03 | Neighbor 1 PE |
| T+1   | adj_is_local[1] | 0 | Remote |
| T+2   | done_valid | 0 | Clear |

### 3.3 Test Case 3: Backpressure from NoC Adapter

**Scenario**: NoC Adapter busy, notification delayed.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | done_valid | 1 | Compute complete |
| T+2   | tx_notif_ready | 0 | NoC Adapter not ready |
| T+3   | tx_notif_ready | 0 | Still not ready |
| T+4   | tx_notif_ready | 1 | Ready again |


---

## 4. Expected Output

### 4.1 Test Case 1: Node with 2 Remote Neighbors

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | reset_valid | 1 | Reset edges |
| T+1   | reset_node_id | 0x10 | Node ID |
| T+1   | reset_is_factor | 0 | Variable |
| T+2   | tx_notif_valid | 1 | First notification |
| T+2   | tx_notif_source_node_id | 0x10 | Source node |
| T+2   | tx_notif_target_node_id | 0x20 | Target neighbor 0 |
| T+2   | tx_notif_is_factor | 0 | Variable |
| T+2   | tx_notif_target_x | 0x02 | Target X coord 0 |
| T+2   | tx_notif_target_y | 0x00 | Target Y coord 0 |
| T+3   | tx_notif_valid | 1 | Second notification |
| T+3   | tx_notif_source_node_id | 0x10 | Source node |
| T+3   | tx_notif_target_node_id | 0x30 | Target neighbor 1 |
| T+3   | tx_notif_is_factor | 0 | Variable |
| T+3   | tx_notif_target_x | 0x03 | Target X coord 1 |
| T+3   | tx_notif_target_y | 0x00 | Target Y coord 1 |
| T+4   | tx_notif_valid | 0 | Complete |
| T+4   | wb_done | 1 | Done signal |

### 4.2 Test Case 2: Mixed Local and Remote Neighbors

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | reset_valid | 1 | Reset edges |
| T+2   | tx_notif_valid | 1 | Notification (remote only) |
| T+2   | tx_notif_target_node_id | 0x30 | Remote neighbor |
| T+2   | tx_notif_target_x | 0x03 | Remote X coord |
| T+2   | tx_notif_target_y | 0x00 | Remote Y coord |
| T+3   | tx_notif_valid | 0 | No more notifications |
| T+3   | wb_done | 1 | Done signal |

### 4.3 Test Case 3: Backpressure from NoC Adapter

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | reset_valid | 1 | Reset edges |
| T+2   | tx_notif_valid | 1 | Try to send |
| T+3   | tx_notif_valid | 1 | Still trying |
| T+3   | tx_notif_source_node_id | 0x10 | Data held stable |
| T+4   | tx_notif_valid | 1 | Still trying |
| T+5   | tx_notif_valid | 1 | Can send now |
| T+6   | wb_done | 1 | Done signal |


---

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___
              ________
done      ___|        |_____________________________________
              ________
reset     ____|        |_____________________________________
                  ________    ________
tx_notif  _______|        |__|        |______________________
                                      ________
wb_done   ___________________________|        |______________
```


---

## 6. Pass/Fail Criteria

- [ ] `reset_valid` asserted within 1 cycle of `done_valid`
- [ ] NOTIFICATION sent to each remote neighbor
- [ ] Local neighbors skipped (no notification)
- [ ] `tx_notif_target_x` and `tx_notif_target_y` correct for each neighbor
- [ ] `wb_done` asserted after all notifications sent
- [ ] Backpressure: notifications held when `tx_notif_ready = 0`
- [ ] No notifications sent for local neighbors


---

## 7. Corner Cases

1. **All local neighbors**: No notifications sent
2. **All remote neighbors**: Notifications to all
3. **Maximum adj_count**: Node with MAX_ADJ_COUNT neighbors
4. **Reset during notification**: Verify clean state after reset
5. **Back-to-back completions**: Multiple nodes complete same cycle

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
