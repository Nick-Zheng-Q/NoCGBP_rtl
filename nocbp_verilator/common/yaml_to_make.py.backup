#!/usr/bin/env python3
"""
Minimal YAML -> Makefile vars converter for test configs.
Supports a small subset:
- key: value (string)
- key: [a, b, c]
- key:\n    - a\n    - b
Comments (#) and empty lines are ignored.
"""
import sys
import re

LIST_ITEM_RE = re.compile(r"^\s*-\s*(.+)$")
KEY_VALUE_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.*)$")

ALLOWED_KEYS = {
    "rtl_f": "RTL_F",
    "rtl_sv": "RTL_SV",
    "incdirs": "INCDIRS",
    "defines": "DEFINES",
    "top": "TOP",
    "top_sv": "TOP_SV",
    "tb": "TB",
}


def parse_simple_yaml(path: str):
    if not path:
        return {}
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


def emit_make_vars(data: dict):
    out_lines = []
    for yaml_key, make_key in ALLOWED_KEYS.items():
        if yaml_key not in data:
            continue
        value = data[yaml_key]
        if isinstance(value, list):
            value = " ".join(value)
        out_lines.append(f"{make_key} := {value}")
    return "\n".join(out_lines)


def main():
    if len(sys.argv) != 2:
        return 0
    path = sys.argv[1]
    data = parse_simple_yaml(path)
    if not data:
        return 0
    sys.stdout.write(emit_make_vars(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
