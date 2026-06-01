# bsg_manycore 改造细化方案（PE+ScratchPad+阵列DMA+实际DRAM）

## 前置说明
- 顺序思考工具（Sequential‑Thinking MCP）在当前会话不可用，无法按指南调用；本文件以显式假设与不确定性补足。
- 运行环境为只读沙箱，本文件通过提升权限写入。
- 输入来源仅为本仓库本地 HDL 与 testbench；未访问外部网络。

## 任务日志（开始）
- 日期：2026-01-28
- 目标：在 mesh/ruche 两种网络下，实现“统一定制处理单元 + 本地 ScratchPad + 阵列级 DMA + 实际 DRAM 接口”的修改路线细化。
- 输入来源：`v/`、`testbenches/` 与 `v/chip/` 目录。
- 不确定性：具体 DRAM 类型与控制器接口尚未确认。

---

## 总体设计假设（基于你的最新输入）
1) **mesh/ruche 都要试**：需要同时支持 `e_network_mesh` 与 `e_network_half_ruche_x`。
2) **ScratchPad 仅本地使用**：不对外 NoC 暴露。
3) **DMA 由 PE 控制**：PE 通过 NoC 写 DMA 控制寄存器。
4) **DRAM 为实际接口**：不是仿真模型，需要对接真实 DRAM 控制器/PHY。

---

## A. 统一定制处理单元 + 本地 ScratchPad

### A1. 新建/替换处理单元模块
- **建议基线**：复制 `v/bsg_manycore_accel_default.sv` 作为你的 PE 模块（例如 `v/bsg_manycore_proc_custom.sv`）。
- **原因**：它已通过 `bsg_manycore_endpoint_standard` 对接 NoC，改动最小。
- **新增内容**：在该模块内部添加 ScratchPad RAM，并仅对本 PE 内部逻辑可见。

### A2. 统一绑定 hetero_socket
- 修改 `v/bsg_manycore_hetero_socket.sv`：
  - 将所有 `HETERO_TYPE_MACRO` 映射到你的 PE 模块，或者新增一个类型并只保留该类型。

### A3. 让所有 tile 使用同一类型
- 将 `hetero_type_vec_p` 设为你的类型编号（全阵列一致）：
  - mesh：`v/bsg_manycore_pod_mesh_array.sv`
  - ruche：`v/bsg_manycore_pod_ruche_array.sv`
  - 仿真入口：`testbenches/common/v/bsg_nonsynth_manycore_testbench.sv`

### A4. ScratchPad 使用方式建议（本地专用）
- 建议以本地地址空间访问（不映射到 NoC 全局地址）。
- 由 PE 内部指令或控制逻辑访问该 RAM。

---

## B. 每个 Tile 阵列一个 DMA（由 PE 控制）

### B1. DMA 的放置层级
- **推荐在 tile array 层级实例化**：
  - mesh：`v/bsg_manycore_tile_compute_array_mesh.sv`
  - ruche：`v/bsg_manycore_tile_compute_array_ruche.sv`
- 每个 tile array（子阵列）一个 DMA。

### B2. DMA 作为 NoC 端点
- 用 `bsg_manycore_endpoint_standard` 将 DMA 接入 NoC，作为一个“固定坐标的节点”。
- 控制寄存器通过 NoC 访问，由 PE 下发。

### B3. DMA 控制面（建议寄存器集合）
- `DMA_SRC_ADDR`、`DMA_DST_ADDR`、`DMA_LEN`、`DMA_CFG`、`DMA_STATUS`、`DMA_START`。
- PE 通过 remote store 写入，DMA 完成后可：
  - 轮询 `DMA_STATUS`；或
  - 发回通知包（如写回某个 PE 地址）。

### B4. DMA 数据面连接方向
- **阵列内**：DMA 与 NoC 连接。
- **阵列外（DRAM）**：通过 vcache/wormhole/IO 侧连接 DRAM 端。
- 若你计划绕过 vcache 直连 DRAM，需新增专用接口与仲裁。

---

## C. 实际 DRAM 接口接入

