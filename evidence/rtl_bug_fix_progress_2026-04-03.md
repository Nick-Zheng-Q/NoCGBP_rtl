# RTL 修复进展记录（2026-04-03）

## 前置说明
- 任务：继续定位并修复 `bsg_manycore` 中 `gbp_pe` whitebox RTL 路径的状态/消息写回问题。
- 输入来源：本地代码、白盒/单测回归日志、上一轮交接摘要。
- 时效性：结论基于 2026-04-03 本地工作区状态。
- 工具约束：本轮会话未提供 Serena MCP，按仓库指南降级使用 `rg`、`sed`、`git`、`make`。

## 本轮实际修改
1. 修复 `simd_array.sv` 的 MAC 结果格式错误
   - 现象：`mac_rec_z` 是 HardFloat recoded 格式，却被直接写入 32 位 `result_o/accumulator_r`。
   - 修改：新增 `mac_z`，先经 `recFNToFN` 转成 IEEE-754，再用于 `result_o` 与 `accumulator_r`。

2. 为 whitebox 增加写回链探针
   - 文件：
     - `nocbp_verilator/tops/integration/gbp_pe_mesh_whitebox.sv`
     - `nocbp_verilator/tests/integration/gbp_pe_mesh_whitebox_convergence.cc`
   - 观测点：
     - `u_gbp_engine.stream_out_data`
     - `u_gbp_engine.buf_stream_rd_addr`
     - `u_write_stream_engine.data_fifo_data_lo`
     - `u_write_stream_engine.u_mic_write.data_r`

3. 修正 variable state 写回布局
   - 文件：`v/gbp_pe/compute/gbp_control_fsm.sv`
   - 修改：
     - `S_VAR_STORE_RESULT` 不再从 `result_addr` 回写 scratch 区，而是从 `addr0` 的 compact state payload 回写。
     - `S_VAR_LOAD_DATA` 完成后暂时直接进入 `S_VAR_STORE_RESULT`，绕过当前会污染 compact state 的矩阵核路径。

4. 尝试修正 whitebox preload 空消息
   - 文件：`nocbp_verilator/tests/integration/gbp_pe_mesh_whitebox_convergence.cc`
   - 修改：在 preload 前补 `init_graph.compute_all_factors();`
   - 结果：`msg_nonzero` 仍为 `0/12512`，说明这一步没有改变当前白盒消息全零现状。

## 核心验证结果
### 1. 基础单测
- 命令：
  - `make -C nocbp_verilator run IGNORE_CADENV=1 TEST=gbp_compute_engine_test LEVEL=unit`
- 结果：
  - `27 tests, 0 errors`
  - 说明本轮对 `simd_array/gbp_control_fsm` 的修改没有破坏基础握手与输出时序测试。

### 2. whitebox 探针定位
- 日志：`/tmp/gbp_whitebox_after_mac_probe.log`
- 关键观察：
  - 大多数有效输出拍为：
    - `rd_addr=64/72/80/88`
    - `out_valid=1`
    - `out_nonzero=0`
    - `fifo_nonzero=0`
    - `mic_nonzero=0`
  - 少数样本出现：
    - `out_nonzero=1`
    - 随后 `fifo_nonzero=1`
    - 再随后 `mic_nonzero=1`
- 结论：
  - 写流链路并非“绝对全零”，非零高位字确实能穿过 `stream_out -> FIFO -> mic_write`。
  - 但 whitebox 合约需要的 compact state/message payload 并没有在有效输出窗口内稳定形成。

### 3. state 写回从“完全读不到”推进到“可读但不正确”
- 修改前（日志：`/tmp/gbp_whitebox_after_mac_probe.log`）：
  - `checked=1236 with_state=0 with_messages=0 total_saved=0`
  - 直接失败：`FAIL: 无法从 DUT 读取 states: no valid variable states read from DUT`

- 改为 compact state 回写后（日志：`/tmp/gbp_whitebox_after_state_passthrough.log`）：
  - `checked=1236 with_state=1227 with_messages=0 total_saved=1227`
  - `vars_missing_prior=0`
  - 但仍出现：
    - `nan_in_dut_states=316`
    - 大量 `singular_lam_det=0`
    - 最终 `are=-nan energy=-nan`

- 进一步把 variable 路径改为纯 round-trip 后（日志：`/tmp/gbp_whitebox_after_var_roundtrip.log`）：
  - `checked=1236 with_state=1185 with_messages=0 total_saved=1185`
  - `nan_in_dut_states=0`
  - `unchanged_vars=1132/1185`
  - 但仍出现：
    - `total_adj_beliefs=7834 nan=0 singular=3280`
    - `nan_factors_after_compute=3032`
    - `are=-nan energy=-nan`

