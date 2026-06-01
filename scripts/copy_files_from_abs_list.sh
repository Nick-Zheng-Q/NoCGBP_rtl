#!/usr/bin/env zsh

set -u

# 读取绝对路径清单，并把文件复制到目标目录。
# 对仓库内文件，保留相对仓库根目录的层级，避免同名文件互相覆盖。

if [[ "$#" -ne 2 ]]; then
  print -u2 "用法: $0 <绝对路径清单.txt> <目标目录>"
  exit 1
fi

list_file_input="$1"
dest_root_input="$2"

if [[ ! -f "$list_file_input" ]]; then
  print -u2 "错误: 清单文件不存在: $list_file_input"
  exit 1
fi

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)
list_file=$(realpath "$list_file_input")

mkdir -p "$dest_root_input"
dest_root=$(cd -- "$dest_root_input" && pwd)

typeset -i total_count=0
typeset -i copied_count=0
typeset -i missing_count=0
typeset -i failed_count=0

while IFS= read -r raw_line || [[ -n "$raw_line" ]]; do
  line="${raw_line%$'\r'}"

  if [[ -z "$line" ]]; then
    continue
  fi

  if [[ "$line" == \#* ]]; then
    continue
  fi

  total_count=$((total_count + 1))

  if [[ ! -f "$line" ]]; then
    print -u2 "缺失: $line"
    missing_count=$((missing_count + 1))
    continue
  fi

  src_abs=$(realpath "$line")

  if [[ "$src_abs" == "$repo_root/"* ]]; then
    rel_path="${src_abs#$repo_root/}"
  else
    rel_path="${src_abs#/}"
  fi

  out_path="$dest_root/$rel_path"
  out_dir=$(dirname -- "$out_path")

  if ! mkdir -p "$out_dir"; then
    print -u2 "失败: 无法创建目录: $out_dir"
    failed_count=$((failed_count + 1))
    continue
  fi

  if ! cp -f "$src_abs" "$out_path"; then
    print -u2 "失败: 无法复制文件: $src_abs"
    failed_count=$((failed_count + 1))
    continue
  fi

  copied_count=$((copied_count + 1))
done < "$list_file"

print "清单文件: $list_file"
print "目标目录: $dest_root"
print "总条目: $total_count"
print "成功复制: $copied_count"
print "缺失文件: $missing_count"
print "复制失败: $failed_count"

if (( missing_count > 0 || failed_count > 0 )); then
  exit 2
fi

exit 0
