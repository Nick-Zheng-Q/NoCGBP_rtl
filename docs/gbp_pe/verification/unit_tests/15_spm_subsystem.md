# spm_subsystem Unit Test

## 1. Test Objective

Verify that `spm_subsystem` correctly:
- Routes read/write requests to correct bank
- Handles bank conflicts with arbitration
- Returns read data to correct client
- Supports multiple simultaneous accesses

## 2. Preconditions

- Module: `spm_subsystem`
- Parameters: `NB = 8` (number of banks)
- Clock: 100MHz (10ns period)
- Reset: Active high (`reset_i`)
- Initial state: All ports idle

## 3. Test Stimulus

### 3.1 Test Case 1: Single Read Request

**Scenario**: Client 0 reads from bank 0.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | rd_if[0].spm_rd_req_valid | 1 | Read request |
| T+1   | rd_if[0].spm_rd_req_addr | 0x0000 | Bank 0, row 0 |
| T+2   | rd_if[0].spm_rd_req_valid | 0 | Clear |

### 3.2 Test Case 2: Two Reads, Different Banks

**Scenario**: Client 0 and 1 read from different banks.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | rd_if[0].spm_rd_req_valid | 1 | Client 0 read |
| T+1   | rd_if[0].spm_rd_req_addr | 0x0000 | Bank 0 |
| T+1   | rd_if[1].spm_rd_req_valid | 1 | Client 1 read |
| T+1   | rd_if[1].spm_rd_req_addr | 0x0080 | Bank 1 |
| T+2   | rd_if[0].spm_rd_req_valid | 0 | Clear |
| T+2   | rd_if[1].spm_rd_req_valid | 0 | Clear |

### 3.3 Test Case 3: Read-Write Same Bank

**Scenario**: One client reads, another writes to same bank.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | rd_if[0].spm_rd_req_valid | 1 | Read request |
| T+1   | rd_if[0].spm_rd_req_addr | 0x0000 | Bank 0 |
| T+1   | wr_if[0].spm_wr_req_valid | 1 | Write request |
| T+1   | wr_if[0].spm_wr_req_addr | 0x0004 | Bank 0 |
| T+1   | wr_if[0].spm_wr_req_data | 0xDEAD | Write data |
| T+2   | rd_if[0].spm_rd_req_valid | 0 | Clear |
| T+2   | wr_if[0].spm_wr_req_valid | 0 | Clear |

## 4. Expected Output

### 4.1 Test Case 1: Single Read Request

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rd_if[0].spm_rd_req_ready | 1 | Grant access |
| T+2   | rd_if[0].spm_rd_rsp_valid | 1 | Read data valid |
| T+2   | rd_if[0].spm_rd_rsp_data | DATA | Read data |

### 4.2 Test Case 2: Two Reads, Different Banks

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rd_if[0].spm_rd_req_ready | 1 | Grant client 0 |
| T+1   | rd_if[1].spm_rd_req_ready | 1 | Grant client 1 |
| T+2   | rd_if[0].spm_rd_rsp_valid | 1 | Client 0 data |
| T+2   | rd_if[1].spm_rd_rsp_valid | 1 | Client 1 data |

### 4.3 Test Case 3: Read-Write Same Bank

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | rd_if[0].spm_rd_req_ready | 1 | Grant read |
| T+1   | wr_if[0].spm_wr_req_ready | 1 | Grant write |
| T+2   | rd_if[0].spm_rd_rsp_valid | 1 | Read data |

## 5. Timing Diagram

```
Test Case 2:
           ___     ___     ___     ___
clk_i    _|   |___|   |___|   |___|   |___
              ________    ________
rd_req0   ___|        |__|        |________
              ________    ________
rd_req1   ___|        |__|        |________
              ________    ________
rd_ready0 ____|        |__|        |________
              ________    ________
rd_ready1 ____|        |__|        |________
                      ________    ________
rd_rsp0   _______|        |__|        |____
                      ________    ________
rd_rsp1   _______|        |__|        |____
```

## 6. Pass/Fail Criteria

- [ ] Requests routed to correct bank by address
- [ ] Different banks accessed in parallel
- [ ] Bank conflicts arbitrated correctly
- [ ] Read data returned to correct client
- [ ] Write data written with correct byte enables
- [ ] No data corruption from concurrent access

## 7. Corner Cases

1. **All banks active**: Maximum concurrency
2. **Same bank conflict**: Arbitration
3. **Read-write conflict**: Same bank, same cycle
4. **Reset during access**: Clean state
5. **Maximum address range**: Edge addresses
