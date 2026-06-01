# spm_bank Unit Test

## 1. Test Objective

Verify that `spm_bank` correctly:
- Stores and retrieves data
- Supports byte-enable writes
- Handles read-during-write behavior
- Maintains data integrity

## 2. Preconditions

- Module: `spm_bank`
- Parameters: `ROW_ADDR_W = 8`, `BEAT_BITS = 32`, `WSTRB_W = 4`
- Clock: 100MHz (10ns period)
- Reset: Active high (`reset_i`)
- Initial state: Memory undefined (or zero after reset)

## 3. Test Stimulus

### 3.1 Test Case 1: Basic Read/Write

**Scenario**: Write data to address, then read back.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | bank_wr_en | 1 | Write enable |
| T+1   | bank_wr_addr | 0x10 | Write address |
| T+1   | bank_wr_data | 0xDEADBEEF | Write data |
| T+1   | bank_wr_wstrb | 4'b1111 | All bytes |
| T+2   | bank_wr_en | 0 | Clear write |
| T+3   | bank_rd_en | 1 | Read enable |
| T+3   | bank_rd_addr | 0x10 | Read address |
| T+4   | bank_rd_en | 0 | Clear read |

### 3.2 Test Case 2: Byte-Enable Write

**Scenario**: Write only lower 2 bytes.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | bank_wr_en | 1 | Write enable |
| T+1   | bank_wr_addr | 0x20 | Write address |
| T+1   | bank_wr_data | 0x0000BEEF | Write data |
| T+1   | bank_wr_wstrb | 4'b0011 | Lower 2 bytes only |
| T+2   | bank_wr_en | 0 | Clear write |

### 3.3 Test Case 3: Multiple Addresses

**Scenario**: Write to multiple addresses, read back.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | bank_wr_en | 1 | Write addr 0x00 |
| T+1   | bank_wr_addr | 0x00 | Address 0 |
| T+1   | bank_wr_data | 0x11111111 | Data 0 |
| T+1   | bank_wr_wstrb | 4'b1111 | All bytes |
| T+2   | bank_wr_en | 1 | Write addr 0x01 |
| T+2   | bank_wr_addr | 0x01 | Address 1 |
| T+2   | bank_wr_data | 0x22222222 | Data 1 |
| T+2   | bank_wr_wstrb | 4'b1111 | All bytes |
| T+3   | bank_wr_en | 0 | Clear |

## 4. Expected Output

### 4.1 Test Case 1: Basic Read/Write

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+4   | bank_rd_data | 0xDEADBEEF | Read data matches write |

### 4.2 Test Case 2: Byte-Enable Write

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+3   | bank_rd_en | 1 | Read enable |
| T+3   | bank_rd_addr | 0x20 | Read address |
| T+4   | bank_rd_data[15:0] | 0xBEEF | Lower 2 bytes written |
| T+4   | bank_rd_data[31:16] | 0x0000 | Upper 2 bytes unchanged |

### 4.3 Test Case 3: Multiple Addresses

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+4   | bank_rd_en | 1 | Read addr 0x00 |
| T+4   | bank_rd_addr | 0x00 | Address 0 |
| T+5   | bank_rd_data | 0x11111111 | Data 0 |
| T+6   | bank_rd_en | 1 | Read addr 0x01 |
| T+6   | bank_rd_addr | 0x01 | Address 1 |
| T+7   | bank_rd_data | 0x22222222 | Data 1 |

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___
clk_i    _|   |___|   |___|   |___|   |___|   |___|   |___
              ________
wr_en     ___|        |_____________________________________
              ________
wr_addr   XXXX| 0x10 |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
              ________
wr_data   XXXX|DEADBEEF|XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
                          ________
rd_en     _____________|        |___________________________
                          ________
rd_addr   XXXXXXXXXXXXXX| 0x10 |XXXXXXXXXXXXXXXXXXXXXXXXXXX
                              ________
rd_data   XXXXXXXXXXXXXXXXXX|DEADBEEF|XXXXXXXXXXXXXXXXXXXXXX
```

## 6. Pass/Fail Criteria

- [ ] Write data stored correctly
- [ ] Read data matches written data
- [ ] Byte-enable writes only specified bytes
- [ ] Unaffected bytes retain previous value
- [ ] Multiple addresses independent
- [ ] Read latency: 1 cycle

## 7. Corner Cases

1. **Reset clears data**: Verify memory state after reset
2. **Read-during-write**: Behavior when same address read and written same cycle
3. **All byte enables**: Full word write
4. **No byte enables**: No write occurs
5. **Maximum address**: Edge of memory space
6. **Back-to-back writes**: Continuous write stream
