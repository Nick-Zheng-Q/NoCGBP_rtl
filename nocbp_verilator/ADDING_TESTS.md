# 如何添加新测试（Verilator + C++ 最小框架）

本文档说明如何在 `dv/verilator` 框架中新增 **unit / system** 两层级测试。

## 0. 目录与命名约定

只支持两个层次：`unit` 与 `system`。

```
dv/verilator/
  Makefile
  common/
    sim_main.cc
  tops/
    unit/
    system/
  tests/
    unit/
    system/
  build/
```

**命名规则（必须遵守）**
- 测试名：`<test>`
- DUT 顶层文件：`dv/verilator/tops/<level>/<test>_top.sv`
- DUT 顶层模块名：`<test>_top`
- C++ 测试文件：`dv/verilator/tests/<level>/<test>.cc`
- C++ include：`#include "V<test>_top.h"`

运行时指定：
```
make -C dv/verilator run LEVEL=<unit|system> TEST=<test>
```

---

## 1) 新建 DUT wrapper（SystemVerilog 顶层）

### 1.1 选择层级
- **unit**：单模块或小组合逻辑验证
- **system**：mesh + tile + PE 通路的整体验证

### 1.2 新建 wrapper 文件
路径示例：
```
dv/verilator/tops/unit/route_sel_top.sv
```

示例：
```systemverilog
module route_sel_top (
  input  logic clk,
  input  logic rst_n,
  input  logic [3:0] in_a,
  output logic [3:0] out_y
);
  // 在这里实例化你的 DUT，并把端口接到 wrapper 的端口上
  // tnoc_route_selector u_dut (...);
endmodule
```

**注意**：如果 DUT 依赖 package/interface，请在编译时加入对应 SV 文件（见第 3 节）。

---

## 2) 新建 C++ 测试代码

### 2.1 新建测试文件
路径示例：
```
dv/verilator/tests/unit/route_sel.cc
```

### 2.2 必须实现 `run_test`
`sim_main.cc` 会调用这个入口。

最小模板：
```cpp
#include <cstdio>
#include "verilated.h"
#include "Vroute_sel_top.h"

static void tick(Vroute_sel_top* dut) {
  dut->clk = 0; dut->eval();
  dut->clk = 1; dut->eval();
}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  auto* dut = new Vroute_sel_top;
  dut->rst_n = 0;
  tick(dut); tick(dut);
  dut->rst_n = 1;

  // 施加输入
  dut->in_a = 3;
  tick(dut);

  // 检查输出
  if (dut->out_y != 3) {
    std::fprintf(stderr, "FAIL: out_y=%u\n", dut->out_y);
    delete dut;
    return 1;
  }

  delete dut;
  return 0;
}
```

---

## 3) 编译/运行测试

### 3.1 基本命令
```
make -C dv/verilator run LEVEL=unit TEST=route_sel
```

### 3.2 用 YAML 绑定 RTL 依赖（推荐方式）
如果 DUT 需要更多 RTL 文件或 filelist，请为该测试创建 YAML 配置文件：

- 路径：`dv/verilator/tests/<level>/<test>.yaml`
- 该文件会被 Makefile 自动读取，无需命令行传参

支持的字段：
- `rtl_f`：verilator 的 `-f` filelist
- `rtl_sv`：额外 SV 源文件（列表或字符串）
- `incdirs`：include 目录（列表或字符串）
- `defines`：宏定义（列表或字符串）
- 可选高级覆盖：`top` / `top_sv` / `tb`

示例：
```yaml
# dv/verilator/tests/unit/route_sel.yaml
rtl_f: rtl/compile.f
incdirs:
  - rtl/common
defines:
  - TNOC_DEFAULT_DATA_WIDTH=32
```

之后仍然用同一条命令运行：
```
make -C dv/verilator run LEVEL=unit TEST=route_sel
```

---

## 4) Verilator 注意事项（必须了解）

- Verilator 主要支持 **可综合 SystemVerilog 子集**。
- 不支持 UVM / class / randomize / mailbox / queue 等高级 SV 测试特性。
- 建议所有测试行为放在 **C++** 中。
- 如果你在 SV wrapper 内写复杂 TB 行为，可能无法通过编译。

---

## 5) 常见问题

### Q1. 报错找不到 `V<test>_top.h`
- 检查 `TEST=<test>` 是否匹配模块名
- 检查顶层模块名是否为 `<test>_top`

### Q2. 报错 “Undefined module”
- 说明缺 RTL 依赖
- 用 `RTL_F` 或 `RTL_SV` 补齐依赖文件

### Q3. 需要波形
- 运行时加 `TRACE=1`
- 需要在 C++ 里启用 trace（如果你需要，我可以提供模板）

---

