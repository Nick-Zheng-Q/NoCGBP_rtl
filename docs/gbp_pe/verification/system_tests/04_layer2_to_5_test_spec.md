# Layer 2-5 测试 Spec

> 版本：2026-06-11
> 目标：定义从 DMA 单元测试到端到端收敛的完整测试规范

---

## Layer 2: DMA + Global Scheduler 单元测试

### 2.1 DMA 单元测试

#### 2.1.1 测试目标

验证 DMA 模块正确地：
- 从 AXI 接口读取 DRAM 数据
- 将数据写入目标 PE 的 SPM
- 处理多 PE 并发请求的仲裁
- 正确报告完成状态

#### 2.1.2 测试场景

| 编号 | 场景 | 描述 |
|------|------|------|
| L2-DMA-01 | 单 PE 单次传输 | PE0 发起 DMA 请求，DMA 从"DRAM"读取 64 字节，写入 PE0 SPM |
| L2-DMA-02 | 多 PE 并发请求 | PE0 和 PE1 同时发起 DMA 请求，DMA round-robin 仲裁 |
| L2-DMA-03 | 大块传输 | 传输 1KB 数据，验证 AXI burst 和 SPM 写入的正确性 |
| L2-DMA-04 | AXI 反压 | AXI read 侧反压（arready=0），DMA 正确等待 |
| L2-DMA-05 | SPM 反压 | SPM 写入侧反压（wr_ready=0），DMA 正确等待 |
| L2-DMA-06 | 边界地址 | DRAM 地址和 SPM 地址在边界值时的正确性 |
| L2-DMA-07 | 传输完整性 | 验证所有数据字都正确写入 SPM（逐字比较） |

#### 2.1.3 测试框架

```
┌──────────────────────────────────────────────┐
│  DMA Testbench (C++)                         │
│                                              │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐  │
│  │ AXI BFM │◄──►│   DMA   │◄──►│ SPM BFM │  │
│  │ (mock)  │    │  (DUT)  │    │ (mock)  │  │
│  └─────────┘    └─────────┘    └─────────┘  │
│       │              ▲              │        │
│       │              │              │        │
│       ▼              │              ▼        │
│  DRAM data      PE requests    SPM verify    │
│  (testbench     (testbench     (testbench    │
│   generated)     generated)     compares)    │
└──────────────────────────────────────────────┘
```

**AXI BFM**：模拟 DRAM 响应，返回预设数据
**SPM BFM**：记录 DMA 写入的地址和数据，供验证
**PE 请求模拟**：testbench 驱动 `pe_dma_valid_i` 信号

#### 2.1.4 验证标准

- 所有传输的数据字与 DRAM 源数据一致
- SPM 写入地址正确（base_addr + offset）
- 多 PE 并发时仲裁公平（round-robin）
- 完成信号 `pe_dma_done_o` 在传输结束后正确断言

---

### 2.2 Global Scheduler 单元测试

#### 2.2.1 测试目标

验证 Global Scheduler 正确地：
- 接收 host 任务配置
- 触发 DMA 加载
- 等待所有 PE 完成
- 切换到下一个 subgraph
- 计数迭代轮次

#### 2.2.2 测试场景

| 编号 | 场景 | 描述 |
|------|------|------|
| L2-GS-01 | 单 subgraph 单轮 | 配置 1 个 subgraph，启动，等待完成 |
| L2-GS-02 | 多 subgraph 循环 | 配置 4 个 subgraph，验证顺序切换 |
| L2-GS-03 | 多 PE 完成计数 | 4 个 PE 分别在不同时刻完成，GS 正确等待全部完成 |
| L2-GS-04 | 迭代轮次控制 | 设置 max_iterations=10，验证在第 10 轮后停止 |
| L2-GS-05 | Host 状态读取 | 验证 host_status_o、current_subgraph_o、iteration_count_o 正确 |
| L2-GS-06 | 任务完成 | 验证 host_task_done_o 在所有迭代完成后正确断言 |
| L2-GS-07 | DMA 触发 | 验证 dma_start_o、dma_dram_addr_o、dma_pe_mask_o 正确 |

#### 2.2.3 测试框架

