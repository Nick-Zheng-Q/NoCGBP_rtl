# Compute Subsystem Integration Test

## 1. Test Objective

Verify that the Compute Subsystem correctly:
- Computes a variable node update end-to-end
- Issues read descriptors to Read Stream Engine
- Receives SPM beats via Read Stream Engine
- Performs matrix operations
- Issues write descriptors and streams results via Write Stream Engine
- Signals `done_valid` and `batch_done`

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                  Compute Subsystem                           │
│                                                              │
│   ┌─────────────┐    rd_desc[0]    ┌──────────────────┐     │
│   │             │ ───────────────▶ │ Read Stream Eng 0│     │
│   │   Compute   │    rd_beat[0]    │  (STATE) + AGU   │     │
│   │   Unit      │ ◀─────────────── │                  │     │
│   │             │    rd_desc[1]    ├──────────────────┤     │
│   │             │ ───────────────▶ │ Read Stream Eng 1│     │
│   │             │    rd_beat[1]    │  (STAGING) + AGU │     │
│   │             │ ◀─────────────── │                  │     │
│   │             │    wr_desc       ├──────────────────┤     │
│   │             │ ───────────────▶ │ Write Stream Eng │     │
│   │             │    wr_word       │  + AGU           │     │
│   └─────────────┘                  └──────────────────┘     │
│        ▲                                                     │
│        │ ns_valid/ns_data/ns_last                            │
│        │                                                     │
│   ┌─────────────┐                                            │
│   │ Accumulator │ (mocked in this test)                      │
│   │   (mock)    │                                            │
│   └─────────────┘                                            │
└─────────────────────────────────────────────────────────────┘
```

**Modules under test:** `compute_unit`, `read_stream_engine` (×2), `write_stream_engine`, `agu`

**Mocked modules:** `accumulator` (neighbor state stream), `spm_bank_array` (SPM data source/sink)

> **Note:** RSE0 (STATE) is active in current tests. RSE1 (STAGING) is tied off until batched staging mode is enabled.

## 3. Preconditions

- Clock: 100MHz
- Reset: Active low
- SPM initialized with test node state data
- Accumulator mock produces test neighbor state stream

## 4. Test Stimulus

### ✅ 4.1 Test Case 1: Variable Node Update (dof=1, 1 neighbor) — IMPLEMENTED

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | cmd_valid | 1 | Start compute |
| T+1   | cmd_node_id | 0x10 | Node ID |
| T+1   | cmd_is_factor | 0 | Variable node |
| T+1   | cmd_dof | 1 | DOF = 1 |
| T+1   | cmd_state_words | 8 | 8 FP32 words = 1 × 256b engine beat |
| T+2   | cmd_valid | 0 | Clear |
| T+3   | ns_valid | 1 | Neighbor data start |
| T+3   | ns_data | NEIGHBOR_W0 | Word 0 |
| ...   | ... | ... | Stream 8 neighbor words |
| T+10  | ns_last | 1 | Last neighbor word |
| T+11  | rd_beat_ready | 1 | Ready for SPM beats |
| T+11  | rd_beat_data | {SELF_W1, SELF_W0} | Own state beat 0 (64b) |
| ...   | ... | ... | More beats (4 × 64b = 1 × 256b) |
| T+M   | wr_word_ready | 1 | Ready for results |

> **Implementation note:** `cmd_state_words` must be a multiple of `WORDS_PER_BEAT * BEATS_PER_GBP_IN = 2 * 4 = 8` so that the read_stream_engine issues enough 64b beats for the compute_unit assembler to form a 256b engine word.

### ❌ 4.2 Test Case 2: Factor Node Update (dof=6, 2 neighbors) — NOT YET IMPLEMENTED

Factor node uses completely different FSM states (`S_FAC_LOAD_DATA` → `S_FAC_LOOP_INIT` → `S_FAC_CAVITY_ACCUM` → ... → `S_FAC_DONE`).

**Stimulus**: Same as Test Case 1 but `cmd_is_factor = 1`, `cmd_dof = 6`, `cmd_adj_count = 2`.

**Expected**: FSM traverses factor path, computes per-edge cavity, produces messages.

### ✅ 4.3 Test Case 3: Backpressure Through Subsystem — IMPLEMENTED

**Scenario**: SPM Arbiter and Accumulator both apply backpressure.

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+1   | cmd_valid | 1 | Start compute |
| T+3   | ns_ready | 0 | Accumulator stalls |
| T+4   | rd_beat_ready | 0 | Compute Unit stalls |
| T+5   | spm_rd_ready | 0 | Read Stream Engine stalls |
| T+6   | ns_ready | 1 | Resume |
| T+7   | rd_beat_ready | 1 | Resume |
| T+8   | spm_rd_ready | 1 | Resume |

### ❌ 4.4 Test Case 4: Multi-DOF Variable (dof=3, 6) — NOT YET IMPLEMENTED

**Expected**: `compact_payload_beats(dof)` changes (1→2→3→4 for dof=1,2,3,4,5,6). Verify `stream_target_beats` matches actual data consumed.

### ❌ 4.5 Test Case 5: Multiple Adjacent Nodes (msg_count=3) — NOT YET IMPLEMENTED

**Expected**: Variable node accumulates messages from 3 neighbors. `accum_count_r` must reach `msg_count - 1` before `S_VAR_STORE_RESULT`.

### ❌ 4.6 Test Case 6: Batch Done Assertion — NOT YET IMPLEMENTED

**Expected**: After multiple compute commands, `batch_done_o` asserts when the batch is complete.

## 5. Expected Output

### 5.1 Test Case 1: Variable Node Update

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | cmd_ready | 1 | Command accepted |
| T+1   | rd_desc_valid | 1 | Issue read descriptor for own state |
| T+1   | rd_desc_base_addr | 0x0100 | State base address |
| T+1   | rd_desc_word_count | 12 | State words |
| T+N+1 | rd_beat_ready | 1 | Accepting beats |
| T+M   | wr_desc_valid | 1 | Issue write descriptor |
| T+M   | wr_desc_base_addr | 0x0200 | Result address |
| T+M   | wr_word_valid | 1 | Result word 0 |
| T+M+1 | wr_word_valid | 1 | Result word 1 |
| T+M+P | done_valid | 1 | Compute complete |
| T+M+P | done_node_id | 0x10 | Correct node |

### 5.2 Test Case 3: Backpressure

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+3   | ns_ready | 0 | CU stalls accumulator |
| T+4   | rd_beat_ready | 0 | CU not ready |
| T+5   | spm_rd_ready | 0 | RSE not ready |
| T+6   | ns_ready | 1 | Resume propagation |
| T+7   | rd_beat_ready | 1 | Resume |
| T+8   | spm_rd_ready | 1 | Resume |

## 6. Timing Diagram

```
Compute Subsystem Flow:
           ___     ___     ___     ___     ___     ___     ___     ___
