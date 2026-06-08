# PE Unit Test 计划（RTL 版本）

> 版本：2026-06-04（ subsystem 架构修订版）
> 架构：`noc_adapter` + `gbp_pe_memory_subsystem` + `gbp_pe_control_subsystem` + `gbp_pe_compute_subsystem` + `gbp_pe_fetch_subsystem` + `pull_server`

---

## 1. 目标

- 将 PE 按 **4 个 subsystem + noc_adapter + pull_server** 拆分为可独立验证的单元。
- 底层模块（AGU、RSE、WSE、arbiter 等）独立验证。
- 以最小依赖的 testbench 验证 subsystem 行为，不依赖完整系统集成。
- 先底层模块稳定，再 subsystem 集成，最后顶层 `gbp_pe` 集成测试。

## 2. 范围假设

- PE 通过 `noc_adapter` 对接 NoC，替换旧 `bsg_manycore_endpoint_standard`。
- PE 内部实现与 `nocbp_simulator/pe/ProcessingElement.*` 语义一致。
- Unit test 使用可控的假 NoC/NI 驱动（只需送/收 `noc_adapter` 信号）。
- Subsystem wrapper 文件（`gbp_pe_*_subsystem.sv`）作为集成测试的 DUT。

## 3. 测试层级

```
Layer 3: gbp_pe 顶层集成（待实现）
Layer 2: 4 个 subsystem 集成（部分实现，待补充）
Layer 1: 底层模块单元（大部分已实现，4 个待修复）
```

---

## 4. Layer 1: 底层模块测试

### 4.1 已验证通过（12 个模块）

| 模块 | 文件 | 用例数 | 覆盖场景 |
|------|------|--------|----------|
| AGU | `agu.cc` | 3 | Normal sequence, backpressure, single word |
| Metadata Scanner | `metadata_scanner.cc` | 1 | Single node scan |
| Neighbor State Accumulator | `neighbor_state_accumulator.cc` | 4 | Local/remote/mixed, backpressure |
| NoC Adapter | `noc_adapter.cc` | 4 | Incoming/outgoing, credit stall |
| Node Scheduler | `node_scheduler.cc` | 3 | Round-robin, empty queue, wrap-around |
| Phase Controller | `phase_controller.cc` | 3 | Normal switch, continuous, reset during switch |
| Pull Client | `pull_client.cc` | 2 | Normal fetch, backpressure |
| Pull Server | `pull_server.cc` | 2 | Normal response, backpressure |
| Response Collector | `response_collector.cc` | 2 | Normal collection, backpressure |
| Scoreboard Prefetcher | `scoreboard_prefetcher.cc` | 3 | Multiple edges, local edge, node readiness |
| Writeback Controller | `writeback_controller.cc` | 3 | 2 remote, mixed local/remote, backpressure |
| GBP Compute Engine | `gbp_compute_engine_test.cc` | 27 | Variable/factor, 1-6 DOF, stream completion, backpressure |

### 4.2 编译失败待修复（4 个模块）

| 模块 | 文件 | 根因 | 修复方案 |
|------|------|------|----------|
| Compute Unit | `compute_unit.cc` | `BEAT_BITS=64` 后 Verilator 生成标量 `QData`，旧代码用数组下标 | 改为标量访问 |
| Read Stream Engine | `read_stream_engine_top.cc` | 同上 | 同上 |
| SPM Arbiter | `spm_arbiter.cc` | `wr_data_N` 从 struct 变为 `QData`，旧代码访问 `.data` 成员 | 改为直接访问标量 |
| Write Stream Engine | `write_stream_engine_top.cc` | 编译超时（可能也是语法问题） | 先修复语法，再排查超时 |

**修复原则：** 这 4 个模块的 RTL 接口已随 `BEAT_BITS=64` 稳定，只需同步更新 C++ 测试代码中的数据访问方式。

---

## 5. Layer 2: Subsystem 集成测试

### 5.1 测试状态总览

| Subsystem | 当前用例 | 状态 | 待补充（P0 优先） |
|-----------|----------|------|------------------|
| Memory | 4 | ✅ PASS | 写后读一致性、全0 wstrb、8 client 并发、地址边界 |
| Control | 3 | ✅ PASS | 多 edge 序列、adj_last、local state reader、factor/variable priority |
| Compute | 2 | ✅ PASS | **factor node**、多 DOF、msg_count>1、batch_done、数值正确性 |
| Fetch | 3 | ✅ PASS | **response 完整路径**、多 entry 去重、scoreboard 满、node_ready 位图 |

### 5.2 Memory Subsystem 用例

**已实现：**
1. `test_write_read_client0` — 单 client 写后读基础通路
2. `test_partial_wstrb` — 字节级写使能验证
3. `test_concurrent_reads` — 2 个 client 同时读不同 bank
4. `test_bank_conflict` — 2 个 client 读同一 bank 的仲裁时序

**待补充：**
5. `test_write_then_read_same_addr` — 写后读一致性（同一地址写 0xDEAD，读回确认）
6. `test_zero_wstrb` — wstrb=0 时不应改变内存内容
7. `test_all_clients_concurrent` — 8 个 client 同时访问，验证仲裁公平性
8. `test_max_address_boundary` — 地址接近 2^18-1 时的映射正确性
9. `test_burst_write` — write_stream_engine 连续多 beat 写（WSE 集成验证）
10. `test_burst_read` — read_stream_engine 连续多 beat 读（RSE 集成验证）

