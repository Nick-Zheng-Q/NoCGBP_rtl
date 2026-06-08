# spm_bank_array Unit Test

## 1. Test Objective

Verify that `spm_bank_array` correctly:
- Instantiates multiple `spm_bank` instances
- Routes requests to correct bank based on address
- Returns read data from correct bank
- Supports parallel access to different banks


---

## 2. Preconditions

- Module: `spm_bank_array`
- Parameters: `NB = 8` (number of banks)
- Clock: 100MHz (10ns period)
- Reset: Active high (`reset_i`)
- Initial state: All banks idle


---

## 3. Test Stimulus

### 3.1 Test Case 1: Single Bank Access

**Scenario**: Read from bank 0.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | bank_if[0].bank_rd_en | 1 | Bank 0 read |
| T+1   | bank_if[0].bank_rd_addr | 0x10 | Row address |
| T+2   | bank_if[0].bank_rd_en | 0 | Clear |

### 3.2 Test Case 2: Parallel Bank Access

**Scenario**: Read from bank 0 and bank 1 simultaneously.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | bank_if[0].bank_rd_en | 1 | Bank 0 read |
| T+1   | bank_if[0].bank_rd_addr | 0x10 | Bank 0 row |
| T+1   | bank_if[1].bank_rd_en | 1 | Bank 1 read |
| T+1   | bank_if[1].bank_rd_addr | 0x20 | Bank 1 row |
| T+2   | bank_if[0].bank_rd_en | 0 | Clear |
| T+2   | bank_if[1].bank_rd_en | 0 | Clear |

### 3.3 Test Case 3: Write and Read Same Bank

**Scenario**: Write to bank 2, then read back.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | reset_i | 0 | Reset deasserted |
| T+1   | bank_if[2].bank_wr_en | 1 | Bank 2 write |
| T+1   | bank_if[2].bank_wr_addr | 0x30 | Write address |
| T+1   | bank_if[2].bank_wr_data | 0xCAFEBABE | Write data |
| T+1   | bank_if[2].bank_wr_wstrb | 4'b1111 | All bytes |
| T+2   | bank_if[2].bank_wr_en | 0 | Clear write |
| T+3   | bank_if[2].bank_rd_en | 1 | Bank 2 read |
| T+3   | bank_if[2].bank_rd_addr | 0x30 | Read address |
| T+4   | bank_if[2].bank_rd_en | 0 | Clear read |


---

## 4. Expected Output

### 4.1 Test Case 1: Single Bank Access

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+2   | bank_if[0].bank_rd_data | DATA | Read data from bank 0 |

### 4.2 Test Case 2: Parallel Bank Access

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+2   | bank_if[0].bank_rd_data | DATA_0 | Bank 0 data |
| T+2   | bank_if[1].bank_rd_data | DATA_1 | Bank 1 data |

### 4.3 Test Case 3: Write and Read Same Bank

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+4   | bank_if[2].bank_rd_data | 0xCAFEBABE | Read data matches write |


---

## 5. Timing Diagram

```
Test Case 2:
           ___     ___     ___     ___
clk_i    _|   |___|   |___|   |___|   |___
              ________    ________
bank0_rd  ___|        |__|        |________
              ________    ________
bank1_rd  ___|        |__|        |________
                      ________    ________
bank0_data______| DATA0  |__| DATA0  |____
                      ________    ________
bank1_data______| DATA1  |__| DATA1  |____
```


---

## 6. Pass/Fail Criteria

- [ ] Each bank operates independently
- [ ] Parallel access to different banks works
- [ ] Read data returned from correct bank
- [ ] Write data stored in correct bank
- [ ] Bank selection based on address bits
- [ ] No interference between banks


---

## 7. Corner Cases

1. **All banks active**: Maximum concurrency
2. **Same bank conflict**: Multiple access to one bank
3. **Reset during access**: Clean state
4. **Maximum address range**: Edge addresses
5. **Back-to-back access**: Continuous operation

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
