# 剩余测试隐患与 Direction C 规划

> **记录日期**: 2026-06-09
> **关联提交**: `d2e9a34e` (Reverse CSR + spec rewrite + system test pass)
> **当前状态**: Direction B (2×2 mesh functional) ✅ PASS; Direction C 🟡 Planned

---

## 一、剩余测试隐患 / 未解决问题

### 🔴 1.1 预存单元测试失败（非 Reverse CSR 引入）

| 测试 | 失败数 | 根因 | 修复优先级 |
|------|--------|------|-----------|
| `metadata_scanner` (unit) | 3/5 | `HEADER_WORDS` 从 4→2 迁移时，fake SPM 数据布局未同步更新 | P1 |
| `scoreboard_prefetcher` (unit) | 4/8 | 同上；`adj_should_register` 逻辑与旧测试期望不一致 | P1 |

**影响**: 这两个模块是 GBP 核心数据路径。单元测试失败不代表 RTL 功能错误（system test 通过证明端到端工作），但意味着模块级 corner case 覆盖不完整，后续重构时缺乏回归保护。

**修复路径**:
1. 更新 `metadata_scanner` fake SPM 的 header 字宽（从 4-word 改为 2-word）
2. 更新 `scoreboard_prefetcher` 测试的 adj_count 和 node_id 映射
3. 重新验证 `adj_should_register` 在重复 SCAN 场景下的行为

---

### 🟡 1.2 测试覆盖度缺口

| 缺口 | 当前状态 | 风险 | 建议补充测试 |
|------|----------|------|-------------|
| **多邻居节点** | 2×2 mesh 中每个节点仅 1 个邻居 | 无法验证 `rev_len > 1` 的 entry stream、无法验证 scoreboard 多 edge 并行 | 添加 4-node fully-connected 子图到 mesh 测试 |
| **Hash 冲突** | 使用 hash=0，无冲突路径 | `S_CHECK_KEY` 的 probe 逻辑未验证 | 添加故意构造的冲突 key（同一 hash bucket 中放 2 个不同 key） |
| **Queue 溢出** | `PENDING_Q_DEPTH=16`，测试中仅 1 个通知 | 高 degree 节点或通知突发时可能丢节点 | 添加 >16 个通知同时到达的 stress test |
| **多轮迭代** | 仅验证单轮 GBP (factor→variable→通知回 PE0) | 无法验证 phase 切换稳定性、visited_mask 清除正确性、round-robin 交替 | 运行 ≥3 轮完整迭代，验证 beliefs 收敛趋势 |
| **边界 SPM 地址** | Reverse CSR 在固定基地址 | 若 graph 更大，RevEntry 数组可能与其他区域重叠 | 添加 SPM 地址空间检查（初始化时断言各区域不重叠） |

---

### 🟡 1.3 已知 RTL 警告（功能正确，但需跟踪）

| 警告 | 来源 | 影响 | 处理建议 |
|------|------|------|---------|
| `cud0 error: counter overflow at time 0` | `noc_adapter.ep_std.cud0` DPI 计数器 | 所有使用 noc_adapter 的测试均有；时间戳为 0 时计数器初始化/复位时序问题 | 标记为 **won't fix (DPI artifact)**；若迁移到真实仿真器（VCS/Xcelium）应消失 |
| Verilator `UNOPTFLAT` / `VARHIDDEN` | `gbp_pe` 等大型模块 | 已用 `-Wno` 屏蔽；可能影响仿真速度 | 在 PPA 优化阶段重新评估 |

---

### 🟢 1.4 已修复但需持续验证的问题

| 问题 | 修复位置 | 验证方式 |
|------|----------|---------|
| SPM 地址重叠 | `reverse_index_lookup.sv` 默认参数 | 系统测试通过；建议添加 SPM 布局断言 |
| 线性探针越界 | `reverse_index_lookup.sv` `S_CHECK_KEY` | 代码审查覆盖；建议补充 hash 冲突测试 |
| `rx_notif_ready_o` 时序 | `reverse_index_lookup.sv` 输出赋值 | 系统测试通过；建议补充单周期通知脉冲测试 |
| `phase_factor_r` 振荡 | `phase_controller.sv` 边沿检测 | `phase_scheduling` integration test 通过；`gbp_pe_control_subsystem` unit test 通过 |
| pending_queue 静默溢出 | `node_scheduler.sv` 添加 `pending_q_overflow_o` | 当前无测试触发；建议添加 overflow 指示测试 |

