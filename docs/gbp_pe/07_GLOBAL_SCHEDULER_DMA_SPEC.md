# Global Scheduler & DMA Spec

> 版本：2026-06-11
> 状态：Draft

---

## 1. 架构总览

```
Host (C++ testbench)
  │ 寄存器接口（独立设计）
  ▼
┌─────────────────────────────┐
│     Global Scheduler         │
│  ┌───────────┐ ┌──────────┐ │
│  │ Host IF   │ │ PE Array │ │
│  │ (regs)    │ │ IF       │ │
│  └───────────┘ └──────────┘ │
└──────────────┬──────────────┘
               │ task_start / subgraph_done
               ▼
┌─────────────────────────────┐         ┌──────────┐
│         DMA Engine           │ ◄─────► │   DRAM   │
│  ┌───────────┐ ┌──────────┐ │  AXI    │          │
│  │ AXI Master│ │ PE SPM   │ │         └──────────┘
│  │ (read)    │ │ Writer   │ │
│  └───────────┘ └──────────┘ │
└──────────────┬──────────────┘
               │ spm_write (per PE)
               ▼
┌─────────────────────────────┐
│     PE Array (N×M mesh)      │
│  ┌─────┐ ┌─────┐ ┌─────┐   │
│  │PE00 │ │PE10 │ │PE20 │   │
│  └──┬──┘ └──┬──┘ └──┬──┘   │
│     │       │       │       │
│  ┌──▼──┐ ┌──▼──┐ ┌──▼──┐   │
│  │PE01 │ │PE11 │ │PE21 │   │
│  └─────┘ └─────┘ └─────┘   │
└─────────────────────────────┘
```

---

## 2. Global Scheduler

### 2.1 职责

1. 接收 host 的 subgraph 任务配置
2. 触发 DMA 加载 subgraph 数据到所有 PE
3. 等待所有 PE 完成当前 subgraph
4. 切换到下一个 subgraph
5. 报告状态给 host

### 2.2 Subgraph 任务模型

一个 subgraph 包含：
- **subgraph_id**：当前 subgraph 编号
- **n_pes**：参与的 PE 数量
- **pe_task_list**：每个 PE 的任务描述（DRAM 地址、SPM 地址、长度）
- **iteration_count**：当前迭代轮次

Global Scheduler 维护一个 subgraph 列表（循环执行）：
```
subgraph_0 → subgraph_1 → ... → subgraph_K → subgraph_0 → ...
```

每个 subgraph 对应所有 PE。PE 之间通过 NoC 交换消息（NOTIFICATION/FETCH），不需要 Global Scheduler 干预。

### 2.3 状态机

```
IDLE
  │ host writes task_start
  ▼
LOADING
  │ DMA loads subgraph data to all PEs
  │ DMA done for all PEs
  ▼
RUNNING
  │ PEs execute GBP nodes
  │ All PEs signal pe_subgraph_done
  ▼
COLLECTING
  │ Read results (optional DMA back to DRAM)
  │ Update iteration counter
  ▼
SWITCHING
  │ Advance to next subgraph
  │ If all iterations done → IDLE
  │ Else → LOADING
```

### 2.4 接口定义

```systemverilog
module global_scheduler #(
    parameter int NUM_PE_X = 2,
    parameter int NUM_PE_Y = 2,
    parameter int MAX_SUBGRAPHS = 64
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // ── Host Register Interface ──
    // (独立设计，此处定义信号方向)
    input  logic        host_task_start_i,       // 启动任务
    input  logic [31:0] host_subgraph_count_i,    // subgraph 总数
    input  logic [31:0] host_max_iterations_i,    // 最大迭代轮数
    input  logic [31:0] host_dram_base_addr_i,    // DRAM 基地址
    output logic [31:0] host_status_o,            // 状态寄存器
    output logic [31:0] host_current_subgraph_o,  // 当前 subgraph ID
    output logic [31:0] host_iteration_count_o,   // 当前迭代轮次
    output logic        host_task_done_o,         // 任务完成

    // ── DMA Interface ──
    output logic        dma_start_o,              // DMA 启动
    output logic [31:0] dma_dram_addr_o,          // DRAM 源地址
    output logic [31:0] dma_spm_addr_o,           // SPM 目标地址
    output logic [15:0] dma_length_o,             // 传输长度（字）
    output logic [NUM_PE_X*NUM_PE_Y-1:0] dma_pe_mask_o,  // 目标 PE 掩码
    input  logic        dma_done_i,               // DMA 完成
    input  logic [NUM_PE_X*NUM_PE_Y-1:0] dma_pe_done_i, // 每个 PE 的 DMA 完成

    // ── PE Array Interface ──
    input  logic [NUM_PE_X*NUM_PE_Y-1:0] pe_subgraph_done_i,  // PE subgraph 完成
    output logic        subgraph_switch_o,         // subgraph 切换脉冲
    output logic [31:0] current_subgraph_id_o      // 当前 subgraph ID
);
```

