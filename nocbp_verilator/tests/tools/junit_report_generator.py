#!/usr/bin/env python3
"""
JUnit/xUnit XML test report generator.
Generates standard JUnit XML reports from test results.
"""
import xml.etree.ElementTree as ET
from datetime import datetime
from typing import Sequence, TypedDict
import sys
import pathlib


class TestResult(TypedDict):
    name: str
    classname: str
    status: str  # "PASS", "FAIL", "ERROR", "TIMEOUT"
    duration: float
    message: str
    output: str


def generate_junit_xml(results: Sequence[TestResult], output_path: str, suite_name: str = "verilator_tests"):
    """Generate JUnit XML report from test results."""
    
    # 创建根元素
    testsuites = ET.Element("testsuites")
    testsuite = ET.SubElement(testsuites, "testsuite")
    testsuite.set("name", suite_name)
    testsuite.set("tests", str(len(results)))
    
    # 计数器
    failures = sum(1 for r in results if r["status"] == "FAIL")
    errors = sum(1 for r in results if r["status"] in ("ERROR", "TIMEOUT"))
    testsuite.set("failures", str(failures))
    testsuite.set("errors", str(errors))
    testsuite.set("timestamp", datetime.now().isoformat())
    
    # 计算总时间
    total_time = sum(r["duration"] for r in results)
    testsuite.set("time", f"{total_time:.3f}")
    
    # 添加测试用例
    for result in results:
        testcase = ET.SubElement(testsuite, "testcase")
        testcase.set("name", result["name"])
        testcase.set("classname", result["classname"])
        testcase.set("time", f"{result['duration']:.3f}")
        
        if result["status"] == "FAIL":
            failure = ET.SubElement(testcase, "failure")
            failure.set("message", result["message"])
            failure.set("type", "AssertionError")
            failure.text = result["output"]
        elif result["status"] in ("ERROR", "TIMEOUT"):
            error = ET.SubElement(testcase, "error")
            error.set("message", result["message"])
            error.set("type", "RuntimeError" if result["status"] == "ERROR" else "TimeoutError")
            error.text = result["output"]
        
        # 添加标准输出
        if result["output"]:
            system_out = ET.SubElement(testcase, "system-out")
            system_out.text = result["output"]
    
    # 写入文件
    tree = ET.ElementTree(testsuites)
    ET.indent(tree, space="  ")
    tree.write(output_path, encoding="utf-8", xml_declaration=True)


def parse_matrix_output(matrix_output_path: str) -> Sequence[TestResult]:
    """Parse matrix output file and convert to test results."""
    results = []
    
    try:
        with open(matrix_output_path, "r", encoding="utf-8") as f:
            content = f.read()
    except FileNotFoundError:
        return results
    
    # 解析矩阵输出
    blocks = content.split("\n\n")
    for block in blocks:
        if not block.strip():
            continue
        
        lines = block.strip().split("\n")
        result = {
            "name": "unknown",
            "classname": "verilator.tests",
            "status": "PASS",
            "duration": 0.0,
            "message": "",
            "output": block,
        }
        
        for line in lines:
            if line.startswith("ROW: "):
                result["name"] = line[5:].strip()
            elif line.startswith("ROW_STATUS: "):
                status = line[12:].strip()
                if status == "FAIL":
                    result["status"] = "FAIL"
                    result["message"] = "Test failed"
            elif line.startswith("DURATION: "):
                try:
                    duration_str = line[10:].strip().rstrip("s")
                    result["duration"] = float(duration_str)
                except ValueError:
                    pass
        
        # 根据测试名称推断classname
        name = result["name"]
        if "integration" in name or "mesh" in name or "endpoint" in name:
            result["classname"] = "verilator.integration"
        elif "unit" in name or "compute" in name or "control" in name:
            result["classname"] = "verilator.unit"
        else:
            result["classname"] = "verilator.tests"
        
        results.append(result)
    
    return results


def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description="Generate JUnit XML report from matrix output")
    parser.add_argument("--input", required=True, help="Input matrix output file")
    parser.add_argument("--output", required=True, help="Output XML file path")
    parser.add_argument("--suite-name", default="verilator_tests", help="Test suite name")
    args = parser.parse_args()
    
    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)
    
    if not input_path.exists():
        print(f"Error: Input file not found: {input_path}", file=sys.stderr)
        return 1
    
    # 确保输出目录存在
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    # 解析结果
    results = parse_matrix_output(str(input_path))
    
    if not results:
        print("Warning: No test results found in input file", file=sys.stderr)
        return 1
    
    # 生成XML报告
    generate_junit_xml(results, str(output_path), args.suite_name)
    
    print(f"Generated JUnit XML report: {output_path}")
    print(f"  Tests: {len(results)}")
    print(f"  Failures: {sum(1 for r in results if r['status'] == 'FAIL')}")
    print(f"  Errors: {sum(1 for r in results if r['status'] in ('ERROR', 'TIMEOUT'))}")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())