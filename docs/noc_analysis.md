# bsg_manycore NoC/HDL 结构与替换处理单元说明

## 前置说明
- 顺序思考工具（Sequential-Thinking MCP）在当前会话不可用，无法按指南调用；已改为显式记录假设与不确定性，并在日志中说明偏差原因。
- 运行环境为只读沙箱，需要提升权限才能写入文件，本文件由工具写入。
- 输入来源为本仓库本地 HDL 与 testbench 文件；未访问外部网络。

## 任务日志（开始）
- 日期：2026-01-28
- 目标：梳理 HDL 顶层/层级关系，回答 ruche vs mesh、拓扑与代码对应、替换处理单元方式三项问题；并归档说明。
- 输入来源：`v/` 与 `testbenches/` 目录。
- 不确定性：你关注的“顶层”可能指仿真 testbench 或芯片级顶层；文中以 testbench 顶层与 pod/array 顶层并列说明。

## 一、HDL 顶层与层级组织（原分析内容归档）

### 1) 仿真环境的“顶层”
- `testbenches/common/v/bsg_nonsynth_manycore_testbench.sv`
  - 作用：完整系统级 testbench。
  - 关键例化：
    - `bsg_nonsynth_manycore_tag_master`（tag 初始化/复位分发）
    - 根据 `bsg_manycore_network_cfg_p` 选择：
      - `bsg_manycore_pod_ruche_array`（半 ruche X 网络）
      - 或 `bsg_manycore_pod_mesh_array`（纯 mesh 网络）
- `testbenches/mesh_example/mesh_top_example.sv`
  - 作用：最小 mesh 示例。
  - 关键例化：
    - `bsg_manycore_mesh_node`（两节点路由器）
    - `mesh_master_example` / `mesh_slave_example`
    - `bsg_manycore_link_sif_tieoff`（未使用端口封口）
- `testbenches/common/v/bsg_manycore_top_crossbar.sv`
  - 作用：crossbar 网络 + vanilla core 阵列的简化顶层。
  - 关键例化：
    - `bsg_manycore_crossbar`
    - 多个 `bsg_manycore_proc_vanilla`

### 2) Pod 级网络层级（ruche / mesh）
- `bsg_manycore_pod_ruche_array`
  - 例化：
    - 每个 pod 一个 `bsg_tag_client` + `bsg_dff_chain` 复位链
    - 每行一个 `bsg_manycore_pod_ruche_row`
- `bsg_manycore_pod_ruche_row`
  - 例化：
    - 每个 pod 一个 `bsg_manycore_pod_ruche`
    - 同行 pod 的水平/ruche/barrier 链路互连
- `bsg_manycore_pod_ruche`
  - 例化：
    - 顶/底 vcache：`bsg_manycore_tile_vcache_array`
    - 计算子阵列：`bsg_manycore_tile_compute_array_ruche`

- `bsg_manycore_pod_mesh_array / bsg_manycore_pod_mesh_row / bsg_manycore_pod_mesh`
  - 与 ruche 版本对应：mesh 版本在 `bsg_manycore_pod_mesh` 内例化 `bsg_manycore_tile_compute_array_mesh`，其余结构类似。

### 3) Tile 计算阵列层级
- `bsg_manycore_tile_compute_array_mesh`
  - 例化：
    - 多个 `bsg_manycore_tile_compute_mesh`
    - `bsg_mesh_router_buffered` 由 tile 内部的 `bsg_manycore_mesh_node` 间接例化
- `bsg_manycore_tile_compute_array_ruche`
  - 例化：
    - 多个 `bsg_manycore_tile_compute_ruche`
    - ruche link 与 barrier ruche 的 stitch

### 4) 单 Tile（计算节点）内部结构
- `bsg_manycore_tile_compute_mesh`
  - 例化：
    - `bsg_manycore_mesh_node`（fwd/rev 路由）
    - `bsg_barrier`（barrier 网络）
    - `bsg_manycore_hetero_socket`（选择具体计算单元）
- `bsg_manycore_mesh_node`
  - 例化：
    - `bsg_mesh_router_buffered`（fwd）
    - `bsg_mesh_router_buffered`（rev）
- `bsg_manycore_hetero_socket`
  - 例化（按 `hetero_type_p` 选择）：
    - `bsg_manycore_proc_vanilla`
    - `bsg_manycore_gather_scatter`
    - `bsg_manycore_accel_default`（多类型默认映射）