---

## 二、Direction C（全系统算法验证）规划

### 2.1 Direction 定义回顾

| Direction | 目标 | 当前状态 |
|-----------|------|----------|
| **A** | Single-PE whitebox: 单 PE 端到端功能正确 | 🟡 `gbp_pe` unit test 编译极慢，未完整验证 |
| **B** | 2×2 mesh functional: 多 PE 互联、NoC 路由、握手正确 | ✅ **PASS** (`mesh_2x2_gbp_interconnect`) |
| **C** | Golden reference: 硬件计算的 belief 数值与 Python FP32 参考一致 | 🟡 Document complete; RTL testbench + Python ref TBD |

---

### 2.2 Direction C 前**必须完成**的工作

> 这些是 Direction C 的**前置条件**。若跳过，Direction C 失败时无法定位问题是算法错误还是基础设施错误。

#### P0 — Direction A 必须通过

Direction A (`gbp_pe` single-PE top-level whitebox) 是 Direction C 的最小可行验证单元。如果单 PE 都无法产生正确的 belief，mesh 中的数值正确性无从谈起。

- [ ] `gbp_pe` unit test 完整编译并通过（当前 Verilator 编译超过 5 分钟，可能需 `-O0` 或拆分模块）
- [ ] 单 PE 完整 pipeline：SCAN → FETCH → COMPUTE → WRITEBACK → NOTIFICATION
- [ ] 验证单 PE 的 STATE 输出值在数学上合理（至少符号/数量级正确）

#### P1 — 修复预存单元测试失败

`metadata_scanner` 和 `scoreboard_prefetcher` 的失败会掩盖真实的算法错误。Direction C 的 golden reference 对比需要这两个模块 100% 可靠。

- [ ] `metadata_scanner` unit test 5/5 PASS
- [ ] `scoreboard_prefetcher` unit test 8/8 PASS

#### P2 — Compute Unit 基础数值验证

在Direction C的全系统对比之前，先验证 `compute_unit` / `gbp_compute_engine` 的单个操作正确：

- [ ] Variable belief: `eta_sum = prior_eta + Σ(msg_eta)` — 至少验证 1-DOF 和 2-DOF
- [ ] Factor message: Schur complement extraction — 验证 2×2 矩阵分块正确
- [ ] Damping: `msg_damped = 0.3 * msg_old + 0.7 * msg_new` — 验证混合比例
- [ ] FP32 精度: 与 `numpy.float32` 逐位对比（允许 rounding error）

**建议**: 在 Direction A 的 testbench 中注入已知输入（ handcrafted eta/lam），读取输出并与手算结果对比。这比直接上 Direction C 更快定位问题。

---

### 2.3 与 Direction C **重合**的工作

> 这些工作既属于 Direction C 本身，也是 Direction B 的延伸。可以**并行或复用**，不必等 Direction C 开始才做。

#### ✅ 重合 1: SPM 数据布局与初始化

Direction B 的 `mesh_2x2_gbp_interconnect.cc` 已经定义了完整的 SPM 初始化格式（NodeHeader + AdjEntry + State + Reverse CSR）。Direction C 直接使用相同的数据布局，只是 State 中的 prior eta/lam 值从 "占位符" 变为 "有数学意义的数值"。

**复用方式**:
- 将 `init_spm_for_full_iteration()` 改造为接受 `graph_config` 结构体
- Direction C 的 Python ref 和 RTL testbench 共用同一个 `graph_config` JSON

#### ✅ 重合 2: 4-Node Mesh 拓扑

Direction C Case 2 明确声明使用与 Direction B 相同的 4-node mesh 拓扑（N0→N1/N3, N2→N3）。Direction B 已经验证了 NoC 路由、FETCH/RESPONSE 流程、NOTIFICATION 回环。

