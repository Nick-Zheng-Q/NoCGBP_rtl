# NoC Subsystem Integration Test

## 1. Test Objective

Verify that the NoC Subsystem correctly:
- Receives NOTIFICATION stores and routes to ScoreboardPrefetcher
- Receives FETCH_REQUEST stores and routes to Pull Server
- Transmits FETCH_RESPONSE via NoC Adapter TX
- Transmits NOTIFICATION via NoC Adapter TX
- Manages credit-based flow control
- Handles concurrent TX from Pull Client, Pull Server, and Writeback Controller

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      NoC Subsystem                           │
│                                                              │
│   External NoC                                               │
│        │                                                     │
│        ▼                                                     │
│   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐   │
│   │ NoC Adapter │───▶│ NoC Adapter │    │ NoC Adapter │   │
│   │    RX       │    │    TX       │◀───│    TX       │   │
│   └──────┬──────┘    └──────┬──────┘    └──────┬──────┘   │
│          │                  │                  │            │
│          ▼                  ▲                  ▲            │
│   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐   │
│   │ Scoreboard  │    │ Pull Client │    │ Writeback   │   │
│   │ Prefetcher  │    │             │    │ Controller  │   │
│   └─────────────┘    └─────────────┘    └─────────────┘   │
│                                              ▲              │
│   ┌─────────────┐                            │              │
│   │ Pull Server │────────────────────────────┘              │
│   │             │    (FETCH_RESPONSE)                       │
│   └─────────────┘                                           │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

**Modules under test:** `noc_adapter`, `noc_adapter_rx`, `noc_adapter_tx`, `pull_server`, `writeback_controller`

**Mocked modules:** `scoreboard_prefetcher` (notification sink), `pull_client` (fetch request source)

## 3. Preconditions

- Clock: 100MHz
- Reset: Active low
- NoC link initialized with credits available
- `X_CORD_W = 6`, `Y_CORD_W = 5`, `DATA_WIDTH = 32`

## 4. Test Stimulus

### 4.1 Test Case 1: NOTIFICATION Ingress + Fetch Request Egress

**Scenario**: External PE sends NOTIFICATION, local PE sends FETCH_REQUEST.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | link_sif_i | NOTIF_STORE | NoC NOTIFICATION store |
| T+1   | store_addr | MBX_NOTIFICATION | Mailbox address |
| T+1   | store_data | {is_factor, src, tgt} | Payload |
| T+2   | req_valid | 1 | Pull Client has request |
| T+2   | req_target_node_id | 0x10 | Target |
| T+2   | req_target_x | 0x02 | Dest X |
| T+2   | req_target_y | 0x03 | Dest Y |
| T+3   | out_credit_or_ready_o | 1 | NoC TX ready |

### 4.2 Test Case 2: FETCH_REQUEST Ingress + FETCH_RESPONSE Egress

**Scenario**: External PE requests fetch, local PE responds.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | link_sif_i | FETCH_REQ_STORE0 | Store 0: target_node_id |
| T+2   | link_sif_i | FETCH_REQ_STORE1 | Store 1: consumer_node_id |
| T+3   | link_sif_i | FETCH_REQ_STORE2 | Store 2: is_factor, x, y |
| T+4   | link_sif_i | FETCH_REQ_STORE3 | Store 3: txn_id |
| T+5   | spm_rd_ready | 1 | SPM ready |
| T+5   | spm_rd_data | {STATE_W1, STATE_W0} | State beat |
| T+6   | out_credit_or_ready_o | 1 | NoC TX ready |

### 4.3 Test Case 3: Concurrent TX Arbitration

**Scenario**: Pull Client, Pull Server, and Writeback Controller all want to TX.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | tx_notif_valid | 1 | Writeback has notification |
| T+1   | tx_fetch_req_valid | 1 | Pull Client has fetch |
| T+1   | tx_fetch_resp_valid | 1 | Pull Server has response |
| T+2   | out_credit_or_ready_o | 1 | NoC TX ready |

