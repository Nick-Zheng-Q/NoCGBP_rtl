# writeback_controller

**Status**: Complete, all tests passing

**Files**:
- `v/gbp_pe/writeback_controller.sv` — RTL (~120 lines)
- `nocbp_verilator/tops/unit/writeback_controller_top.sv` — test wrapper
- `nocbp_verilator/tests/unit/writeback_controller.cc` — C++ testbench (3 test cases)
- `nocbp_verilator/tests/unit/writeback_controller.yaml` — test config
- `nocbp_verilator/tests/unit/writeback_controller.f` — file list

**Architecture**:
- FSM: IDLE → SEND → DONE
- IDLE: accepts done_valid, triggers reset_valid (combinational)
- SEND: iterates adjacency list, sends NOTIFICATION to remote neighbors, skips local
- DONE: asserts wb_done, returns to IDLE

**Test results**: 3/3 PASS
- Test Case 1: 2 Remote Neighbors ✓
- Test Case 2: Mixed Local and Remote (skip local) ✓
- Test Case 3: Backpressure ✓

Run: `make LEVEL=unit TEST=writeback_controller VERILATOR_WARNINGS=none`
