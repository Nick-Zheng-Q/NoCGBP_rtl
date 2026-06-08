# pull_server

**Status**: Complete, all tests passing

**Files**:
- `v/gbp_pe/pull_server.sv` — RTL (~150 lines)
- `nocbp_verilator/tops/unit/pull_server_top.sv` — test wrapper
- `nocbp_verilator/tests/unit/pull_server.cc` — C++ testbench (2 test cases)
- `nocbp_verilator/tests/unit/pull_server.yaml` — test config
- `nocbp_verilator/tests/unit/pull_server.f` — file list

**Architecture**:
- FSM: IDLE → LOOKUP → SEND_DATA → SEND_DONE
- LOOKUP: reads NodeHeader from SPM, extracts state_base and state_words
- SEND_DATA: streams state_words words from STATE region, each as a TX data word
- SEND_DONE: signals completion (tx_valid without tx_data_valid)
- Echoes txn_id from request in all TX outputs

**Key design decisions**:
- Header bit layout: {state_words[56:53], state_base[52:37], adj_base[36:21], adj_count[20:17], dof[16:13], node_id[12:0]}
- state_base is at bits [37 +: SPM_ADDR_W] (not at [21 +: SPM_ADDR_W] which is adj_base)
- SPM read is combinational in test wrapper

**Bug found during testing**: pull_server originally read state_base from wrong bit position (adj_base field). Fixed to use bits [37 +: SPM_ADDR_W] matching metadata_scanner layout.

**Test results**: 2/2 PASS
- Test Case 1: Normal Fetch Response (2 data words) ✓
- Test Case 2: Backpressure ✓

Run: `make LEVEL=unit TEST=pull_server VERILATOR_WARNINGS=none`
