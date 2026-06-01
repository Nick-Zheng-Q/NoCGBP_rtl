#!/bin/bash
# 验证脚本 - 测试所有改进功能

set -e

echo "=== 验证 Verilator 测试系统改进 ==="
echo ""

# 1. 测试 list-tests
echo "1. 测试 list-tests 功能..."
make -C nocbp_verilator list-tests | grep -q "Unit Tests" && echo "   ✓ list-tests 工作正常" || echo "   ✗ list-tests 失败"
echo ""

# 2. 测试 show-config
echo "2. 测试 show-config 功能..."
make -C nocbp_verilator show-config | grep -q "VERILATOR=" && echo "   ✓ show-config 工作正常" || echo "   ✗ show-config 失败"
echo ""

# 3. 测试 clean-all
echo "3. 测试 clean-all 功能..."
make -C nocbp_verilator clean-all && echo "   ✓ clean-all 工作正常" || echo "   ✗ clean-all 失败"
echo ""

# 4. 测试超时功能
echo "4. 测试超时功能..."
timeout 5 sleep 1 && echo "   ✓ timeout 命令可用" || echo "   ✗ timeout 命令不可用"
echo ""

# 5. 测试YAML解析器
echo "5. 测试YAML解析器..."
cd nocbp_verilator && python3 common/yaml_to_make.py tests/unit/example.yaml | grep -q "TOP :=" && echo "   ✓ YAML解析器工作正常" || echo "   ✗ YAML解析器失败"
cd ..
echo ""

# 6. 测试JUnit报告生成器
echo "6. 测试JUnit报告生成器..."
cd nocbp_verilator && python3 tests/tools/junit_report_generator.py --help | grep -q "usage:" && echo "   ✓ JUnit报告生成器可用" || echo "   ✗ JUnit报告生成器不可用"
cd ..
echo ""

# 7. 测试矩阵运行器
echo "7. 测试矩阵运行器..."
cd nocbp_verilator && python3 tests/integration/gbp_pe_noc_matrix.py --help | grep -q "parallel" && echo "   ✓ 矩阵运行器并行功能可用" || echo "   ✗ 矩阵运行器并行功能不可用"
cd ..
echo ""

# 8. 测试公共工具库
echo "8. 测试公共工具库..."
ls -la nocbp_verilator/tests/common/*.hpp | grep -q "test_utils.hpp" && echo "   ✓ 公共工具库已创建" || echo "   ✗ 公共工具库未创建"
echo ""

echo "=== 验证完成 ==="