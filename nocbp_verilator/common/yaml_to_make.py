#!/usr/bin/env python3
"""
YAML -> Makefile vars converter for test configs.
Uses PyYAML for robust YAML parsing with fallback to simple parser.

Supports:
- Full YAML specification via PyYAML
- Fallback to simple parser if PyYAML not available
- Proper error handling and validation
"""
import sys
import os

# Try to import PyYAML, fall back to simple parser if not available
try:
    import yaml
    HAS_PYYAML = True
except ImportError:
    HAS_PYYAML = False
    import re

ALLOWED_KEYS = {
    "rtl_f": "RTL_F",
    "rtl_sv": "RTL_SV",
    "incdirs": "INCDIRS",
    "defines": "DEFINES",
    "top": "TOP",
    "top_sv": "TOP_SV",
    "tb": "TB",
    "verilator_warnings": "VERILATOR_WARNINGS",
}

def parse_yaml_with_pyyaml(path: str) -> dict:
    """Parse YAML file using PyYAML."""
    if not path:
        return {}
    
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = yaml.safe_load(f)
    except FileNotFoundError:
        return {}
    except yaml.YAMLError as e:
        print(f"Error parsing YAML file {path}: {e}", file=sys.stderr)
        return {}
    
    if not isinstance(data, dict):
        return {}
    
    return data

def parse_yaml_simple(path: str) -> dict:
    """Parse YAML file using simple parser (fallback)."""
    if not path:
        return {}
    
    LIST_ITEM_RE = re.compile(r"^\s*-\s*(.+)$")
    KEY_VALUE_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.*)$")
    
    try:
        with open(path, "r", encoding="utf-8") as f:
            lines = f.readlines()
    except FileNotFoundError:
        return {}

    data = {}
    current_key = None
    for raw in lines:
        line = raw.strip("\n")
        # strip comments
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        m = KEY_VALUE_RE.match(line)
        if m:
            key, value = m.group(1), m.group(2).strip()
            current_key = key
            if value == "":
                data[current_key] = []
                continue
            # inline list
            if value.startswith("[") and value.endswith("]"):
                inner = value[1:-1].strip()
                items = []
                if inner:
                    for part in inner.split(","):
                        item = part.strip()
                        if item:
                            items.append(item)
                data[current_key] = items
            else:
                data[current_key] = value
            continue

        m = LIST_ITEM_RE.match(line)
        if m and current_key:
            item = m.group(1).strip()
            if isinstance(data.get(current_key), list):
                data[current_key].append(item)
            else:
                data[current_key] = [item]
            continue

    return data

def parse_yaml(path: str) -> dict:
    """Parse YAML file using best available parser."""
    if HAS_PYYAML:
        return parse_yaml_with_pyyaml(path)
    else:
        return parse_yaml_simple(path)

def emit_make_vars(data: dict) -> str:
    """Convert YAML data to Makefile variable assignments."""
    out_lines = []
    for yaml_key, make_key in ALLOWED_KEYS.items():
        if yaml_key not in data:
            continue
        value = data[yaml_key]
        if isinstance(value, list):
            # 处理列表，确保所有元素都是字符串
            str_items = []
            for item in value:
                if isinstance(item, (int, float)):
                    str_items.append(str(item))
                elif isinstance(item, bool):
                    str_items.append("1" if item else "0")
                else:
                    str_items.append(str(item))
            value = " ".join(str_items)
        elif isinstance(value, bool):
            value = "1" if value else "0"
        elif isinstance(value, (int, float)):
            value = str(value)
        else:
            value = str(value)
        out_lines.append(f"{make_key} := {value}")
    return "\n".join(out_lines)

def validate_config(data: dict, path: str) -> bool:
    """Validate YAML configuration."""
    # 检查必需字段
    if "top" in data and not isinstance(data["top"], str):
        print(f"Error: 'top' must be a string in {path}", file=sys.stderr)
        return False
    
    # 检查列表字段
    list_fields = ["rtl_f", "rtl_sv", "incdirs", "defines"]
    for field in list_fields:
        if field in data:
            if not isinstance(data[field], (list, str)):
                print(f"Error: '{field}' must be a string or list in {path}", file=sys.stderr)
                return False
    
    return True

def main():
    if len(sys.argv) != 2:
        return 0
    path = sys.argv[1]
    
    # 解析YAML
    data = parse_yaml(path)
    if not data:
        return 0
    
    # 验证配置
    if not validate_config(data, path):
        return 1
    
    # 输出Makefile变量
    sys.stdout.write(emit_make_vars(data))
    return 0

if __name__ == "__main__":
    sys.exit(main())