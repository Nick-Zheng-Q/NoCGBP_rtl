#!/usr/bin/env python3
"""Update dv/verilator/.clangd with auto-managed include paths.

- Adds absolute -I<path> entries under a managed block.
- Avoids duplicate includes (normalizes paths).
"""
import os
import sys
import re

BEGIN = "# BEGIN auto-includes"
END = "# END auto-includes"
INDENT = "    "
INCLUDE_RE = re.compile(r"^\s*-\s+-I(.+)$")


def normalize(path: str) -> str:
    return os.path.normpath(os.path.abspath(path))


def read_lines(path: str):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.readlines()
    except FileNotFoundError:
        return []


def write_lines(path: str, lines):
    with open(path, "w", encoding="utf-8") as f:
        f.writelines(lines)


def find_block(lines):
    begin_idx = end_idx = None
    for i, line in enumerate(lines):
        if BEGIN in line:
            begin_idx = i
        if END in line:
            end_idx = i
    return begin_idx, end_idx


def insert_block(lines):
    add_idx = None
    for i, line in enumerate(lines):
        if line.strip() == "Add:":
            add_idx = i
            break
    if add_idx is None:
        return lines, None, None

    block = [
        f"{INDENT}{BEGIN}\n",
        f"{INDENT}{END}\n",
    ]
    insert_at = add_idx + 1
    return lines[:insert_at] + block + lines[insert_at:], insert_at, insert_at + 1


def extract_paths(lines, begin_idx, end_idx):
    paths = []
    seen = set()
    for line in lines[begin_idx + 1 : end_idx]:
        m = INCLUDE_RE.match(line)
        if not m:
            continue
        path = normalize(m.group(1).strip())
        if path not in seen:
            seen.add(path)
            paths.append(path)
    return paths


def build_block(paths):
    block = [f"{INDENT}{BEGIN}\n"]
    for p in paths:
        block.append(f"{INDENT}- -I{p}\n")
    block.append(f"{INDENT}{END}\n")
    return block


def main():
    if len(sys.argv) != 3:
        return 0

    clangd_path = sys.argv[1]
    include_path = normalize(sys.argv[2])

    lines = read_lines(clangd_path)
    begin_idx, end_idx = find_block(lines)

    if begin_idx is None or end_idx is None or end_idx < begin_idx:
        lines, begin_idx, end_idx = insert_block(lines)
        if begin_idx is None:
            return 0
        begin_idx, end_idx = find_block(lines)

    paths = extract_paths(lines, begin_idx, end_idx)
    if include_path not in paths:
        paths.append(include_path)

    new_block = build_block(paths)
    new_lines = lines[:begin_idx] + new_block + lines[end_idx + 1 :]
    write_lines(clangd_path, new_lines)
    return 0


if __name__ == "__main__":
    sys.exit(main())
