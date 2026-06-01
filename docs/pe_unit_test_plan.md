# PE Unit Test 计划（RTL 版本）

## 目标
- 将 PE 的计算调度、消息处理、NoC 交互、加载/激活路径拆分为可独立验证的单元测试。
- 以最小依赖的 testbench 验证 PE 行为，不依赖完整系统集成。
- 便于逐步落地：先跑最小路径，再覆盖复杂场景。

## 范围假设
- PE 通过 `bsg_manycore_endpoint_standard` 对接 NoC。
- PE 内部实现与 `nocbp_simulator/pe/ProcessingElement.*` 语义一致。
- Unit test 使用可控的假 NoC/NI 驱动（只需送/收 endpoint 信号）。

## Testbench 结构（建议）
- **DUT**：PE RTL 顶层（含 endpoint_standard）。
- **驱动器**：
  - in_request 驱动：模拟 NoC 下发请求（读/写/消息/控制）。
  - out_request 监控：捕获 PE 向 NoC 发出的消息包。
  - return_path 驱动：为 PE 返回读取响应（如需要）。
- **Scoreboard**：
  - 记录期望发送序列（目的 PE、message type、payload、vnet 等）。
  - 校验握手时序与内容。
- **时钟/周期控制**：统一 cycle 计数，便于对齐调度和延迟。

## 拆分测试套件与用例

### Suite A: Endpoint 与基础握手
1. **A1 基本 in_request 接收**
   - 输入：单个合法请求（写/读各一例）。
   - 期望：`in_v_o`/`in_*` 正确拉起，`in_yumi_i` 后清除。
2. **A2 out_request 发送握手**
   - 输入：PE 触发一次发送路径。
   - 期望：`out_v_i`/`out_packet_i` -> `out_credit_or_ready_o` 互锁成功。
3. **A3 return path 响应**
   - 输入：模拟返回包。
   - 期望：`returned_*` 正确呈现并可被 PE 消费。

### Suite B: 调度与计算
1. **B1 factor/variable 交替调度**
   - 输入：各 1 个 factor/variable 节点激活。
   - 期望：按 priority phase 交替计算（每周期最多 1 节点）。
2. **B2 factor_compute_delay**
   - 输入：配置延迟参数，连续激活节点。
   - 期望：计算触发频率满足延迟门控。
3. **B3 visited tracking**
   - 输入：多节点队列。
   - 期望：一个 phase 内每节点只处理一次，phase 切换后重置。

### Suite C: 消息路径（GBP + priority-switch）
1. **C1 factor->variable 单播**
   - 输入：factor 计算产生多条消息。
   - 期望：每个相邻 variable 生成独立消息。
2. **C2 variable->factor 聚合**
   - 输入：variable 相邻多个 factor 归属同一 PE。
   - 期望：同一目标 PE 合并发送（若 RTL 不做聚合，应显式确认并改写期望）。
3. **C3 去重/覆盖**
   - 输入：重复 message key（node_id、src_pe、is_factor）更新。
   - 期望：旧消息被覆盖或替换，队列维持 O(1) 行为。
4. **C4 priority switch 消息**
   - 输入：priority-switch 消息进入专用通道（vnet/type）。
   - 期望：不进入 GBP 主队列，仅更新 switch flags。

### Suite D: Load/激活路径（MC/SPM 交互）
1. **D1 node_capacity 限制**
   - 输入：超过容量的 pending nodes。
   - 期望：只发出 capacity 内的 load 请求。
2. **D2 inflight 抑制**
   - 输入：重复请求同一节点。
   - 期望：inflight 阻止重复 load。
3. **D3 load->activate**
   - 输入：MC 返回加载完成。
   - 期望：节点被激活并可进入计算。

### Suite E: 边界与异常
1. **E1 空队列**：无可计算节点时保持空闲。
2. **E2 单类型节点**：只有 factor 或只有 variable 的极端图。
3. **E3 NI buffer 条件**：返回包延迟、乱序到达的处理。
4. **E4 本地消息路径**：target PE = self 的消息走本地队列（若支持）。

## 期望输出与检查点
- 每个用例给出：输入 stimulus、期望 NoC 包序列、周期对齐条件。
- 每个 suite 输出：pass/fail + 关键 counter/trace（例如计算次数、切换次数）。

## 后续落地建议
- 先实现 Suite A + B，确保 PE 计算调度稳定。
- 再实现 Suite C，覆盖消息路径与切换协议。
- 最后加入 Suite D/E，补齐加载与异常。