### 2.5 时序

```
Cycle 0:   host_task_start_i = 1, subgraph_count=4, max_iterations=10
Cycle 1:   Global Scheduler → IDLE → LOADING
Cycle 1:   dma_start_o = 1, dma_dram_addr = base + subgraph_0_offset
Cycle 2-N: DMA loads data to all PEs
Cycle N+1: dma_done_i = 1 → LOADING → RUNNING
Cycle N+1: subgraph_switch_o = 1 (pulse)
Cycle N+2: PEs start executing
...
Cycle M:   All PEs signal pe_subgraph_done → RUNNING → COLLECTING
Cycle M+1: COLLECTING → SWITCHING → LOADING (next subgraph)
...
Cycle Z:   All iterations done → IDLE, host_task_done_o = 1
```

---

## 3. DMA Engine

### 3.1 职责

1. 接收 PE 的 DMA 请求（DRAM → SPM）
2. 通过 AXI master 从 DRAM 读取数据
3. 将数据写入目标 PE 的 SPM
4. 支持多个 PE 并发请求（round-robin 仲裁）
5. 报告完成状态

### 3.2 设计约束

- **PE 触发**：PE 发起 DMA 请求，不是 Global Scheduler 直接控制
- **AXI 接口**：AXI master read（从 DRAM 读）
- **SPM 写入**：直接写入 PE 的 SPM（通过 SPM arbiter 的 DMA 端口）
- **全量传输**：NodeHeader + AdjEntry + STATE，一次 DMA 完成一个 PE 的完整 subgraph 数据

### 3.3 DMA 请求格式

每个 PE 发送一个 DMA 请求：
```systemverilog
typedef struct packed {
    logic [31:0]  dram_addr;     // DRAM 源地址
    logic [SPM_ADDR_W-1:0] spm_addr;  // SPM 目标地址（word address）
    logic [15:0]  length;        // 传输长度（32-bit words）
} dma_request_t;
```

### 3.4 接口定义

```systemverilog
module dma_engine #(
    parameter int NUM_PE = 4,
    parameter int SPM_ADDR_W = 18,
    parameter int AXI_ADDR_W = 32,
    parameter int AXI_DATA_W = 64
)(
    input  logic clk_i,
    input  logic rst_n_i,

    // ── PE Request Interface (per PE) ──
    input  logic [NUM_PE-1:0]                 pe_dma_valid_i,
    output logic [NUM_PE-1:0]                 pe_dma_ready_o,
    input  logic [NUM_PE-1:0][31:0]           pe_dma_dram_addr_i,
    input  logic [NUM_PE-1:0][SPM_ADDR_W-1:0] pe_dma_spm_addr_i,
    input  logic [NUM_PE-1:0][15:0]           pe_dma_length_i,

    // ── PE Done (per PE) ──
    output logic [NUM_PE-1:0]                 pe_dma_done_o,

    // ── AXI Master Read Interface ──
    output logic [AXI_ADDR_W-1:0]            axi_araddr_o,
    output logic [7:0]                       axi_arlen_o,
    output logic [2:0]                       arsize_o,
    output logic                             axi_arvalid_o,
    input  logic                             axi_arready_i,
    input  logic [AXI_DATA_W-1:0]           axi_rdata_i,
    input  logic                             axi_rvalid_i,
    output logic                             axi_rready_o,
    input  logic                             axi_rlast_i,

    // ── PE SPM Write Interface (per PE) ──
    output logic [NUM_PE-1:0]                 spm_wr_valid_o,
    input  logic [NUM_PE-1:0]                 spm_wr_ready_i,
    output logic [NUM_PE-1:0][SPM_ADDR_W-1:0] spm_wr_addr_o,
    output logic [NUM_PE-1:0][63:0]           spm_wr_data_o,

    // ── Global Scheduler Interface ──
    input  logic                             gs_start_i,
    input  logic [31:0]                      gs_dram_addr_i,
    input  logic [31:0]                      gs_spm_addr_i,
    input  logic [15:0]                      gs_length_i,
    input  logic [NUM_PE-1:0]                gs_pe_mask_i,
    output logic                             gs_done_o,
    output logic [NUM_PE-1:0]                gs_pe_done_o
);
```

### 3.5 状态机（per PE）

```
IDLE
  │ pe_dma_valid_i OR gs_start_i (with pe_mask)
  ▼
AXI_READ
  │ Issue AXI read request
  │ Receive data beats
  ▼
SPM_WRITE
  │ Write 64-bit beats to PE SPM
  │ If more data → AXI_READ
  ▼
DONE
  │ Assert pe_dma_done / gs_pe_done
  ▼
IDLE
```

### 3.5 仲裁

当多个 PE 同时请求 DMA 时，使用 round-robin 仲裁：
- 每次只服务一个 PE 的请求
- 一个 PE 的请求完成后，切换到下一个
- Global Scheduler 的请求（gs_start_i）优先级高于 PE 自发请求