```
┌──────────────────────────────────────────────┐
│  GS Testbench (C++)                          │
│                                              │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐  │
│  │ Host IF │◄──►│ Global  │◄──►│ PE Done │  │
│  │ (mock)  │    │Scheduler│    │ (mock)  │  │
│  └─────────┘    │  (DUT)  │    └─────────┘  │
│                 └─────────┘                  │
│                      │                       │
│                      ▼                       │
│                 DMA trigger                  │
│                (verified)                    │
└──────────────────────────────────────────────┘
```

**Host IF mock**：模拟 host 寄存器读写
**PE Done mock**：模拟 PE 的 `pe_subgraph_done_i` 信号
**DMA trigger 验证**：检查 `dma_start_o` 等信号的时序

---

## Layer 3: PE + DMA 集成测试

### 3.1 测试目标

验证 PE 能通过 DMA 加载 subgraph 数据并正确执行。

### 3.2 测试场景

| 编号 | 场景 | 描述 |
|------|------|------|
| L3-01 | PE 发起 DMA 请求 | PE 收到 subgraph_switch 后，正确发起 DMA 请求 |
| L3-02 | DMA 加载后 PE 执行 | DMA 将数据写入 SPM 后，PE 的 node scheduler 正确读取并执行 |
| L3-03 | 单节点计算 | DMA 加载一个 variable 节点的数据，PE 执行计算，验证结果 |
| L3-04 | 多节点计算 | DMA 加载多个节点的数据，PE 依次计算，验证所有结果 |
| L3-05 | Factor + Variable | DMA 加载 factor 和 variable 节点，PE 执行完整的 factor→variable 流程 |
| L3-06 | subgraph_done 信号 | PE 完成所有节点后，正确断言 subgraph_done |

### 3.3 测试框架

```
┌──────────────────────────────────────────────┐
│  PE+DMA Testbench (C++)                      │
│                                              │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐  │
│  │ AXI BFM │◄──►│   DMA   │◄──►│   PE    │  │
│  │ (mock)  │    │  (DUT)  │    │  (DUT)  │  │
│  └─────────┘    └─────────┘    └─────────┘  │
│       │                             │        │
│       ▼                             ▼        │
│  DRAM data                     SPM verify    │
│  (preloaded)                   (readback)    │
└──────────────────────────────────────────────┘
```

### 3.4 PE 接口变更

PE 需要新增端口（见 `07_GLOBAL_SCHEDULER_DMA_SPEC.md` §4）：

```systemverilog
// DMA Request
output logic                 dma_valid_o,
input  logic                 dma_ready_i,
output logic [31:0]          dma_dram_addr_o,
output logic [SPM_ADDR_W-1:0] dma_spm_addr_o,
output logic [15:0]          dma_length_o,

// Subgraph Control
input  logic                 subgraph_switch_i,
input  logic [31:0]          subgraph_id_i,
output logic                 subgraph_done_o,
```

### 3.5 验证标准

- PE 发起的 DMA 请求地址和长度正确
- DMA 写入 SPM 的数据与 DRAM 源一致
- PE 计算结果与预期一致（需要先修复 Layer 1 的 staging buffer bug）
- `subgraph_done_o` 在所有节点完成后正确断言

---

## Layer 4: 全系统集成测试

### 4.1 测试目标

验证 Global Scheduler + DMA + PE 阵列协同工作。

### 4.2 测试场景

| 编号 | 场景 | 描述 |
|------|------|------|
| L4-01 | 2×1 最小系统 | 2 个 PE，1 个 subgraph，验证基本流程 |
| L4-02 | 2×2 mesh | 4 个 PE，多 subgraph，验证 NoC 路由和 DMA 并发 |
| L4-03 | subgraph 切换 | 多个 subgraph 顺序执行，验证数据正确切换 |
| L4-04 | PE 间消息传递 | PE(0,0) 完成 factor 后发送 NOTIFICATION，PE(1,0) 收到并发起 FETCH |
| L4-05 | 迭代收敛 | 多轮迭代后，验证 PE 间的 belief 收敛趋势 |
| L4-06 | DMA + PE 并发 | DMA 加载下一个 subgraph 的同时，PE 执行当前 subgraph（overlap） |

### 4.3 测试框架

