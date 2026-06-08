# Notification Flow Integration Test

## 1. Test Objective

Verify the notification flow from producer to consumer:
1. Producer completes node update
2. Writeback Controller sends NOTIFICATION via NoC Adapter
3. NoC Adapter forms `e_remote_store` packet
4. Consumer's NoC Adapter decodes incoming store
5. Consumer's ScoreboardPrefetcher records notification


---

## 2. Preconditions

- System: 2 PEs (PE_A at (0,0), PE_B at (1,0))
- Node N on PE_A, Node M on PE_B
- Edge (M, N): M is consumer, N is producer
- All modules reset and idle


---

## 3. Test Stimulus

### Test Case 1: Notification End-to-End

#### Phase 1: Producer Compute Complete (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | done_valid | 1 | Node N compute complete |
| T+1   | done_node_id | 0x10 | Node N ID |
| T+1   | done_is_factor | 0 | Variable node |
| T+2   | done_valid | 0 | Clear |

### Phase 2: Writeback Controller (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+2   | tx_notif_valid | 1 | NOTIFICATION request |
| T+2   | tx_notif_source_node_id | 0x10 | Producer node N |
| T+2   | tx_notif_is_factor | 0 | Variable |
| T+2   | tx_notif_target_x | 0x01 | Target X coord (PE_B) |
| T+2   | tx_notif_target_y | 0x00 | Target Y coord |
| T+3   | tx_notif_valid | 0 | Clear |

**Note**: `tx_notif_target_node_id` is provided for interface symmetry but is **not** encoded into the notification packet. The destination PE is selected by `(target_x, target_y)`. Consumer node matching is done by the receiving ScoreboardPrefetcher against registered edges.

### Phase 3: NoC Adapter TX (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+3   | out_v_i | 1 | Packet sent |
| T+3   | out_packet_i.addr | 0x1000 | MBX_NOTIFICATION |
| T+3   | out_packet_i.op | e_remote_store | Store operation |
| T+3   | out_packet_i.dst_x | 1 | PE_B X coord |
| T+3   | out_packet_i.dst_y | 0 | PE_B Y coord |

### Phase 4: NoC Latency

| Cycle | Description |
|-------|-------------|
| T+4..T+8 | NoC traversal (5 cycles) |

### Phase 5: NoC Adapter RX (PE_B)

| Cycle | PE_B Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+9   | in_v_o | 1 | Incoming store |
| T+9   | in_addr_o | 0x1000 | MBX_NOTIFICATION |
| T+9   | in_data_o | {0, 0x10} | {is_factor, source_node_id} |
| T+9   | in_src_x_cord_o | 0 | PE_A X coord |
| T+9   | in_src_y_cord_o | 0 | PE_A Y coord |

### Phase 6: ScoreboardPrefetcher (PE_B)

| Cycle | PE_B Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+10  | rx_notif_valid | 1 | Notification received |
| T+10  | rx_notif_source_node_id | 0x10 | Producer node N |
| T+10  | rx_notif_source_x | 0x00 | PE_A X coord |
| T+10  | rx_notif_source_y | 0x00 | PE_A Y coord |
| T+11  | rx_notif_valid | 0 | Clear |

**Note**: There is no `rx_notif_target_node_id` output from the NoC Adapter. The ScoreboardPrefetcher matches registered edges whose `source_node_id` equals the received `rx_notif_source_node_id`.


---

## 4. Expected Output

### Phase 1: Writeback Controller (PE_A)

| Cycle | PE_A Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+2   | tx_notif_ready | 1 | Ready to accept |

### Phase 2: NoC Adapter TX (PE_A)

| Cycle | PE_A Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+3   | tx_notif_ready | 1 | Accepted |
| T+3   | out_credit_or_ready_o | 1 | Credits available |

### Phase 3: NoC Adapter RX (PE_B)

| Cycle | PE_B Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+9   | rx_notif_valid | 1 | Notification routed |
| T+9   | rx_notif_source_node_id | 0x10 | Source node N |
| T+9   | rx_notif_source_x | 0x00 | PE_A X coord |
| T+9   | rx_notif_source_y | 0x00 | PE_A Y coord |
| T+9   | rx_notif_is_factor | 0 | Variable |

### Phase 4: ScoreboardPrefetcher (PE_B)

| Cycle | PE_B Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+11  | fetch_req_valid | 1 | Fetch request issued |
| T+11  | fetch_req_target_node_id | 0x10 | Target node N |
| T+11  | fetch_req_consumer_node_id | 0x20 | Consumer node M |
| T+12  | scoreboard_occupancy | 1 | Entry added |


---

## 5. Timing Diagram

```
PE_A (Producer)                    PE_B (Consumer)
    |                                    |
    |--[done_valid]-->           |
    |                                    |
    |--[tx_notif_valid]-->               |
    |                                    |
    |--[out_v_i]-->                      |
    |   (e_remote_store to 0x1000)       |
    |                                    |
    |        [NoC Latency: 5 cycles]     |
    |                                    |
    |                          [in_v_o]<-|
    |                          (decoded) |
    |                                    |
    |                     [rx_notif_valid]
    |                                    |
    |                     [Scoreboard: IDLE→NOTIFIED]
    |                                    |
    |                     [fetch_req_valid]
```


---

## 6. Pass/Fail Criteria

- [ ] NOTIFICATION sent within 1 cycle of compute completion
- [ ] NoC packet formed correctly with `e_remote_store` op
- [ ] Destination coordinates correct (PE_B at (1,0))
- [ ] Source coordinates included (PE_A at (0,0))
- [ ] NoC latency consistent (5 cycles)
- [ ] Incoming store decoded correctly by address
- [ ] Source coordinates extracted from incoming request
- [ ] Scoreboard edge transitions: IDLE → NOTIFIED
- [ ] Fetch request issued within 1 cycle of notification


---

### Test Case 2: Multiple Notifications

**Scenario**: Three producer nodes (0x10, 0x11, 0x12) on PE_A each have a remote edge to three consumer nodes (0x20, 0x21, 0x22) on PE_B. Trigger compute done sequentially and verify all three fetch requests are issued.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1    | Register 3 edges on PE_B (consumer→producer) | Edges stored in adjacency table |
| 2    | Trigger `done_valid` for producer 0x10 | Writeback sends NOTIFICATION to PE_B |
| 3    | Wait for `wb_done` | Producer 0x10 notification accepted |
| 4    | Repeat for producers 0x11 and 0x12 | Three notifications sent |
| 5    | Monitor `pe_b_fetch_req_valid` | Three fetch requests seen with matching (target, consumer) pairs |
| 6    | Final occupancy | `scoreboard_occupancy == 3` |

### Test Case 3: Scoreboard Occupancy Tracking

**Scenario**: Register one edge and trigger one notification. Verify scoreboard occupancy transitions from 0 → 1.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1    | Reset and register edge (M=0x20 → N=0x10) | Initial occupancy = 0 |
| 2    | Trigger `done_valid` for N=0x10 | Occupancy becomes 1 within 100 cycles |


## 7. Corner Cases

1. **NoC latency variation**: Test with different latencies (3-10 cycles)
2. **Multiple notifications**: Multiple producers notify same consumer — **implemented in Test 2**
3. **Scoreboard occupancy tracking** — **implemented in Test 3**
4. **Credit exhaustion**: NoC backpressures due to no credits
5. **Reset during NoC traversal**: Verify clean recovery
6. **Simultaneous notifications**: Multiple edges notified same cycle

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
