#!/usr/bin/env bash
# sync_gitee.sh — 把当前分支（默认 main）同步到 Gitee 镜像。
#
# 背景：本仓库「一份源码、两个托管平台」。README 里仓库地址需各自对应：
#   - GitHub（权威源 origin）：README 里 clone/提示词地址都是 GitHub
#   - Gitee（镜像 gitee）：     README 里 clone/提示词地址应该是 Gitee
# Git 无法让同一份文件在两个平台显示不同内容，所以用「推送前临时替换」：
#   1. 用 tools/_rewrite_readme_remote.py 把 README 里的 GitHub 仓库地址换成 Gitee（仅工作区，不入历史）
#   2. git commit 临时提交并 push 到 gitee
#   3. 本地 reset 回退，工作区与历史恢复 GitHub 版
# 这样 origin(GitHub) 的 main 永远是干净的全 GitHub 版；gitee 的 main 上的 README 是 Gitee 版。
#
# 用法：
#   tools/sync_gitee.sh [分支名]          # 默认 main；交互提示输入 Gitee 令牌
#   GITEE_TOKEN=xxx tools/sync_gitee.sh   # 环境变量传令牌，不交互
#
# 依赖：gitee remote 已配置（git remote add gitee https://gitee.com/sdgoods/sdgoods-esp32s3.git）

set -euo pipefail

BRANCH="${1:-main}"
GITEE_REPO_PATH="gitee.com/sdgoods/sdgoods-esp32s3"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# 确保工作树干净，避免误伤未提交改动
if ! git diff --quiet -- README.md README_EN.md; then
  echo "⚠️  README.md / README_EN.md 有未提交改动，请先提交或还原再跑本脚本。" >&2
  exit 1
fi

# 读取令牌
TOKEN="${GITEE_TOKEN:-}"
if [ -z "$TOKEN" ]; then
  read -r -p "请输入 Gitee 私人令牌（Personal Access Token，需 projects 权限）: " TOKEN
fi
if [ -z "$TOKEN" ]; then
  echo "✗ 未提供令牌，退出。" >&2
  exit 1
fi
PUSH_URL="https://sdgoods:${TOKEN}@${GITEE_REPO_PATH}.git"

# 1. 生成 Gitee 版 README（Python 重写，只改仓库地址、不碰 actions badge 与第三方上游）
echo "→ 生成 Gitee 版 README ..."
python3 tools/_rewrite_readme_remote.py gitee

# 2. 提交并推送到 Gitee
echo "→ 提交临时替换并推送到 Gitee ..."
git add README.md README_EN.md
git commit -q -m "docs: gitee mirror README (仓库地址改为 Gitee)"
git push "$PUSH_URL" "$BRANCH"

# 3. 回退临时提交，恢复 GitHub 版
echo "→ 恢复 GitHub 版 README ..."
git reset --soft HEAD~1
git checkout -- README.md README_EN.md

echo "✅ 已同步到 Gitee（https://${GITEE_REPO_PATH}），本地与 origin(GitHub) 仍为 GitHub 版 README。"