cmd       ___|        |________________________________________________
                  ________    ________    ________
ns        ___________|   W0   |__|   W1   |__|   W2   |__|  LAST  |___
                          ________    ________    ________
rd_beat   _______________|   B0   |__|   B1   |__|   B2   |___________
                                              ________    ________
wr_word   ___________________________________|   R0   |__|   R1   |___
                                                                      ________
done      ___________________________________________________________|        |
```

## 7. Pass/Fail Criteria

- [ ] Command accepted and read descriptor issued within 1 cycle
- [ ] All own-state beats consumed before compute starts
- [ ] Neighbor state consumed in parallel with own-state reads
- [ ] Write descriptor issued after compute completes
- [ ] All result words written to correct addresses
- [ ] `done_valid` asserted after writeback complete
- [ ] Backpressure propagates correctly: SPM stall → RSE stall → CU stall → accumulator stall
- [ ] No deadlock under sustained backpressure

## 8. Corner Cases

1. **Zero neighbors**: Compute with no incoming state
2. **Maximum DOF**: dof=8, staging buffer limit
3. **State_words = 1**: Minimum data
4. **Descriptor while SPM stalled**: RSE buffers descriptor
5. **Reset during compute**: All modules clean abort
6. **Odd word_count**: Last beat partial handling end-to-end

## 9. Related Documents

| Document | Content |
|----------|---------|
| `../../04_PE_MICROARCHITECTURE.md` §2.9 | Compute Unit architecture |
| `../../05_INTERFACES.md` §2.10-2.13 | Compute Unit, Stream Engines, AGU ports |
| `../../06_PE_CONTROL_FLOW.md` §3.5 | Compute stage flow |
| `../unit_tests/09_compute_unit.md` | Compute Unit unit test |
| `../unit_tests/20_read_stream_engine.md` | RSE unit test |
| `../unit_tests/21_write_stream_engine.md` | WSE unit test |
| `../unit_tests/22_agu.md` | AGU unit test |
