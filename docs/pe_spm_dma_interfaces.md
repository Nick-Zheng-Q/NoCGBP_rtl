# PE / SPM / DMA 接口细化规范（面向NoC接入）

## 前置说明
- 本文为接口细化文档，基于本仓库现有 NoC/endpoint 习惯用法整理；不引入外部资料。
- 假设：PE 通过 `bsg_manycore_endpoint_standard` 连接 NoC；SPM 仅本地可见；DMA 通过 NoC 受控并对接实际 DRAM 接口。
- 不确定性：DRAM 控制器具体协议（AXI/自定义）与 DMA 完成通知方式未最终确认。

## 任务日志（开始）
- 日期：2026-01-28
- 目标：明确 PE/SPM/DMA 三个模块的接口细化版本（端口/参数/行为约束）。
- 输入来源：本仓库接口约定与先前分析结论。

---

## 1. PE 模块接口（对接 hetero_socket）

### 1.1 顶层端口（必须对齐 hetero_socket）
**端口清单（方向/位宽/功能）**

| 信号 | 方向（相对 PE） | 位宽 | 功能 |
| --- | --- | --- | --- |
| `clk_i` | input | 1 | 系统时钟。 |
| `reset_i` | input | 1 | 同步复位，高电平有效。 |
| `link_sif_i` | input | `[bsg_manycore_link_sif_width_lp-1:0]` | NoC 输入链路 bundle（fwd/rev 通道）。 |
| `link_sif_o` | output | `[bsg_manycore_link_sif_width_lp-1:0]` | NoC 输出链路 bundle（fwd/rev 通道）。 |
| `barrier_data_i` | input | 1 | barrier 同步令牌输入。 |
| `barrier_data_o` | output | 1 | barrier 同步令牌输出。 |
| `barrier_src_r_o` | output | `[barrier_dirs_p-1:0]` | barrier 源方向寄存值（路由/仲裁）。 |
| `barrier_dest_r_o` | output | `[barrier_lg_dirs_lp-1:0]` | barrier 目的方向寄存值（路由/仲裁）。 |
| `my_x_i` | input | `[x_subcord_width_lp-1:0]` | tile 在 pod 内的 X 坐标。 |
| `my_y_i` | input | `[y_subcord_width_lp-1:0]` | tile 在 pod 内的 Y 坐标。 |
| `pod_x_i` | input | `[pod_x_cord_width_p-1:0]` | pod X 坐标。 |
| `pod_y_i` | input | `[pod_y_cord_width_p-1:0]` | pod Y 坐标。 |

**link_sif bundle（`bsg_manycore_link_sif_s`）字段**

> `link_sif_i`/`link_sif_o` 均为 `bsg_manycore_link_sif_s`，内部包含 `fwd`（请求）与 `rev`（返回）两个 ready/valid 通道。

| 通道 | 字段 | 位宽 | 功能 |
| --- | --- | --- | --- |
| `fwd` | `v` | 1 | 前向请求有效。 |
| `fwd` | `ready_and_rev` | 1 | 前向通道就绪/credit 返回握手。 |
| `fwd` | `data` | `[packet_width_lp-1:0]` | 请求包 payload。 |
| `rev` | `v` | 1 | 返回包有效。 |
| `rev` | `ready_and_rev` | 1 | 返回通道就绪/credit 返回握手。 |
| `rev` | `data` | `[return_packet_width_lp-1:0]` | 返回包 payload。 |

其中：
- `packet_width_lp = bsg_manycore_packet_width(addr_width_p, data_width_p, x_cord_width_p, y_cord_width_p)`
- `return_packet_width_lp = bsg_manycore_return_packet_width(x_cord_width_p, y_cord_width_p, data_width_p)`
- `bsg_manycore_link_sif_width_lp = bsg_ready_and_link_sif_width(packet_width_lp) + bsg_ready_and_link_sif_width(return_packet_width_lp)`

### 1.2 参数（建议与现有宏一致）
- `x_cord_width_p`, `y_cord_width_p`
- `addr_width_p`, `data_width_p`
- `dmem_size_p`, `vcache_size_p`, `vcache_block_size_in_words_p`, `vcache_sets_p`
- `icache_entries_p`, `icache_tag_width_p`, `icache_block_size_in_words_p`
- `num_tiles_x_p`, `num_tiles_y_p`
- `pod_x_cord_width_p`, `pod_y_cord_width_p`
- `fwd_fifo_els_p`, `rev_fifo_els_p`
- `barrier_dirs_p`, `ipoly_hashing_p`, `debug_p`
- `x_subcord_width_lp = clog2(num_tiles_x_p)`
- `y_subcord_width_lp = clog2(num_tiles_y_p)`
- `barrier_lg_dirs_lp = clog2(barrier_dirs_p+1)`

