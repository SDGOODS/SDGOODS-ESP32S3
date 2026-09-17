#!/usr/bin/env bash
# 把 sdgoods-ai/skills/* 安装到当前用户的 WorkBuddy skills 目录。
# 用法：bash sdgoods-ai/install.sh
set -e
SRC="$(cd "$(dirname "$0")/skills" && pwd)"
DST="$HOME/.workbuddy/skills"
mkdir -p "$DST"
for d in "$SRC"/*/; do
  [ -d "$d" ] || continue
  name="$(basename "$d")"
  echo "installing $name -> $DST/$name"
  rm -rf "$DST/$name"
  cp -R "$d" "$DST/$name"
done
echo "done. 重启 WorkBuddy 后，谷仓 SDGOODS 的 6 个 Skill 即可被 AI 调用。"
