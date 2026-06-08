# Read Stream Engine Unit Test

> **Status:** ❌ **编译失败** (待修复)
> **Root cause:** `BEAT_BITS=64` 后 Verilator 生成标量 `QData`，旧 C++ 代码使用数组下标（如 `beat_data[0]`）访问，类型不匹配。

## 1. Test Objective

Verify that `read_stream_engine` correctly:
- Accepts read descriptors from Compute Unit
- Generates sequential SPM word addresses via internal AGU
- Reads 64-bit beats from SPM Arbiter
- Returns beats to Compute Unit in order
- Handles backpressure from both Compute Unit and SPM Arbiter

---

## 2. Preconditions

- Module: `read_stream_engine`
- Parameters: `SPM_ADDR_W = 18`, `BEAT_BITS = 64`, `STATE_WORDS_W = 6`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE, no pending descriptor

---

## 3. Test Stimulus

### 3.1 Test Case 1: Normal Descriptor Execution (Even word_count)

**Scenario**: Descriptor for 4 words (2 beats) from STATE region.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | desc_valid | 1 | Descriptor valid |
| T+1   | desc_base_addr | 0x0100 | Word address |
| T+1   | desc_word_count | 4 | 4 words = 2 beats |
| T+1   | desc_is_staging | 0 | STATE region |
| T+2   | desc_valid | 0 | Clear |
| T+3   | spm_rd_ready | 1 | SPM ready |
| T+4   | spm_rd_ready | 1 | SPM ready |
| T+4   | spm_rd_data | {WORD_1, WORD_0} | First beat |
| T+5   | spm_rd_data | {WORD_3, WORD_2} | Second beat |

### 3.2 Test Case 2: Odd word_count (Last Beat Partial)

**Scenario**: Descriptor for 3 words (1.5 beats).

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | desc_valid | 1 | Descriptor valid |
| T+1   | desc_word_count | 3 | 3 words |
| T+3   | spm_rd_ready | 1 | SPM ready |
| T+4   | spm_rd_data | {WORD_1, WORD_0} | First full beat |
| T+5   | spm_rd_data | {XX, WORD_2} | Second beat (only lower word valid) |

### 3.3 Test Case 3: Backpressure from Compute Unit

**Scenario**: SPM returns data but Compute Unit is not ready.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | desc_valid | 1 | Descriptor valid |
| T+1   | desc_word_count | 4 | 4 words |
| T+3   | spm_rd_ready | 1 | SPM ready |
| T+3   | spm_rd_data | {W1, W0} | Beat 0 arrives |
| T+4   | beat_ready | 0 | Compute Unit not ready |
| T+5   | beat_ready | 0 | Still not ready |
| T+6   | beat_ready | 1 | Ready now |

---

## 4. Expected Output

### 4.1 Test Case 1: Normal Descriptor Execution

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | desc_ready | 1 | Descriptor accepted |
| T+2   | spm_rd_valid | 1 | Start SPM read |
| T+2   | spm_rd_addr | 0x0100 | Base word address |
| T+3   | spm_rd_valid | 1 | Continue read |
| T+3   | spm_rd_addr | 0x0101 | Next word address |
| T+4   | beat_valid | 1 | First beat to CU |
| T+4   | beat_data | {W1, W0} | Data correct |
| T+5   | beat_valid | 1 | Second beat |
| T+5   | beat_data | {W3, W2} | Data correct |

### 4.2 Test Case 2: Odd word_count

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+4   | beat_valid | 1 | First full beat |
| T+5   | beat_valid | 1 | Second beat (partial) |
| T+5   | beat_data[31:0] | WORD_2 | Lower word valid |

### 4.3 Test Case 3: Backpressure

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+3   | spm_rd_ready | 1 | SPM not stalled |
| T+4   | beat_valid | 1 | Beat held valid |
| T+5   | beat_valid | 1 | Beat still held |
| T+6   | beat_valid | 0 | Beat consumed |

---

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___|   |___
              ________
desc      ___|        |__________________________________________
                  ________    ________
spm_rd    _______|        |__|        |__________________________
                                  ________    ________
beat      _______________________|        |__|        |__________
```

---

## 6. Pass/Fail Criteria

- [ ] Descriptor accepted when `desc_valid && desc_ready`
- [ ] `spm_rd_addr` increments by 1 each cycle (word address)
- [ ] `beat_valid` asserted when SPM data available
- [ ] `beat_data` matches `spm_rd_data` (pass-through with optional FIFO)
- [ ] Even `word_count`: all beats fully valid
- [ ] Odd `word_count`: last beat valid but upper word may be don't-care
- [ ] Backpressure: `spm_rd_valid` does not stall on `beat_ready == 0` (data buffered)
- [ ] FSM returns to IDLE after all words delivered

---

## 7. Corner Cases

1. **word_count = 1**: Single word, one partial beat
2. **word_count = 0**: Edge case, no SPM access
3. **Maximum word_count**: 65535 words (full 16-bit range)
4. **Descriptor while busy**: New descriptor arrives before previous completes
5. **SPM Arbiter always not ready**: Verify descriptor not lost
6. **Reset during active read**: Clean abort

---

## 8. Related Documents

| Document | Content |
|----------|---------|
| `../../02_SPM_AND_METADATA.md` | SPM layout, bank mapping, beat size |
| `../../04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters |
| `../../05_INTERFACES.md` §2.11 | Read Stream Engine port definition |
| `../../05_INTERFACES.md` §2.13 | AGU port definition |
| `../README.md` | Verification documentation index |
