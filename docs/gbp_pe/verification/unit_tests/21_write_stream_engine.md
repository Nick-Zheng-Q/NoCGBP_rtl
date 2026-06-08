# Write Stream Engine Unit Test

> **Status:** ❌ **编译超时/可能失败** (待排查)
> **Symptom:** `make` 在编译阶段超时（60s+无输出）。可能原因同其他模块：`BEAT_BITS=64` 后标量接口与旧 C++ 数组访问不兼容，导致编译卡住。

## 1. Test Objective

Verify that `write_stream_engine` correctly:
- Accepts write descriptors from Compute Unit
- Accepts 32-bit FP32 words from Compute Unit
- Assembles 2 words into 64-bit beats
- Writes beats to SPM Arbiter with correct `wstrb`
- Handles odd word_count (last word needs partial wstrb)

---

## 2. Preconditions

- Module: `write_stream_engine`
- Parameters: `SPM_ADDR_W = 18`, `BEAT_BITS = 64`, `FP32_W = 32`, `STATE_WORDS_W = 6`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE

---

## 3. Test Stimulus

### 3.1 Test Case 1: Normal Write (Even word_count)

**Scenario**: Write 4 words (2 beats) to SPM.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | desc_valid | 1 | Descriptor valid |
| T+1   | desc_base_addr | 0x0200 | Word address |
| T+1   | desc_word_count | 4 | 4 words |
| T+2   | desc_valid | 0 | Clear |
| T+3   | word_valid | 1 | Word 0 |
| T+3   | word_data | WORD_0 | Data |
| T+4   | word_valid | 1 | Word 1 |
| T+4   | word_data | WORD_1 | Data |
| T+5   | spm_wr_ready | 1 | SPM ready |
| T+6   | word_valid | 1 | Word 2 |
| T+6   | word_data | WORD_2 | Data |
| T+7   | word_valid | 1 | Word 3 |
| T+7   | word_data | WORD_3 | Data |

### 3.2 Test Case 2: Odd word_count (Partial Last Beat)

**Scenario**: Write 3 words (1 full beat + 1 partial).

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | desc_valid | 1 | Descriptor valid |
| T+1   | desc_word_count | 3 | 3 words |
| T+3   | word_valid | 1 | Word 0 |
| T+4   | word_valid | 1 | Word 1 |
| T+5   | spm_wr_ready | 1 | SPM ready |
| T+6   | word_valid | 1 | Word 2 |

### 3.3 Test Case 3: Backpressure from SPM Arbiter

**Scenario**: SPM not ready when beat is assembled.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | desc_valid | 1 | Descriptor valid |
| T+1   | desc_word_count | 4 | 4 words |
| T+3   | word_valid | 1 | Word 0 |
| T+4   | word_valid | 1 | Word 1 |
| T+5   | spm_wr_ready | 0 | SPM not ready |
| T+6   | spm_wr_ready | 0 | Still not ready |
| T+7   | spm_wr_ready | 1 | Ready now |

---

## 4. Expected Output

### 4.1 Test Case 1: Normal Write

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | desc_ready | 1 | Descriptor accepted |
| T+3   | word_ready | 1 | Accept word 0 |
| T+4   | word_ready | 1 | Accept word 1 |
| T+5   | spm_wr_valid | 1 | First beat assembled |
| T+5   | spm_wr_addr | 0x0200 | Base address |
| T+5   | spm_wr_data | {W1, W0} | Assembled beat |
| T+5   | spm_wr_wstrb | 8'b1111_1111 | Full beat valid |
| T+7   | spm_wr_valid | 1 | Second beat |
| T+7   | spm_wr_addr | 0x0201 | Next address |
| T+7   | spm_wr_data | {W3, W2} | Assembled beat |
| T+7   | spm_wr_wstrb | 8'b1111_1111 | Full beat valid |

### 4.2 Test Case 2: Odd word_count

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+5   | spm_wr_valid | 1 | First full beat {W1, W0} |
| T+5   | spm_wr_wstrb | 8'b1111_1111 | Full valid |
| T+7   | spm_wr_valid | 1 | Second beat {XX, W2} |
| T+7   | spm_wr_wstrb | 8'b0000_1111 | Only lower 4 bytes valid |

### 4.3 Test Case 3: Backpressure

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+4   | word_ready | 1 | Word 1 accepted (buffered) |
| T+5   | word_ready | 0 | Stall word 2 (beat buffer full) |
| T+6   | word_ready | 0 | Still stalled |
| T+7   | spm_wr_valid | 1 | First beat written |
| T+7   | word_ready | 1 | Word 2 now accepted |

---

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___|   |___
              ________
desc      ___|        |__________________________________________
                      ________    ________    ________    ________
word      ___________|   W0   |__|   W1   |__|   W2   |__|   W3   |
                              ________                ________
spm_wr    ___________________|   B0   |______________|   B1   |___
```

---

## 6. Pass/Fail Criteria

- [ ] Descriptor accepted when `desc_valid && desc_ready`
- [ ] Words accepted when `word_valid && word_ready`
- [ ] Two 32-bit words assembled into one 64-bit beat
- [ ] `spm_wr_addr` increments by 1 each beat (word address)
- [ ] Even `word_count`: all beats have `wstrb = 8'b1111_1111`
- [ ] Odd `word_count`: last beat has `wstrb = 8'b0000_1111`
- [ ] Backpressure: `word_ready` deasserts when SPM stalls
- [ ] FSM returns to IDLE after all words written

---

## 7. Corner Cases

1. **word_count = 1**: Single partial beat with wstrb = 4'b1111
2. **word_count = 0**: No write
3. **Maximum word_count**: Verify no overflow
4. **Descriptor while busy**: Reject or queue
5. **SPM never ready**: Verify no deadlock
6. **Reset during active write**: Clean abort

---

## 8. Related Documents

| Document | Content |
|----------|---------|
| `../../02_SPM_AND_METADATA.md` | SPM layout, byte order, wstrb policy |
| `../../04_PE_MICROARCHITECTURE.md` | Module descriptions, parameters |
| `../../05_INTERFACES.md` §2.12 | Write Stream Engine port definition |
| `../../05_INTERFACES.md` §2.13 | AGU port definition |
| `../README.md` | Verification documentation index |
