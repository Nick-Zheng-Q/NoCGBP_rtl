# scoreboard_prefetcher

**Status**: Complete, all tests passing

**Files**:
- `v/gbp_pe/scoreboard_prefetcher.sv` — RTL (~180 lines)
- `nocbp_verilator/tops/unit/scoreboard_prefetcher_top.sv` — test wrapper
- `nocbp_verilator/tests/unit/scoreboard_prefetcher.cc` — C++ testbench (2 test cases)
- `nocbp_verilator/tests/unit/scoreboard_prefetcher.yaml` — test config
- `nocbp_verilator/tests/unit/scoreboard_prefetcher.f` — file list

**Architecture**:
- Flat edge table indexed by free_ptr (0..SCOREBOARD_DEPTH-1)
- Per-edge state: IDLE → NOTIFIED → IN_FLIGHT → READY
- **adj_valid allocates edge slot and immediately marks NOTIFIED** (v2 change)
- **Dedup guard**: adj_valid for an already-ready node is ignored (prevents duplicate registration on second SCAN)
- Scan loop finds NOTIFIED edges, issues fetch requests
- Completion marks edge READY, decrements scoreboard
- Post-compute reset clears all edges for a node
- Node readiness: `node_has_edge && (node_pending == 0)`
- STAGING coordination: staging_reserve_valid_o/words_o for reservation, staging_batch_closed_i to pause scanning

**Ports added in recheck**:
- staging_reserve_valid_o, staging_reserve_words_o, staging_reserve_ready_i, staging_batch_closed_i
- complete_node_id_i, complete_consumer_node_id_i (debug)
- reset_is_factor_i
- fetch_req_is_factor_o (uses edge_is_factor from adj_valid / NOTIFICATION)

**v2 design changes**:
- `adj_valid` now directly triggers edge registration + NOTIFIED state (was: adj_valid set IDLE, NOTIFICATION set NOTIFIED)
- `rx_notif_valid` retained as fallback for race cases (NOTIFICATION arrives before adj_valid)
- Added `adj_should_register` guard to skip re-registration for already-ready nodes
- `node_pending` tracking replaces pure edge-state AND for node_ready

**Key design decisions**:
- Flat 1D array (not 2D) for Verilator compatibility
- Sequential scan with scan_ptr for finding NOTIFIED edges
- free_ptr for edge slot allocation
- Per-node pending count tracked via delayed adj_v_r / comp_v_r signals

**Test results**: 2/2 PASS
- Test Case 1: Single Edge Lifecycle ✓
- Test Case 2: Scoreboard Full ✓

Run: `make LEVEL=unit TEST=scoreboard_prefetcher VERILATOR_WARNINGS=none`