**复用方式**:
- Direction C 的 RTL testbench 可以直接继承 `mesh_2x2_gbp_top.sv`
- 只需替换 SPM 初始化数据（有数学意义的 prior）和增加 belief 读取逻辑

#### ✅ 重合 3: FETCH/RESPONSE 数据路径

Direction B 验证了 FETCH_REQUEST 能自动生成、FETCH_RESPONSE 能正确路由、state 能被写入 STAGING。Direction C 需要在此基础上验证：**写入 STAGING 的数值与 golden reference 期望的输入数值一致**。

**复用方式**:
- Direction B 的 `spm_read_word()` DPI 函数可直接用于 Direction C 读取最终 STATE
- 增加 `spm_read_fp32()` 辅助函数，将 32-bit word 解释为 IEEE-754 FP32

#### ✅ 重合 4: 多轮迭代框架

Direction B 目前只跑了单轮迭代（factor→variable）。Direction C 需要运行多轮（通常 10 轮或到收敛）。这个框架可以现在就开始搭建：

- [ ] 在 Direction B testbench 中增加 "连续触发多轮 compute" 的循环
- [ ] 每轮结束后读取所有 PE 的 STATE 区域
- [ ] 观察 beliefs 是否稳定（即使不对比 golden reference，也能验证不发散）

---

### 2.4 Direction C 专属工作（不与其他方向重合）

| 工作 | 说明 | 工作量估计 |
|------|------|-----------|
| Python golden reference | 实现 `gbp_reference.py`，支持 4-node chain 和 4-node mesh | 1-2 天 |
| FP32 逐对比逻辑 | Testbench 中读取 RTL STATE，与 Python 输出的 JSON 对比，容差 `|delta| < 1e-4` | 0.5 天 |
| Damping 验证 | 分别验证 damping=0.0（无阻尼）和 damping=0.3（RTL 默认） | 0.5 天 |
| Convergence 检测 | 实现 `|delta_mu| < 1e-4` 的自动判断 | 0.5 天 |
| 报告生成 | 输出 pass/fail 报告，包含每轮每节点的 belief 对比表 | 0.5 天 |

---

## 三、建议的下一步执行顺序

```
Week 1: 修复预存单元测试失败
  ├─ metadata_scanner unit test → 5/5 PASS
  └─ scoreboard_prefetcher unit test → 8/8 PASS

Week 2: Direction A 强化
  ├─ gbp_pe unit test 完整编译（可能需要 -O0）
  ├─ 单 PE handcrafted 数值测试（验证 compute_unit 基本运算）
  └─ 多轮迭代框架（在 Direction B testbench 中跑 3+ 轮）

Week 3: Direction C 基础设施
  ├─ Python golden reference (4-node chain + 4-node mesh)
  ├─ Testbench belief 读取 + JSON 对比
  └─ 容差和收敛检测逻辑

Week 4: Direction C 执行与收敛
  ├─ Run Direction C on 4-node chain (sanity check)
  ├─ Run Direction C on 4-node mesh (full validation)
  └─ Debug numerical mismatches (if any)
```

---

## 四、风险登记册

| ID | 风险 | 可能性 | 影响 | 缓解措施 |
|----|------|--------|------|---------|
| R1 | `gbp_pe` unit test Verilator 编译 OOM/超时 | 高 | 阻塞 Direction A | 使用 `-O0`、拆分 top-level、或换用 VCS |
| R2 | Compute Unit FP32 与 NumPy FP32 存在系统性偏差（如 fused mul-add 差异） | 中 | Direction C 无法收敛到容差内 | 先跑单操作对比，确认偏差来源 |
| R3 | Reverse CSR hash 冲突导致节点丢失（高 degree graph） | 低 | 算法结果错误但难以调试 | 补充 hash 冲突测试；监控 `pending_q_overflow_o` |
| R4 | NoC 延迟导致多轮迭代中消息乱序 | 低 | Belief 更新顺序与参考不同 | Direction C 使用同步 schedule（每轮 barrier） |
| R5 | SPM 地址空间在大 graph 下重叠 | 中 | 数据损坏 | 初始化时添加地址空间断言 |
