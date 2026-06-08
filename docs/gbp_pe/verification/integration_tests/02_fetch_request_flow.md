# Fetch Request Flow Integration Test

## 1. Test Objective

Verify the fetch request flow across modules:
1. ScoreboardPrefetcher receives NOTIFICATION
2. ScoreboardPrefetcher issues FETCH_REQUEST
3. Pull Client forms 3-store sequence
4. NoC Adapter sends stores to producer PE
5. Producer's NoC Adapter decodes incoming stores
6. Producer's Pull Server receives assembled request


---

## 2. Preconditions

- System: 2 PEs (PE_A at (0,0), PE_B at (1,0))
- Node M on PE_B, Node N on PE_A
- Edge (M, N): M is consumer, N is producer
- NOTIFICATION already received (edge in NOTIFIED state)


---

## 3. Test Stimulus

### Phase 1: ScoreboardPrefetcher Issues Fetch (PE_B)

| Cycle | PE_B Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | fetch_req_valid | 1 | Fetch request |
| T+1   | fetch_req_target_node_id | 0x10 | Target node N |
| T+1   | fetch_req_consumer_node_id | 0x20 | Consumer node M |
| T+1   | fetch_req_is_factor | 0 | Variable |
| T+1   | fetch_req_target_x | 0x00 | Target X coord |
| T+1   | fetch_req_target_y | 0x00 | Target Y coord |
| T+1   | fetch_req_txn_id | 0x03 | Edge index |
| T+2   | fetch_req_valid | 0 | Clear |

### Phase 2: Pull Client Forms Stores (PE_B)

| Cycle | PE_B Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+2   | tx_fetch_req_valid | 1 | First store |
| T+2   | tx_fetch_req_target_node_id | 0x10 | Target node |
| T+2   | tx_fetch_req_consumer_node_id | 0x20 | Consumer node |
| T+2   | tx_fetch_req_is_factor | 0 | Variable |
| T+2   | tx_fetch_req_target_x | 0x00 | Target X coord |
| T+2   | tx_fetch_req_target_y | 0x00 | Target Y coord |
| T+2   | tx_fetch_req_txn_id | 0x03 | Edge index |
| T+3   | tx_fetch_req_valid | 1 | Second store |
| T+3   | tx_fetch_req_target_node_id | 0x10 | Target node |
| T+4   | tx_fetch_req_valid | 1 | Third store |
| T+4   | tx_fetch_req_txn_id | 0x03 | Txn ID |
| T+5   | tx_fetch_req_valid | 0 | Complete |

### Phase 3: NoC Adapter TX (PE_B)

| Cycle | PE_B Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+2   | out_v_i | 1 | First store packet |
| T+2   | out_packet_i.addr | 0x1004 | MBX_FETCH_REQ_0 |
| T+2   | out_packet_i.op | e_remote_store | Store |
| T+2   | out_packet_i.dst_x | 0 | PE_A X |
| T+2   | out_packet_i.dst_y | 0 | PE_A Y |
| T+3   | out_v_i | 1 | Second store packet |
| T+3   | out_packet_i.addr | 0x1008 | MBX_FETCH_REQ_1 |
| T+4   | out_v_i | 1 | Third store packet |
| T+4   | out_packet_i.addr | 0x100C | MBX_FETCH_REQ_2 |
| T+4   | out_packet_i.data | 0x03 | txn_id |

### Phase 4: NoC Latency

| Cycle | Description |
|-------|-------------|
| T+5..T+9 | NoC traversal (5 cycles) |

### Phase 5: NoC Adapter RX (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+10  | in_v_o | 1 | First store |
| T+10  | in_addr_o | 0x1004 | MBX_FETCH_REQ_0 |
| T+10  | in_data_o | {0, 0x20} | {is_factor, consumer_id} |
| T+10  | in_src_x_cord_o | 1 | PE_B X |
| T+10  | in_src_y_cord_o | 0 | PE_B Y |
| T+11  | in_v_o | 0 | Clear |
| T+12  | in_v_o | 1 | Second store |
| T+12  | in_addr_o | 0x1008 | MBX_FETCH_REQ_1 |
| T+12  | in_data_o | 0x10 | target_node_id |
| T+13  | in_v_o | 0 | Clear |
| T+14  | in_v_o | 1 | Third store |
| T+14  | in_addr_o | 0x100C | MBX_FETCH_REQ_2 |
| T+14  | in_data_o | 0x03 | txn_id |
| T+15  | in_v_o | 0 | Clear |