### 1.3 PE 与 NoC 的内部接口（推荐使用 endpoint_standard）
> 复用 `bsg_manycore_endpoint_standard` 时，PE 需要处理以下语义接口。

**入站请求（NoC → PE）**

| 信号 | 方向（相对 PE） | 位宽 | 功能 |
| --- | --- | --- | --- |
| `in_v_o` | output | 1 | 请求有效。 |
| `in_addr_o` | output | `[addr_width_p-1:0]` | 请求地址。 |
| `in_data_o` | output | `[data_width_p-1:0]` | 写数据/负载数据。 |
| `in_mask_o` | output | `[(data_width_p>>3)-1:0]` | 字节掩码。 |
| `in_we_o` | output | 1 | 写/读指示：1=写，0=读。 |
| `in_load_info_o` | output | `$bits(bsg_manycore_load_info_s)` | load 元信息（浮点/原子等）。 |
| `in_src_x_cord_o` | output | `[x_cord_width_p-1:0]` | 请求源 X 坐标。 |
| `in_src_y_cord_o` | output | `[y_cord_width_p-1:0]` | 请求源 Y 坐标。 |
| `in_yumi_i` | input | 1 | PE 消费确认。 |

**出站响应（PE → NoC）**

| 信号 | 方向（相对 PE） | 位宽 | 功能 |
| --- | --- | --- | --- |
| `returning_data_i` | input | `[data_width_p-1:0]` | 读/响应数据。 |
| `returning_v_i` | input | 1 | 响应有效。 |

**出站请求（PE → NoC）**

| 信号 | 方向（相对 PE） | 位宽 | 功能 |
| --- | --- | --- | --- |
| `out_v_i` | input | 1 | 发起请求有效。 |
| `out_packet_i` | input | `[packet_width_lp-1:0]` | 请求包 payload。 |
| `out_credit_or_ready_o` | output | 1 | 允许发送（credit/ready 语义）。 |

**入站响应（NoC → PE）**

| 信号 | 方向（相对 PE） | 位宽 | 功能 |
| --- | --- | --- | --- |
| `returned_data_r_o` | output | `[data_width_p-1:0]` | 返回数据。 |
| `returned_reg_id_r_o` | output | `[bsg_manycore_reg_id_width_gp-1:0]` | 返回寄存器 ID。 |
| `returned_v_r_o` | output | 1 | 返回有效。 |
| `returned_pkt_type_r_o` | output | `$bits(bsg_manycore_return_packet_type_e)` | 返回包类型。 |
| `returned_fifo_full_o` | output | 1 | 返回 FIFO 满指示。 |
| `returned_credit_v_r_o` | output | 1 | credit 返回有效。 |
| `returned_credit_reg_id_r_o` | output | `[bsg_manycore_reg_id_width_gp-1:0]` | credit 关联寄存器 ID。 |
| `returned_yumi_i` | input | 1 | PE 消费确认。 |

**调试/信用统计**

| 信号 | 方向（相对 PE） | 位宽 | 功能 |
| --- | --- | --- | --- |
| `out_credits_used_o` | output | `[credit_counter_width_p-1:0]` | 预期待归还 credit 数。 |
| `global_x_i` | input | `[x_cord_width_p-1:0]` | 全局 X 坐标（调试）。 |
| `global_y_i` | input | `[y_cord_width_p-1:0]` | 全局 Y 坐标（调试）。 |

### 1.4 行为约束（必须遵守）
- endpoint 默认请求-响应串行：一次请求处理完并返回后再取下一请求。
- 若 PE 不打算使用 Barrier，必须在 PE 内部安全消耗或直通相关信号。
- 若 PE 不使用 credit 接口，需确保 tile 内 fwd/rev 配置与 endpoint 一致。

---

## 2. SPM（ScratchPad Memory）接口（本地专用）

### 2.1 目标与约束
- SPM 仅供本 PE 使用，不对 NoC 公开。
- 地址空间由 PE 内部定义，不占用全局 NoC 地址。

### 2.2 推荐端口（单口同步RAM风格）
- 时钟/复位
  - `spm_clk_i`：SPM 时钟（可复用 `clk_i`），控制读写时序。
  - `spm_reset_i`：SPM 复位（可复用 `reset_i`），复位内部状态/输出。
- 读写端口（单端口）
  - `spm_v_i`：访问有效；为 1 表示当前周期有读/写请求。
  - `spm_wen_i`：写使能；1=写请求，0=读请求。
  - `spm_addr_i`：本地 SPM 地址；宽度由 SPM 深度决定。
  - `spm_wdata_i`：写数据；宽度与 PE 数据宽度一致。
  - `spm_wmask_i`：字节掩码；每字节 1 位，1 表示写入对应字节。
  - `spm_rdata_o`：读数据；在同步读时于下一拍输出。
  - `spm_ready_o`：可选握手；为 1 表示本周期可接受请求（用于多周期/仲裁场景）。

