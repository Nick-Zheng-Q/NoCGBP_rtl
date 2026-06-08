# response_collector

**Status**: Complete, all tests passing

**Files**:
- `v/gbp_pe/response_collector.sv` — RTL (~50 lines)
- `nocbp_verilator/tops/unit/response_collector_top.sv` — test wrapper
- `nocbp_verilator/tests/unit/response_collector.cc` — C++ testbench (2 test cases)
- `nocbp_verilator/tests/unit/response_collector.yaml` — test config
- `nocbp_verilator/tests/unit/response_collector.f` — file list

**Architecture**:
- Pass-through design: data words flow directly from RX to accumulator
- rx_ready always 1 (accumulator handles backpressure)
- Completion pulse on rx_done_valid_i with txn_id

**Test results**: 2/2 PASS
- Test Case 1: Normal Response Collection ✓
- Test Case 2: Backpressure ✓

Run: `make LEVEL=unit TEST=response_collector VERILATOR_WARNINGS=none`
