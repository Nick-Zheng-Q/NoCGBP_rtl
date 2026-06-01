# Verilator 测试系统改进总结

本文档总结了对 verilator 测试系统所做的所有改进。

## 1. 高优先级改进

### 1.1 测试发现机制 (make list-tests)

**功能**：让用户能够轻松发现可用的测试，而不需要手动探索目录。

**使用方法**：
```bash
make -C nocbp_verilator list-tests
```

**输出示例**：
```
=== Unit Tests ===
agu
compute_unit_top
control_unit_top
...

=== Integration Tests ===
endpoint_noc
gbp_pe_mesh_2pe
...

=== System Tests ===
(no tests yet)
```

### 1.2 测试超时机制

**功能**：防止测试无限挂起，特别是长时间运行的集成测试。

**使用方法**：
```bash
# 使用默认超时（30分钟）
make -C nocbp_verilator run LEVEL=unit TEST=example

# 自定义超时时间（秒）
make -C nocbp_verilator run LEVEL=unit TEST=example TIMEOUT=600
```

**配置**：
- 默认超时时间：1800秒（30分钟）
- 可通过 `TIMEOUT` 变量自定义
- 如果系统有 `timeout` 命令，会自动使用

### 1.3 统一构建目录清理 (make clean-all)

**功能**：提供清理所有构建产物的便捷命令。

**使用方法**：
```bash
make -C nocbp_verilator clean-all
```

**清理内容**：
- `build/` 目录
- 所有 `*.yaml.mk` 文件
- `.clangd` 文件

## 2. 中优先级改进

### 2.1 提取C++公共代码

**功能**：减少测试文件中的代码重复，提高可维护性。

**新增文件**：
- `tests/common/test_utils.hpp` - 通用测试工具函数
- `tests/common/oracle_utils.hpp` - Oracle相关工具函数
- `tests/common/verilator_utils.hpp` - Verilator相关工具函数
- `tests/common/example_test_utils.hpp` - 使用示例

**使用方法**：
```cpp
#include "tests/common/test_utils.hpp"
#include "tests/common/oracle_utils.hpp"
#include "tests/common/verilator_utils.hpp"

using namespace test_utils;
using namespace oracle_utils;
using namespace verilator_utils;

int run_test(int argc, char** argv) {
  verilator_init(argc, argv);
  
  auto* dut = new Vmy_dut;
  reset_dut(dut);
  
  // 使用公共工具函数
  if (!dut->ready_o) {
    delete dut;
    return fail("my_test", "ready_o should be high after reset");
  }
  
  print_test_pass("my_test");
  delete dut;
  return 0;
}
```

### 2.2 改进YAML配置解析器

**功能**：使用标准PyYAML库替代自定义解析器，提高可靠性和功能。

**改进内容**：
- 支持完整的YAML规范
- 更好的错误处理
- 支持嵌套映射、多行字符串等
- 如果PyYAML不可用，自动回退到简单解析器

**使用方法**：
```bash
# 安装PyYAML（可选，但推荐）
pip install pyyaml

# 使用YAML配置
make -C nocbp_verilator run LEVEL=unit TEST=example
```

### 2.3 添加并行测试执行

**功能**：让矩阵运行器支持并行执行测试，加快回归测试速度。

**使用方法**：
```bash
# 顺序执行（默认）
python3 nocbp_verilator/tests/integration/gbp_pe_noc_matrix.py --output results.txt --suite smoke

# 并行执行（4个worker）
python3 nocbp_verilator/tests/integration/gbp_pe_noc_matrix.py --output results.txt --suite smoke --parallel 4

# 自定义超时
python3 nocbp_verilator/tests/integration/gbp_pe_noc_matrix.py --output results.txt --suite smoke --parallel 4 --timeout 600

# 详细输出
python3 nocbp_verilator/tests/integration/gbp_pe_noc_matrix.py --output results.txt --suite smoke --parallel 4 --verbose
```

**新增参数**：
- `--parallel N`：并行worker数量（默认1，顺序执行）
- `--timeout N`：每个测试的超时时间（默认1800秒）
- `--verbose`：详细输出

