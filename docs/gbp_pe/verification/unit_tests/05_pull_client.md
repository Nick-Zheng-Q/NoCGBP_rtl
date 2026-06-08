# Pull Client Unit Test

## 1. Test Objective

Verify that the Pull Client correctly:
- Receives fetch requests from ScoreboardPrefetcher
- Forms FETCH_REQUEST stores via NoC Adapter
- Handles 3-store sequence (is_factor + consumer_id, target_node_id, txn_id)
- Manages backpressure from NoC Adapter


---

## 2. Preconditions

- Module: `pull_client`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE


---

## 3. Test Stimulus

### 3.1 Test Case 1: Normal Fetch Request

**Scenario**: Single fetch request from ScoreboardPrefetcher.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | req_valid | 1 | Request from Scoreboard |
| T+1   | req_target_node_id | 0x10 | Target node N |
| T+1   | req_consumer_node_id | 0x20 | Consumer node M |
| T+1   | req_is_factor | 0 | Variable node |
| T+1   | req_target_x | 0x02 | Target X coord |
| T+1   | req_target_y | 0x00 | Target Y coord |
| T+1   | req_txn_id | 0x03 | Edge index (txn_id) |
| T+2   | req_valid | 0 | Clear request |
| T+3   | tx_fetch_req_ready | 1 | NoC Adapter ready |

### 3.2 Test Case 2: Backpressure Handling

**Scenario**: NoC Adapter applies backpressure.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | req_valid | 1 | Request from Scoreboard |
| T+2   | tx_fetch_req_ready | 0 | NoC Adapter not ready |
| T+3   | tx_fetch_req_ready | 0 | Still not ready |
| T+4   | tx_fetch_req_ready | 1 | Ready again |


---

## 4. Expected Output

### 4.1 Test Case 1: Normal Fetch Request

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | req_ready | 1 | Accept request |
| T+2   | tx_fetch_req_valid | 1 | First store sent |
| T+2   | tx_fetch_req_target_node_id | 0x10 | Target node |
| T+2   | tx_fetch_req_consumer_node_id | 0x20 | Consumer node |
| T+2   | tx_fetch_req_is_factor | 0 | Variable |
| T+2   | tx_fetch_req_target_x | 0x02 | Target X coord |
| T+2   | tx_fetch_req_target_y | 0x00 | Target Y coord |
| T+3   | tx_fetch_req_valid | 1 | Second store sent |
| T+3   | tx_fetch_req_target_node_id | 0x10 | Target node (repeated) |
| T+4   | tx_fetch_req_valid | 1 | Third store sent |
| T+4   | tx_fetch_req_txn_id | 0x03 | txn_id (edge index) |
| T+5   | tx_fetch_req_valid | 0 | Complete |

### 4.2 Test Case 2: Backpressure Handling

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | req_ready | 1 | Accept request |
| T+2   | tx_fetch_req_valid | 1 | Try to send |
| T+3   | tx_fetch_req_valid | 1 | Still trying |
| T+3   | tx_fetch_req_target_node_id | 0x10 | Data held stable |
| T+4   | tx_fetch_req_valid | 1 | Still trying |
| T+5   | tx_fetch_req_valid | 1 | Can send now |


---

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___
              ________
req      ___|        |_____________________________________
              ________
req_ready____|        |_____________________________________
                  ________    ________    ________
tx_valid _______|        |__|        |__|        |__________
                  ________    ________    ________
tx_data  XXXXXXX| STORE1 |XX| STORE2 |XX| STORE3 |XXXXXXXXX
```


---

## 6. Pass/Fail Criteria

- [ ] Request accepted when `req_valid && req_ready`
- [ ] 3-store sequence generated correctly
- [ ] First store: `{is_factor, consumer_node_id}` to `MBX_FETCH_REQ_0`
- [ ] Second store: `{target_node_id}` to `MBX_FETCH_REQ_1`
- [ ] Third store: `{txn_id}` to `MBX_FETCH_REQ_2`
- [ ] Backpressure: data held stable when `tx_fetch_req_ready = 0`
- [ ] `req_ready` deasserted after request accepted


---

## 7. Corner Cases

1. **Reset during transaction**: Verify clean state after reset
2. **Back-to-back requests**: Multiple requests in succession
3. **Maximum backpressure**: NoC Adapter busy for many cycles
4. **Simultaneous request and ready**: Both valid same cycle

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