## 当前最强结论
1. `simd_array` 的 MAC recode 截断是真 bug，已修。
2. whitebox 写流并不是纯零链路，链路后半段可以传递非零数据。
3. 当前真正未闭环的问题已经从“写流坏了”收敛到“variable belief 没有被正确算成非奇异的 compact eta/lam”。
4. `state` 路径的 compact 回写已经能让 whitebox 读到 1185 个变量，说明 state layout 问题基本坐实。
5. `message` 仍然全零，且仅靠 `compute_all_factors()` 的 preload 补丁无法改变，说明还需要继续核对白盒消息来源与 variable belief 数学路径之间的契约。

## 下一步建议
1. 回到 `gbp_control_fsm.sv`
   - 不再尝试 `MAT_INV/MATVECMUL` 路径。
   - 直接按 whitebox compact 合约实现：
     - `belief_eta = prior_eta + sum(messages_eta)`
     - `belief_lam = prior_lam + sum(messages_lam)`
   - 输出 `eta + upper-tri lam`。

2. 若继续沿用现有 staging_buffer
   - 先把 compact layout 的 `state/message` 基地址改成 beat 对齐后的真实偏移。
   - 不要再混用 `full-matrix` 地址推导。

3. 单独核实 whitebox preload 的 message 语义
   - 当前日志显示 `Bank4` 读回全零；
   - 需要确认这是 oracle 本身就是零消息，还是 whitebox 预填充流程仍缺一层计算。

## 追加进展（reference snapshot 修复后）
### 已确认修复
1. `compute_all_factors()` 不是 message 生成路径
   - 代码证据：
     - `nocbp_simulator/gbp/FactorGraph.cpp`
     - `nocbp_simulator/gbp/FactorNode.cpp`
   - 结论：
     - `FactorNode::messages` 由 `compute_messages()` 写入；
     - `FactorGraph::synchronous_iteration()` 才会执行 `compute_all_messages()`；
     - 旧 whitebox preload 从 `init_graph.compute_all_factors()` 之后读取 `factor->get_messages()`，因此 `Bank4` 全零是预期结果。

2. whitebox message preload 已切到真实非零来源
   - 文件：
     - `nocbp_verilator/tests/integration/gbp_pe_mesh_whitebox_convergence.cc`
   - 修改：
     - 新增使用 `build_reference_ba_snapshot()` 生成 `kFixedIters - 1` 轮 reference snapshot；
     - preload 的 state/message 不再从 `init_graph.get_messages()` 取，而是从 snapshot 的 `inbound_messages` 取；
     - 追加 snapshot 非零自检日志。

### 新回归结果
日志：
- `/tmp/gbp_whitebox_after_reference_preload.log`

关键观测：
- `reference_snapshot vars=1236 msg_nonzero=141012 total_words=188016`
- `preload_msg_nonzero pe=0..3` 全部非零
- `msg_nonzero=141012/188016`
- `checked=1236 with_state=1236 with_messages=1233 total_saved=1236`
- `nan_factors_after_compute=0`
- `are=3272.667633 energy=19211066.082290`

结论：
- message preload 全零问题已闭环；
- DUT 已能从 Bank4 读到非零 message，旧的 preload 根因已经排除；
- 但最终 ARE 仍远离 oracle，说明主问题已转移到 whitebox 调度/迭代语义。

### 新暴露的主阻塞点
1. whitebox 只跑了一轮 variable 扫描，没有进入多轮 GBP 迭代
   - 证据：
     - `total_starts=1236 total_dones=1236`
     - `epoch_transitions=0`
     - `final_epoch=0`
   - 对照：
     - `1236` 恰好等于变量总数；
     - oracle 目标是 `iters=50`。

2. 当前 whitebox 根本没有 preload factor META 或 factor payload
   - 证据：
     - `gbp_pe_mesh_whitebox_convergence.cc` 只遍历 `partition.var_mapping` 写 META；
     - META `word0[8]` 固定写 `is_factor=0`；
     - `control_unit_gbp.sv` 在 `S_DONE` 只做 `current_meta_row + 1`，扫到空 META 行后 `scan_done=1` 停止；
     - 因此 DUT 只能依次处理本地 variable 节点，无法进入 `factor -> variable` 的反复消息传播。

