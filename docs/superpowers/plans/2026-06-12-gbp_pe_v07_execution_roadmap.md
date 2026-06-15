# GBP PE v0.7 执行路线图

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` for each phase if implementation is requested. This document is a **roadmap** (execution order), not a line-by-line implementation plan.

**Goal:** 按照依赖顺序，把 `docs/gbp_pe/08_NEW_COMPUTE_UNIT.md` v0.7 规范落地为可工作的 RTL + 测试，最终通过算法正确性门（Direction C）。

**Architecture:** 以 `gbp_compute_core` + `compute_unit_wrapper` 替换旧 `gbp_compute_engine`；先单元、后子系统、再集成、最后系统级 golden-reference。

**Tech Stack:** SystemVerilog, Verilator, C++ testbench (`nocbp_verilator`), Python oracle/reference。

---

## Phase 0: 现状盘点与基线（1–2 天）

**目标：** 确认哪些 RTL/测试已经可用，哪些只是 spec。

- [ ] **Step 0.1: 列出已有 RTL 与测试**
  - Files:
    - `v/gbp_pe/compute_core/gbp_compute_core.sv`
    - `v/gbp_pe/compute_core/compute_unit_wrapper.sv`
    - `v/gbp_pe/compute_core/*.sv`（其余子模块）
    - `v/gbp_pe/compute_unit.sv`（旧实现）
    - `v/gbp_pe/compute/gbp_compute_engine.sv`（旧实现）
    - `nocbp_verilator/tests/unit/gbp_compute_core_test.cc`
    - `nocbp_verilator/tests/unit/compute_unit.cc`
    - `nocbp_verilator/tests/unit/gbp_pe_compute_subsystem.cc`
  - Command: `find v/gbp_pe/compute_core -name '*.sv' | sort`
  - Command: `find nocbp_verilator/tests/unit -name '*compute*' | sort`

- [ ] **Step 0.2: 跑一遍当前 lint / 单元测试，拿到基线**
  - Run:
    ```bash
    cd nocbp_verilator
    make lint LEVEL=unit TEST=gbp_compute_core_test
    make lint LEVEL=unit TEST=compute_unit
    make lint LEVEL=unit TEST=gbp_pe_compute_subsystem
    ```
  - Expected: 记录 exit code / error count；不要修复，只记录。

- [ ] **Step 0.3: 标注 doc/RTL/测试 一致性差距**
  - Output: 在 `docs/superpowers/plans/2026-06-12-gbp_pe_v07_gap_notes.md` 列出
    - 哪些子模块已实现 vs 未实现
    - 哪些测试已跑通 vs 失败
    - 08 规范中哪些接口/字段在 RTL 里还没出现（如 `ctx_id`、`op_id`、`WB_BELIEF` payload order 等）

---

## Phase 1: Compute Core 单元验证（2–3 周）

**目标：** `gbp_compute_core` 单独能正确执行 `OP_BELIEF` 和 `OP_MSG_F2V`。

- [ ] **Step 1.1: 补全 `gbp_compute_core` 接口与 08 规范一致**
  - Files:
    - Modify: `v/gbp_pe/compute_core/gbp_compute_core.sv`
    - Reference: `docs/gbp_pe/08_NEW_COMPUTE_UNIT.md` §8
  - 关键点：
    - `gbp_core_req_t` 含 `op_id`/`ctx_id`
    - `operand_stream_beat_t` 类型化 beat
    - `gbp_core_rsp_t` 输出

- [ ] **Step 1.2: 实现/修复 `ldlt_solve_core` 多 RHS + 正则化**
  - Files:
    - Modify: `v/gbp_pe/compute_core/ldlt_solve_core.sv`
  - Test command:
    ```bash
    cd nocbp_verilator
    make run LEVEL=unit TEST=gbp_compute_core_test
    ```

- [ ] **Step 1.3: 实现/修复 `cavity_builder` 与 `packed_accumulator` 流式累加**
  - Files:
    - Modify: `v/gbp_pe/compute_core/cavity_builder.sv`
    - Modify: `v/gbp_pe/compute_core/packed_accumulator.sv`

- [ ] **Step 1.4: 实现/修复 `op_decoder` 与 `operand_window`**
  - Files:
    - Modify: `v/gbp_pe/compute_core/op_decoder.sv`
    - Modify: `v/gbp_pe/compute_core/operand_window.sv`

- [ ] **Step 1.5: 实现/修复 `belief_result_builder`、`rhs_builder_for_message`、`schur_update_unit`、`damping_unit`**
  - Files:
    - Modify: `v/gbp_pe/compute_core/belief_result_builder.sv`
    - Modify: `v/gbp_pe/compute_core/rhs_builder_for_message.sv`
    - Modify: `v/gbp_pe/compute_core/schur_update_unit.sv`
    - Modify: `v/gbp_pe/compute_core/damping_unit.sv`

- [ ] **Step 1.6: 扩展 `gbp_compute_core_test.cc` 覆盖 SE2/SE3 / degree=10 / 正则化 / 阻尼**
  - Files:
    - Modify: `nocbp_verilator/tests/unit/gbp_compute_core_test.cc`
  - Reference: `docs/gbp_pe/verification/unit_tests/09_compute_unit.md`
  - Test command:
    ```bash
    cd nocbp_verilator
    make run LEVEL=unit TEST=gbp_compute_core_test
    ```

**退出标准：** `gbp_compute_core_test` 全部通过；`docs/gbp_pe/verification/unit_tests/09_compute_unit.md` 中所有 REQUIRED test cases 都有对应 C++ case。

---

## Phase 2: `compute_unit_wrapper` 与 Stream 对接（1–2 周）

**目标：** wrapper 能把 stream descriptor 转成 operand beats，并把 `gbp_core_rsp_t` 转成 writeback record。

- [ ] **Step 2.1: 实现/修复 `compute_unit_wrapper.sv`**
  - Files:
    - Modify: `v/gbp_pe/compute_core/compute_unit_wrapper.sv`
  - Reference: `docs/gbp_pe/08_NEW_COMPUTE_UNIT.md` §24

- [ ] **Step 2.2: 实现/修复 `writeback_packer.sv`**
  - Files:
    - Modify: `v/gbp_pe/compute_core/writeback_packer.sv`
  - 关键点：
    - `WB_MSG` / `WB_BELIEF` payload order 与 08 §22 一致
    - residual 放入 `WB_BELIEF` 但不写入 SPM STATE

- [ ] **Step 2.3: 更新 `compute_unit.cc` 单元测试到 v0.7**
  - Files:
    - Modify: `nocbp_verilator/tests/unit/compute_unit.cc`
  - Test command:
    ```bash
    cd nocbp_verilator
    make run LEVEL=unit TEST=compute_unit
    ```

**退出标准：** `compute_unit` 单元测试通过；wrapper 能完成一次完整的 `OP_BELIEF` + `OP_MSG_F2V` 并产生正确 writeback。

---

## Phase 3: Compute Subsystem 集成（1–2 周）

**目标：** `gbp_pe_compute_subsystem` 能把控制命令翻译成 `cu_cmd_t`，调度 read/write stream engine，拿到结果。

- [ ] **Step 3.1: 更新 `gbp_pe_compute_subsystem.sv` 使用新 wrapper**
  - Files:
    - Modify: `v/gbp_pe/gbp_pe_compute_subsystem.sv`
  - 关键点：
    - 移除旧 `compute_unit.sv` / `gbp_compute_engine.sv` 实例
    - 接入 `compute_unit_wrapper` + `gbp_compute_core`
    - 把 PE-level `cmd_node_id`, `cmd_is_factor`, `cmd_dof`, ... 翻译成 `cu_cmd_t`

- [ ] **Step 3.2: 更新 `read_stream_engine.sv` / `write_stream_engine.sv` 接口匹配 v0.7**
  - Files:
    - Modify: `v/gbp_pe/read_stream_engine.sv`
    - Modify: `v/gbp_pe/write_stream_engine.sv`

- [ ] **Step 3.3: 更新 `gbp_pe_compute_subsystem.cc` 测试**
  - Files:
    - Modify: `nocbp_verilator/tests/unit/gbp_pe_compute_subsystem.cc`
  - Reference: `docs/gbp_pe/verification/subsystem_tests/01_compute_subsystem.md`
  - Test command:
    ```bash
    cd nocbp_verilator
    make run LEVEL=unit TEST=gbp_pe_compute_subsystem
    ```

**退出标准：** `gbp_pe_compute_subsystem` 测试通过，包含 factor node（Test Case 2）和多 DOF（Case 4）。

---

## Phase 4: Control + Fetch + Memory 子系统协同（1–2 周）

**目标：** 完整 PE 能跑通 `notification → fetch → response → compute → writeback`。

- [ ] **Step 4.1: 确保 `gbp_pe_control_subsystem` 与新 compute subsystem 握手**
  - Files:
    - Modify: `v/gbp_pe/gbp_pe_control_subsystem.sv`
    - Modify: `v/gbp_pe/node_scheduler.sv`
    - Modify: `v/gbp_pe/metadata_scanner.sv`
  - Test command:
    ```bash
    cd nocbp_verilator
    make run LEVEL=unit TEST=control_subsystem
    ```

- [ ] **Step 4.2: 更新 integration tests 到 v0.7**
  - Files:
    - Modify: `nocbp_verilator/tests/integration/full_pull_cycle.cc`
    - Modify: `nocbp_verilator/tests/integration/multi_node_concurrent.cc`
    - Modify: `nocbp_verilator/tests/integration/new_pe_integration.cc`
  - Test commands:
    ```bash
    make run LEVEL=integration TEST=full_pull_cycle
    make run LEVEL=integration TEST=multi_node_concurrent
    make run LEVEL=integration TEST=new_pe_integration
    ```

**退出标准：** 所有 integration tests 通过。

---

## Phase 5: 完整 PE 单元与 mesh 2x2 系统测试（1–2 周）

**目标：** 单个 `gbp_pe` 与 2x2 mesh 能跑完一次迭代。

- [ ] **Step 5.1: 更新 `gbp_pe` 顶层集成**
  - Files:
    - Modify: `v/gbp_pe/gbp_pe.sv`

- [ ] **Step 5.2: 更新 `13_gbp_pe.md` 对应 C++ 测试**
  - Files:
    - Modify: `nocbp_verilator/tests/unit/gbp_pe.cc`（如存在）或新建
  - Reference: `docs/gbp_pe/verification/unit_tests/13_gbp_pe.md`

- [ ] **Step 5.3: 实现/更新 mesh 2x2 top 与 `01_mesh_2x2_gbp_interconnect.md` 测试**
  - Files:
    - Modify: `nocbp_verilator/tops/system/mesh_2x2_gbp_top.sv`（如存在）或新建
    - Modify: `nocbp_verilator/tests/system/01_mesh_2x2_gbp_interconnect.cc`（如存在）或新建
  - Reference: `docs/gbp_pe/verification/system_tests/01_mesh_2x2_gbp_interconnect.md`

**退出标准：** mesh 2x2 测试通过，消息路径正确。

---

## Phase 6: 算法正确性门 / Golden Reference（2–3 周）

**目标：** RTL 输出与 Python FP32 参考在 tolerance 内一致。

- [ ] **Step 6.1: 准备 4-node GBP Python oracle**
  - Files:
    - Create/Modify: `nocbp_verilator/tests/oracle/gbp_4node_reference.py`
  - Reference: `docs/gbp_pe/verification/system_tests/02_gbp_algorithm_golden_reference.md`

- [ ] **Step 6.2: 实现 `02_gbp_algorithm_golden_reference` 测试**
  - Files:
    - Create/Modify: `nocbp_verilator/tests/system/02_gbp_algorithm_golden_reference.cc`
  - Test command:
    ```bash
    cd nocbp_verilator
    make run LEVEL=system TEST=gbp_algorithm_golden_reference
    ```

**退出标准：** `|hw_mu - ref_mu| < 1e-4`，`lam` 对角线误差 `< 1e-3`。

---

## 依赖关系

```
Phase 0 ──► Phase 1 ──► Phase 2 ──► Phase 3 ──► Phase 4 ──► Phase 5 ──► Phase 6
               ↑             ↑             ↑
               │             │             │
          08 spec       08 §24      subsystem docs
```

**当前最大阻塞点：** `docs/gbp_pe/verification/README.md` 已标注——factor node（Compute Subsystem Test Case 2）未实现，会阻塞 Phase 6 算法正确性门。

---

## 建议的下一步（立即开始）

1. **Phase 0.2**：跑 lint，拿到当前 RTL 基线。
2. **Phase 1.1**：检查 `gbp_compute_core.sv` 接口与 08 规范的差距（`ctx_id`、`op_id`、struct 类型）。
3. 根据 Phase 0 结果，决定是否先补接口还是先修 `ldlt_solve_core` / `cavity_builder`。
