#!/usr/bin/env bash
# ============================================================================
# SDGOODS AI Toolkit · 多平台安装器 (Phase 2)
# ----------------------------------------------------------------------------
# 把 sdgoods-ai/skills/* 与各平台适配件装到用户本机，让各类 AI 编码助手
# （WorkBuddy / Claude Code / Cursor）克隆本仓库后能立刻按规范开发谷仓次元屏。
#
# 用法示例：
#   bash sdgoods-ai/install.sh                  # 探测已装平台，全部安装（全局）
#   bash sdgoods-ai/install.sh --platform=claude
#   bash sdgoods-ai/install.sh --platform=all --scope=local --with-mcp
#   bash sdgoods-ai/install.sh --dry-run       # 只打印将做什么，不改文件
#
# 安全：本脚本只复制本仓库自带的开源文档/配置，绝不写入任何密钥/令牌。
#       MCP 配置样例不含 token；登录走邮箱验证码/OAuth，由 server 侧处理。
# ============================================================================
set -eo pipefail

# ---- 路径 ----
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SKILLS_SRC="$SCRIPT_DIR/skills"
AGENT_SRC="$SCRIPT_DIR/agent/sdgoods-dev.md"
MCP_DIR="$SCRIPT_DIR/mcp"

# ---- 默认参数 ----
PLATFORM="auto"        # auto | workbuddy | claude | cursor | all
SCOPE="global"         # global | local
WITH_MCP=0
DRY_RUN=0

# ---- 解析参数 ----
for a in "$@"; do
  case "$a" in
    --platform=*) PLATFORM="${a#*=}" ;;
    --scope=*)    SCOPE="${a#*=}" ;;
    --with-mcp)   WITH_MCP=1 ;;
    --dry-run)    DRY_RUN=1 ;;
    -h|--help)
      grep '^#' "$0" | sed 's/^# \{0,1\}//' | sed -n '3,20p'
      exit 0 ;;
    *) echo "未知参数: $a （--help 查看用法）" >&2; exit 2 ;;
  esac
done

DRY=""
[ "$DRY_RUN" -eq 1 ] && DRY="[dry-run] "

# ---- 工具函数 ----
run() { echo "${DRY}$*"; [ "$DRY_RUN" -eq 1 ] || eval "$@"; }

# 复制一个 skill 目录到目标
install_skill() {
  local src="$1" dst="$2"
  local name; name="$(basename "$src")"
  run "mkdir -p \"$dst\""
  run "rm -rf \"$dst/$name\""
  run "cp -R \"$src\" \"$dst/$name\""
  echo "  ✓ skill -> $dst/$name"
}

# 生成 Claude Code 的 subagent 文件（带 Claude 前导 frontmatter，正文复用通用 agent）
gen_claude_agent() {
  local out="$1"
  run "mkdir -p \"$(dirname "$out")\""
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "  ✓ agent -> $out (Claude subagent frontmatter + 正文)"
    return
  fi
  {
    printf '%s\n' '---'
    printf 'name: sdgoods-dev\n'
    printf 'description: 谷仓次元屏 (SDGOODS-ESP32S3) 固件二次开发助手。当用户要在该 ESP32-S3 圆屏徽章上新增/修改应用、编译烧录、或提交到 SDGOODS 开放平台时使用；自动遵守 AGENTS.md 的两层边界、许可与联机铁律。\n'
    printf 'tools: Read, Grep, Glob, Bash, Edit, Write\n'
    printf 'model: inherit\n'
    printf '%s\n' '---'
    printf '\n'
    cat "$AGENT_SRC"
  } > "$out"
  echo "  ✓ agent -> $out"
}

# 生成 Cursor 的 project rule 文件（带 Cursor frontmatter，正文为精简硬约束 + 指向 skills）
gen_cursor_rule() {
  local out="$1"
  run "mkdir -p \"$(dirname "$out")\""
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "  ✓ rule  -> $out (Cursor rule frontmatter + 硬约束摘要)"
    return
  fi
  {
    printf '%s\n' '---'
    printf 'description: 谷仓次元屏 SDGOODS-ESP32S3 二次开发硬约束与工具包。涉及本仓库 ESP32-S3 圆屏固件开发、加应用、编译烧录、或提交开放平台时使用。\n'
    printf "globs: '**/*.{c,h,md,py}'\n"
    printf 'alwaysApply: false\n'
    printf '%s\n' '---'
    printf '\n'
    printf '本仓库是谷仓次元屏（SDGOODS-ESP32S3）固件。开发前先读根目录 [AGENTS.md](../../AGENTS.md) 与 [sdgoods-ai/README.md](../../sdgoods-ai/README.md)。\n'
    printf '\n'
    printf '硬约束（违反即出 bug）：\n'
    printf '1. 两层边界：平台层 components/sdgoods_board/ 一般不改（只鼓励改 board_pins.h）；应用只放 main/apps/；平台层不得 include 应用层头；跨层用 sdgoods_hooks 函数指针表。\n'
    printf '2. 新应用用 `python3 tools/new_app.py <id> "<名>"` 生成，不要删 `# >>> new_app.py` 标记。\n'
    printf '3. 全仓 Apache-2.0；新文件跑 `python3 tools/add_license_headers.py --apply` 盖章。\n'
    printf '4. 界面文案一律 `SDG_T("中文","English")`。\n'
    printf '5. 不要 `rm -rf build_xxx`，换新 build 目录名。\n'
    printf '\n'
    printf '工具包（sdgoods-ai/skills/）：check-env / new-app / build-flash / screenshot / fonts / publish。需要时用 Read 读取对应 SKILL.md 按其步骤执行。\n'
  } > "$out"
  echo "  ✓ rule  -> $out"
}

