# Response Collector Unit Test

## 1. Test Objective

Verify that the Response Collector correctly:
- Receives FETCH_RESPONSE metadata, data, and done stores
- Reassembles multi-word response data
- Feeds data into Neighbor State Accumulator
- Signals completion to ScoreboardPrefetcher


---

## 2. Preconditions

- Module: `response_collector`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE


---

## 3. Test Stimulus

### 3.1 Test Case 1: Normal Response Collection

**Scenario**: Receive response with 2 data words.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | rx_fetch_resp_valid | 1 | Metadata received |
| T+1   | rx_fetch_resp_is_factor | 0 | Variable |
| T+1   | rx_fetch_resp_state_words | 2 | Two words |
| T+2   | rx_fetch_resp_valid | 1 | Data word 0 |
| T+2   | rx_fetch_resp_data | WORD_0 | First state word |
| T+2   | rx_fetch_resp_data_valid | 1 | Data valid |
| T+2   | rx_fetch_resp_last | 0 | Not last |
| T+3   | rx_fetch_resp_valid | 1 | Data word 1 |
| T+3   | rx_fetch_resp_data | WORD_1 | Second state word |
| T+3   | rx_fetch_resp_data_valid | 1 | Data valid |
| T+3   | rx_fetch_resp_last | 1 | Last data word |
| T+4   | rx_fetch_resp_done_valid | 1 | Done signal |
| T+4   | rx_fetch_resp_txn_id | 0x03 | txn_id for edge matching |
| T+5   | rx_fetch_resp_done_valid | 0 | Clear |

### 3.2 Test Case 2: Accumulator Not Ready (Pass-Through)

**Scenario**: Accumulator is not ready during data streaming. The current RTL implements a pass-through on the RX path; `rx_fetch_resp_ready` remains asserted and `remote_valid`/`remote_data` are forwarded regardless of `remote_ready`. Downstream backpressure must be handled outside this module.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | rx_fetch_resp_valid | 1 | Metadata received |
| T+2   | rx_fetch_resp_valid | 1 | Data word 0 |
| T+2   | remote_ready | 0 | Accumulator not ready |
| T+3   | remote_ready | 0 | Still not ready |
| T+4   | remote_ready | 1 | Ready again |


---

## 4. Expected Output

### 4.1 Test Case 1: Normal Response Collection

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rx_fetch_resp_ready | 1 | Ready to accept |
| T+2   | remote_valid | 1 | Data to accumulator |
| T+2   | remote_data | WORD_0 | First state word |
| T+2   | remote_last | 0 | Not last |
| T+3   | remote_valid | 1 | Data to accumulator |
| T+3   | remote_data | WORD_1 | Second state word |
| T+3   | remote_last | 1 | Last data word |
| T+4   | complete_valid | 1 | Completion signal |
| T+4   | complete_txn_id | 0x03 | Edge index for matching |
| T+4   | complete_node_id | 0x10 | Producer node |
| T+4   | complete_consumer_node_id | 0x20 | Consumer node |

### 4.2 Test Case 2: Accumulator Not Ready (Pass-Through)

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+2   | rx_fetch_resp_ready | 1 | Still ready (no RX backpressure) |
| T+2   | remote_valid | 1 | Data forwarded even if accumulator not ready |
| T+2   | remote_data | WORD_0 | First state word |
| T+3   | rx_fetch_resp_ready | 1 | Still ready |
| T+3   | remote_valid | 1 | Data forwarded |
| T+4   | rx_fetch_resp_ready | 1 | Still ready |
| T+4   | remote_valid | 0 | No new data |


---

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___
              ________    ________    ________
rx_valid  ___|        |__|        |__|        |____________
              ________    ________    ________
rx_data   XXXX| META  |XX| WORD0 |XX| WORD1 |XXXXXXXXXXXX
                                  ________
rx_last   _______________________|        |________________
                      ________    ________
accum     ___________|        |__|        |________________
                                  ________
complete  _______________________|        |________________
```


---

## 6. Pass/Fail Criteria

- [ ] Metadata received and stored correctly
- [ ] Data words collected in order
- [ ] `remote_valid` asserted for each data word
- [ ] `remote_last` asserted on last data word
- [ ] `complete_valid` asserted after done store
- [ ] Completion signals contain correct node IDs
- [ ] Accumulator-not-ready case forwards data (pass-through behavior)


---

## 7. Corner Cases

1. **Single data word**: Response with state_words = 1
2. **Maximum data words**: Response with large state
3. **Reset during collection**: Verify clean state after reset
4. **Done before all data**: Error condition
5. **Accumulator always ready**: No backpressure

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
