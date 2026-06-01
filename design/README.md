# GBP Top-Down 设计维护入口

日期：2026-04-10
状态：生效

## 目的
- 用统一入口维护 GBP RTL system 的 top-down 设计视图。
- 把“系统是什么”和“接下来要改什么”拆成两份长期文档，避免信息散落在 `v/gbp_pe/*.md`、`docs/`、`evidence/`、临时对话里。

## 文档列表
- `design/gbp_system_topdown.md`
  - 系统分层
  - 模块职责
  - 关键接口
  - 时序/握手规则
  - 当前已知缺口与边界
- `design/gbp_change_register.md`
  - 当前计划修改项
  - 每项修改的原因、影响模块、接口影响、状态
  - 已落地但待验证的修改

## 维护规则
1. 任何模块职责变化、端口变化、握手变化、时序变化，必须先更新 `design/gbp_system_topdown.md`，再改 RTL。
2. 任何新任务、重构、缺陷修复、白盒验证发现的主根因，必须先进入 `design/gbp_change_register.md`。
3. `v/gbp_pe/*.md` 可以继续保留为专题文档，但系统级权威入口以 `design/` 下两份文档为准。
4. `evidence/` 保留“发生了什么”的审计记录，`design/` 保留“系统定义是什么、准备改什么”的当前态文档。

## 当前权威范围
- 主路径：`v/gbp_pe/*`
- manycore 集成入口：`v/gbp_pe/gbp_pe.sv`
- PE 内部顶层：`v/gbp_pe/pe_top.sv`
- Verilator 框架：`nocbp_verilator/*`

## 当前不作为权威设计源的内容
- `v/pe/gbp_pe.sv`
  - 仅视为旧实验路径或遗留模块；除非后续明确恢复，否则不作为当前 GBP 主设计定义。
