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

### 🔴 4.2 Test Case 2: Factor Node Update (mixed DOF: 2 neighbors, DOF=3 and DOF=6) — REQUIRED

> **This is the #1 blocking test.** Factor node compute path exists in RTL (`gbp_control_fsm.sv`) but has **zero test coverage**. Without this test, the GBP algorithm cannot converge.
>
> **Mixed DOF is required.** Factor nodes must handle adjacent variables with different DOF.

Factor node uses completely different FSM states (`S_FAC_LOAD_DATA` → `S_FAC_LOOP_INIT` → `S_FAC_CAVITY_ACCUM` → `S_FAC_EXTRACT_BLOCKS` → `S_FAC_INVERT_LNONO` → `S_FAC_COMPUTE_MESSAGE` → `S_FAC_STORE_MESSAGE` → `S_FAC_NEXT_ADJACENT` → `S_FAC_DONE`).

**Preconditions**:
- SPM initialized with factor node STATE containing:
  - `message_0`: old message to variable 0 (compact form, **9 words** for DOF=3)
  - `message_1`: old message to variable 1 (compact form, **27 words** for DOF=6)
  - `measurement`: 6 words (measurement residual)
  - `Jacobian`: 54 words (measurement_dim=6 × Σdof_i=9)
- Total STATE = 9 + 27 + 6 + 54 = **96 words**
- `cmd_neighbor_dof[0] = 3`, `cmd_neighbor_dof[1] = 6`
- Accumulator provides 2 neighbor beliefs (from variables 0 and 1)

**Message offsets** (cumulative, strict compact form):
- `msg_offset[0] = 0` → `msg_addr[0] = 0x0400`
- `msg_offset[1] = compact_words(3) = 9` → `msg_addr[1] = 0x0409`
- `measurement_addr = 0x0400 + 9 + 27 = 0x0424`
- `jacobian_addr = 0x0424 + 6 = 0x042A`

**Stimulus**:

| Cycle | Signal | Value | Description |
|-------|--------|-------|-------------|
| T+0   | rst_n | 1 | Reset deasserted |
| T+1   | cmd_valid | 1 | Start compute |
| T+1   | cmd_node_id | 0x20 | Node ID |
| T+1   | cmd_is_factor | 1 | **Factor node** |
| T+1   | cmd_dof | 6 | Factor measurement_dim |
| T+1   | cmd_adj_count | 2 | 2 neighbors |
| T+1   | cmd_state_words | 96 | Total STATE words |
| T+1   | cmd_state_base | 0x0400 | Factor STATE base address |
| T+1   | cmd_neighbor_dof_0 | 3 | Neighbor 0 DOF |
| T+1   | cmd_neighbor_dof_1 | 6 | Neighbor 1 DOF |
| T+1   | damping_factor | 0x3E99999A | FP32 0.3 |
| T+2   | cmd_valid | 0 | Clear |
| T+3   | ns_valid | 1 | Neighbor 0 belief (DOF=3, 9 words) |
| T+3   | ns_data | V0_ETA_0 | Variable 0 eta[0] |
| ...   | ... | ... | Stream 9 words |
| T+11  | ns_last | 1 | Neighbor 0 last |
| T+12  | ns_valid | 1 | Neighbor 1 belief (DOF=6, 27 words) |
| T+12  | ns_data | V1_ETA_0 | Variable 1 eta[0] |
| ...   | ... | ... | Stream 27 words |
| T+38  | ns_last | 1 | Neighbor 1 last |
| T+39  | rd_beat_ready | 1 | Ready for factor STATE beats |
| T+39  | rd_beat_data | {F_STATE_W1, F_STATE_W0} | Factor STATE beat 0 |
| ...   | ... | ... | Stream 48 beats (96 words / 2) |

**Expected**:

| Cycle | Signal | Expected Value | Description |
|-------|--------|----------------|-------------|
| T+1   | cmd_ready | 1 | Command accepted |
| T+39  | rd_desc_valid | 1 | Issue read descriptor for factor STATE |
| T+39  | rd_desc_base_addr | 0x0400 | Factor STATE base |
| T+39  | rd_desc_word_count | 96 | 96 words |
| T+P   | wr_desc_valid | 1 | Write descriptor for message_0 |
| T+P   | wr_desc_base_addr | 0x0400 | msg_0 at base + 0 |
| T+P   | wr_desc_word_count | 9 | compact_words(3) = 9 |
| T+Q   | wr_desc_valid | 1 | Write descriptor for message_1 |
| T+Q   | wr_desc_base_addr | 0x0409 | msg_1 at base + 9 |
| T+Q   | wr_desc_word_count | 27 | compact_words(6) = 27 |
| T+R   | done_valid | 1 | Compute complete |
| T+R   | done_node_id | 0x20 | Correct node |
| T+R   | done_is_factor | 1 | Factor |

**Pass criteria**:
- [ ] FSM traverses all factor states in correct order
- [ ] Reads 96 words of factor STATE correctly
- [ ] Computes 2 per-edge messages with **different sizes** (9 words and 27 words)
- [ ] Cavity computation excludes the correct variable for each edge
- [ ] Schur complement extracted correctly for each edge
- [ ] Applies damping: `msg_new = (1-0.3)*msg_computed + 0.3*msg_old`
- [ ] Writes message_0 (9 words) back to STATE[0x0400..0x0408]
- [ ] Writes message_1 (27 words) back to STATE[0x0409..0x0423]
- [ ] `done_valid` asserted after both messages written
- [ ] **No skipped states, no faked computation**

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

### 🟡 4.4 Test Case 4: Multi-DOF Variable (dof=3, 6) — REQUIRED

**Expected**: `compact_payload_beats(dof)` changes (1→2→3→4 for dof=1,2,3,4,5,6). Verify `stream_target_beats` matches actual data consumed.

**Stimulus**: Variable node with DOF=3 (9 words) and DOF=6 (27 words), 1 neighbor each.

**Pass criteria**:
- [ ] DOF=3: read 9 words, write 9 words
- [ ] DOF=6: read 27 words, write 27 words
- [ ] `stream_target_beats` = ceil(words / 2) for each case

### 🟡 4.5 Test Case 5: Multiple Adjacent Nodes (msg_count=3) — REQUIRED

**Expected**: Variable node accumulates messages from 3 neighbors. `accum_count_r` must reach `msg_count - 1` before `S_VAR_STORE_RESULT`.

**Stimulus**: Variable node, DOF=3, 3 remote neighbors.

**Pass criteria**:
- [ ] Accumulator receives 3 × 9 = 27 words of neighbor data
- [ ] `accum_count_r` counts 0, 1, 2
- [ ] Only after `accum_count_r == 2` does FSM transition to store result

### 🟡 4.6 Test Case 6: Batch Done Assertion — REQUIRED

**Expected**: After multiple compute commands, `batch_done_o` asserts when the batch is complete.

**Stimulus**: Two variable node updates back-to-back.

**Pass criteria**:
- [ ] `batch_done_o` asserts after first node if staging batch closed
- [ ] `done_valid` asserts only after second node complete

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
