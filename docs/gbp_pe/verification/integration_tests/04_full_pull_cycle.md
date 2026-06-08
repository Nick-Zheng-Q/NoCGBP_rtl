# Full Pull Cycle Integration Test

## 1. Test Objective

Verify the complete GBP pull cycle across multiple modules:
1. Producer updates node → Writeback Controller sends NOTIFICATION
2. Consumer's ScoreboardPrefetcher receives NOTIFICATION → issues FETCH_REQUEST
3. Producer's Pull Server receives request → sends FETCH_RESPONSE
4. Consumer's Response Collector receives response → marks edge READY
5. Consumer's Node Scheduler selects node when all edges ready


---

## 2. Preconditions

- System: 2 PEs (PE_A at (0,0), PE_B at (1,0))
- Node M on PE_B, Node N on PE_A
- Edge (M, N): M is consumer, N is producer
- All modules reset and idle
- SPM on PE_A contains valid NodeHeader and STATE for node N


---

## 3. Test Stimulus

### Phase 1: Producer Update (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+0   | done_valid | 1 | Node N compute complete |
| T+0   | done_node_id | 0x10 | Node N ID |
| T+0   | done_is_factor | 0 | Variable node |
| T+1   | done_valid | 0 | Clear |

### Phase 2: Consumer Notification (PE_B)

| Cycle | PE_B Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+5   | rx_notif_valid | 1 | NOTIFICATION received (NoC latency) |
| T+5   | rx_notif_source_node_id | 0x10 | Producer node N |
| T+5   | rx_notif_source_x | 0x00 | PE_A X coord |
| T+5   | rx_notif_source_y | 0x00 | PE_A Y coord |
| T+6   | rx_notif_valid | 0 | Clear |

### Phase 3: Consumer Fetch Request (PE_B)

| Cycle | PE_B Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+7   | tx_fetch_req_valid | 1 | Fetch request issued |
| T+7   | tx_fetch_req_target_node_id | 0x10 | Target node N |
| T+7   | tx_fetch_req_consumer_node_id | 0x04 | Consumer node M |
| T+7   | tx_fetch_req_target_x | 0x00 | Target X coord |
| T+7   | tx_fetch_req_target_y | 0x00 | Target Y coord |
| T+8   | tx_fetch_req_valid | 0 | Clear |

### Phase 4: Producer Fetch Response (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+12  | rx_fetch_req_valid | 1 | Fetch request received (NoC latency) |
| T+12  | rx_fetch_req_target_node_id | 0x10 | Target node N |
| T+12  | rx_fetch_req_consumer_node_id | 0x04 | Consumer node M |
| T+13  | rx_fetch_req_valid | 0 | Clear |
| T+14  | spm_rd_valid | 1 | SPM read for NodeHeader |
| T+15  | spm_rd_data | HEADER | NodeHeader data |
| T+16  | spm_rd_valid | 1 | SPM read for STATE |
| T+17  | spm_rd_data | WORD_0 | First state word |
| T+18  | spm_rd_data | WORD_1 | Second state word |

### Phase 5: Consumer Response Collection (PE_B)

| Cycle | PE_B Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+25  | rx_fetch_resp_valid | 1 | Response metadata received (NoC latency) |
| T+25  | rx_fetch_resp_state_words | 2 | Two state words |
| T+26  | rx_fetch_resp_valid | 1 | Data word 0 |
| T+26  | rx_fetch_resp_data | WORD_0 | First state word |
| T+26  | rx_fetch_resp_data_valid | 1 | Data valid |
| T+27  | rx_fetch_resp_valid | 1 | Data word 1 |
| T+27  | rx_fetch_resp_data | WORD_1 | Second state word |
| T+27  | rx_fetch_resp_data_valid | 1 | Data valid |
| T+27  | rx_fetch_resp_last | 1 | Last data word |
| T+28  | rx_fetch_resp_done_valid | 1 | Done signal received |
| T+28  | rx_fetch_resp_txn_id | 0x03 | txn_id for edge matching |
| T+29  | rx_fetch_resp_done_valid | 0 | Clear |

### Phase 6: Node Scheduling (PE_B)

| Cycle | PE_B Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+30  | node_ready[4] | 1 | Node M ready (all edges done) |
| T+31  | sched_valid | 1 | Node M selected |
| T+31  | sched_node_id | 0x04 | Node M ID |
| T+32  | sched_valid | 0 | Clear |


---

## 4. Expected Output

### Phase 1: Producer Notification (PE_A)

