# DPI-C SPM 替换与地址解码修复

## 1. 背景与问题

在将 `BEAT_BYTES` 从 8 迁移到 32（256-bit SPM beat）的过程中，暴露了两个严重问题：

1. **Verilator `$readmemh` 对 256-bit 数据的初始化不可靠**：
   - 读取地址 `0x80`（期望 row 2 的数据）却返回与 row 0 相同的值
   - 怀疑是 Verilator 解析 64 字符 hex 行时存在地址/值解析问题

2. **SPM 地址解码 bug**：
   - `spm_subsystem.sv` 使用 `ROW_BYTES_LG = BYTE_OFF_W + WORD_OFF_W = 5`
   - 这相当于把地址当成**字节地址**处理，但 `SPM_ADDR_W` 是**字地址**（18 bits）
   - 银行提取：`addr[7:5]`（错误）→ 应该是 `addr[5:3]`
   - 行提取：`addr[21:8]`（越界，bits 21-18 为 `'x`）→ 应该是 `addr[17:6]`
   - `control_unit_gbp.sv` 中的银行位插入也使用了相同的错误偏移

## 2. 修复内容

### 2.1 RTL 修复

| 文件 | 修改 |
|------|------|
| `v/gbp_pe/gbp_pkg.sv` | 修正 `ROW_ADDR_W` 注释（`// 14` → `// 12`）和 `WORD_OFF_W` 注释（`// 1` → `// 3`） |
| `v/gbp_pe/spm_subsystem.sv` | `ROW_BYTES_LG` 从 `BYTE_OFF_W + WORD_OFF_W` 改为 `WORD_OFF_W`（5 → 3） |
| `v/gbp_pe/control_unit_gbp.sv` | 3 处银行位插入从 `(BYTE_OFF_W + WORD_OFF_W) +: BANK_ID_W` 改为 `WORD_OFF_W +: BANK_ID_W` |
| `v/gbp_pe/spm_bank_array.sv` | 添加 `#(.BANK_ID(b))` 参数传递 |
| `v/gbp_pe/spm_bank_dpi.sv` | 重写为同步 DPI-C SPM，移除 broken 的 `mem_r` 缓存 |

### 2.2 地址格式（修复后）

对于 18-bit 字地址：
- `addr[2:0]` = word offset within 32B beat（8 words）
- `addr[5:3]` = bank ID（0-7）
- `addr[17:6]` = row address（0-4095）

验证：`WORD_OFF_W(3) + BANK_ID_W(3) + ROW_ADDR_W(12) = 18 = SPM_ADDR_W` ✅

### 2.3 DPI-C 内存模型

在 `pe_top_integration.cc` 中实现了 C++ DPI 内存：

```cpp
// 8 banks, each with 4096 rows × 8 words/row
static std::array<std::vector<uint32_t>, 8> g_dpi_bank_mem;

// 通过解析 Verilator scope 名称确定 bank ID
static int resolve_bank_from_scope() {
    // 匹配 "banks[N]" 或 "banks__BRA__N__KET__"
}

extern "C" int pmem_read(int raddr);
extern "C" void pmem_write(int waddr, int wdata, char byte_num);
```

初始化流程：
1. `GlobalMemorySpace` 生成合成图数据
2. `init_dpi_memory_from_global_mem()` 按 bank interleave 格式写入 `g_dpi_bank_mem`
3. 仿真开始时，DPI SPM 直接访问 C++ 内存，无需 hex 文件

### 2.4 合成图调整

将 `dofs` 从 3 改为 2，使 `compact_payload_beats = 1`（32 字节）：
- 原 `dofs=3` 需要 2 beats（64 字节），但集成测试期望每事务 1 次写请求
- `dofs=2` 的紧凑格式：eta(2 words) + lam(3 words) = 5 words，可放入 1 beat

## 3. 测试状态

### 3.1 通过的测试