### 5.3 Control Subsystem 用例

**已实现：**
1. `test_cmd_production` — adjacency 输入产生 compute command
2. `test_backpressure` — adj_valid 在 adj_ready=0 时保持
3. `test_phase_switch` — 1024 个节点 visited 后 phase 切换

**待补充：**
4. `test_multi_edge_sequence` — 连续发送 3 个 adjacency，验证 cmd 连续产生且 node_id 正确
5. `test_adj_last_handling` — adj_last=1 时验证 scanner 正确结束当前节点
6. `test_local_state_reader` — local adjacency 触发本地 SPM 读取，验证 state 数据输出到 accumulator
7. `test_factor_variable_priority` — 同时有 factor 和 variable ready 时，按 priority phase 选择
8. `test_empty_queue_idle` — 无可计算节点时，subsystem 保持空闲，无异常输出

### 5.4 Compute Subsystem 用例

**已实现：**
1. `test_simple_compute` — variable node, DOF=1, state_words=8, 完成计算并断言 done_valid
2. `test_backpressure` — ns_ready=0 时 ns_valid 保持，done 不过早断言

**待补充（P0）：**
3. `test_factor_node_compute` — **factor 节点计算**，验证 S_FAC_LOAD_DATA → S_FAC_LOOP_INIT → ... → S_FAC_DONE 完整路径
4. `test_multi_dof` — DOF=2,3,4,5,6 各验证一次 compact_payload_beats 和 stream_target_beats 匹配
5. `test_multi_adjacent` — msg_count=3（3 个 adjacent node），验证累积计算完成

**待补充（P1）：**
6. `test_batch_done` — 验证 batch_done_o 在合适时机断言
7. `test_staging_mode` — RSE1 staging 读使能后，验证 STATE + STAGING 双路数据输入
8. `test_numerical_correctness` — 给定已知输入，验证计算结果数值（简化：identity belief）

### 5.5 Fetch Subsystem 用例

**已实现：**
1. `test_remote_fetch` — remote adjacency + notification → tx_fetch_req_valid
2. `test_local_filtered` — local adjacency 不产生 fetch request
3. `test_backpressure` — tx_fetch_req_ready=0 时 valid 保持

**待补充（P0）：**
4. `test_response_full_path` — **模拟完整的 fetch response**：rx_fetch_resp_valid → response_collector → SPM write → remote_valid_o 断言
5. `test_multi_entry_dedup` — 同一 neighbor_id 出现 3 次，只应产生 1 个 fetch request

**待补充（P1）：**
6. `test_scoreboard_full` — 连续注入 `SCOREBOARD_DEPTH` 个 remote adjacency，验证 scoreboard_full 阻塞
7. `test_notification_mismatch` — notification 的 source_node_id 不匹配任何 adjacency，应被忽略
8. `test_staging_batch_closed` — staging_batch_closed=1 时，新的 fetch request 应被阻塞
9. `test_node_ready_bitmap` — 验证 1024-bit node_ready_o 在 local edge 和 remote edge ready 时精确置位
10. `test_reset_clear_edge` — reset_valid 清除特定 consumer_node 的所有 edges

---

## 6. Layer 3: gbp_pe 顶层集成测试（待规划）

顶层 `gbp_pe.sv` 集成 noc_adapter + 4 subsystems + accumulator + writeback_controller + pull_server。

**第一阶段（基础握手）：**
- 单个 NoC 写请求进入，验证 SPM 数据正确写入
- 单个 NoC 读请求进入，验证返回数据正确

**第二阶段（端到端 GBP 单步）：**
- 注入一个 variable node 的 adjacency（local + remote）
- 注入 notification 激活 remote neighbor
- 验证 fetch request 发出 → 模拟 fetch response 返回 → 计算完成 → writeback 发出

**第三阶段（多节点收敛）：**
- 2x2 mesh 上运行简化 GBP 图
- 验证多轮 superstep 后 belief 收敛

---

## 7. 实施优先级

### Phase 1: 文档与基础设施（当前）
- [x] 更新本测试计划文档
- [ ] 修复 4 个编译失败的底层模块测试
- [ ] 运行全部 Layer 1 测试，确保 100% 通过

### Phase 2: Subsystem P0 补充
- [ ] Compute: factor node + 多 DOF + msg_count>1
- [ ] Fetch: response 完整路径 + 去重
- [ ] Control: 多 edge 序列 + local state reader
- [ ] Memory: 写后读一致性 + 全0 wstrb

### Phase 3: Subsystem P1 完善
- [ ] 剩余边缘情况（见 5.2~5.5 待补充列表）
- [ ] 所有 subsystem 达到 8+ 用例

### Phase 4: 顶层集成
- [ ] gbp_pe 基础 NoC 握手测试
- [ ] gbp_pe 端到端单步 GBP 测试
- [ ] gbp_pe 多节点收敛测试

---

## 8. 期望输出与检查点

- 每个用例给出：输入 stimulus、期望输出序列、周期对齐条件。
- 每个 suite 输出：pass/fail + 关键 trace（如计算次数、切换次数、fetch 次数）。
- 所有编译失败的旧测试修复后，CI 应保证 **零编译失败**。
