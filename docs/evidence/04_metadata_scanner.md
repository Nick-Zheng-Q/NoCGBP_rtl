# metadata_scanner

**Status**: Complete, all tests passing

**Files**:
- `v/gbp_pe/metadata_scanner.sv` — RTL (~160 lines)
- `nocbp_verilator/tops/unit/metadata_scanner_top.sv` — test wrapper (fixed combinational SPM responses)
- `nocbp_verilator/tests/unit/metadata_scanner.cc` — C++ testbench (1 test case)
- `nocbp_verilator/tests/unit/metadata_scanner.yaml` — test config
- `nocbp_verilator/tests/unit/metadata_scanner.f` — file list

**Architecture**:
- FSM: IDLE → RD_HEADER → PARSE_HDR → RD_ADJ → OUTPUT_ADJ → DONE
- Reads NodeHeader from SPM (combinational read, 0-cycle for test)
- Extracts: dof, adj_count, adj_base, state_base, state_words
- Scans AdjEntry[0..adj_count-1], classifies local vs remote by (x, y) comparison
- Outputs adj stream (valid/data/last) and info output (valid when adj_idx=0)

**Key design decisions**:
- NodeHeader bit layout: {state_words, state_base, adj_base, adj_count, dof, node_id} packed LSB-first
- AdjEntry bit layout: {neighbor_y, neighbor_x, neighbor_id} packed LSB-first
- Local/remote: (neighbor_x, neighbor_y) == (my_x, my_y) → local
- SPM read is combinational in test wrapper (0-cycle latency). Real implementation uses SPM Arbiter with 1-cycle latency.

**Test results**: 1/1 PASS
- Test Case 1: Single Node Scan (2 neighbors: 1 local, 1 remote) ✓

**Lessons learned**:
- Verilator initializes memories with random values. Use combinational logic or explicit C++ memory writes for test data.
- Check combinational outputs at falling edge (clk=0; eval()) before rising edge triggers state transitions.

Run: `make LEVEL=unit TEST=metadata_scanner VERILATOR_WARNINGS=none`