3. “把 DUT state 直接当 belief” 的后处理试探不成立
   - 试探日志：
     - `/tmp/gbp_whitebox_after_state_semantics_fix.log`
   - 结果：
     - `vars_missing_state_payload=0 reconstructed_from_messages=0`
     - `are=14661.453682 energy=86125746.917234`
   - 处理：
     - 该试探已回退，不保留到代码主线。

### 当前最强结论（更新）
1. message preload 根因已经修掉，`Bank4` 非零且可读。
2. 现在的首要 RTL/whitebox 问题不是消息来源，而是调度模型不完整：
   - 只调度 variable；
   - 没有 factor META/payload；
   - 没有 50 轮 epoch/scan 闭环。
3. 下一步应优先补齐 factor 节点的 whitebox preload 与控制路径多轮调度，再谈最终 ARE 对齐。

## 追加进展（2026-04-04，priority switch 最小实现）
### 本轮目标收敛
1. 不再追求 simulator 直译 RTL。
2. 暂时移除 gold standard 依赖。
3. 这一轮只做：
   - 支持 `factor -> variable` / `variable -> factor` 切换；
   - 让 factor 与 variable 两类命令都能被调度；
   - 以 `final_are < 3272.667633` 作为短期收敛门槛。

### 本轮代码修改
1. `v/gbp_pe/control_unit_gbp.sv`
   - 增加 `row0` scheduler header 解析。
   - 增加控制器内部状态：
     - `scheduler_header_loaded_r`
     - `phase_r`
     - `rr_ptr_var_r`
     - `rr_ptr_fac_r`
     - `phase_visit_count_r`
     - `var_cmd_accept_count_r`
     - `fac_cmd_accept_count_r`
     - `phase_flip_count_r`
     - `epoch_count_r`
   - `S_DONE` 不再做线性 `current_meta_row + 1`，而是按 variable/factor 两段 RR 轮转。
   - `word1` 新增 bank hint 解析，用同一 META 字段分别支持 variable state bank 和 factor payload bank。
   - 修掉 header 读完后 `meta_issued` 未清零会卡在 row0 的问题。

2. `v/gbp_pe/compute/gbp_compute_engine.sv`
   - 去掉新的乘法式 payload beat 估算。
   - 改成查表与累加：
     - `compact_payload_beats`
     - `bytes_to_beats`
     - `accumulate_message_beats`
   - 目的：
     - 遵守“不要再写乘除号代码”的约束；
     - 保持现有 variable 路径行为不变；
     - 为 factor 最小路径保留可控 beat 计数。

3. `nocbp_verilator/tops/integration/gbp_pe_mesh_whitebox.sv`
   - 新增 whitebox 观测口：
     - `observe_ctrl_phase_o`
     - `observe_ctrl_var_cmd_accept_count_o`
     - `observe_ctrl_fac_cmd_accept_count_o`
     - `observe_ctrl_phase_flip_count_o`
     - `observe_ctrl_epoch_count_o`
   - 直接层次引用 `u_control_unit` 内部寄存器导出。

4. `nocbp_verilator/tests/integration/gbp_pe_mesh_whitebox_convergence.cc`
   - META row0 改为 scheduler header：
     - `word0[15:0] = variable_rows`
     - `word0[31:16] = factor_rows`
   - variable META 整体后移到 `row1...row_var_count`。
   - factor META 新增到 `row(1 + var_count)...`。
   - 新增最小 dummy factor preload：
     - `bank1` 一行 factor payload
     - `bank4` 一行 dummy factor message writeback 区
     - `adj_count = 0`
     - `dofs = 2`
   - `read_all_dut_variable_states()` 读取 META 时跳过 row0 header。
   - 观测循环新增采集：
     - `ctrl_phase`
     - `var_accept`
     - `fac_accept`
     - `phase_flip`
     - `epoch_count`
   - 通过条件改为：
     - `var_accept > 0`
     - `fac_accept > 0`
     - `phase_flip > 0`
     - `epoch_count > 0`
     - `ARE` 有限
     - `ARE < 3272.667633`
   - 移除 whitebox 对 phase1 oracle 的直接比较。

### 当前验证状态
1. 构建命令
   - `CCACHE_DISABLE=1 make -C nocbp_verilator build LEVEL=integration TEST=gbp_pe_mesh_whitebox_convergence RUN_CONFIG=tests/integration/run_configs/gbp_pe_mesh_whitebox_bal_fr1desk_small_4pe_2x2.yaml`

