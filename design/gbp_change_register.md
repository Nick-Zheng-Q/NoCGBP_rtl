# GBP 变更清单

日期：2026-04-10
状态：持续维护

## 1. 目的
- 维护“我们要修改的内容”。
- 每个修改项必须回答四个问题：
  1. 为什么改
  2. 改哪些模块
  3. 改哪些接口或时序
  4. 现在处于什么状态

## 2. 状态定义
- `待开始`：已确认要改，尚未动手
- `进行中`：正在修改或定位中
- `已落地待验证`：RTL/测试已改，但还没完成可信回归
- `已完成`：实现与验证都闭环
- `已废弃`：该方向不再继续

## 3. 当前修改项

| ID | 主题 | 当前问题 | 目标修改 | 影响模块/接口 | 状态 | 证据/来源 |
|---|---|---|---|---|---|---|
| CR-001 | Verilator 命名基线统一 | `nocbp_verilator` 文档要求 `<test>_top`，但 Makefile 默认 `TOP=$(TEST)`，最小示例都跑不通 | 统一 `Makefile`、README、ADDING_TESTS、YAML 规则，强制 wrapper/顶层命名一致 | `nocbp_verilator/Makefile`、`README.md`、`ADDING_TESTS.md`、测试 YAML | 待开始 | `evidence/2026-04-09_gbp_rtl_verilator_review.md` |
| CR-002 | 替换假 DUT 单测 | `tests/unit/gbp_pe.cc` 依赖 stub + 源码字符串扫描，不能证明真实行为 | 改成真实 `v/gbp_pe/gbp_pe.sv` wrapper + 行为断言 | `tests/unit/gbp_pe.cc`、`tops/unit/gbp_pe_*`、可能新增 filelist | 待开始 | 同上 |
| CR-003 | 清理旧版 `v/pe/gbp_pe.sv` 回归路径 | `gbp_pe_fpu` 仍绑定旧版模块，且当前配置直接坏掉 | 将旧实验模块从主回归矩阵隔离；若保留则必须改名/改路径 | `tests/unit/gbp_pe_fpu.*`、`v/pe/gbp_pe.sv` | 待开始 | 同上 |
| CR-004 | 补齐 compute 占位实现 | `compute_unit_wrapper`、`gbp_control_fsm`、`gbp_compute_engine` 仍有 placeholder/TODO | 逐项补齐 factor/variable 计算链和除法相关路径 | `compute_unit_wrapper.sv`、`compute/gbp_control_fsm.sv`、`compute/gbp_compute_engine.sv` | 进行中 | `design/gbp_system_topdown.md`、`evidence/2026-04-09_gbp_rtl_verilator_review.md` |
| CR-005 | whitebox 长程推进卡点 | 4PE whitebox 虽已从 0 提升到 539 次事件，但仍未完成一轮 | 继续定位 `control_unit_gbp` / `compute_unit_wrapper` / `write_stream_engine` 的长程无进展点 | `control_unit_gbp.sv`、`read_stream_engine.sv`、`write_stream_engine.sv`、`compute_unit_wrapper.sv` | 进行中 | `evidence/2026-03-27_gbp_pe_mesh_whitebox_dpi_spm.md` |
| CR-006 | 系统文档先于 RTL 修改 | 过去系统定义分散，容易“边改边解释” | 以后所有接口/时序变化先落 `design/gbp_system_topdown.md` | `design/gbp_system_topdown.md`、所有相关 RTL | 已完成 | 本次变更 |

## 4. 已落地但仍需回归验证的修改

| ID | 已落地内容 | 当前判断 | 后续验证要求 | 来源 |
|---|---|---|---|---|
| VR-001 | `spm_subsystem` 从静态 bank 路由改为按地址 bank bits 动态路由 | 已修掉 compute 写回误清空 `bank0` 的主问题，但不是最终根因 | 需要继续验证长程进度、`rd_rsp` 对齐、无额外 bank 串扰 | `evidence/2026-03-27_gbp_pe_mesh_whitebox_dpi_spm.md` |
| VR-002 | `read_stream_engine` 修正 descriptor 提前接受与 META 污染 | 已把白盒进度从极低值提升到 539，但仍会卡住 | 需要继续验证高 `meta_row` 区段的流控释放 | 同上 |
| VR-003 | `compute_unit_wrapper` 修正 `compute_done -> rsp_done` 双重等待 | 已经不再卡死在第一轮写回前 | 需要在长程工作负载下验证没有新的 completion/backpressure 退化 | 同上 |

## 5. 当前不应继续投入的方向

| ID | 方向 | 原因 | 状态 |
|---|---|---|---|
| AB-001 | 继续把 `nocbp_verilator/tests/unit/gbp_pe.cc` 当成真实回归 | 它测的是 stub 和源码片段，不是主路径行为 | 已废弃 |
| AB-002 | 把旧 `v/pe/gbp_pe.sv` 的测试结果当成当前 GBP 主系统结论 | 当前主路径已切到 `v/gbp_pe/*` | 已废弃 |
| AB-003 | 把“白盒 mesh 能启动”解释成“GBP 已收敛” | 现有证据明确显示未完成一轮 | 已废弃 |

## 6. 使用规则
1. 新任务进入前，先在本文件新增条目，再开始改 RTL。
2. 条目状态每次只允许单向推进，不允许无记录回退。
3. 如果某次回归推翻原判断，保留旧条目并补充“结论修正”，不要覆盖历史。