### 3.6 AXI 时序

```
Cycle 0: axi_arvalid=1, axi_araddr=dram_addr, axi_arlen=length-1
Cycle 1: axi_arready=1 → AXI read issued
Cycle 2-N: axi_rvalid=1, axi_rdata=beat_data
           spm_wr_valid=1, spm_wr_addr=spm_addr+offset
Cycle N+1: axi_rlast=1 → transfer complete
```

---

## 4. PE 端接口变更

### 4.1 新增端口

PE 需要新增以下端口：

```systemverilog
// ── DMA Request ──
output logic                 dma_valid_o,
input  logic                 dma_ready_i,
output logic [31:0]          dma_dram_addr_o,
output logic [SPM_ADDR_W-1:0] dma_spm_addr_o,
output logic [15:0]          dma_length_o,

// ── Subgraph Control ──
input  logic                 subgraph_switch_i,    // 新 subgraph 开始
input  logic [31:0]          subgraph_id_i,        // 当前 subgraph ID
output logic                 subgraph_done_o,      // 当前 subgraph 完成
```

### 4.2 PE 内部行为变更

```
1. subgraph_switch_i 脉冲 → PE 进入 "loading" 状态
2. PE 发送 DMA 请求（dram_addr, spm_addr, length）
3. DMA 完成后，PE 进入 "running" 状态
4. PE 内部 node scheduler 开始调度当前 subgraph 的节点
5. 所有节点完成后，PE 发送 subgraph_done_o
6. PE 等待下一个 subgraph_switch_i
```

### 4.3 Node Scheduler 变更

当前 node_scheduler 调度 `NUM_NODES_PER_PE` 个节点。需要改为：
- 调度当前 subgraph 的 active node list
- active list 从 SPM 中的 NodeHeader 读取
- subgraph 完成后，清除 visited_mask，准备下一个 subgraph

---

## 5. Subgraph 数据格式

### 5.1 DRAM 布局

每个 subgraph 在 DRAM 中的布局：

```
DRAM[base_addr + offset]:
  ┌──────────────────────────────┐
  │ Subgraph Header              │
  │  - subgraph_id               │
  │  - n_pes                     │
  │  - pe_data_offset[0..n-1]    │
  └──────────────────────────────┘
  ┌──────────────────────────────┐
  │ PE 0 Data                    │
  │  - NodeHeaders               │
  │  - FwdEdgeArray              │
  │  - RevKeyHash + RevHeader    │
  │  - RevEntryArray             │
  │  - STATE (per node)          │
  └──────────────────────────────┘
  ┌──────────────────────────────┐
  │ PE 1 Data                    │
  │  ...                         │
  └──────────────────────────────┘
  ...
```

### 5.2 SPM 布局

DMA 将 PE 数据直接写入 SPM 的对应区域：
- META region：NodeHeader + FwdEdgeArray + RevCSR
- STATE region：每个节点的 state block

DMA 不需要理解数据格式，只做地址映射。

---

## 6. 与端到端测试的关系

### 6.1 测试流程

```
1. Host 初始化：
   - 配置 subgraph 列表（DRAM 地址、长度）
   - 设置 max_iterations
   - 写入 task_start

2. Global Scheduler 自动执行：
   - 对每个 subgraph：
     a. 触发 DMA 加载
     b. 等待所有 PE 完成
     c. 切换到下一个 subgraph

3. PE 自动执行：
   - DMA 完成后，node scheduler 开始
   - 遍历当前 subgraph 的所有节点
   - 完成后发送 subgraph_done

4. Host 检查结果：
   - 等待 task_done
   - 从 SPM 读取 variable beliefs
   - 计算 ARE
```

### 6.2 分层验证

| 层级 | 测试内容 | 状态 |
|------|---------|------|
| L1: DMA 单元 | AXI read + SPM write | 待实现 |
| L2: Global Scheduler 单元 | 状态机 + PE 交互 | 待实现 |
| L3: DMA + GS 集成 | DMA + GS 联动 | 待实现 |
| L4: PE + DMA 集成 | PE 发起 DMA + 执行 | 待实现 |
| L5: 全系统端到端 | 4 PE mesh + GBP 收敛 | 待实现 |

---

## 7. 待确认项

1. **AXI 接口宽度**：64-bit? 128-bit? 256-bit?
2. **DRAM 地址空间**：32-bit? 48-bit?
3. **PE 发起 DMA 的时机**：收到 subgraph_switch_i 后立即发起？还是需要额外的触发？
4. **多个 subgraph 的迭代**：一个 subgraph 迭代多少轮才切换到下一个？还是所有 subgraph 各迭代一轮算一轮？
5. **subgraph 之间的 overlap**：你提到"需要判断 overlap 来进行 load"。overlap 是指什么？相邻 subgraph 共享的节点？还是指 DMA 加载和 PE 执行可以重叠？