## 6) 一个最小完整示例（已经提供）

- DUT：`dv/verilator/tops/unit/example_top.sv`
- Test：`dv/verilator/tests/unit/example.cc`
- 运行：
```
make -C dv/verilator run LEVEL=unit TEST=example
```

---

如需我补充 “system 级 top 骨架模板” 或 “波形 trace 模板”，告诉我即可。

---

## 7) GBP Oracle Test Environment (Phase-1)

This section documents the reproducible command flow for the GBP phase-1 test environment in this repository.

### 7.1 Oracle Artifacts

- Expected oracle artifact: `tests/oracle/gbp_oracle_phase1.json`
- Observed oracle artifact: `tests/oracle/generated/gbp_oracle_phase1.json`
- Hardware-output oracle (unit): `tests/oracle/gbp_pe_hw_oracle_phase1.json`

### 7.2 Task 1 Contract Validation

```bash
g++ -std=c++17 tests/common/oracle_contract.cc tests/tools/oracle_contract_loader.cc -Itests/common -o /tmp/oracle_contract_loader
/tmp/oracle_contract_loader tests/oracle/gbp_oracle_contract_phase1.yaml
/tmp/oracle_contract_loader tests/oracle/gbp_oracle_contract_phase1_missing_rel_err.yaml
```

Expected markers:
- pass: `oracle contract parse: PASS`
- negative: `ERROR: missing required field: threshold_state_message_rel_err`

### 7.3 Generate Deterministic Oracle Artifact

```bash
python3 tests/tools/generate_gbp_oracle_phase1.py --seed 12345 --output tests/oracle/generated/gbp_oracle_phase1.json
```

Expected marker:
- `oracle artifact written: tests/oracle/generated/gbp_oracle_phase1.json`

### 7.4 Unit/Integration Matrix

```bash
make run LEVEL=unit TEST=control_unit_top
make run LEVEL=unit TEST=stream_dispatcher_top
make run LEVEL=unit TEST=gbp_pe
make run LEVEL=unit TEST=pe_unit
make run LEVEL=integration TEST=pe_top_integration VERILATOR="verilator -Wno-fatal -Wno-WIDTHCONCAT -Wno-EOFNEWLINE"
```

Expected pass markers:
- `All tests passed` (unit tests)
- `gbp_pe: stimulus-control checks passed`
- `pe_unit: function checks passed`
- `pe_top integration: PASS`

### 7.5 Oracle Compare Controls

- `gbp_pe` unit test:
  - override oracle path: `GBP_PE_ORACLE_PATH`
  - force negative mismatch: `GBP_PE_ORACLE_PERTURB=1`
- `pe_top_integration` test:
  - expected oracle path: `PE_TOP_ORACLE_PATH` (default `tests/oracle/gbp_oracle_phase1.json`)
  - observed oracle path: `PE_TOP_OBS_ORACLE_PATH` (default `tests/oracle/generated/gbp_oracle_phase1.json`)
  - force negative mismatch: `PE_TOP_ORACLE_PERTURB=1`

Negative examples:

```bash
GBP_PE_ORACLE_PERTURB=1 make run LEVEL=unit TEST=gbp_pe
PE_TOP_ORACLE_PERTURB=1 make run LEVEL=integration TEST=pe_top_integration VERILATOR="verilator -Wno-fatal -Wno-WIDTHCONCAT -Wno-EOFNEWLINE"
```

### 7.6 Determinism Rule

Do **not** use raw file hash of full JSON as deterministic criterion, because `generated_at_utc` can change.

Use embedded field comparison:

```bash
python3 tests/tools/generate_gbp_oracle_phase1.py --seed 12345 --output tests/oracle/generated/gbp_oracle_phase1_run1.json
python3 tests/tools/generate_gbp_oracle_phase1.py --seed 12345 --output tests/oracle/generated/gbp_oracle_phase1_run2.json
python3 - <<'PY'
import json
a=json.load(open('tests/oracle/generated/gbp_oracle_phase1_run1.json'))['checksum']['value']
b=json.load(open('tests/oracle/generated/gbp_oracle_phase1_run2.json'))['checksum']['value']
print(a)
print(b)
print('MATCH' if a==b else 'MISMATCH')
PY
```

Expected marker:
- `MATCH`

### 7.7 Troubleshooting

- `FAIL: unable to open oracle file ...`
  - Ensure path is relative to `nocbp_verilator` working directory (`tests/oracle/...`).
- `FAIL: oracle missing threshold ...` or `...missing required ... metrics...`
  - Regenerate/refresh artifact with `tests/tools/generate_gbp_oracle_phase1.py`.
- `clangd` missing in diagnostics
  - In this environment, treat successful compile+run as authoritative syntax gate.

---

