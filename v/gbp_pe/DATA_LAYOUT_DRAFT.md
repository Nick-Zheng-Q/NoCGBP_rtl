# GBP PE Data Layout (META / STATE / MESSAGE) - Frozen v1

## Purpose
This document freezes v1 hardware data format and addressing rules for `META / STATE / MESSAGE` so RTL parsing and tests can be implemented deterministically.

Reference: `nocbp_simulator/` concepts only. Hardware format is not copied 1:1 from simulator.

## Fixed Parameters (from `v/gbp_pe/gbp_pkg.sv`)
- `NB = 8` -> `BANK_ID_W = 3`
- `BEAT_BYTES = 32` -> beat width = `256b`
- `SPM_ADDR_W = 20`
- `TXN_ID_W = 8`
- `XFER_BYTES_W = 16`
- `STEP_BYTES_W = 8`
- Stream classes (`stream_type_e`, 2b):
  - `STREAM_META = 2'b00`
  - `STREAM_VEC = 2'b01`
  - `STREAM_MESSAGE = 2'b10`

## Bank Partition (Frozen)
- `B0`: META and control metadata
- `B1-B3`: STATE payload planes
- `B4-B7`: MESSAGE payload planes

## Format Freeze

### 1) META record (B0)
- One META record per active work item
- Size: `32B` (`256b`, one beat)
- Alignment: `32B`

#### META word/bit layout (little-endian words, `word0` is lowest address)

`word0[31:0]`
- `[31:24] version` (8)
- `[23:16] flags` (8)
  - `flags[0]`: `valid`
  - `flags[1]`: `in_use`
  - `flags[2]`: `done`
  - `flags[7:3]`: reserved
- `[15:8] compute_op` (8)
- `[7:0] txn_id` (8)

`word1[31:0]`
- `[31:12] state_base_addr` (20)
- `[11:9] state_bank_hint` (3, expected in `B1-B3`)
- `[8:0] reserved`

`word2[31:0]`
- `[31:12] message_base_addr` (20)
- `[11:9] message_bank_hint` (3, expected in `B4-B7`)
- `[8:0] reserved`

`word3[31:0]`
- `[31:16] state_xfer_bytes` (16)
- `[15:0] message_xfer_bytes` (16)

`word4[31:0]`
- `[31:24] state_step_bytes` (8)
- `[23:16] message_step_bytes` (8)
- `[15:0] reserved`

`word5[31:0]`
- `[31:16] state_count` (16)
- `[15:0] message_count` (16)

`word6[31:0]`
- `[31:0] meta_id` (32)

`word7[31:0]`
- `[31:0] checksum_or_reserved` (32)

### 2) STATE payload (B1-B3)
- Transfer granularity: `32B` beat
- Contract: opaque `256b` payload consumed by compute
- Address rule:
  - `state_addr = state_base_addr + state_idx * state_step_bytes`
  - Control must force bank bits to `B1-B3`

### 3) MESSAGE payload (B4-B7)
- Transfer granularity: `32B` beat
- Contract: opaque `256b` read/write payload
- Address rule:
  - `msg_addr = message_base_addr + msg_idx * message_step_bytes`
  - Control/write path must force bank bits to `B4-B7`

## Descriptor Mapping (Control Behavior)

### META read descriptor
- class: `STREAM_META`
- `base_addr = meta_addr (B0)`
- `xfer_bytes = 32`
- `addr_step_bytes = 32`
- `txn_id = META.word0[7:0]`

### STATE read descriptor
- class: `STREAM_VEC`
- `base_addr = META.state_base_addr` (bank-forced `B1-B3`)
- `xfer_bytes = META.state_xfer_bytes`
- `addr_step_bytes = META.state_step_bytes`
- `txn_id = META.txn_id`

### MESSAGE read descriptor
- class: `STREAM_MESSAGE`
- `base_addr = META.message_base_addr` (bank-forced `B4-B7`)
- `xfer_bytes = META.message_xfer_bytes`
- `addr_step_bytes = META.message_step_bytes`
- `txn_id = META.txn_id`

### MESSAGE write descriptor
- write path stays `compute -> write_stream_engine`
- destination must remain in `B4-B7`
- `txn_id` propagates from active META context

## Validation Rules
- META fetch is exactly one beat (`32B`)
- all class base addresses are `32B` aligned
- for active META: `state_xfer_bytes != 0` and `message_xfer_bytes != 0`
- invalid class-to-bank mapping is test failure
- `txn_id` remains unchanged through META->STATE->MESSAGE descriptor chain

## Test Vector Templates

### A. Control-unit META decode template
Use this template in `control_unit_top` tests.

```
Vector ID: meta_decode_nominal

META words (w7..w0 shown for readability):
  w7 = 0x00000000
  w6 = 0x00000001   // meta_id
  w5 = 0x00020003   // state_count=2, message_count=3
  w4 = 0x20200000   // state_step=0x20, message_step=0x20
  w3 = 0x00400020   // state_xfer=64, message_xfer=32
  w2 = 0x00805000   // message_base_addr=0x00805, bank_hint=0b000 (example)
  w1 = 0x00403000   // state_base_addr=0x00403, bank_hint=0b000 (example)
  w0 = 0x0101002A   // version=1, flags=1(valid), compute_op=0, txn_id=0x2A

Expected decoded fields:
  txn_id = 0x2A
  state_base_addr   = word1[31:12] = 20'h00403
  message_base_addr = word2[31:12] = 20'h00805
  NOTE: parser extracts 20-bit base addresses directly from `[31:12]`.

Expected emitted descriptors:
  desc0 (META):
    class=STREAM_META
    xfer_bytes=32
    addr_step=32
  desc1 (STATE):
    class=STREAM_VEC
    txn_id=0x2A
    xfer_bytes=64
    addr_step=32
    bank in B1-B3
  desc2 (MESSAGE):
    class=STREAM_MESSAGE
    txn_id=0x2A
    xfer_bytes=32
    addr_step=32
    bank in B4-B7
```

### B. Bank-mapping invariant template
Use in dispatcher/integration tests.

```
Case: class_to_bank_valid
  STREAM_META    -> B0 only          (pass)
  STREAM_VEC     -> B1/B2/B3 only    (pass)
  STREAM_MESSAGE -> B4/B5/B6/B7 only (pass)

Case: class_to_bank_invalid
  STREAM_META    -> B4 (fail)
  STREAM_VEC     -> B0 (fail)
  STREAM_MESSAGE -> B2 (fail)
```

### C. Backpressure determinism template
Use in integration tests (`pe_top_integration`).

```
Stimulus:
  - Two META transactions with distinct txn_id (0x11, 0x12)
  - Periodically deassert ready on dispatch/read path for N cycles

Expected:
  - Descriptor order per txn remains META->STATE->MESSAGE
  - No txn cross-association
  - write payload for txn 0x11 cannot appear tagged/associated as txn 0x12
```

## Ownership Summary (for implementation)
- `control_unit`: issues META read, parses META, emits STATE/MESSAGE read requests
- `stream_dispatcher`: read request routing only
- `compute_unit`: consumes read data and emits write payload
- `write_stream_engine`: writes compute payload to MESSAGE bank range

## Notes
- STATE/MESSAGE payload semantics remain opaque in v1 to avoid blocking compute algorithm refinement.
- Future incompatible layout changes must bump `version` and define parser compatibility behavior.
