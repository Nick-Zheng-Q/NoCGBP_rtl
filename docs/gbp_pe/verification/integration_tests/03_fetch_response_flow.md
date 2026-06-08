# Fetch Response Flow Integration Test

## 1. Test Objective

Verify the fetch response flow across modules:
1. Pull Server receives FETCH_REQUEST
2. Pull Server reads NodeHeader and STATE from SPM
3. Pull Server streams response via NoC Adapter
4. Consumer's NoC Adapter decodes incoming stores
5. Response Collector reassembles data
6. Response Collector feeds data to Accumulator
7. Response Collector signals completion to Scoreboard


---

## 2. Preconditions

- System: 2 PEs (PE_A at (0,0), PE_B at (1,0))
- Node N on PE_A, Node M on PE_B
- FETCH_REQUEST already received by PE_A's Pull Server
- SPM on PE_A contains valid NodeHeader and STATE for node N (2 words)


---

## 3. Test Stimulus

### Phase 1: Pull Server Reads SPM (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | req_valid | 1 | Fetch request |
| T+1   | req_target_node_id | 0x10 | Target node N |
| T+1   | req_consumer_node_id | 0x20 | Consumer node M |
| T+2   | req_valid | 0 | Clear |
| T+3   | spm_rd_ready | 1 | SPM read ready |
| T+3   | spm_rd_data | HEADER | NodeHeader |
| T+4   | spm_rd_ready | 1 | SPM read ready |
| T+4   | spm_rd_data | WORD_0 | First state word |
| T+5   | spm_rd_ready | 1 | SPM read ready |
| T+5   | spm_rd_data | WORD_1 | Second state word |

### Phase 2: Pull Server Sends Response (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+6   | tx_fetch_resp_valid | 1 | Metadata store |
| T+6   | tx_fetch_resp_node_id | 0x10 | Producer node |
| T+6   | tx_fetch_resp_state_words | 2 | Two words |
| T+7   | tx_fetch_resp_valid | 1 | Data word 0 |
| T+7   | tx_fetch_resp_data | WORD_0 | First word |
| T+7   | tx_fetch_resp_data_valid | 1 | Data valid |
| T+7   | tx_fetch_resp_last | 0 | Not last |
| T+8   | tx_fetch_resp_valid | 1 | Data word 1 |
| T+8   | tx_fetch_resp_data | WORD_1 | Second word |
| T+8   | tx_fetch_resp_data_valid | 1 | Data valid |
| T+8   | tx_fetch_resp_last | 1 | Last word |
| T+9   | tx_fetch_resp_valid | 1 | Done store |
| T+9   | tx_fetch_resp_data_valid | 0 | No data |

### Phase 3: NoC Adapter TX (PE_A)

| Cycle | PE_A Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+6   | out_v_i | 1 | Metadata packet |
| T+6   | out_packet_i.addr | 0x1010 | MBX_RESP_META |
| T+7   | out_v_i | 1 | Data word 0 packet |
| T+7   | out_packet_i.addr | 0x1014 | MBX_RESP_DATA |
| T+8   | out_v_i | 1 | Data word 1 packet |
| T+8   | out_packet_i.addr | 0x1014 | MBX_RESP_DATA |
| T+9   | out_v_i | 1 | Done packet |
| T+9   | out_packet_i.addr | 0x1018 | MBX_RESP_DONE |

### Phase 4: NoC Latency

| Cycle | Description |
|-------|-------------|
| T+10..T+14 | NoC traversal for metadata (5 cycles) |
| T+11..T+15 | NoC traversal for data 0 (5 cycles) |
| T+12..T+16 | NoC traversal for data 1 (5 cycles) |
| T+13..T+17 | NoC traversal for done (5 cycles) |

### Phase 5: Response Collector (PE_B)

