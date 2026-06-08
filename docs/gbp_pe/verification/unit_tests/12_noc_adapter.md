# NoC Adapter Unit Test

## 1. Test Objective

Verify that the NoC Adapter correctly:
- Wraps `bsg_manycore_endpoint_standard`
- Decodes incoming stores to GBP mailbox addresses
- Routes messages to appropriate internal modules
- Forms outgoing `e_remote_store` packets
- Manages credit-based flow control


---

## 2. Preconditions

- Module: `noc_adapter`
- Parameters: `GBP_BASE_ADDR = 16'h1000`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: No pending transactions


---

## 3. Test Stimulus

### 3.1 Test Case 1: Incoming NOTIFICATION

**Scenario**: External PE sends NOTIFICATION store to this PE.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | in_v_o | 1 | Incoming request valid |
| T+1   | in_addr_o | 0x1000 | MBX_NOTIFICATION address |
| T+1   | in_data_o | {0, 0x10} | {is_factor=0, source_node_id=0x10} |
| T+1   | in_src_x_cord_o | 3 | Source PE X |
| T+1   | in_src_y_cord_o | 2 | Source PE Y |
| T+2   | in_v_o | 0 | Clear request |

### 3.2 Test Case 2: Incoming FETCH_REQUEST (3 stores)

**Scenario**: External PE sends FETCH_REQUEST (3-store sequence).

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | in_v_o | 1 | First store |
| T+1   | in_addr_o | 0x1004 | MBX_FETCH_REQ_0 |
| T+1   | in_data_o | {0, 0x20} | {is_factor=0, consumer_node_id=0x20} |
| T+1   | in_src_x_cord_o | 3 | Source PE X |
| T+1   | in_src_y_cord_o | 2 | Source PE Y |
| T+2   | in_v_o | 0 | Clear first store |
| T+3   | in_v_o | 1 | Second store |
| T+3   | in_addr_o | 0x1008 | MBX_FETCH_REQ_1 |
| T+3   | in_data_o | 0x10 | target_node_id = 0x10 |
| T+4   | in_v_o | 0 | Clear second store |
| T+5   | in_v_o | 1 | Third store |
| T+5   | in_addr_o | 0x100C | MBX_FETCH_REQ_2 |
| T+5   | in_data_o | 0x03 | txn_id = 0x03 |
| T+6   | in_v_o | 0 | Clear third store |

### 3.3 Test Case 3: Outgoing NOTIFICATION

**Scenario**: Internal Writeback Controller sends NOTIFICATION via adapter.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | tx_notif_valid | 1 | Notification request |
| T+1   | tx_notif_source_node_id | 0x10 | Source node |
| T+1   | tx_notif_target_node_id | 0x20 | Target node |
| T+1   | tx_notif_is_factor | 0 | Variable node |
| T+1   | tx_notif_target_x | 5 | Target PE X |
| T+1   | tx_notif_target_y | 3 | Target PE Y |
| T+2   | tx_notif_valid | 0 | Clear request |

### 3.4 Test Case 4: Credit Stall

**Scenario**: NoC backpressures due to credit exhaustion.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | tx_notif_valid | 1 | Try to send |
| T+1   | out_credit_or_ready_o | 0 | No credits |
| T+2   | out_credit_or_ready_o | 0 | Still no credits |
| T+3   | out_credit_or_ready_o | 1 | Credit available |


---

## 4. Expected Output

### 4.1 Test Case 1: Incoming NOTIFICATION

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rx_notif_valid | 1 | Notification routed |
| T+1   | rx_notif_source_node_id | 0x10 | Source node |
| T+1   | rx_notif_source_x | 3 | Source PE X |
| T+1   | rx_notif_source_y | 2 | Source PE Y |
| T+1   | rx_notif_is_factor | 0 | Variable |
| T+2   | rx_notif_valid | 0 | Clear |

### 4.2 Test Case 2: Incoming FETCH_REQUEST (3 stores)

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rx_fetch_req_valid | 0 | Not ready yet (need 3 stores) |
| T+3   | rx_fetch_req_valid | 0 | Still waiting (2 of 3 stores) |
| T+5   | rx_fetch_req_valid | 0 | Still waiting (3 of 3 stores, latch in progress) |
| T+6   | rx_fetch_req_valid | 1 | All 3 stores received |
| T+6   | rx_fetch_req_target_node_id | 0x10 | Target node |
| T+6   | rx_fetch_req_consumer_node_id | 0x20 | Consumer node |
| T+6   | rx_fetch_req_is_factor | 0 | Variable |
| T+6   | rx_fetch_req_src_x | 3 | Source PE X |
| T+6   | rx_fetch_req_src_y | 2 | Source PE Y |
| T+7   | rx_fetch_req_valid | 0 | Clear |

### 4.3 Test Case 3: Outgoing NOTIFICATION

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | tx_notif_ready | 1 | Ready to accept |
| T+2   | out_v_i | 1 | Packet sent |
| T+2   | out_packet_i.addr | 0x1000 | MBX_NOTIFICATION |
| T+2   | out_packet_i.op | e_remote_store | Store operation |
| T+2   | out_packet_i.payload | {0, 0x10} | {is_factor, source_node_id} |
| T+2   | out_packet_i.dst_x | target_x | Target X coord |
| T+2   | out_packet_i.dst_y | target_y | Target Y coord |
| T+2   | out_packet_i.src_x | my_x | Source X coord |
| T+2   | out_packet_i.src_y | my_y | Source Y coord |

### 4.4 Test Case 4: Credit Stall

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | tx_notif_ready | 0 | Not ready (no credits) |
| T+2   | tx_notif_ready | 0 | Still not ready |
| T+3   | tx_notif_ready | 1 | Ready (credit available) |
| T+4   | out_v_i | 1 | Packet sent |


---

## 5. Timing Diagram

```
Test Case 1 (Incoming NOTIFICATION):
           ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___
              ________
in_v      ___|        |__________________
              ________
in_addr   XXXX|0x1000 |XXXXXXXXXXXXXXXXXX
              ________
in_data   XXXX|  DATA |XXXXXXXXXXXXXXXXXX
              ________
rx_notif  ____|        |__________________
```


---

## 6. Pass/Fail Criteria

- [ ] Incoming stores decoded correctly by address
- [ ] 3-store FETCH_REQUEST assembled correctly
- [ ] Outgoing packets formed with correct `bsg_manycore_packet_s` format
- [ ] Credit-based flow control respected
- [ ] Source coordinates extracted correctly from incoming requests
- [ ] `tx_busy` asserted when any TX channel active


---

## 7. Corner Cases

1. **Reset during transaction**: Verify clean state after reset
2. **Simultaneous TX requests**: Arbitration between notif/fetch_req/fetch_resp
3. **Address decode error**: Store to unknown GBP address
4. **Maximum outstanding credits**: Fill credit counter
5. **Back-to-back requests**: No gap between incoming stores

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
