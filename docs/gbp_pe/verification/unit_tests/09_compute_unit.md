# Compute Unit Unit Test

> **Status:** 🔴 **编译失败 + 功能缺失** (待修复)
>
> **Issue 1 (Compile):** `BEAT_BITS` 改回 64 后，Verilator 将 64-bit 信号从数组（`CData[8]`）编译为标量（`QData`）。旧 C++ 测试代码仍使用数组下标访问（如 `beat_data[0]`），导致编译错误。
>
> **Issue 2 (Functional):** Only variable node test exists. **Factor node compute path has zero test coverage.** This is the #1 blocker for GBP algorithm convergence.

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

### 🔴 3.2 Test Case 2: Factor Node Message Computation with Mixed DOF (REQUIRED)

> **This test is REQUIRED.** Without it, factor node compute path is unverified.
>
> **Mixed DOF is required.** Neighbor 0 has DOF=3 (9 words), neighbor 1 has DOF=6 (27 words). Factor node must use per-edge DOF to compute message sizes and offsets.

**Scenario**: Factor node with 2 neighbors (mixed DOF: 3 and 6), reads old messages + measurement + Jacobian, computes 2 outgoing messages with damping.

**Preconditions**:
- `cmd_state_base_i = 0x0400` (factor STATE base)
- `damping_factor_i = 0x3E99999A` (FP32 0.3)
- `cmd_neighbor_dof_0 = 3`, `cmd_neighbor_dof_1 = 6`
- SPM[0x0400..0x0408]: old message_0 (**9 words**, DOF=3)
- SPM[0x0409..0x0423]: old message_1 (**27 words**, DOF=6)
- SPM[0x0424..0x0429]: measurement (6 words)
- SPM[0x042A..0x045F]: Jacobian (6×9=54 words, measurement_dim=6, Σdof_i=9)
- Total STATE = 9 + 27 + 6 + 54 = **96 words**

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | cmd_valid | 1 | Compute command |
| T+1   | cmd_node_id | 0x20 | Node ID |
| T+1   | cmd_is_factor | 1 | **Factor node** |
| T+1   | cmd_dof | 6 | Factor measurement_dim |
| T+1   | cmd_adj_count | 2 | 2 adjacent variables |
| T+1   | cmd_state_words | 96 | Total STATE words |
| T+1   | cmd_state_base | 0x0400 | Factor STATE base |
| T+1   | cmd_neighbor_dof_0 | 3 | Neighbor 0 DOF |
| T+1   | cmd_neighbor_dof_1 | 6 | Neighbor 1 DOF |
| T+1   | damping_factor | 0x3E99999A | Damping = 0.3 |
| T+2   | cmd_valid | 0 | Clear |
| T+3   | ns_valid | 1 | Neighbor 0 belief (DOF=3, 9 words) |
| T+3   | ns_data | V0_W0 | Variable 0 belief word 0 |
| ...   | ... | ... | Stream 9 words (V0 eta + Lambda compact) |
| T+11  | ns_last | 1 | Neighbor 0 last |
| T+12  | ns_valid | 1 | Neighbor 1 belief (DOF=6, 27 words) |
| T+12  | ns_data | V1_W0 | Variable 1 belief word 0 |
| ...   | ... | ... | Stream 27 words (V1 eta + Lambda compact) |
| T+38  | ns_last | 1 | Neighbor 1 last |
| T+39  | rd_beat_ready | 1 | Ready for factor STATE beats |
| T+39  | rd_beat_data | {F_W1, F_W0} | Factor STATE beat 0 |
| ...   | ... | ... | Stream 48 beats (96 words / 2) |

**Expected Output**:

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | cmd_ready | 1 | Accept command |
| T+39  | rd_desc_valid | 1 | Read descriptor for factor STATE |
| T+39  | rd_desc_base_addr | 0x0400 | Factor STATE base |
| T+39  | rd_desc_word_count | 96 | 96 words |
| T+M   | wr_desc_valid | 1 | Write descriptor for message_0 |
| T+M   | wr_desc_base_addr | 0x0400 | msg_0 at base + 0 |
| T+M   | wr_desc_word_count | 9 | compact_words(3) = 9 |
| T+N   | wr_desc_valid | 1 | Write descriptor for message_1 |
| T+N   | wr_desc_base_addr | 0x0409 | msg_1 at base + 9 |
| T+N   | wr_desc_word_count | 27 | compact_words(6) = 27 |
| T+P   | done_valid | 1 | Compute complete |
| T+P   | done_is_factor | 1 | Factor |

**Pass criteria**:
- [ ] FSM traverses factor states: `S_FAC_LOAD_DATA` → `S_FAC_LOOP_INIT` → `S_FAC_CAVITY_ACCUM` → `S_FAC_EXTRACT_BLOCKS` → `S_FAC_INVERT_LNONO` → `S_FAC_COMPUTE_MESSAGE` → `S_FAC_STORE_MESSAGE` → `S_FAC_NEXT_ADJACENT` → `S_FAC_DONE`
- [ ] Reads exactly 96 words from factor STATE
- [ ] Computes **2 messages with different sizes** (9 words and 27 words)
- [ ] Cavity excludes the correct variable for each edge
- [ ] Schur complement extracted correctly for each edge
- [ ] Applies damping to both messages
- [ ] Writes message_0 (9 words) to 0x0400..0x0408
- [ ] Writes message_1 (27 words) to 0x0409..0x0423
- [ ] `done_valid` asserts after both messages written

---

### 3.3 Test Case 3: Backpressure Handling

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

### 4.3 Test Case 3: Backpressure Handling

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

### Variable Node (Test Case 1)
- [ ] Command accepted when `cmd_valid && cmd_ready`
- [ ] Neighbor state read correctly from Accumulator
- [ ] Own state read correctly from SPM
- [ ] Matrix operations performed correctly (MAT_ADD, MAT_INV, MAT_VEC_MUL)
- [ ] Writeback data written to correct SPM address
- [ ] `done_valid` asserted after writeback complete
- [ ] Backpressure handled correctly

### Factor Node (Test Case 2) — REQUIRED
- [ ] Command accepted with `cmd_is_factor = 1`
- [ ] Factor STATE (old messages + measurement + Jacobian) read correctly from SPM
- [ ] Neighbor beliefs accumulated correctly
- [ ] Joint belief built from measurement + Jacobian + incoming messages
- [ ] Per-edge cavity computed (MAT_SUB)
- [ ] Schur complement extracted (MAT_INV + MAT_MUL)
- [ ] Damping applied: `msg_new = (1-damping)*msg_computed + damping*msg_old`
- [ ] Both messages written back to correct SPM addresses
- [ ] `done_valid` asserted after all messages written


---

## 7. Corner Cases

1. **Reset during compute**: Verify clean state after reset
2. **Maximum DOF**: Test with DOF = 8 (staging buffer limit)
3. **Zero neighbors**: Node with no neighbors
4. **Factor node schedule**: Different operation sequence — **REQUIRED, see Test Case 2**
5. **SPM read error**: Invalid data in SPM
6. **Simultaneous command and data**: Both arrive same cycle
7. **Factor node with DOF=1**: Smallest factor (2 words per message)
8. **Factor node with DOF=6**: Large factor (27 words per message)

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
