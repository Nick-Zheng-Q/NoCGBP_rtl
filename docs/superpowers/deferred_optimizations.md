# Deferred Optimizations / Cleanups

> This file tracks improvements that have been identified but are not being implemented immediately.
> Each entry should include: date identified, what, why it matters, and what would need to change.

---

## 2026-06-12: Remove or simplify `factor_type` in `gbp_compute_core`

**What:**
- `gbp_factor_type_e` (`FACTOR_SCALAR`, `FACTOR_SE2`, `FACTOR_BA`, `FACTOR_SE3`) is currently carried through `gbp_core_req_t`, `cu_cmd_t`, and `gbp_compute_core` command registers.
- It is used only in `op_decoder.sv` to validate that `(factor_type, dim_i, dim_o)` combinations are legal.
- It does **not** change the data path in V0.

**Why it is waste:**
- For V0 the compute core is generic over `dim_i`/`dim_o`; the factor-specific math is encoded in the operand stream layout (e.g., `OST_MSG_STATIC`, `OST_CAV_*`).
- Carrying `factor_type` through the core command path adds bits and enum plumbing with no functional effect.

**Suggested cleanup:**
1. Remove `factor_type` from `gbp_core_req_t` and `cu_cmd_t` in `v/gbp_pe/compute_core/gbp_op_pkg.sv`.
2. Remove `cmd_factor_type_i` from `op_decoder.sv` and the corresponding validation logic.
3. Make `op_decoder` validate only `dim_i`/`dim_o` legality (and `direction` for BA-like cases if needed in V1).
4. Update `compute_unit_wrapper.sv` and any tests that drive `cmd_factor_type_i`.

**Impact / risk:**
- Low risk for V0 functionality; mostly deletion.
- May conflict with future V1 factor-specific hardware acceleration, so the field could be reintroduced later when it actually controls logic.

**Related files:**
- `v/gbp_pe/compute_core/gbp_op_pkg.sv`
- `v/gbp_pe/compute_core/op_decoder.sv`
- `v/gbp_pe/compute_core/compute_unit_wrapper.sv`
- `v/gbp_pe/compute_core/gbp_compute_core.sv`
- `nocbp_verilator/tests/unit/compute_unit_wrapper.cc`
- `nocbp_verilator/tests/unit/gbp_compute_core.cc`
- `nocbp_verilator/tops/unit/compute_unit_wrapper_top.sv`
- `nocbp_verilator/tops/unit/gbp_compute_core_top.sv`

---
