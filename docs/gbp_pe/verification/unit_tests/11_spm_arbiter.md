# SPM Arbiter Unit Test

## 1. Test Objective

Verify that the SPM Arbiter correctly:
- Arbitrates between multiple SPM clients
- Routes read/write requests to correct bank
- Handles bank conflicts with round-robin arbitration
- Returns read data to correct client

## 2. Preconditions

- Module: `spm_arbiter`
- Parameters: `NUM_BANKS = 8`, `NUM_CLIENTS = 6`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: All ports idle

## 3. Test Stimulus

### 3.1 Test Case 1: Single Client Read

**Scenario**: Metadata Scanner reads from SPM.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | rd_valid[0] | 1 | Client 0 read request |
| T+1   | rd_addr[0] | 0x0100 | Address (bank 0) |
| T+2   | rd_valid[0] | 0 | Clear request |
| T+3   | bank_rd_data[0] | DATA | Bank 0 returns data |

### 3.2 Test Case 2: Multiple Clients, Different Banks

**Scenario**: Two clients read from different banks simultaneously.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | rd_valid[0] | 1 | Client 0 read |
| T+1   | rd_addr[0] | 0x0100 | Bank 0 |
| T+1   | rd_valid[1] | 1 | Client 1 read |
| T+1   | rd_addr[1] | 0x0120 | Bank 1 |
| T+2   | rd_valid[0] | 0 | Clear |
| T+2   | rd_valid[1] | 0 | Clear |

### 3.3 Test Case 3: Bank Conflict

**Scenario**: Two clients read from same bank.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | rd_valid[0] | 1 | Client 0 read |
| T+1   | rd_addr[0] | 0x0100 | Bank 0 |
| T+1   | rd_valid[1] | 1 | Client 1 read |
| T+1   | rd_addr[1] | 0x0140 | Bank 0 (conflict) |
| T+2   | rd_valid[0] | 0 | Clear |
| T+2   | rd_valid[1] | 0 | Clear |

### 3.4 Test Case 4: Read and Write Same Bank

**Scenario**: One client reads, another writes to same bank.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | rd_valid[0] | 1 | Client 0 read |
| T+1   | rd_addr[0] | 0x0100 | Bank 0 |
| T+1   | wr_valid[2] | 1 | Client 2 write |
| T+1   | wr_addr[2] | 0x0104 | Bank 0 |
| T+1   | wr_data[2] | WR_DATA | Write data |
| T+2   | rd_valid[0] | 0 | Clear |
| T+2   | wr_valid[2] | 0 | Clear |

## 4. Expected Output

### 4.1 Test Case 1: Single Client Read

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rd_ready[0] | 1 | Grant access |
| T+1   | bank_rd_en[0] | 1 | Bank 0 read enable |
| T+1   | bank_rd_addr[0] | ROW_ADDR | Bank row address |
| T+2   | rd_data[0] | DATA | Read data returned |

### 4.2 Test Case 2: Multiple Clients, Different Banks

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rd_ready[0] | 1 | Grant client 0 |
| T+1   | rd_ready[1] | 1 | Grant client 1 |
| T+1   | bank_rd_en[0] | 1 | Bank 0 enable |
| T+1   | bank_rd_en[1] | 1 | Bank 1 enable |
| T+2   | rd_data[0] | DATA_0 | Client 0 data |
| T+2   | rd_data[1] | DATA_1 | Client 1 data |

### 4.3 Test Case 3: Bank Conflict

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rd_ready[0] | 1 | Grant client 0 (round-robin) |
| T+1   | rd_ready[1] | 0 | Stall client 1 |
| T+1   | bank_rd_en[0] | 1 | Bank 0 enable |
| T+2   | rd_ready[0] | 0 | Stall client 0 |
| T+2   | rd_ready[1] | 1 | Grant client 1 |
| T+2   | bank_rd_en[0] | 1 | Bank 0 enable |
| T+3   | rd_data[0] | DATA_0 | Client 0 data |
| T+3   | rd_data[1] | DATA_1 | Client 1 data |

### 4.4 Test Case 4: Read and Write Same Bank

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rd_ready[0] | 1 | Grant read |
| T+1   | wr_ready[2] | 1 | Grant write |
| T+1   | bank_rd_en[0] | 1 | Bank 0 read |
| T+1   | bank_wr_en[0] | 1 | Bank 0 write |
| T+2   | rd_data[0] | DATA | Read data |

## 5. Timing Diagram

```
Test Case 3 (Bank Conflict):
           ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___
              ________    ________
rd_valid0 ___|        |__|        |__________________________
              ________    ________
rd_valid1 ___|        |__|        |__________________________
              ________            ________
rd_ready0 ____|        |__________|        |__________________
                      ________            ________
rd_ready1 ___________|        |__________|        |__________
              ________    ________
bank_en   ____|   0   |__|   0   |__________________________
                                  ________    ________
rd_data0  _______________________| DATA0|____| DATA1|________
```

## 6. Pass/Fail Criteria

- [ ] Round-robin arbitration for bank conflicts
- [ ] Different banks accessed in parallel
- [ ] `rd_ready`/`wr_ready` asserted when granted
- [ ] `bank_rd_en`/`bank_wr_en` asserted for active banks
- [ ] Read data returned to correct client
- [ ] No data corruption from concurrent access

## 7. Corner Cases

1. **All clients active**: Maximum contention
2. **Same bank, same address**: Read-write conflict
3. **Reset during arbitration**: Verify clean state
4. **Back-to-back requests**: Continuous arbitration
5. **Single bank**: All access to one bank