```
┌──────────────────────────────────────────────────────────────┐
│  System Testbench (C++)                                      │
│                                                              │
│  ┌─────────┐    ┌─────────┐    ┌──────────────────────────┐ │
│  │ Host IF │◄──►│ Global  │◄──►│      DMA Engine          │ │
│  │ (mock)  │    │Scheduler│    │                          │ │
│  └─────────┘    │  (DUT)  │    └────────┬─────────────────┘ │
│                 └─────────┘             │ AXI               │
│                                         ▼                   │
│  ┌─────────────────────────────────────────────────────────┐│
│  │                    2×2 Mesh                             ││
│  │  ┌─────┐    ┌─────┐                                    ││
│  │  │PE00 │◄──►│PE10 │    (router + PE × 4)              ││
│  │  └──┬──┘    └──┬──┘                                    ││
│  │     │          │                                        ││
│  │  ┌──▼──┐    ┌──▼──┐                                    ││
│  │  │PE01 │◄──►│PE11 │                                    ││
│  │  └─────┘    └─────┘                                    ││
│  └─────────────────────────────────────────────────────────┘│
│                                                              │
│  ┌─────────┐                                                │
│  │ AXI BFM │ (mock DRAM)                                   │
│  └─────────┘                                                │
└──────────────────────────────────────────────────────────────┘
```

### 4.4 验证标准

- 所有 PE 正确加载 subgraph 数据
- PE 间 NoC 消息（NOTIFICATION/FETCH）正确路由
- subgraph 切换后数据正确更新
- 迭代轮次计数正确
- 无死锁

---

## Layer 5: 端到端收敛测试

### 5.1 测试目标

在参数化 mesh 上运行真实 benchmark 图，验证 GBP 算法收敛。

### 5.2 测试场景

| 编号 | 图 | mesh | 迭代数 | 验证 |
|------|-----|------|--------|------|
| L5-01 | synthetic_line | 2×1 | 10 | ARE < threshold |
| L5-02 | synthetic_lattice | 2×2 | 20 | ARE < threshold |
| L5-03 | bal_fr1desk_small | 4×2 | 50 | ARE < threshold |

### 5.3 测试流程

```
1. C++ testbench 加载图数据 txt + partition JSON
2. 初始化 DRAM（AXI BFM 预加载 subgraph 数据）
3. 配置 Global Scheduler（subgraph_count, max_iterations）
4. 写入 host_task_start
5. Global Scheduler 自动执行：
   a. 触发 DMA 加载 subgraph 0 到所有 PE
   b. 所有 PE 自调度执行
   c. 所有 PE 完成 → 切换到 subgraph 1
   d. 重复直到所有 subgraph 完成一轮
   e. 重复直到 max_iterations 轮
6. host_task_done 断言
7. 从 SPM 读取所有 variable 节点的 belief
8. 计算 ARE
9. 验证 ARE < threshold → PASS/FAIL
```

### 5.4 ARE 计算（纯 C++，不链接外部库）

```cpp
// BA 图的 ARE
double compute_are_ba(
    const std::vector<CameraBelief>& cameras,  // 6 DOF each
    const std::vector<LandmarkBelief>& landmarks,  // 3 DOF each
    const std::vector<Observation>& observations,  // (cam_id, lmk_id, u, v)
    const CameraIntrinsics& K
) {
    double total_error = 0.0;
    for (const auto& obs : observations) {
        // Reproject landmark through camera
        Eigen::Vector3d point = landmarks[obs.lmk_id].pos;
        Eigen::Vector3d rotated = cameras[obs.cam_id].rotation * point;
        Eigen::Vector3d translated = rotated + cameras[obs.cam_id].translation;
        Eigen::Vector2d projected(translated[0] / translated[2], translated[1] / translated[2]);
        Eigen::Vector2d pixel = K.focal * projected + K.principal;
        // Reprojection error
        total_error += (pixel - Eigen::Vector2d(obs.u, obs.v)).norm();
    }
    return total_error / observations.size();
}

// 线性图的 ARE
double compute_are_linear(
    const std::vector<VariableBelief>& variables,
    const std::vector<FactorNode>& factors
) {
    double total_residual = 0.0;
    for (const auto& factor : factors) {
        Eigen::VectorXd residual = factor.measurement;
        for (int k = 0; k < factor.connected_vars.size(); k++) {
            residual -= factor.J[k] * variables[factor.connected_vars[k]].belief;
        }
        total_residual += residual.norm();
    }
    return total_residual / factors.size();
}
```

