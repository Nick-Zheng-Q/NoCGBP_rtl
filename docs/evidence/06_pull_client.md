# pull_client

**Status**: Complete, all tests passing

**Files**:
- `v/gbp_pe/pull_client.sv` — RTL (~100 lines)
- `nocbp_verilator/tops/unit/pull_client_top.sv` — test wrapper
- `nocbp_verilator/tests/unit/pull_client.cc` — C++ testbench (2 test cases)
- `nocbp_verilator/tests/unit/pull_client.yaml` — test config
- `nocbp_verilator/tests/unit/pull_client.f` — file list

**Architecture**:
- FSM: IDLE → S0 → S1 → S2 → IDLE
- Latches request in IDLE, sends 3 stores sequentially
- Each store waits for tx_ready_i
- tx_store_idx_o indicates which store (0, 1, 2)
- All 3 stores carry same target coords, node IDs, txn_id

**Test results**: 2/2 PASS
- Test Case 1: Normal Fetch Request (3 stores) ✓
- Test Case 2: Backpressure ✓

Run: `make LEVEL=unit TEST=pull_client VERILATOR_WARNINGS=none`