## 5. Expected Output

### 5.1 Test Case 1: NOTIFICATION + Fetch Request

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rx_notif_valid_o | 1 | Notification to Scoreboard |
| T+1   | rx_notif_source_node_id | src | Correct source |
| T+3   | link_sif_o | FETCH_REQ_STORE | TX store 0 |
| T+4   | link_sif_o | FETCH_REQ_STORE | TX store 1 |
| T+5   | link_sif_o | FETCH_REQ_STORE | TX store 2 |

### 5.2 Test Case 2: FETCH_REQUEST + FETCH_RESPONSE

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+4   | req_valid | 1 | Pull Server accepts request |
| T+4   | req_txn_id | txn_id | Echo txn_id |
| T+5   | spm_rd_valid | 1 | Read STATE |
| T+6   | link_sif_o | RESP_META | Response metadata store |
| T+7   | link_sif_o | RESP_DATA | Data word 0 |
| T+8   | link_sif_o | RESP_DATA | Data word 1 |
| T+9   | link_sif_o | RESP_DONE | Done store |

### 5.3 Test Case 3: Concurrent TX

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+2   | tx_notif_ready | 1 | Grant notification (highest priority) |
| T+2   | tx_fetch_req_ready | 0 | Stall fetch request |
| T+2   | tx_fetch_resp_ready | 0 | Stall response |
| T+3   | tx_fetch_req_ready | 1 | Grant fetch request |
| T+3   | tx_fetch_resp_ready | 0 | Still stall response |
| T+4   | tx_fetch_resp_ready | 1 | Grant response |

## 6. Timing Diagram

```
FETCH_RESPONSE Flow:
           ___     ___     ___     ___     ___     ___     ___     ___
fetch_req ___|        |________________________________________________
                  ________
spm_rd    _______|        |____________________________________________
                          ________    ________    ________    ________
link_tx   _______________|  META  |__| DATA0  |__| DATA1  |__| DONE   |
```

## 7. Pass/Fail Criteria

- [ ] NOTIFICATION store decoded to `rx_notif_valid` + fields
- [ ] FETCH_REQUEST 4-store sequence decoded to `req_valid` + fields
- [ ] FETCH_RESPONSE metadata + data + done stores sent in correct order
- [ ] NOTIFICATION TX stores sent with correct payload
- [ ] Credit-based flow control respected (no TX without credit)
- [ ] Concurrent TX arbitrated (priority: NOTIFICATION > FETCH_REQ > FETCH_RESP)
- [ ] No data corruption across different message types
- [ ] Mailbox addresses decoded correctly (`MBX_NOTIFICATION`, `MBX_FETCH_REQ_*`, `MBX_RESP_*`)

## 8. Corner Cases

1. **No credits available**: All TX stalled until credit returns
2. **Interleaved RX stores**: NOTIFICATION and FETCH_REQUEST stores arrive mixed
3. **Partial FETCH_REQUEST**: Only 2 of 4 stores arrive (incomplete request)
4. **TX during RX**: Simultaneous TX and RX on same cycle
5. **Reset during TX**: Clean abort, partial message not sent
6. **Maximum message size**: Large state_words response (many data stores)

## 9. Related Documents

| Document | Content |
|----------|---------|
| `../../03_NOC_PROTOCOL.md` | Mailbox encoding, store sequences |
| `../../04_PE_MICROARCHITECTURE.md` §2.6, 2.10, 2.12 | Pull Server, Writeback, NoC Adapter |
| `../../05_INTERFACES.md` §2.6, 2.14, 2.15 | Pull Server, Writeback, NoC Adapter ports |
| `../../06_PE_CONTROL_FLOW.md` §2.1, 2.4 | Background processes |
| `../unit_tests/06_pull_server.md` | Pull Server unit test |
| `../unit_tests/10_writeback_controller.md` | Writeback unit test |
| `../unit_tests/12_noc_adapter.md` | NoC Adapter unit test |