| Cycle | PE_A Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+1   | tx_notif_valid | 1 | NOTIFICATION sent |
| T+1   | tx_notif_source_node_id | 0x10 | Producer node N |
| T+1   | tx_notif_target_node_id | 0x04 | Consumer node M |
| T+1   | tx_notif_target_x | 0x01 | Target X coord |
| T+1   | tx_notif_target_y | 0x00 | Target Y coord |

### Phase 2: Consumer Scoreboard Update (PE_B)

| Cycle | PE_B Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+6   | fetch_req_valid | 1 | Fetch request issued |
| T+6   | fetch_req_target_node_id | 0x10 | Target node N |
| T+6   | fetch_req_consumer_node_id | 0x04 | Consumer node M |
| T+7   | scoreboard_occupancy | 1 | One entry added |

### Phase 3: Producer Pull Server Response (PE_A)

| Cycle | PE_A Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+19  | tx_fetch_resp_valid | 1 | Response metadata sent |
| T+19  | tx_fetch_resp_node_id | 0x10 | Producer node N |
| T+19  | tx_fetch_resp_state_words | 2 | Two state words |
| T+20  | tx_fetch_resp_valid | 1 | Data word 0 sent |
| T+20  | tx_fetch_resp_data | WORD_0 | First state word |
| T+20  | tx_fetch_resp_data_valid | 1 | Data valid |
| T+21  | tx_fetch_resp_valid | 1 | Data word 1 sent |
| T+21  | tx_fetch_resp_data | WORD_1 | Second state word |
| T+21  | tx_fetch_resp_data_valid | 1 | Data valid |
| T+21  | tx_fetch_resp_last | 1 | Last data word |

### Phase 4: Consumer Completion (PE_B)

| Cycle | PE_B Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+28  | complete_valid | 1 | Completion signal |
| T+28  | complete_node_id | 0x10 | Producer node N |
| T+28  | complete_consumer_node_id | 0x04 | Consumer node M |
| T+29  | scoreboard_occupancy | 0 | Entry removed |
| T+30  | node_ready[4] | 1 | Node M ready |
| T+26  | remote_valid | 1 | Data fed to accumulator |
| T+26  | remote_data | WORD_0 | First state word |
| T+27  | remote_valid | 1 | More data |
| T+27  | remote_data | WORD_1 | Second state word |
| T+27  | remote_last | 1 | Last data word |


---

## 5. Timing Diagram

```
PE_A (Producer)                    PE_B (Consumer)
    |                                    |
    |--[done_valid]--->                |
    |                                    |
    |--[tx_notif]-------[NoC]--->[rx_notif]
    |                                    |
    |                  [Scoreboard: NOTIFIED]
    |                                    |
    |<---[NoC]----------[tx_fetch_req]---|
    |                                    |
    |  [Pull Server: LOOKUP→SEND]        |
    |                                    |
    |--[tx_fetch_resp]---[NoC]--->[rx_fetch_resp]
    |  (metadata)                        |
    |                                    |
    |--[tx_fetch_resp]---[NoC]--->[rx_fetch_resp]
    |  (data word 0)                     |
    |                                    |
    |--[tx_fetch_resp]---[NoC]--->[rx_fetch_resp]
    |  (data word 1, last)               |
    |                                    |
    |--[tx_fetch_resp]---[NoC]--->[rx_fetch_resp_done]
    |  (done)                            |
    |                                    |
    |                  [Scoreboard: READY]
    |                  [node_ready = 1]
    |                                    |
    |                  [Scheduler: select node M]
```


---

## 6. Pass/Fail Criteria

- [ ] NOTIFICATION sent within 1 cycle of compute completion
- [ ] Scoreboard edge transitions: IDLE → NOTIFIED → IN_FLIGHT → READY
- [ ] FETCH_REQUEST issued within 1 cycle of notification
- [ ] Pull Server response sequence: metadata → data[0] → ... → data[N-1] → done
- [ ] Response Collector receives all data words in order
- [ ] Node becomes ready after all edges complete
- [ ] Scheduler selects ready node
- [ ] Data flows correctly from SPM through Pull Server to Accumulator
- [ ] NoC latency is consistent (5 cycles in this test)


---

## 7. Corner Cases

1. **NoC latency variation**: Test with different latencies (3-10 cycles)
2. **Multiple concurrent edges**: Node with 4 remote edges
3. **Out-of-order responses**: Responses arrive in different order
4. **Scoreboard full during fetch**: Consumer has many outstanding fetches
5. **SPM read error**: Invalid data in SPM
6. **Reset during pull cycle**: Verify clean recovery

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