### 5.5 SPM 读取

```cpp
struct VariableBelief {
    int node_id;
    int dof;
    std::vector<float> eta;
    std::vector<float> lambda;  // upper-triangular
};

std::vector<VariableBelief> read_beliefs_from_spm(
    int pe_id,
    const std::vector<int>& var_node_ids
) {
    std::vector<VariableBelief> beliefs;
    for (int node_id : var_node_ids) {
        VariableBelief b;
        b.node_id = node_id;
        auto header = spm_read_node_header(pe_id, node_id);
        b.dof = header.dof;
        int compact_words = b.dof + b.dof * (b.dof + 1) / 2;
        for (int i = 0; i < compact_words; i++) {
            uint32_t word = spm_read_word(pe_id, header.state_base + i);
            if (i < b.dof) b.eta.push_back(uint32_to_float(word));
            else           b.lambda.push_back(uint32_to_float(word));
        }
        beliefs.push_back(b);
    }
    return beliefs;
}
```

### 5.6 验证标准

| 图 | ARE 阈值 | 来源 |
|----|---------|------|
| synthetic_line | < 0.1 | oracle |
| synthetic_lattice | < 0.15 | oracle |
| bal_fr1desk_small | < 2.0 | oracle |

### 5.7 测试框架

```
┌──────────────────────────────────────────────────────────────┐
│  E2E Testbench (C++) + Python scripts                        │
│                                                              │
│  ┌──────────┐   ┌──────────┐   ┌──────────────────────────┐ │
│  │gen_spm   │   │ run_e2e  │   │    System Testbench      │ │
│  │_init.py  │──►│.py       │──►│  (Layer 4 framework)     │ │
│  └──────────┘   └──────────┘   └──────────┬───────────────┘ │
│                                           │                 │
│                                           ▼                 │
│                              ┌──────────────────────────┐   │
│                              │ ARE Calculator (C++)     │   │
│                              │ - parse_bal_txt()        │   │
│                              │ - read_beliefs_from_spm()│   │
│                              │ - compute_are_ba()       │   │
│                              │ - compute_are_linear()   │   │
│                              └──────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

---

## 依赖关系

```
Layer 1 (PE 内部) ← 已完成，但有 staging buffer bug
    │
    ▼
Layer 2 (DMA + GS 单元) ← 不依赖 Layer 1 bug 修复
    │
    ▼
Layer 3 (PE + DMA 集成) ← 依赖 Layer 1 bug 修复 + Layer 2
    │
    ▼
Layer 4 (全系统集成) ← 依赖 Layer 3
    │
    ▼
Layer 5 (端到端收敛) ← 依赖 Layer 4
```

## 文件结构

```
nocbp_verilator/
├── tests/
│   ├── unit/
│   │   ├── dma.cc                    (Layer 2: DMA 单元测试)
│   │   ├── dma.f
│   │   ├── dma.yaml
│   │   ├── global_scheduler.cc       (Layer 2: GS 单元测试)
│   │   ├── global_scheduler.f
│   │   └── global_scheduler.yaml
│   ├── integration/
│   │   ├── pe_dma_integration.cc     (Layer 3)
│   │   └── pe_dma_integration.yaml
│   ├── system/
│   │   ├── mesh_NxM_gbp.cc           (Layer 4+5: 统一 testbench)
│   │   ├── mesh_NxM_gbp.f
│   │   ├── mesh_NxM_gbp.yaml
│   │   └── configs/                  (partition JSON + graph configs)
│   └── tools/
│       ├── are_calculator.hpp        (Layer 5: ARE 计算)
│       ├── parse_bal_txt.py          (Layer 5: BAL 数据解析)
│       └── run_e2e_convergence.py    (Layer 5: 自动化脚本)
├── tops/
│   ├── unit/
│   │   ├── dma_top.sv                (Layer 2: DMA test top)
│   │   └── global_scheduler_top.sv   (Layer 2: GS test top)
│   └── system/
│       └── mesh_NxM_gbp_top.sv       (Layer 4+5: 参数化 mesh top)
└── v/gbp_pe/
    ├── dma_engine.sv                 (Layer 2: DMA RTL)
    └── global_scheduler.sv           (Layer 2: GS RTL)
```
