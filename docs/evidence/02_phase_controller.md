# phase_controller

**Status**: Complete, all tests passing

**Files**:
- `v/gbp_pe/phase_controller.sv` — RTL (~30 lines)
- `nocbp_verilator/tops/unit/phase_controller_top.sv` — test wrapper
- `nocbp_verilator/tests/unit/phase_controller.cc` — C++ testbench (3 test cases)
- `nocbp_verilator/tests/unit/phase_controller.yaml` — test config
- `nocbp_verilator/tests/unit/phase_controller.f` — file list

**Architecture**:
- 1-bit register `phase_factor_r`: 1 = FACTOR_PHASE, 0 = VARIABLE_PHASE
- `phase_switch_pulse_o = no_schedulable_nodes_i` (combinational)
- `phase_factor_first_o = phase_factor_r` (registered, updates 1 cycle after pulse)
- Reset: starts in FACTOR_PHASE

**Test results**: 3/3 PASS
- Test Case 1: Normal Phase Switch ✓
- Test Case 2: Continuous Scheduling ✓
- Corner Case: Reset during switch ✓

**Timing note**: Spec timing diagram shows phase_switch_pulse at T+2 and phase_factor_first change at T+3 (1 cycle after pulse). This is implemented as: pulse is combinational from input, phase register updates on next clock edge.

Run: `make LEVEL=unit TEST=phase_controller VERILATOR_WARNINGS=none`
