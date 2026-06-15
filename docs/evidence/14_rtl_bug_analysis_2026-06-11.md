# RTL Bug 分析与 Spec 出入

> 版本：2026-06-11
> 状态：待修复
> 关联：TC3（Variable Node Identity）、TC4（Factor→Variable End-to-End）

---

## 1. 失败现象

| 测试 | 描述 | 期望 | 实际 |
|------|------|------|------|
| TC3 | Variable Node, DOF=1, adj_count=0 | mu = inv(3.0) * 2.0 = 0.667 | mu = 1.0 |
| TC4 | Factor→Variable, DOF=2, adj_count=1 | mu ≈ [0.75, 1.2] | mu = [28.0, ...] |

---

## 2. 时序追踪（TC3）

从 debug 输出追踪：

```
Cycle T:   S_IDLE → S_VAR_LOAD_DATA
Cycle T+1: S_VAR_LOAD_DATA → S_VAR_INVERT_LAM
           STG_WR: addr=0 data=4040000040000000  ← stream 写入 {eta=2.0, lam=3.0}
           STG_SIMD: lane=0 addr=1 data=3f800000 ← SIMD 覆盖 staging[1] = 1.0!
```

FSM 在 LOAD 阶段只停留了 1 拍。stream 写入和 SIMD 写入发生在同一拍。SIMD 写（旧数据 1.0f）覆盖了 stream 写的新数据（3.0f）。

---

## 3. Bug 列表

### Bug 1：`stream_target_beats` 在 `start_input_stream` 那拍为 0

**位置**：`v/gbp_pe/compute/gbp_compute_engine.sv`

**根因**：
```systemverilog
assign state_target_beats = bytes_to_beats(gbp_stream_xfer_bytes);
assign start_input_stream = !stream_active_r && (gbp_stream_req_state || gbp_stream_req_messages);
```

`gbp_stream_xfer_bytes` 是 FSM 的组合输出。在 `start_input_stream` 触发那拍，FSM 刚从 IDLE 转到 LOAD，`gbp_stream_xfer_bytes` 还是 0（IDLE 时默认值）。所以 `stream_target_beats=0`，`stream_done` 立即触发。

**影响**：FSM 立即离开 LOAD，stream 数据实际上没有被处理。

**修复方向**：`state_target_beats` 需要在 `start_input_stream` 那拍就有正确值。可以：
- 用 `cmd_state_words_i` 直接计算（不经过 FSM 的 `stream_xfer_bytes`）
- 或延迟 `start_input_stream` 一拍

---

### Bug 2：`mat_cmd_valid_r` 残留

**位置**：`v/gbp_pe/compute/gbp_control_fsm.sv`

**根因**：
```systemverilog
mat_cmd_valid_r <= mat_cmd_valid_next;
```

当矩阵 FSM 完成（`mat_done_r=1`）后，`mat_cmd_valid_r` 应该被清除。否则当下一个状态（如 INVERT_LAM）检查 `mat_cmd_ready` 时，旧的 `mat_cmd_valid_r` 触发新操作。

**影响**：矩阵操作在错误时机启动，用旧参数执行。

**修复方向**：在 `mat_done_r=1` 时清除 `mat_cmd_valid_r`：
```systemverilog
if (mat_done_r)
    mat_cmd_valid_r <= 1'b0;
else
    mat_cmd_valid_r <= mat_cmd_valid_next;
```

---

### Bug 3：stream 和 SIMD 写冲突

**位置**：`v/gbp_pe/compute/staging_buffer.sv`

**根因**：

staging buffer 有两个写源：
- stream 写：`stream_wr_valid`（来自 gbp_compute_engine 的 stream 路径）
- SIMD 写：`simd_wr_valid`（来自 matrix_fsm 的计算结果）

当两者在同一拍写同一地址时，SIMD 写覆盖 stream 写（因为 SIMD 写在 always_ff 块中排在后面）。

**影响**：stream 写入的 prior 数据被 SIMD 写覆盖，矩阵操作读到错误数据。

**修复方向**：
- 确保 stream 写和 SIMD 写不在同一拍发生（修复 Bug 1 后自然解决）
- 或在 staging buffer 中添加写冲突检测

---

### Bug 4：Factor 路径 `stream_xfer_bytes` 不一致

**位置**：`v/gbp_pe/compute/gbp_control_fsm.sv` + `v/gbp_pe/compute/gbp_compute_engine.sv`

**根因**：

FSM 设置：
```systemverilog
S_FAC_LOAD_DATA: stream_xfer_bytes = cmd_state_words * 4;
```

Engine 计算：
```systemverilog
state_target_beats = bytes_to_beats(gbp_stream_xfer_bytes);
```

但 `cmd_state_words` 是 FSM 的输入，`stream_xfer_bytes` 是 FSM 的输出。两者通过不同的路径传递，可能不一致。

**影响**：Factor 数据加载可能不完整。

**修复方向**：统一使用 `cmd_state_words` 计算 `stream_target_beats`。

---

## 4. Spec 出入

| # | Spec 描述（`06_PE_CONTROL_FLOW.md` §3.5） | RTL 实际 | 出入类型 |
|---|------------------------------------------|---------|---------|
| 1 | stream 写完成后矩阵才读取 staging buffer | 同一拍发生，数据竞争 | 时序违反 |
| 2 | `stream_target_beats` 在 stream 开始时就有正确值 | 依赖延迟一拍的寄存器 | 时序违反 |
| 3 | 矩阵命令只在显式发出时才有效 | `mat_cmd_valid_r` 可能遗留 | 逻辑错误 |
| 4 | Variable path: LOAD → ACCUMULATE → INVERT → MVMUL → STORE | LOAD 直接跳到 INVERT（因为 Bug 1） | 流程违反 |
| 5 | Factor 的 `stream_xfer_bytes = state_words * 4` | `stream_xfer_bytes` 和 `state_words` 独立计算 | 接口不一致 |

---

## 5. 修复优先级

| 优先级 | Bug | 原因 |
|--------|-----|------|
| P0 | Bug 1: `stream_target_beats` 时序 | 所有 stream 数据加载依赖此 |
| P0 | Bug 2: `mat_cmd_valid_r` 残留 | 矩阵操作错误启动 |
| P1 | Bug 3: stream/SIMD 写冲突 | 修复 Bug 1 后可能自然解决 |
| P2 | Bug 4: Factor stream 计算 | 影响 factor 路径 |

---

## 6. 修复后的预期时序（TC3）

```
Cycle T:   S_IDLE → S_VAR_LOAD_DATA, start_input_stream
Cycle T+1: stream_active_r = 1, cmd_stream_xfer_bytes_r = 8
Cycle T+2: SPM read data arrives, stream_in_hs fires, staging buffer write
Cycle T+3: stream_done fires (1 beat), FSM → S_VAR_INVERT_LAM
Cycle T+4: mat_cmd_valid_r = 1, MAT_INV command issued
Cycle T+5: MAT_INV reads staging buffer (correct data: lam=3.0)
Cycle T+6: MAT_INV result: inv(3.0) = 0.333
Cycle T+7: MAT_VEC_MUL: mu = 0.333 * 2.0 = 0.667
Cycle T+8: STORE result to SPM
```
