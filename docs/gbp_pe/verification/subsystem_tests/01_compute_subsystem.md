# Compute Subsystem Integration Test (v0.7)

> **Status:** 🟡 **Spec updated for v0.7 architecture, implementation pending**
>
> **Architecture change:** The compute subsystem now wraps `compute_unit_wrapper` + `gbp_compute_core` + stream engines. This test spec covers the new v0.7 architecture.

## 1. Test Objective

Verify that the Compute Subsystem correctly:
- Receives compute commands from control subsystem
- Issues read descriptors to Read Stream Engines
- Receives SPM beats via Read Stream Engines
- Feeds operand streams to `gbp_compute_core` via `compute_unit_wrapper`
- Issues write descriptors and streams results via Write Stream Engine
- Signals `done_valid` and `batch_done`

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                  Compute Subsystem (v0.7)                            │
│                                                                      │
│   ┌──────────────────────┐    rd_desc[0]    ┌──────────────────┐    │
│   │                      │ ───────────────▶ │ Read Stream Eng 0│    │
│   │  compute_unit_wrapper│    rd_beat[0]    │  (STATE) + AGU   │    │
│   │  ┌────────────────┐  │ ◀─────────────── │                  │    │
│   │  │ gbp_compute_   │  │    rd_desc[1]    ├──────────────────┤    │
│   │  │ core           │  │ ───────────────▶ │ Read Stream Eng 1│    │
│   │  │                │  │    rd_beat[1]    │  (STAGING) + AGU │    │
│   │  │ • op_decoder   │  │ ◀─────────────── │                  │    │
│   │  │ • operand_     │  │                  ├──────────────────┤    │
│   │  │   window       │  │    wr_desc       │                  │    │
│   │  │ • cavity_      │  │ ───────────────▶ │ Write Stream Eng │    │
│   │  │   builder      │  │    wr_record     │  + AGU           │    │
│   │  │ • ldlt_solve   │  │                  │                  │    │
│   │  │ • schur_update │  │                  └──────────────────┘    │
│   │  │ • damping_unit │  │                                          │
│   │  │ • belief_*     │  │                                          │
│   │  └────────────────┘  │                                          │
│   │                      │                                          │
│   │  ┌────────────────┐  │                                          │
│   │  │ writeback_     │  │                                          │
│   │  │ packer         │  │                                          │
│   │  └────────────────┘  │                                          │
│   └──────────────────────┘                                          │
│        ▲                                                             │
│        │ ns_valid/ns_data/ns_last                                    │
│        │                                                             │
│   ┌──────────────────────┐                                          │
│   │ Neighbor State       │ (mocked in this test)                    │
│   │ Accumulator          │                                          │
│   └──────────────────────┘                                          │
└─────────────────────────────────────────────────────────────────────┘
```

**Modules under test:** `compute_unit_wrapper`, `gbp_compute_core`, `read_stream_engine` (×2), `write_stream_engine`, `agu`

**Mocked modules:** `accumulator` (neighbor state stream), `spm_bank_array` (SPM data source/sink)

## 3. Preconditions

- Clock: 100MHz
- Reset: Active-high synchronous
- SPM initialized with test node state data
- Accumulator mock produces test neighbor state stream

## 4. Test Cases

### 4.1 Test Case 1: `OP_MSG_F2V` — Scalar Factor (dim=1)

**Scenario**: Scalar factor message update end-to-end.

**Command** (via `cu_cmd_t`):
```
op = OP_MSG_F2V
factor_type = FACTOR_SCALAR
dim_i = DIM_1
dim_o = DIM_1
direction = 0
damping = 0.3
diag_lambda = 1e-9
pivot_eps = 1e-12
regularize_en = 1
operand_desc[0] = {valid=1, kind=OST_MSG_STATIC, base_addr=0x100, nbeats=1}
operand_desc[1] = {valid=1, kind=OST_CAV_FACTOR_O, base_addr=0x110, nbeats=1}
operand_desc[2] = {valid=1, kind=OST_CAV_BELIEF_O, base_addr=0x120, nbeats=1}
operand_desc[3] = {valid=1, kind=OST_CAV_OLD_TO_O, base_addr=0x130, nbeats=1}
```

**Expected**:
- Wrapper issues 4 read requests
- RSE0 reads SPM beats and forwards to wrapper
- Wrapper forwards operand beats to `gbp_compute_core`
- Core computes and produces `rsp_valid_o`
- Wrapper converts to `writeback_record_t`
- WSE writes result to SPM
- `done_valid_o` asserts

**Pass criteria**:
- [ ] Read requests match `operand_desc[]`
- [ ] Operand beats consumed in correct order
- [ ] Writeback record has correct `nwords` and `payload`
- [ ] `cu_done_t.success = 1`
- [ ] Numerical result matches golden reference

---

### 4.2 Test Case 2: `OP_MSG_F2V` — SE(3) Factor (dim=6)

**Scenario**: SE(3) factor message update. Tests multi-beat streams.

**Command**:
```
op = OP_MSG_F2V
factor_type = FACTOR_SE3
dim_i = DIM_6
dim_o = DIM_6
direction = 0
damping = 0.5
operand_desc[0] = {valid=1, kind=OST_MSG_STATIC, base_addr=0x200, nbeats=6}
operand_desc[1] = {valid=1, kind=OST_CAV_FACTOR_O, base_addr=0x260, nbeats=2}
operand_desc[2] = {valid=1, kind=OST_CAV_BELIEF_O, base_addr=0x280, nbeats=2}
operand_desc[3] = {valid=1, kind=OST_CAV_OLD_TO_O, base_addr=0x2A0, nbeats=2}
```

**Pass criteria**:
- [ ] Multi-beat streams handled correctly
- [ ] 6×6 matrix operations work
- [ ] Output matches golden reference

---

### 4.3 Test Case 3: `OP_BELIEF` — SE(2) Variable (dim=3, degree=5)

**Scenario**: SE(2) variable belief update with 5 incoming messages.

**Command**:
```
op = OP_BELIEF
dim_i = DIM_3
degree = 5
operand_desc[0] = {valid=1, kind=OST_BELIEF_PRIOR, base_addr=0x300, nbeats=1}
operand_desc[1] = {valid=1, kind=OST_BELIEF_MSG, base_addr=0x320, nbeats=3}
```

**Pass criteria**:
- [ ] `belief_operand_unpacker` correctly unpacks prior and messages
- [ ] `packed_accumulator` accumulates 5 messages
- [ ] `belief_solve_adapter` creates correct solve request
- [ ] `ldlt_solve_core` produces correct mu
- [ ] `belief_result_builder` computes residual
- [ ] Output matches golden reference

---

### 4.4 Test Case 4: Backpressure Through Subsystem

**Scenario**: SPM Arbiter and Accumulator both apply backpressure.

**Stimulus**:
```
T+1: cmd_valid = 1
T+3: ns_ready = 0 (Accumulator stalls)
T+4: rd_beat_ready = 0 (Compute Unit stalls)
T+5: spm_rd_ready = 0 (Read Stream Engine stalls)
T+6: ns_ready = 1 (Resume)
T+7: rd_beat_ready = 1 (Resume)
T+8: spm_rd_ready = 1 (Resume)
```

**Pass criteria**:
- [ ] Backpressure propagates correctly
- [ ] No deadlock under sustained backpressure
- [ ] All data consumed correctly after stall

---

### 4.5 Test Case 5: Pivot Failure Reporting

**Scenario**: Singular matrix triggers pivot failure.

**Stimulus**: Construct `A_cav` to be near-singular.

**Pass criteria**:
- [ ] `cu_done_t.fail = 1`
- [ ] `cu_done_t.min_pivot` reported
- [ ] No V0 automatic retry (wrapper reports to scheduler)

---

### 4.6 Test Case 6: `degree_mismatch` Detection

**Scenario**: Send fewer messages than `cmd.degree`.

**Stimulus**: Set `degree=5`, send only 3 messages with `last=1`.

> **Programming rule (v0.7):** The scheduler/compiler must guarantee that the number of `OST_BELIEF_MSG` beats matches `degree_i`. If they differ, behavior is undefined and the accumulator may wait indefinitely.

**Pass criteria**:
- [ ] `cu_done_t.degree_mismatch = 1`
- [ ] This condition is flagged as a programming error; subsequent behavior is undefined and the test may hang

---

## 5. Timing Diagram

```
Compute Subsystem Flow (v0.7):
           ___     ___     ___     ___     ___     ___     ___     ___
