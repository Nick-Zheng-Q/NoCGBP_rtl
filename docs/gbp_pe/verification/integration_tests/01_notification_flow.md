# Notification Flow Integration Test

## 1. Test Objective

Verify the notification flow from producer to consumer:
1. Producer completes node update
2. Writeback Controller sends NOTIFICATION via NoC Adapter
3. NoC Adapter forms `e_remote_store` packet
4. Consumer's NoC Adapter decodes incoming store
5. Consumer's ScoreboardPrefetcher records notification

## 2. Preconditions

- System: 2 PEs (PE_A at (0,0), PE_B at (1,0))
- Node N on PE_A, Node M on PE_B
- Edge (M, N): M is consumer, N is producer
- All modules reset and idle

## 3. Test Stimulus

### Phase 1: Producer Compute Complete (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | compute_done_valid | 1 | Node N compute complete |
| T+1   | compute_done_node_id | 0x10 | Node N ID |
| T+1   | compute_done_is_factor | 0 | Variable node |
| T+2   | compute_done_valid | 0 | Clear |

### Phase 2: Writeback Controller (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+2   | tx_notif_valid | 1 | NOTIFICATION request |
| T+2   | tx_notif_source_node_id | 0x10 | Producer node N |
| T+2   | tx_notif_target_node_id | 0x20 | Consumer node M |
| T+2   | tx_notif_is_factor | 0 | Variable |
| T+2   | tx_notif_target_pe | 0x01 | Target PE_B |
| T+3   | tx_notif_valid | 0 | Clear |

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
| T+10  | rx_notif_target_node_id | 0x20 | Consumer node M |
| T+10  | rx_notif_source_pe | 0x00 | PE_A coordinates |
| T+11  | rx_notif_valid | 0 | Clear |

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
| T+9   | rx_notif_source_pe | 0x00 | PE_A coordinates |
| T+9   | rx_notif_is_factor | 0 | Variable |

### Phase 4: ScoreboardPrefetcher (PE_B)

| Cycle | PE_B Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+10  | fetch_req_valid | 1 | Fetch request issued |
| T+10  | fetch_req_target_node_id | 0x10 | Target node N |
| T+10  | fetch_req_consumer_node_id | 0x20 | Consumer node M |
| T+11  | scoreboard_occupancy | 1 | Entry added |

## 5. Timing Diagram

```
PE_A (Producer)                    PE_B (Consumer)
    |                                    |
    |--[compute_done_valid]-->           |
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

## 7. Corner Cases

1. **NoC latency variation**: Test with different latencies (3-10 cycles)
2. **Multiple notifications**: Multiple producers notify same consumer
3. **Credit exhaustion**: NoC backpressures due to no credits
4. **Reset during NoC traversal**: Verify clean recovery
5. **Simultaneous notifications**: Multiple edges notified same cycle