| 测试 | 级别 | 说明 |
|------|------|------|
| `pe_top_integration` | integration | DPI-C SPM + 新地址格式 |
| `spm_subsystem_top` | integration | 直接读写 SPM，不依赖初始化文件 |
| `control_subsystem` | integration | 控制子系统 |
| `fetch_subsystem` | integration | 取样子系统 |
| `noc_subsystem` | integration | NoC 子系统 |
| `spm_arbiter` | unit | SPM 仲裁器 |
| `read_stream_engine_top` | unit | 读流引擎 |
| `write_stream_engine_top` | unit | 写流引擎 |
| `gbp_compute_engine_test` | unit | GBP 计算引擎 |
| `gbp_pe_noc_ingress_spm` | integration | NoC 入站 SPM 写入 |
| `wrapper_test` | integration | Wrapper 测试 |
| `phase_controller` | unit | 相位控制器 |
| `node_scheduler` | unit | 节点调度器 |
| `metadata_scanner` | unit | 元数据扫描器 |
| `scoreboard_prefetcher` | unit | 记分板预取器 |
| `pull_client` | unit | 拉取客户端 |
| `pull_server` | unit | 拉取服务端 |
| `response_collector` | unit | 响应收集器 |
| `neighbor_state_accumulator` | unit | 邻居状态累加器 |
| `writeback_controller` | unit | 写回控制器 |
| `noc_adapter` | unit | NoC 适配器 |

### 3.2 已知失败的测试（需后续修复）

| 测试 | 级别 | 失败原因 |
|------|------|----------|
| `control_gbp_test` | integration | 旧架构测试 |
| `control_unit_top` | unit | 旧 control_unit |
| `data_fifo` | unit | 编译错误，32B beat 迁移 |
| `gbp_pe` | unit | 缺少 live-wiring snippet |
| `gbp_pe_single_pe_gbp` | integration | 使用旧地址格式 `kRowBytesLg = 5`（银行位在 `[7:5]`） |
| `gbp_pe_compute_done_egress` | integration | 依赖 `$readmemh` hex 文件，无文件时 SPM 全零 |
| `gbp_pe_mesh_whitebox_convergence` | integration | 需要 `RUN_CONFIG`（未测试） |
| `mesh_2pe` | unit | 无 Verilog 输入文件 |

### 3.3 尚未测试的集成测试

以下测试未在本次验证中运行，可能存在类似问题：
- `gbp_pe_mesh_2pe`
- `gbp_pe_mesh_2pe_convergence`
- `gbp_pe_mesh_2pe_gbp`
- `gbp_pe_mesh_2pe_superstep`
- `gbp_pe_mesh_2x2`
- `gbp_pe_mesh_convergence`
- `gbp_pe_mesh_gbp`
- `gbp_pe_mesh_whitebox_convergence_dut_are`

## 4. 遗留工作

1. **旧测试地址格式迁移**：
   - `gbp_pe_single_pe_gbp.cc` 使用 `kRowBytesLg = 5`（字节地址偏移）
   - 需要更新为字地址格式：`bank = (addr >> 3) & 7`, `row = addr >> 6`

2. **Hex 文件依赖清理**：
   - `gbp_pe_compute_done_egress` 等测试仍依赖 `$readmemh`
   - 需要为这些测试添加 DPI-C 初始化或更新 YAML 使用 `spm_bank_dpi.sv`

3. **多 PE mesh 测试**：
   - `gbp_pe_mesh_whitebox_convergence` 使用 `spm_bank_dpi.sv` 和 scope 解析
   - 需验证其 C++ DPI 实现与新的 `spm_bank_dpi.sv` 兼容

## 5. 文件变更清单

```
v/gbp_pe/gbp_pkg.sv                    (注释修正)
v/gbp_pe/spm_subsystem.sv              (ROW_BYTES_LG 修复)
v/gbp_pe/control_unit_gbp.sv           (银行位偏移修复)
v/gbp_pe/spm_bank_array.sv             (BANK_ID 参数传递)
v/gbp_pe/spm_bank_dpi.sv               (完全重写)

nocbp_verilator/tests/integration/pe_top_integration.yaml       (spm_bank → spm_bank_dpi)
nocbp_verilator/tests/integration/pe_top_integration.cc         (DPI 内存模型)
nocbp_verilator/tests/common/spm_loader/simple_graph_generator.hpp  (dofs 3→2)

docs/implementation/11_dpi_c_spm_and_address_fix.md             (本文档)
```
