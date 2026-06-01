# pe_top Unit Test (PE System Layer)

## 1. Test Objective

Verify that `pe_top` correctly:
- Assembles control, stream, compute, and SPM subsystems
- Handles command interface from NoC bridge
- Manages ingress write path
- Coordinates compute and writeback

## 2. Preconditions

- Module: `pe_top`
- Clock: 100MHz (10ns period)
- Reset: Active high (`reset_i`)
- Initial state: IDLE, no pending commands

## 3. Test Stimulus

### 3.1 Test Case 1: Simple Compute Command

**Scenario**: Receive compute command, execute, return done.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | cmd_valid_i | 1 | Command valid |
| T+1   | cmd_kind_i | 2'b00 | Compute command |
| T+1   | cmd_txn_id_i | 0x05 | Transaction ID |
| T+2   | cmd_valid_i | 0 | Clear command |
| T+10  | rsp_done_o | 1 | Compute complete |
| T+10  | rsp_error_o | 0 | No error |

### 3.2 Test Case 2: Ingress Write Path

**Scenario**: External write to SPM via ingress path.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | ingress_wr_valid_i | 1 | Ingress write valid |
| T+1   | ingress_wr_addr_i | 0x100 | SPM address |
| T+1   | ingress_wr_data_i | 0xBEEF | Write data |
| T+2   | ingress_wr_valid_i | 0 | Clear |

### 3.3 Test Case 3: Compute with Stream Read

**Scenario**: Compute reads neighbor state from SPM.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | cmd_valid_i | 1 | Compute command |
| T+1   | cmd_kind_i | 2'b00 | Compute |
| T+3   | rd_req_valid_o | 1 | SPM read request |
| T+3   | rd_req_addr_o | 0x200 | Read address |
| T+4   | rd_req_valid_o | 0 | Clear |

## 4. Expected Output

### 4.1 Test Case 1: Simple Compute Command

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | cmd_ready_o | 1 | Accept command |
| T+2   | compute_start_o | 1 | Compute started |
| T+2   | cmd_txn_id_o | 0x05 | Transaction ID passed |
| T+10  | compute_done_o | 1 | Compute complete |
| T+10  | wr_txn_id_o | 0x05 | Transaction ID for writeback |

### 4.2 Test Case 2: Ingress Write Path

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | ingress_wr_ready_o | 1 | Accept write |
| T+2   | ingress_wr_req_valid_o | 1 | Write to SPM |
| T+2   | ingress_wr_req_addr_o | 0x100 | SPM address |
| T+2   | ingress_wr_req_data_low_o | 0xBEEF | Write data |

### 4.3 Test Case 3: Compute with Stream Read

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | cmd_ready_o | 1 | Accept command |
| T+3   | rd_req_valid_o | 1 | SPM read |
| T+3   | rd_req_addr_o | 0x200 | Read address |

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___
clk_i    _|   |___|   |___|   |___|   |___|   |___
              ________
cmd       ___|        |___________________________________
              ________
cmd_ready ____|        |___________________________________
                  ________________________________________
compute   _______|                                        |__
                                                      ________
rsp_done  ___________________________________________|        |__
```

## 6. Pass/Fail Criteria

- [ ] Command accepted when `cmd_valid_i && cmd_ready_o`
- [ ] Compute starts correctly
- [ ] Ingress writes reach SPM
- [ ] Stream reads issued correctly
- [ ] `rsp_done_o` asserted after compute complete
- [ ] Transaction ID propagated correctly

## 7. Corner Cases

1. **Reset during compute**: Verify clean state
2. **Back-to-back commands**: Command pipelining
3. **Ingress write during compute**: Concurrent access
4. **SPM read error**: Invalid address
5. **Compute error**: Error propagation