### 2.3 时序建议
- 同步读：`spm_rdata_o` 在 `spm_v_i & ~spm_wen_i` 后一拍有效。
- 写：`spm_wen_i` 高时，在时钟沿写入。
- 若需要多周期或仲裁，建议显式增加 `spm_ready_o`。

---

## 3. DMA 模块接口（阵列级，PE 控制）

### 3.1 角色定位
- 每个 tile 子阵列配一个 DMA。
- DMA 作为 NoC 端点存在（使用 endpoint_standard 访问其寄存器）。
- DMA 与 DRAM 接口连接（具体协议待定）。

### 3.2 控制面接口（NoC 侧寄存器 + endpoint_standard）
> DMA 作为 NoC 端点暴露寄存器，接口与 `bsg_manycore_endpoint_standard` 一致。

**DMA 端点信号（与 1.3 相同，但方向相对 DMA）**

| 信号 | 方向（相对 DMA） | 位宽 | 功能 |
| --- | --- | --- | --- |
| `link_sif_i` | input | `[bsg_manycore_link_sif_width_lp-1:0]` | NoC 输入链路 bundle。 |
| `link_sif_o` | output | `[bsg_manycore_link_sif_width_lp-1:0]` | NoC 输出链路 bundle。 |
| `in_v_o` | output | 1 | DMA 寄存器访问请求有效。 |
| `in_addr_o` | output | `[addr_width_p-1:0]` | DMA 寄存器地址。 |
| `in_data_o` | output | `[data_width_p-1:0]` | 写入数据。 |
| `in_mask_o` | output | `[(data_width_p>>3)-1:0]` | 字节掩码。 |
| `in_we_o` | output | 1 | 1=写寄存器，0=读寄存器。 |
| `in_yumi_i` | input | 1 | DMA 消费确认。 |
| `returning_data_i` | input | `[data_width_p-1:0]` | 读寄存器返回数据。 |
| `returning_v_i` | input | 1 | 读寄存器返回有效。 |
| `returned_data_r_o` | output | `[data_width_p-1:0]` | NoC 返回数据。 |
| `returned_v_r_o` | output | 1 | NoC 返回有效。 |
| `returned_yumi_i` | input | 1 | DMA 消费确认。 |

**寄存器建议（均为 `data_width_p` 宽度，字对齐）**
| 寄存器 | 方向 | 位宽 | 功能 |
| --- | --- | --- | --- |
| `DMA_SRC_ADDR` | PE→DMA | `data_width_p` | 源地址起始。 |
| `DMA_DST_ADDR` | PE→DMA | `data_width_p` | 目的地址起始。 |
| `DMA_LEN` | PE→DMA | `data_width_p` | 传输长度（字节/字数需统一）。 |
| `DMA_CFG` | PE→DMA | `data_width_p` | 方向/突发长度/对齐策略/中断开关。 |
| `DMA_START` | PE→DMA | `data_width_p` | 写 1 启动事务（建议自清零）。 |
| `DMA_STATUS` | DMA→PE | `data_width_p` | 忙/完成/错误码。 |

**寄存器建议**
- `DMA_SRC_ADDR`：源地址；DRAM/NoC 空间中的读取起始地址。
- `DMA_DST_ADDR`：目的地址；DRAM/NoC 空间中的写入起始地址。
- `DMA_LEN`：传输长度（字节或字数，需统一定义）。
- `DMA_CFG`：配置位；包含方向、突发长度、对齐策略、可选中断使能等。
- `DMA_START`：启动寄存器；写 1 触发 DMA 事务（建议自清零）。
- `DMA_STATUS`：状态寄存器；包含忙/完成/错误码等标志。

**访问方式**
- PE → NoC → DMA endpoint：写寄存器启动传输。
- DMA 完成后更新 `DMA_STATUS`，并执行写回（已确认采用写回策略）。

**写回策略（已确认）**
- 写回目标地址：固定为 **PE 本地地址**（由 DMA 寄存器配置一次即可）。
- 写回数据格式：**状态码 + 递增序号**（单个字内组合，便于硬件区分多次完成事件）。

### 3.3 数据面接口（DRAM 侧）
**AXI 接口（参考 `bsg_cache_to_axi_hashed`）**