| Cycle | PE_B Signal | Value | Description |
|-------|-------------|-------|-------------|
| T+15  | rx_fetch_resp_valid | 1 | Metadata received |
| T+15  | rx_fetch_resp_txn_id | 0x03 | txn_id for edge matching |
| T+15  | rx_fetch_resp_state_words | 2 | Two words |
| T+16  | rx_fetch_resp_valid | 1 | Data word 0 |
| T+16  | rx_fetch_resp_data | WORD_0 | First word |
| T+16  | rx_fetch_resp_data_valid | 1 | Data valid |
| T+17  | rx_fetch_resp_valid | 1 | Data word 1 |
| T+17  | rx_fetch_resp_data | WORD_1 | Second word |
| T+17  | rx_fetch_resp_data_valid | 1 | Data valid |
| T+17  | rx_fetch_resp_last | 1 | Last word |
| T+18  | rx_fetch_resp_done_valid | 1 | Done received |


---

## 4. Expected Output

### Phase 1: Pull Server Response (PE_A)

| Cycle | PE_A Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+6   | tx_fetch_resp_ready | 1 | Ready |
| T+7   | tx_fetch_resp_ready | 1 | Ready |
| T+8   | tx_fetch_resp_ready | 1 | Ready |
| T+9   | tx_fetch_resp_ready | 1 | Ready |

### Phase 2: Response Collector (PE_B)

| Cycle | PE_B Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+15  | rx_fetch_resp_ready | 1 | Ready |
| T+16  | remote_valid | 1 | Data to accumulator |
| T+16  | remote_data | WORD_0 | First word |
| T+16  | remote_last | 0 | Not last |
| T+17  | remote_valid | 1 | Data to accumulator |
| T+17  | remote_data | WORD_1 | Second word |
| T+17  | remote_last | 1 | Last word |
| T+18  | complete_valid | 1 | Completion |
| T+18  | complete_txn_id | 0x03 | Edge index |
| T+18  | complete_node_id | 0x10 | Producer node |
| T+18  | complete_consumer_node_id | 0x20 | Consumer node |

### Phase 3: Scoreboard (PE_B)

| Cycle | PE_B Signal | Expected Value | Description |
|-------|-------------|----------------|-------------|
| T+18  | scoreboard_occupancy | 0 | Entry removed |
| T+19  | node_ready[20] | 1 | Node M ready |


---

## 5. Timing Diagram

```
PE_A (Producer)                    PE_B (Consumer)
    |                                    |
    |--[req_valid]-->                    |
    |                                    |
    |  [SPM reads: HEADER, WORD0, WORD1] |
    |                                    |
    |--[tx_fetch_resp]-->                |
    |   (metadata: MBX_RESP_META)        |
    |                                    |
    |--[tx_fetch_resp]-->                |
    |   (data 0: MBX_RESP_DATA)          |
    |                                    |
    |--[tx_fetch_resp]-->                |
    |   (data 1: MBX_RESP_DATA, last)    |
    |                                    |
    |--[tx_fetch_resp]-->                |
    |   (done: MBX_RESP_DONE)            |
    |                                    |
    |        [NoC Latency: 5 cycles]     |
    |                                    |
    |                     [rx_fetch_resp]<- (metadata)
    |                                    |
    |                     [rx_fetch_resp]<- (data 0)
    |                     [remote_valid]  |
    |                                    |
    |                     [rx_fetch_resp]<- (data 1, last)
    |                     [remote_valid]  |
    |                                    |
    |                     [rx_fetch_resp_done_valid]<-
    |                     [complete_valid]
    |                     [node_ready = 1]
```


---

## 6. Pass/Fail Criteria

- [ ] SPM reads complete before response starts
- [ ] Response sequence: metadata → data[0] → ... → data[N-1] → done
- [ ] `tx_fetch_resp_last` asserted on last data word only
- [ ] NoC packets formed with correct addresses
- [ ] Response Collector receives all stores in order
- [ ] Data flows correctly to Accumulator
- [ ] `complete_valid` asserted after done store
- [ ] Scoreboard entry removed on completion
- [ ] Node becomes ready after all edges complete


---

## 7. Corner Cases

1. **Large state**: Many data words (e.g., 64 words)
2. **Single data word**: state_words = 1
3. **NoC latency variation**: Different latencies per packet
4. **Reset during response**: Verify clean recovery
5. **Backpressure from NoC**: Pull Server stalls
6. **Backpressure from Accumulator**: Response Collector stalls

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
