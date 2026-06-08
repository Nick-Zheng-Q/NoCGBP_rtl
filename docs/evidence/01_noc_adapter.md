# Implementation Log

## 2026-06-02: noc_adapter (TX/RX/Top)

**Status**: Complete, lint-passing, unit test pending

**Files created**:
- `v/gbp_pe/noc_adapter.sv` — Top-level wrapper (~230 lines)
- `v/gbp_pe/noc_adapter_tx.sv` — TX path with round-robin arbiter (~280 lines)
- `v/gbp_pe/noc_adapter_rx.sv` — RX path with mailbox decode (~190 lines)

**Replaces**: `gbp_pe_endpoint_adapter.sv` + `gbp_pe_noc_bridge.sv`

**Architecture**:
- Wraps `bsg_manycore_endpoint_standard` for NoC link interface
- TX: 3-source round-robin arbiter (notif / fetch_req / fetch_resp)
  - Response FSM sends full response without releasing arbiter (max 17 packets)
- RX: Address-decoded mailbox routing (7 GBP mailboxes at base 0x1000)
  - Fetch request: 3-store latching FSM
  - Response: metadata/data/done tracking

**Key design decisions**:
- All coordinates use (x, y) instead of PE ID
- TX arbiter is round-robin (not priority) to avoid starvation
- `GBP_BASE_ADDR` is a parameter (default 0x1000)
- Response TX does not release arbiter mid-response (bounded by max state_words = 15)

**Documentation updates**:
- `02_SPM_AND_METADATA.md`: AdjEntry pe_id → (x, y), edge_scoreboard_t pe_id → (x, y)
- `03_NOC_PROTOCOL.md`: Notification/fetch routing uses (x, y) directly
- `04_PE_MICROARCHITECTURE.md`: Compute Unit → stream-based, added X_CORD_W/Y_CORD_W params
- `05_INTERFACES.md`: All PE_ID_W ports → (x, y), Compute Unit ports → stream-based
- `06_PE_CONTROL_FLOW.md`: COMPUTE stage → descriptor-driven, WRITEBACK → (x, y)

**Lint status**: No errors from noc_adapter code. Only basejump_stl dependency chain errors (resolved in full build).

**Unit test status**: All 4 test cases pass (from `12_noc_adapter.md`).
- Test Case 1: Incoming NOTIFICATION ✓
- Test Case 2: Incoming FETCH_REQUEST (3 stores) ✓
- Test Case 3: Outgoing NOTIFICATION ✓
- Test Case 4: Credit Stall ✓

Run: `make LEVEL=unit TEST=noc_adapter VERILATOR_WARNINGS=none` in `nocbp_verilator/`

**Bugs found and fixed during testing**:
1. `GBP_BASE_ADDR` parameter was `int` (32-bit) — caused address range check overflow in RX decode. Fixed by using explicit 16-bit casts.
2. `rx_fetch_req_ready_i` tied to 1 in test — caused immediate handshake clear of latch counter. Fixed by tying to 0 (valid stays high for observation).
3. `link_in_s.fwd.ready_and_rev` not driven in test — `out_credit_or_ready_o` was always 0, blocking TX. Fixed by driving to 1 in test wrapper (simulates always-ready router).