### Phase 6: Pull Server Receives Request (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+15  | rx_fetch_req_valid | 1 | Request assembled (all 3 stores) |
| T+15  | rx_fetch_req_target_node_id | 0x10 | Target node N |
| T+15  | rx_fetch_req_consumer_node_id | 0x20 | Consumer node M |
| T+15  | rx_fetch_req_is_factor | 0 | Variable |
| T+15  | rx_fetch_req_src_pe | 0x01 | PE_B coordinates |
| T+15  | rx_fetch_req_txn_id | 0x03 | Txn ID for response matching |
| T+16  | rx_fetch_req_valid | 0 | Clear |


---

## 4. Expected Output

### Phase 1: Pull Client (PE_B)

| Cycle | PE_B Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+1   | req_ready | 1 | Accept request |
| T+2   | tx_fetch_req_valid | 1 | First store |
| T+3   | tx_fetch_req_valid | 1 | Second store |
| T+4   | tx_fetch_req_valid | 1 | Third store |

### Phase 2: NoC Adapter TX (PE_B)

| Cycle | PE_B Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+2   | tx_fetch_req_ready | 1 | Ready |
| T+2   | out_credit_or_ready_o | 1 | Credits available |

### Phase 3: NoC Adapter RX (PE_A)

| Cycle | PE_A Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+10  | rx_fetch_req_valid | 0 | Not ready yet (need 3 stores) |
| T+12  | rx_fetch_req_valid | 0 | Still waiting (2 of 3) |
| T+15  | rx_fetch_req_valid | 1 | All 3 stores received |
| T+15  | rx_fetch_req_target_node_id | 0x10 | Target node |
| T+15  | rx_fetch_req_consumer_node_id | 0x20 | Consumer node |
| T+15  | rx_fetch_req_src_pe | 0x01 | PE_B coordinates |
| T+15  | rx_fetch_req_txn_id | 0x03 | Txn ID |

### Phase 4: ScoreboardPrefetcher (PE_B)

| Cycle | PE_B Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+2   | scoreboard_occupancy | 1 | Entry added |
| T+2   | node_ready[20] | 0 | Not ready (in-flight) |


---

## 5. Timing Diagram

```
PE_B (Consumer)                    PE_A (Producer)
    |                                    |
    |--[fetch_req_valid]-->              |
    |                                    |
    |--[tx_fetch_req_valid]-->           |
    |   (store 1: MBX_FETCH_REQ_0)     |
    |                                    |
    |--[tx_fetch_req_valid]-->           |
    |   (store 2: MBX_FETCH_REQ_1)     |
    |                                    |
    |--[tx_fetch_req_valid]-->           |
    |   (store 3: MBX_FETCH_REQ_2)     |
    |                                    |
    |--[out_v_i]-->                      |
    |   (NoC packet 1)                   |
    |                                    |
    |--[out_v_i]-->                      |
    |   (NoC packet 2)                   |
    |                                    |
    |--[out_v_i]-->                      |
    |   (NoC packet 3)                   |
    |                                    |
    |        [NoC Latency: 5 cycles]     |
    |                                    |
    |                          [in_v_o]<-|
    |                          (store 1) |
    |                                    |
    |                          [in_v_o]<-|
    |                          (store 2) |
    |                                    |
    |                          [in_v_o]<-|
    |                          (store 3) |
    |                                    |
    |                     [rx_fetch_req_valid]
```


---

## 6. Pass/Fail Criteria

- [ ] FETCH_REQUEST issued within 1 cycle of notification
- [ ] Pull Client generates correct 3-store sequence
- [ ] NoC packets formed with correct addresses (0x1004, 0x1008, 0x100C)
- [ ] NoC latency consistent (5 cycles)
- [ ] Producer assembles request after all 3 stores received
- [ ] Source PE coordinates extracted correctly
- [ ] txn_id echoed correctly in assembled request
- [ ] Scoreboard occupancy updated correctly


---

## 7. Corner Cases

1. **NoC latency variation**: Test with different latencies
2. **Credit exhaustion**: NoC backpressures
3. **Reset during NoC traversal**: Verify clean recovery
4. **Multiple concurrent requests**: Different edges
5. **Back-to-back requests**: No gap between requests

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