## 3. 低优先级改进

### 3.1 添加JUnit/xUnit XML测试结果报告

**功能**：生成标准格式的测试报告，便于CI/CD系统集成。

**使用方法**：
```bash
# 生成JUnit XML报告
python3 nocbp_verilator/tests/integration/gbp_pe_noc_matrix.py \
  --output results.txt \
  --suite smoke \
  --junit-xml results.xml

# 使用独立的报告生成器
python3 nocbp_verilator/tests/tools/junit_report_generator.py \
  --input results.txt \
  --output results.xml \
  --suite-name smoke_tests
```

**报告格式**：
```xml
<?xml version='1.0' encoding='utf-8'?>
<testsuites>
  <testsuite name="verilator_smoke" tests="30" failures="0" errors="0" timestamp="2026-05-31T17:45:00.000000" time="120.500">
    <testcase name="endpoint_baseline" classname="verilator.integration" time="5.200">
    </testcase>
    ...
  </testsuite>
</testsuites>
```

### 3.2 改进Verilator警告配置

**功能**：更清晰地管理Verilator警告，避免编译错误。

**警告级别**：
- `strict`：所有警告视为错误（-Wall -Werror）
- `default`：抑制常见警告（默认）
- `minimal`：抑制更多警告（包括WIDTHCONCAT, EOFNEWLINE）
- `none`：禁用所有警告（-Wno-fatal）

**使用方法**：
```bash
# 使用默认警告级别
make -C nocbp_verilator run LEVEL=unit TEST=example

# 使用严格警告级别
make -C nocbp_verilator run LEVEL=unit TEST=example VERILATOR_WARNINGS=strict

# 使用最小警告级别
make -C nocbp_verilator run LEVEL=unit TEST=example VERILATOR_WARNINGS=minimal

# 禁用所有警告
make -C nocbp_verilator run LEVEL=unit TEST=example VERILATOR_WARNINGS=none

# 查看当前配置
make -C nocbp_verilator show-config
```

## 4. 新增Makefile目标

| 目标 | 功能 |
|------|------|
| `list-tests` | 列出所有可用的测试 |
| `clean-all` | 清理所有构建产物 |
| `show-config` | 显示当前配置信息 |

## 5. 环境变量支持

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `VERILATOR` | `verilator` | Verilator可执行文件路径 |
| `LEVEL` | `unit` | 测试级别（unit/integration/system） |
| `TEST` | `example` | 测试名称 |
| `TRACE` | `0` | 是否启用跟踪（0/1） |
| `TIMEOUT` | `1800` | 测试超时时间（秒） |
| `VERILATOR_WARNINGS` | `default` | 警告级别 |

## 6. 依赖关系

### 必需依赖
- Verilator
- Python 3.6+
- Make

### 可选依赖
- PyYAML：用于改进的YAML解析
- timeout命令：用于测试超时

### 安装可选依赖
```bash
# 安装PyYAML
pip install pyyaml

# timeout命令通常已预装在Linux系统中
```

## 7. 向后兼容性

所有改进都保持向后兼容性：
- 现有的测试和配置文件无需修改
- 默认行为保持不变
- 新功能都是可选的

## 8. 示例用例

### 示例1：运行单个测试
```bash
make -C nocbp_verilator run LEVEL=unit TEST=example
```

### 示例2：运行测试套件并生成报告
```bash
python3 nocbp_verilator/tests/integration/gbp_pe_noc_matrix.py \
  --output smoke_results.txt \
  --suite smoke \
  --parallel 4 \
  --timeout 600 \
  --junit-xml smoke_results.xml \
  --verbose
```

### 示例3：清理并重新构建
```bash
make -C nocbp_verilator clean-all
make -C nocbp_verilator run LEVEL=integration TEST=gbp_pe_mesh_2pe
```

### 示例4：查看可用测试
```bash
make -C nocbp_verilator list-tests
```

### 示例5：查看当前配置
```bash
make -C nocbp_verilator show-config
```