| 信号 | 方向（相对 DMA） | 位宽 | 功能 |
| --- | --- | --- | --- |
| `axi_awid_o` | output | `[axi_id_width_p-1:0]` | 写地址 ID。 |
| `axi_awaddr_o` | output | `[axi_addr_width_p-1:0]` | 写地址。 |
| `axi_awlen_o` | output | `[7:0]` | 写突发长度。 |
| `axi_awsize_o` | output | `[2:0]` | 写突发大小。 |
| `axi_awburst_o` | output | `[1:0]` | 写突发类型。 |
| `axi_awcache_o` | output | `[3:0]` | 缓存属性。 |
| `axi_awprot_o` | output | `[2:0]` | 保护属性。 |
| `axi_awlock_o` | output | 1 | 写锁。 |
| `axi_awvalid_o` | output | 1 | 写地址有效。 |
| `axi_awready_i` | input | 1 | 写地址就绪。 |
| `axi_wdata_o` | output | `[axi_data_width_p-1:0]` | 写数据。 |
| `axi_wstrb_o` | output | `[(axi_data_width_p>>3)-1:0]` | 写字节使能。 |
| `axi_wlast_o` | output | 1 | 写最后拍。 |
| `axi_wvalid_o` | output | 1 | 写数据有效。 |
| `axi_wready_i` | input | 1 | 写数据就绪。 |
| `axi_bid_i` | input | `[axi_id_width_p-1:0]` | 写响应 ID。 |
| `axi_bresp_i` | input | `[1:0]` | 写响应码。 |
| `axi_bvalid_i` | input | 1 | 写响应有效。 |
| `axi_bready_o` | output | 1 | 写响应就绪。 |
| `axi_arid_o` | output | `[axi_id_width_p-1:0]` | 读地址 ID。 |
| `axi_araddr_o` | output | `[axi_addr_width_p-1:0]` | 读地址。 |
| `axi_arlen_o` | output | `[7:0]` | 读突发长度。 |
| `axi_arsize_o` | output | `[2:0]` | 读突发大小。 |
| `axi_arburst_o` | output | `[1:0]` | 读突发类型。 |
| `axi_arcache_o` | output | `[3:0]` | 缓存属性。 |
| `axi_arprot_o` | output | `[2:0]` | 保护属性。 |
| `axi_arlock_o` | output | 1 | 读锁。 |
| `axi_arvalid_o` | output | 1 | 读地址有效。 |
| `axi_arready_i` | input | 1 | 读地址就绪。 |
| `axi_rid_i` | input | `[axi_id_width_p-1:0]` | 读响应 ID。 |
| `axi_rdata_i` | input | `[axi_data_width_p-1:0]` | 读数据。 |
| `axi_rresp_i` | input | `[1:0]` | 读响应码。 |
| `axi_rlast_i` | input | 1 | 读最后拍。 |
| `axi_rvalid_i` | input | 1 | 读数据有效。 |
| `axi_rready_o` | output | 1 | 读数据就绪。 |

**自定义 DRAM 接口（若不使用 AXI）**

| 信号 | 方向（相对 DMA） | 位宽 | 功能 |
| --- | --- | --- | --- |
| `dram_req_v_o` | output | 1 | DRAM 请求有效。 |
| `dram_req_ready_i` | input | 1 | DRAM 就绪。 |
| `dram_req_addr_o` | output | `[addr_width_p-1:0]` | DRAM 地址。 |
| `dram_req_wdata_o` | output | `[data_width_p-1:0]` | 写数据。 |
| `dram_req_we_o` | output | 1 | 写使能：1=写，0=读。 |
| `dram_resp_v_i` | input | 1 | 读响应有效。 |
| `dram_resp_data_i` | input | `[data_width_p-1:0]` | 读响应数据。 |

### 3.4 DMA 与 NoC 端点接口（内部）
- 可复用 `bsg_manycore_endpoint_standard`，其语义同 PE 端点。
- DMA 端点坐标/地址映射需明确（阵列内固定坐标或虚拟 I/O 坐标）。

### 3.5 行为约束
- 需要处理对齐与跨页边界（由 DMA_CFG 控制）。
- 若 NoC 侧采用 credit 流控，DMA 端点要遵守 credit 规则。

---

## 4. 交互关系总览（简化）
```
PE  <-->  endpoint_standard  <-->  NoC  <-->  DMA endpoint  <-->  DRAM IF
PE  <-->  SPM (本地 RAM)
```

---

## 5. 关键待确认项
1) DRAM 控制器接口类型（AXI/自定义）
2) DMA 完成通知机制（轮询/中断/写回）
3) DMA 端点坐标/地址映射策略
4) 写回目标地址选择（固定PE本地地址或全局地址）
5) 写回数据格式（状态码/序号/组合）
6) SPM 是否需要握手/多周期访问

---

## 迁移说明
- 无迁移，直接替换。

---

## 任务日志（结束）
- 已细化 PE/SPM/DMA 接口与行为约束，并给出端口与寄存器建议。