### 5) 计算单元内部依赖
- `bsg_manycore_proc_vanilla` 内部使用 `network_tx` / `network_rx` 等网络接口模块。
- `bsg_manycore_accel_default` 内部通过 `bsg_manycore_endpoint_standard` 对接 NoC。

## 二、问题回答

### Q1. ruche 和 mesh 的区别是什么？
- **mesh**：标准 2D 网格，节点只与四邻（N/S/E/W）直连，路由按维度顺序（XY）。
- **ruche**：在 mesh 的基础上增加“跨越式/跳跃式”链路（该仓库主要是 X 方向 ruche），减少跳数与延迟，提升带宽，但布线更复杂。
- 在本项目里：
  - `e_network_mesh` 使用纯 mesh（对应 `bsg_manycore_pod_mesh_array` → `tile_compute_array_mesh`）。
  - `e_network_half_ruche_x` 使用带 X 方向 ruche 的网络（对应 `bsg_manycore_pod_ruche_array` → `tile_compute_array_ruche`）。

### Q2. 现在是什么拓扑结构？和代码结构的关系是什么，有对应吗？
- **“现在”不是唯一固定拓扑**：由 `bsg_manycore_network_cfg_p` 参数选择。
  - `e_network_half_ruche_x` → `bsg_manycore_pod_ruche_array`（ruche 拓扑）
  - `e_network_mesh` → `bsg_manycore_pod_mesh_array`（mesh 拓扑）
- **拓扑与代码层级一一对应**：
  - pod array → pod row → pod → tile array → tile → mesh_node/router
  - ruche 与 mesh 的区别体现在 pod/tile array 的实例化模块不同。

### Q3. 更换处理单元只需要更换 hetero_socket 吗？
- **结论：通常还需要配套修改，不仅仅是 hetero_socket。**
- `bsg_manycore_hetero_socket` 负责根据 `hetero_type_p` 选择处理单元模块，但要“生效”，还需要：
  - 在顶层传入/设置 `hetero_type_vec_p`，让目标 tile 选择你的类型；
  - 确保你的处理单元端口/参数与 hetero_socket 期望一致；
  - 若更改流控策略（credit/ready），还需配合 `bsg_manycore_tile_compute_mesh` 内的 credit 设置与 endpoint 使用方式。
- 若你想“只用 NoC，自己搭接口”：可绕过 hetero_socket，直接用 `bsg_manycore_mesh_node` + `bsg_manycore_endpoint_standard` 自建节点，但这已超出“只改 hetero_socket”的范围。

## 三、最小改动建议（针对替换处理单元）
- 复制 `bsg_manycore_accel_default` 为你的模块，复用 `bsg_manycore_endpoint_standard`。
- 在 `bsg_manycore_hetero_socket` 中新增/替换类型映射到你的模块。
- 在顶层 `hetero_type_vec_p` 为目标 tile 指定该类型编号。
- 如需不同流控方式，调整 tile 内 `fwd_use_credits` / `rev_use_credits` 与 endpoint 逻辑。

## 四、迁移说明
- 无迁移，直接替换（仅限于在 hetero 类型映射处替换处理单元场景）。

## 五、参考文件（本地路径）
- `testbenches/common/v/bsg_nonsynth_manycore_testbench.sv`
- `testbenches/mesh_example/mesh_top_example.sv`
- `testbenches/common/v/bsg_manycore_top_crossbar.sv`
- `v/bsg_manycore_pod_ruche_array.sv`
- `v/bsg_manycore_pod_ruche_row.sv`
- `v/bsg_manycore_pod_ruche.sv`
- `v/bsg_manycore_pod_mesh_array.sv`
- `v/bsg_manycore_pod_mesh_row.sv`
- `v/bsg_manycore_pod_mesh.sv`
- `v/bsg_manycore_tile_compute_array_mesh.sv`
- `v/bsg_manycore_tile_compute_array_ruche.sv`
- `v/bsg_manycore_tile_compute_mesh.sv`
- `v/bsg_manycore_mesh_node.sv`
- `v/bsg_manycore_hetero_socket.sv`
- `v/bsg_manycore_accel_default.sv`
- `v/vanilla_bean/bsg_manycore_proc_vanilla.sv`
- `v/bsg_manycore_endpoint_standard.sv`

## 任务日志（结束）
- 已回答三项问题并将原先结构分析归档。
- 未访问外部网络，未生成 `evidence/` 存档（当前仅写入 `docs/`）。

## 工具调用简报
- 使用 `rg`、`sed`、`ls` 读取并确认模块层级与顶层选择逻辑；写入本文件。