cmd       ___|        |________________________________________________
                  ________    ________    ________    ________
operand   _____|  STATIC  |__| CAV_F  |__| CAV_B  |__| CAV_O  |________
                                                      ________________
rsp       _______________________________________|     result     |________
                                                              ________
done      ___________________________________________________|        |____
```

## 6. Pass/Fail Criteria

- [ ] Command accepted and read requests issued within 1 cycle
- [ ] All operand beats consumed in correct order
- [ ] `rsp_valid_o` asserts after computation
- [ ] Writeback record has correct `nwords`, `kind`, `payload`
- [ ] `done_valid_o` asserts after writeback complete
- [ ] `cu_done_t` has correct `node_id`, `factor_id`, `op`, `success`, `fail`
- [ ] Backpressure propagates correctly
- [ ] No deadlock under sustained backpressure

## 7. Corner Cases

1. **Zero neighbors**: Compute with no incoming state
2. **Maximum DOF**: dim=6, largest matrix operations
3. **Odd word_count**: Last beat partial handling
4. **Descriptor while SPM stalled**: RSE buffers descriptor
5. **Reset during compute**: All modules clean abort
6. **BA direction=1**: Verify transpose handled by operand stream producer
7. **Diagonal loading**: Verify `diag_lambda` applied when `regularize_en=1`
8. **Pivot eps boundary**: Verify fail triggers at exact boundary

## 8. Related Documents

| Document | Content |
|----------|---------|
| `../../08_NEW_COMPUTE_UNIT.md` | v0.7 compute core spec |
| `../../04_PE_MICROARCHITECTURE.md` §2.9 | Compute Unit architecture |
| `../../05_INTERFACES.md` §2.10-2.13 | Compute Unit, Stream Engines, AGU ports |
| `../../06_PE_CONTROL_FLOW.md` §3.5 | Compute stage flow |
| `../unit_tests/09_compute_unit.md` | Compute Unit unit test |
| `../unit_tests/20_read_stream_engine.md` | RSE unit test |
| `../unit_tests/21_write_stream_engine.md` | WSE unit test |
| `../unit_tests/22_agu.md` | AGU unit test |