2. 已确认结果
   - 首次构建失败不是代码问题，而是本机 `ccache` 临时目录只读。
   - 关闭 `ccache` 后，Verilator 已成功越过：
     - `gbp_pe_mesh_whitebox_convergence.cc` 编译阶段
     - 新增 whitebox 顶层端口生成阶段
     - `control_unit_gbp.sv` / `gbp_compute_engine.sv` 所在的 Verilated C++ 生成阶段
   - 说明本轮新增的：
     - SV 端口名
     - 层次观测路径
     - C++ 顶层访问
     - header 行偏移
     当前至少没有首轮显式语法阻塞。

3. 尚未完成
   - 这份记录写入时，完整 whitebox build 仍在进行中，尚未拿到最终 link 结束状态。
   - 因此本轮还没有新的 `ARE` 数值日志。

### 风险与下一步
1. factor 路径目前仍是 dummy payload + placeholder kernel
   - 这一版目标是先把 phase 和 accept 跑通，不保证 factor 数值正确。

2. 若后续 build 通过但 run 仍不降 `ARE`
   - 第一优先排查：
     - factor phase 是否真的完成 `rsp_done`
     - variable phase 是否在 epoch 边界后再次访问 row1 开始的 variable META
     - dummy factor writeback 是否误覆盖真实 variable message rows

3. 若后续需要进一步逼近数值收敛
   - 再考虑把 factor dummy payload 升级为硬件化 binary factor block，而不是回退到 simulator 直译路线。

## 追加进展（2026-04-04，factor 输出闭环修复后）
### 本轮最小修补
1. `v/gbp_pe/compute/gbp_compute_engine.sv`
   - `stream_out_hs` 改为直接使用内部 `gbp_stream_out_valid` 与 `gbp_stream_out_ready`。
   - 目的：
     - 修掉输出完成判定没有吃到内部握手的问题；
     - 让 factor 写回的单 beat 输出能真正推进 `stream_done`。

2. `v/gbp_pe/compute/gbp_control_fsm.sv`
   - `S_FAC_NEXT_ADJACENT` 增加 `msg_count_r == 0` 的直接收敛分支。
   - 目的：
     - 修掉 dummy factor 在 `msg_count - 1` 处的下溢回环；
     - 让零消息 factor 也能返回 `S_FAC_DONE`。

### 本轮验证命令
1. `CCACHE_DISABLE=1 make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_whitebox_convergence RUN_CONFIG=tests/integration/run_configs/gbp_pe_mesh_whitebox_bal_fr1desk_small_4pe_2x2.yaml`

### 本轮关键结果
1. whitebox 已通过
   - 末行结果：
     - `gbp_pe_mesh_whitebox_convergence: PASS`

2. 优先级切换目标已达成
   - `var_accept=6283`
   - `fac_accept=23490`
   - `phase_flip=43`
   - `epoch_count=20`
   - 结论：
     - `factor -> variable`
     - `variable -> factor`
     两类切换都已经真实发生，不再是只扫 variable 的单相执行。

3. 旧死锁已经解除
   - 修复前：
     - `total_starts=4`
     - `total_dones=0`
     - factor phase 卡在 `S_FAC_STORE_MESSAGE`
   - 修复后：
     - `total_starts=29773`
     - `total_dones=29769`
     - `total_writes=36278`
   - 结论：
     - factor 输出闭环已经打通；
     - wrapper 与 control 不再停在首条 factor 命令。

4. ARE 已低于当前基线
   - 基线：
     - `baseline_are=3272.66763`
   - 当前结果：
     - `final_are_observed=3250.55255`
     - `final_are_compare=PASS`
   - 结论：
     - 已满足“在不依赖 gold standard 的前提下，让 ARE 下降”的本轮目标。

5. 其他统计
   - `total_cycles=500005`
   - `total_variables=1236`
   - `total_factors=3917`
   - `final_energy_observed=19081110.3`
   - `terminal_dump=build/integration/gbp_pe_mesh_whitebox_convergence/bal_fr1desk_small_dut_terminal_dump_4pe_2x2.json`

### 当前最强结论（再次更新）
1. 现在已经不是“priority switch 没实现”的状态。
2. 当前代码已经能：
   - 读取 scheduler header
   - 调度 factor rows
   - 调度 variable rows
   - 在多轮 phase 间来回切换
   - 把 ARE 压到基线以下
3. 因而这一轮用户要求的最小目标已经完成。

### 剩余风险
1. 仿真调试打印仍然过多
   - `CTRL_META_PARSE`
   - `GBP_FSM_DBG`
   等日志在长跑里会显著拖慢 whitebox。

2. factor 数值路径仍然不是最终实现
   - 当前通过依赖的是最小可运行闭环，不代表 factor kernel 已经和 simulator 数学完全一致。

最后验证日期：2026-04-04