# 安装 MCP 配置（仅当 --with-mcp）
install_mcp() {
  local platform="$1"
  case "$platform" in
    workbuddy)
      echo "  · MCP (WorkBuddy): 把 $MCP_DIR/mcp-config.example.json 的内容合并进 ~/.workbuddy/mcp.json"
      echo "    （WorkBuddy 设置里也可在 MCP 连接器页粘贴该 JSON。本样例不含任何 token。）"
      ;;
    claude)
      if [ "$SCOPE" = "local" ]; then
        if [ ! -f "./.mcp.json" ]; then
          run "cp \"$MCP_DIR/claude-mcp.example.json\" \"./.mcp.json\""
          echo "  ✓ MCP (Claude 项目) -> ./.mcp.json"
        else
          echo "  · ./.mcp.json 已存在，跳过（如需覆盖请手动合并 $MCP_DIR/claude-mcp.example.json）"
        fi
      else
        echo "  · MCP (Claude 全局): 运行  claude mcp add sdgoods -- sdgoods-mcp"
        echo "    （或把 $MCP_DIR/claude-mcp.example.json 内容并入 ~/.claude.json 的 mcpServers）"
      fi
      ;;
    cursor)
      if [ "$SCOPE" = "local" ]; then
        if [ ! -f "./.cursor/mcp.json" ]; then
          run "mkdir -p ./.cursor && cp \"$MCP_DIR/cursor-mcp.example.json\" \"./.cursor/mcp.json\""
          echo "  ✓ MCP (Cursor 项目) -> ./.cursor/mcp.json"
        else
          echo "  · ./.cursor/mcp.json 已存在，跳过（如需覆盖请手动合并 $MCP_DIR/cursor-mcp.example.json）"
        fi
      else
        echo "  · MCP (Cursor 全局): 把 $MCP_DIR/cursor-mcp.example.json 内容并入 ~/.cursor/mcp.json"
      fi
      ;;
  esac
}

# ---- 各平台安装例程 ----
install_workbuddy() {
  echo "${DRY}[WorkBuddy] 安装 skills 到 ~/.workbuddy/skills/"
  local dst="$HOME/.workbuddy/skills"
  for d in "$SKILLS_SRC"/*/; do [ -d "$d" ] && install_skill "$d" "$dst"; done
  [ "$WITH_MCP" -eq 1 ] && install_mcp workbuddy
  echo "  → 重启 WorkBuddy 后，6 个 Skill 即可被 AI 调用；领域 Agent 可作为 Expert 包加载 $AGENT_SRC"
}

install_claude() {
  local base
  if [ "$SCOPE" = "local" ]; then base="./.claude"; else base="$HOME/.claude"; fi
  echo "${DRY}[Claude Code] 安装到 $base/ （scope=$SCOPE）"
  local dst="$base/skills"
  for d in "$SKILLS_SRC"/*/; do [ -d "$d" ] && install_skill "$d" "$dst"; done
  gen_claude_agent "$base/agents/sdgoods-dev.md"
  [ "$WITH_MCP" -eq 1 ] && install_mcp claude
  echo "  → 在 Claude Code 里用 /agents 或 @sdgoods-dev 调用领域 Agent；Skill 自动可用。"
}

install_cursor() {
  local base
  if [ "$SCOPE" = "local" ]; then base="./.cursor"; else base="$HOME/.cursor"; fi
  echo "${DRY}[Cursor] 安装到 $base/ （scope=$SCOPE）"
  local dst="$base/skills"
  for d in "$SKILLS_SRC"/*/; do [ -d "$d" ] && install_skill "$d" "$dst"; done
  gen_cursor_rule "$base/rules/sdgoods-ai.mdc"
  [ "$WITH_MCP" -eq 1 ] && install_mcp cursor
  echo "  → Cursor 会自动加载 .cursor/rules/sdgoods-ai.mdc；skills/ 作为参考文档按需 Read。"
}

# ---- 平台选择 ----
PLATFORMS=()
if [ "$PLATFORM" = "auto" ]; then
  [ -d "$HOME/.workbuddy" ] && PLATFORMS+=(workbuddy)
  [ -d "$HOME/.claude" ]    && PLATFORMS+=(claude)
  [ -d "$HOME/.cursor" ]    && PLATFORMS+=(cursor)
  if [ ${#PLATFORMS[@]} -eq 0 ]; then
    echo "未探测到已安装的 AI 平台，默认安装全部三平台（全局）。"
    PLATFORMS=(workbuddy claude cursor)
  else
    echo "探测到已装平台：${PLATFORMS[*]}（用 --platform= 可指定）"
  fi
elif [ "$PLATFORM" = "all" ]; then
  PLATFORMS=(workbuddy claude cursor)
else
  PLATFORMS=("$PLATFORM")
fi

for p in "${PLATFORMS[@]}"; do
  case "$p" in
    workbuddy) install_workbuddy ;;
    claude)    install_claude ;;
    cursor)    install_cursor ;;
    *) echo "不支持的平台: $p（workbuddy|claude|cursor|all）" >&2; exit 2 ;;
  esac
  echo
done

echo "${DRY}完成。安全提示：本仓库不含任何密钥；MCP 登录走邮箱验证码/OAuth，凭据只在本机或 MCP 会话内。"
