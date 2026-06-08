# Compute Unit Unit Test

> **Status:** ❌ **编译失败** (待修复)
> **Root cause:** `BEAT_BITS` 改回 64 后，Verilator 将 64-bit 信号从数组（`CData[8]`）编译为标量（`QData`）。旧 C++ 测试代码仍使用数组下标访问（如 `beat_data[0]`），导致编译错误。

## 1. Test Objective

Verify that the Compute Unit correctly:
- Receives compute commands from Node Scheduler
- Reads neighbor state data from Accumulator
- Reads own state from SPM
- Performs matrix operations (MAT_ADD, MAT_INV, etc.)
- Writes back results to SPM
- Signals completion


---

## 2. Preconditions

- Module: `compute_unit`
- Clock: 100MHz (10ns period)
- Reset: Active low (`rst_n`)
- Initial state: IDLE
- SPM contains valid node state data


---

## 3. Test Stimulus

### 3.1 Test Case 1: Variable Node Update (Simple)

**Scenario**: Variable node with 1 remote neighbor, simple update.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | cmd_valid | 1 | Compute command |
| T+1   | cmd_node_id | 0x10 | Node ID |
| T+1   | cmd_is_factor | 0 | Variable node |
| T+1   | cmd_dof | 3 | DOF = 3 |
| T+1   | cmd_adj_count | 1 | 1 neighbor |
| T+2   | cmd_valid | 0 | Clear command |
| T+3   | ns_valid | 1 | Neighbor state data |
| T+3   | ns_data | NEIGHBOR_WORD_0 | Neighbor data |
| T+3   | ns_last | 0 | Not last |
| T+4   | ns_valid | 1 | More neighbor data |
| T+4   | ns_data | NEIGHBOR_WORD_1 | Neighbor data |
| T+4   | ns_last | 1 | Last word |
| T+5   | rd_beat_ready | 1 | Ready for SPM beat |
| T+5   | rd_beat_data | {SELF_WORD_1, SELF_WORD_0} | Own state beat (64-bit) |
| T+6   | rd_beat_ready | 1 | Ready for SPM beat |
| T+6   | rd_beat_data | {SELF_WORD_3, SELF_WORD_2} | Own state beat (64-bit) |

### 3.2 Test Case 2: Backpressure Handling

**Scenario**: SPM and output apply backpressure.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | cmd_valid | 1 | Compute command |
| T+3   | ns_valid | 1 | Neighbor data |
| T+3   | out_ready | 0 | Output not ready |
| T+4   | out_ready | 0 | Still not ready |
| T+5   | out_ready | 1 | Ready again |


---

## 4. Expected Output

### 4.1 Test Case 1: Variable Node Update (Simple)

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | cmd_ready | 1 | Accept command |
| T+3   | ns_ready | 1 | Accept neighbor data |
| T+4   | ns_ready | 1 | Accept neighbor data |
| T+5   | rd_desc_valid | 1 | Issue read descriptor |
| T+5   | rd_desc_base_addr | SELF_ADDR | Own state word address |
| T+5   | rd_desc_word_count | N | Number of state words |
| T+5   | rd_desc_is_staging | 0 | STATE region |
| T+N   | wr_desc_valid | 1 | Issue write descriptor |
| T+N   | wr_desc_base_addr | RESULT_ADDR | Result word address |
| T+N   | wr_desc_word_count | M | Number of result words |
| T+N   | wr_word_valid | 1 | Writeback word 0 |
| T+N   | wr_word_data | RESULT_WORD_0 | Computed result |
| T+N+1 | wr_word_valid | 1 | Writeback word 1 |
| T+N+1 | wr_word_data | RESULT_WORD_1 | Computed result |
| T+N+2 | done_valid | 1 | Completion signal |
| T+N+1 | done_node_id | 0x10 | Node ID |
| T+N+1 | done_is_factor | 0 | Variable |

### 4.2 Test Case 2: Backpressure Handling

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+3   | ns_ready | 0 | Backpressure from output |
| T+4   | ns_ready | 0 | Still backpressured |
| T+5   | ns_ready | 1 | Ready |
| T+6   | wr_word_valid | 1 | Writeback word |


---

## 5. Timing Diagram

```
Test Case 1:
           ___     ___     ___     ___     ___     ___     ___
clk      _|   |___|   |___|   |___|   |___|   |___|   |___|   |___
              ________
cmd       ___|        |_________________________________________________
                      ________    ________
ns        ___________|        |__|        |________________________________
                          ________    ________
rd_beat   _______________|        |__|        |____________________________
                          ________
rd_desc   _______________|        |________________________________________
                                                              ________
wr_word   ___________________________________________________|        |____
                                                                      ________
done      ___________________________________________________________|        |
```


---

## 6. Pass/Fail Criteria

- [ ] Command accepted when `cmd_valid && cmd_ready`
- [ ] Neighbor state read correctly from Accumulator
- [ ] Own state read correctly from SPM
- [ ] Matrix operations performed correctly (cycle model in 04)
- [ ] Writeback data written to correct SPM address
- [ ] `done_valid` asserted after writeback complete
- [ ] Backpressure handled correctly


---

## 7. Corner Cases

1. **Reset during compute**: Verify clean state after reset
2. **Maximum DOF**: Test with DOF = 8 (staging buffer limit)
3. **Zero neighbors**: Node with no neighbors
4. **Factor node schedule**: Different operation sequence
5. **SPM read error**: Invalid data in SPM
6. **Simultaneous command and data**: Both arrive same cycle

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
