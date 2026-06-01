# gbp_pe_noc_bridge Unit Test

## 1. Test Objective

Verify that `gbp_pe_noc_bridge` correctly:
- Decodes incoming requests from endpoint adapter
- Routes to sideband commands or ingress writes
- Handles MMIO register reads/writes
- Manages ordering constraints

## 2. Preconditions

- Module: `gbp_pe_noc_bridge`
- Clock: 100MHz (10ns period)
- Reset: Active high (`reset_i`)
- Initial state: IDLE, no pending requests

## 3. Test Stimulus

### 3.1 Test Case 1: Sideband Command

**Scenario**: Incoming request triggers sideband command.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | core_req_v_i | 1 | Request valid |
| T+1   | core_req_addr_i | 0x1000 | MMIO address |
| T+1   | core_req_data_i | 0x00000005 | Command data |
| T+1   | core_req_we_i | 1 | Write enable |
| T+2   | core_req_v_i | 0 | Clear |

### 3.2 Test Case 2: Ingress Write

**Scenario**: Incoming request triggers ingress write to SPM.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | core_req_v_i | 1 | Request valid |
| T+1   | core_req_addr_i | 0x8000 | Payload address |
| T+1   | core_req_data_i | 0xDEADBEEF | Write data |
| T+1   | core_req_we_i | 1 | Write enable |
| T+2   | core_req_v_i | 0 | Clear |

### 3.3 Test Case 3: MMIO Read

**Scenario**: Read from MMIO status register.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | core_req_v_i | 1 | Request valid |
| T+1   | core_req_addr_i | 0x1000 | MMIO address |
| T+1   | core_req_we_i | 0 | Read |
| T+2   | core_req_v_i | 0 | Clear |

## 4. Expected Output

### 4.1 Test Case 1: Sideband Command

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | core_req_yumi_o | 1 | Accept request |
| T+2   | sideband_cmd_valid_o | 1 | Command issued |
| T+2   | sideband_cmd_kind_o | 2'b01 | Command kind |
| T+2   | sideband_cmd_txn_id_o | 0x05 | Transaction ID |
| T+3   | core_rsp_v_o | 1 | Response |
| T+3   | core_rsp_data_o | 0 | Response data |

### 4.2 Test Case 2: Ingress Write

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | core_req_yumi_o | 1 | Accept request |
| T+2   | ingress_intent_v_o | 1 | Ingress write |
| T+2   | ingress_intent_addr_o | 0x8000 | Address |
| T+2   | ingress_intent_data_o | 0xDEADBEEF | Data |
| T+2   | ingress_intent_bank_o | BANK | Bank ID |
| T+2   | ingress_intent_qid_o | QID | Queue ID |

### 4.3 Test Case 3: MMIO Read

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | core_req_yumi_o | 1 | Accept request |
| T+2   | core_rsp_v_o | 1 | Response valid |
| T+2   | core_rsp_data_o | STATUS | Status data |

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___
clk_i    _|   |___|   |___|   |___|   |___|   |___
              ________
req_v     ___|        |___________________________________
              ________
req_addr  XXXX|0x1000|XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
              ________
req_data  XXXX| 0x05 |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
              ________
yumi      ____|        |___________________________________
                  ________
cmd_valid _______|        |_________________________________
                  ________
cmd_kind  XXXXXXX| 0x01 |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
```

## 6. Pass/Fail Criteria

- [ ] Requests decoded correctly by address
- [ ] Sideband commands issued for MMIO writes
- [ ] Ingress writes triggered for payload addresses
- [ ] MMIO reads return correct status
- [ ] Ordering constraints respected
- [ ] `core_req_yumi_o` handshake correct

## 7. Corner Cases

1. **Reset during decode**: Clean state
2. **Back-to-back requests**: No gap
3. **Invalid address**: `decode_error_o` asserted
4. **Ordering violation**: Payload before tail
5. **Simultaneous sideband and ingress**: Arbitration