## Control+Compute GBP Verification Runbook

### Scope
Single-PE, current topology only. Multi-PE scenarios are out of scope for this runbook.

### PASS Commands (5 commands)

```bash
# 1. control_unit_top
make -C nocbp_verilator run LEVEL=unit TEST=control_unit_top

# 2. compute_unit_top
make -C nocbp_verilator run LEVEL=unit TEST=compute_unit_top

# 3. pe_unit
make -C nocbp_verilator run LEVEL=unit TEST=pe_unit

# 4. gbp_pe
make -C nocbp_verilator run LEVEL=unit TEST=gbp_pe

# 5. pe_top_integration
make -C nocbp_verilator run LEVEL=integration TEST=pe_top_integration VERILATOR="verilator -Wno-fatal -Wno-WIDTHCONCAT -Wno-EOFNEWLINE"
```

### NEGATIVE Commands (3 commands)

```bash
# 1. PE_UNIT_HOLD_DONE=1 (withheld done)
PE_UNIT_HOLD_DONE=1 make -C nocbp_verilator run LEVEL=unit TEST=pe_unit

# 2. GBP_PE_ORACLE_PERTURB=1 (oracle mismatch)
GBP_PE_ORACLE_PERTURB=1 make -C nocbp_verilator run LEVEL=unit TEST=gbp_pe

# 3. PE_TOP_ORACLE_PERTURB=1 (integration oracle mismatch)
PE_TOP_ORACLE_PERTURB=1 make -C nocbp_verilator run LEVEL=integration TEST=pe_top_integration VERILATOR="verilator -Wno-fatal -Wno-WIDTHCONCAT -Wno-EOFNEWLINE"
```

### Expected Markers and Exit-Code Polarity

| Test | PASS Marker | Exit Code |
|------|-------------|-----------|
| control_unit_top | `All tests passed` | 0 |
| compute_unit_top | `OP_MATRIX_PASS_MARKER: add/sub/mul/div matrix clean` | 0 |
| pe_unit | `pe_unit: function checks passed` | 0 |
| gbp_pe | `gbp_pe: oracle pass_rule=abs_err<=abs_tol||rel_err<=rel_tol` | 0 |
| pe_top_integration | `pe_top integration: PASS` | 0 |

| Test (Negative) | FAIL Marker | Expected Exit Code |
|-----------------|-------------|-------------------|
| pe_unit (withheld) | `PE_UNIT_WITHHELD_DONE_MARKER` | non-zero (2) |
| gbp_pe (perturb) | `gbp_pe: FAIL: oracle mismatch scenario=A observed=0x7f800000 expected=0x7f800001 abs_err=1 rel_err=4.6748741e-10 abs_tol=0 rel_tol=0 pass_rule=abs_err<=abs_tol||rel_err<=rel_tol` | non-zero (2) |
| pe_top_integration (perturb) | `FAIL: oracle mismatch workload=synthetic_line metric=final_are observed=0.12072 expected=0.06972 abs_err=0.051 rel_err=0.731497418 abs_tol=0.001 rel_tol=0.01` | non-zero (2) |

---

## Function-Level Stage Gate (control+compute -> full-PE)

This section defines the stage gate criteria for advancing from function-level (control+compute) verification to full-PE verification.

### Required Commands

```bash
# Positive test (should exit 0)
make -C nocbp_verilator run LEVEL=unit TEST=gbp_compute_nodes

# Negative test (should exit non-zero)
GBP_COMPUTE_NODES_PERTURB=1 make -C nocbp_verilator run LEVEL=unit TEST=gbp_compute_nodes

# Semantic report JSON validation (should exit 0)
python3 -m json.tool nocbp_verilator/tests/oracle/generated/gbp_compute_nodes_semantic_report.json >/dev/null
```

### Tolerance Policy

- **abs_tol=1e-4** (abs-only tolerance)
- No relative tolerance (rel_tol=0)
- Pass rule: `abs_err <= abs_tol`

### Required Markers

| Marker Name | Purpose |
|-------------|---------|
| `GBP_COMPUTE_NODES_UNIT_PASS_MARKER` | Positive test pass indicator |
| `GBP_COMPUTE_NODES_NEGATIVE_MARKER` | Negative test fail indicator |
| `GBP_COMPUTE_NODES_SEMANTIC_REPORT_MARKER` | Semantic report generation indicator |

### Stage Gate Rule

Full-PE verification is **only** allowed after satisfying ALL of:

1. Positive test exits with code 0 and contains `GBP_COMPUTE_NODES_UNIT_PASS_MARKER`
2. Negative test exits with non-zero code and contains `GBP_COMPUTE_NODES_NEGATIVE_MARKER`
3. Semantic report JSON is valid (passes `python3 -m json.tool` validation)
