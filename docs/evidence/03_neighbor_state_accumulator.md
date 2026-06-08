# neighbor_state_accumulator

**Status**: Complete, all tests passing

**Files**:
- `v/gbp_pe/neighbor_state_accumulator.sv` — RTL (~45 lines)
- `nocbp_verilator/tops/unit/neighbor_state_accumulator_top.sv` — test wrapper
- `nocbp_verilator/tests/unit/neighbor_state_accumulator.cc` — C++ testbench (4 test cases)
- `nocbp_verilator/tests/unit/neighbor_state_accumulator.yaml` — test config
- `nocbp_verilator/tests/unit/neighbor_state_accumulator.f` — file list

**Architecture**:
- 1-bit state register: 0=LOCAL, 1=REMOTE
- Local data has priority (consumed first), then remote data
- Continuous assignment output mux (combinational)
- State transition: LOCAL→REMOTE when local_last consumed with out_ready=1

**Design note**: The accumulator uses combinational output mux. When local_valid=1 and local_last=1 and out_ready=1, the state transitions to REMOTE on the rising edge. Test must check outputs BEFORE the rising edge (using clk=0; eval()) to see LOCAL-state outputs.

**Test results**: 4/4 PASS
- Test Case 1: Local State Only ✓
- Test Case 2: Remote State Only ✓
- Test Case 3: Mixed Local and Remote ✓
- Test Case 4: Backpressure ✓

Run: `make LEVEL=unit TEST=neighbor_state_accumulator VERILATOR_WARNINGS=none`
