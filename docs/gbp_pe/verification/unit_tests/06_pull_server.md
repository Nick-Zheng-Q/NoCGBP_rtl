# Pull Server Unit Test

## 1. Test Objective

Verify that the Pull Server correctly:
- Receives FETCH_REQUEST from NoC Adapter
- Looks up NodeHeader from SPM
- Streams FETCH_RESPONSE (metadata + data + done) via NoC Adapter
- Handles backpressure from NoC Adapter


---

## 2. Preconditions

- Module: `pull_server`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE, SPM contains valid NodeHeader and STATE data


---

## 3. Test Stimulus

### 3.1 Test Case 1: Normal Fetch Response

**Scenario**: Single FETCH_REQUEST, Pull Server reads SPM and sends response.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | req_valid | 1 | Fetch request received |
| T+1   | req_target_node_id | 0x10 | Target node |
| T+1   | req_consumer_node_id | 0x20 | Consumer node |
| T+1   | req_is_factor | 0 | Variable node |
| T+1   | req_fetch_src_x | 0x02 | Source X coord |
| T+1   | req_fetch_src_y | 0x00 | Source Y coord |
| T+2   | req_valid | 0 | Clear request |
| T+3   | spm_rd_ready | 1 | SPM read ready |
| T+3   | spm_rd_data | HEADER_DATA | NodeHeader data |
| T+4   | spm_rd_ready | 1 | SPM read ready |
| T+4   | spm_rd_data | STATE_WORD_0 | First state word |
| T+5   | spm_rd_ready | 1 | SPM read ready |
| T+5   | spm_rd_data | STATE_WORD_1 | Second state word |
| T+6   | tx_fetch_resp_ready | 1 | NoC Adapter ready |

### 3.2 Test Case 2: Backpressure Handling

**Scenario**: NoC Adapter applies backpressure during response streaming.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | req_valid | 1 | Fetch request received |
| T+3   | spm_rd_ready | 1 | SPM read ready |
| T+4   | tx_fetch_resp_ready | 0 | NoC Adapter not ready |
| T+5   | tx_fetch_resp_ready | 0 | Still not ready |
| T+6   | tx_fetch_resp_ready | 1 | Ready again |


---

## 4. Expected Output

### 4.1 Test Case 1: Normal Fetch Response

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | req_ready | 1 | Accept request |
| T+2   | spm_rd_valid | 1 | Start SPM read |
| T+2   | spm_rd_addr | HEADER_ADDR | NodeHeader address |
| T+3   | spm_rd_valid | 1 | Continue SPM read |
| T+3   | spm_rd_addr | STATE_ADDR | State base address |
| T+4   | tx_fetch_resp_valid | 1 | Send metadata |
| T+4   | tx_fetch_resp_node_id | 0x10 | Producer node |
| T+4   | tx_fetch_resp_state_words | 2 | Two state words |
| T+5   | tx_fetch_resp_valid | 1 | Send data word 0 |
| T+5   | tx_fetch_resp_data | STATE_WORD_0 | First word |
| T+5   | tx_fetch_resp_data_valid | 1 | Data valid |
| T+5   | tx_fetch_resp_last | 0 | Not last yet |
| T+6   | tx_fetch_resp_valid | 1 | Send data word 1 |
| T+6   | tx_fetch_resp_data | STATE_WORD_1 | Second word |
| T+6   | tx_fetch_resp_data_valid | 1 | Data valid |
| T+6   | tx_fetch_resp_last | 1 | Last data word |
| T+7   | tx_fetch_resp_valid | 1 | Send done |
| T+7   | tx_fetch_resp_data_valid | 0 | No data |
| T+8   | FSM state | IDLE | Back to idle |

### 4.2 Test Case 2: Backpressure Handling

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+4   | tx_fetch_resp_valid | 1 | Try to send |
| T+5   | tx_fetch_resp_valid | 1 | Still trying (backpressured) |
| T+5   | tx_fetch_resp_data | STATE_WORD_0 | Data held stable |
| T+6   | tx_fetch_resp_valid | 1 | Still trying |
| T+7   | tx_fetch_resp_valid | 1 | Can send now |
| T+7   | tx_fetch_resp_data | STATE_WORD_0 | Data sent |


---

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___|   |___
              ________
req       ___|        |_____________________________________________
                  ________
spm_rd    _______|        |_______________________________________
                      ________    ________
tx_valid  ___________|        |__|        |________________________
                      ________    ________
tx_data   XXXXXXXXXXX| META  |XX| WORD0 |XXXXXXXXXXXXXXXXXXXXXXXX
                                          ________
tx_last   _______________________________|        |________________
```


---

## 6. Pass/Fail Criteria

- [ ] Request accepted only when `req_valid && req_ready`
- [ ] SPM read latency: 1 cycle between `spm_rd_valid` and `spm_rd_data`
- [ ] Response sequence: metadata → data[0] → ... → data[N-1] → done
- [ ] `tx_fetch_resp_last` asserted on last data word only
- [ ] `tx_fetch_resp_data_valid` asserted for each data word
- [ ] Backpressure: data held stable when `tx_fetch_resp_ready = 0`
- [ ] FSM returns to IDLE after done store


---

## 7. Corner Cases

1. **Reset during response**: Verify clean state after reset
2. **SPM read error**: What if SPM returns invalid data?
3. **Maximum state_words**: Test with large state (e.g., 64 words)
4. **Zero state_words**: Edge case with no data words
5. **Multiple requests**: Verify requests are queued correctly

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
