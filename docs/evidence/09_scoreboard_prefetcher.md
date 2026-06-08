# scoreboard_prefetcher

**Status**: Complete, all tests passing

**Files**:
- `v/gbp_pe/scoreboard_prefetcher.sv` — RTL (~180 lines)
- `nocbp_verilator/tops/unit/scoreboard_prefetcher_top.sv` — test wrapper
- `nocbp_verilator/tests/unit/scoreboard_prefetcher.cc` — C++ testbench (2 test cases)
- `nocbp_verilator/tests/unit/scoreboard_prefetcher.yaml` — test config
- `nocbp_verilator/tests/unit/scoreboard_prefetcher.f` — file list

**Architecture**:
- Flat edge table indexed by edge_index (0..SCOREBOARD_DEPTH-1)
- Per-edge state: IDLE → NOTIFIED → IN_FLIGHT → READY
- Notification allocates edge slot, marks NOTIFIED
- Scan loop finds NOTIFIED edges, issues fetch requests
- Completion marks edge READY, decrements scoreboard
- Post-compute reset clears all edges for a node
- Node readiness: combinational AND of all edge states for that node
- STAGING coordination: staging_reserve_valid_o/words_o for reservation, staging_batch_closed_i to pause scanning

**Ports added in recheck**:
- staging_reserve_valid_o, staging_reserve_words_o, staging_reserve_ready_i, staging_batch_closed_i
- complete_node_id_i, complete_consumer_node_id_i (debug)
- reset_is_factor_i
- fetch_req_is_factor_o (was hardcoded 0, now uses edge_is_factor from notification)

**Key design decisions**:
- Flat 1D array (not 2D) for Verilator compatibility
- Sequential scan with scan_ptr for finding NOTIFIED edges
- free_ptr for edge slot allocation
- Per-node edge_base/count tracked via adj stream

**Test results**: 2/2 PASS
- Test Case 1: Single Edge Lifecycle ✓
- Test Case 2: Scoreboard Full ✓

Run: `make LEVEL=unit TEST=scoreboard_prefetcher VERILATOR_WARNINGS=none`