### C1. 需要确认的硬件细节（关键）
- DRAM 类型（DDR4/DDR5/HBM2/LPDDR）。
- DRAM 控制器 IP 及其接口（AXI/Native/自定义）。
- 目标平台（FPGA/ASIC）。

### C2. 可复用的 IO/桥接模块（仓库已有）
- `v/chip/bsg_manycore_link_to_sdr_*.sv`
- `v/chip/bsg_manycore_link_wh_to_sdr_*.sv`
- `v/chip/bsg_manycore_hor_io_router*.sv`
这些看起来是 wormhole/ruche ↔ SDR/IO 方向的桥接参考，但是否匹配你的 DRAM 控制器接口需评估。

### C3. 可能的接入方式（两种路径）
- **路径1：复用现有 SDR/IO 适配层**
  - 在 `v/chip/` 中接入你的 DRAM 控制器。
  - 通过 wormhole/ruche 路由将 DRAM 请求送出。
- **路径2：新增 DRAM wrapper**
  - 在 pod 或更高层添加 DRAM wrapper，提供 NoC/Wormhole 接口与 DRAM 控制器接口转换。

---

## D. mesh 与 ruche 双路线的落地点

### mesh 路线
- 顶层选择：`e_network_mesh` → `bsg_manycore_pod_mesh_array`
- 重点改动：
  - `v/bsg_manycore_tile_compute_array_mesh.sv`（插入 DMA）
  - `v/bsg_manycore_tile_compute_mesh.sv`（只要 PE 接口没变可不动）

### ruche 路线
- 顶层选择：`e_network_half_ruche_x` → `bsg_manycore_pod_ruche_array`
- 重点改动：
  - `v/bsg_manycore_tile_compute_array_ruche.sv`（插入 DMA）
  - 处理 ruche link stitch 时，DMA 端点坐标/端口选择需明确

---

## E. 修改清单（按优先级）

### 必改
1) `v/bsg_manycore_hetero_socket.sv`（统一使用你的 PE）
2) 你的新 PE 模块（基于 `v/bsg_manycore_accel_default.sv`）
3) `hetero_type_vec_p` 的默认赋值（保证全阵列选同一类型）

### 必改（DMA）
4) `v/bsg_manycore_tile_compute_array_mesh.sv`（mesh DMA）
5) `v/bsg_manycore_tile_compute_array_ruche.sv`（ruche DMA）
6) 新 DMA 模块（建议放 `v/` 下）

### 可能要改（DRAM 实际接口）
7) `v/chip/` 相关 SDR/IO/WH 桥接模块
8)（或）新增 DRAM wrapper 与仲裁模块

---

## F. 关键待确认项（请你提供）
1) **DMA 节点坐标/地址映射**：阵列内固定坐标还是阵列外虚拟节点？
2) **DRAM 控制器类型与接口**：AXI 还是自定义？
3) **DMA 完成通知机制**：轮询还是主动消息？
4) **是否保留 vcache 作为 DRAM 前端**：若保留，DMA 走 vcache；若不保留，需要单独直连路径。

---

## G. 迁移说明
- 无迁移，直接替换（前提：仅替换处理单元与新增 DMA/DRAM 接口，不保留旧处理单元）。

---

## 参考路径
- `v/bsg_manycore_hetero_socket.sv`
- `v/bsg_manycore_accel_default.sv`
- `v/bsg_manycore_tile_compute_array_mesh.sv`
- `v/bsg_manycore_tile_compute_array_ruche.sv`
- `v/bsg_manycore_pod_mesh_array.sv`
- `v/bsg_manycore_pod_ruche_array.sv`
- `v/chip/bsg_manycore_link_to_sdr_*.sv`
- `v/chip/bsg_manycore_link_wh_to_sdr_*.sv`

---

## 任务日志（结束）
- 已按你提供的 4 点配置细化修改范围与落地路径，并覆盖 mesh/ruche 双路线。
- 未访问外部网络，未生成 `evidence/` 存档。

## 工具调用简报
- 使用 shell 将本 Markdown 写入 `docs/noc_mod_plan.md`。
