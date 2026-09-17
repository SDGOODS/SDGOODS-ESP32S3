# 谷仓次元屏 · AI 辅助开发工具包（SDGOODS AI Toolkit）

> 让 AI（WorkBuddy / Claude Code / Cursor / Codex 等）克隆本仓库后，立刻能按规范
> 在**谷仓次元屏（SDGOODS Electric Badge / 谷仓电子徽章）**上二次开发、加应用、编译烧录、
> 并一键提交到**谷仓 SDGOODS 开放平台**。
>
> 本包是「纯文档 + 配置」，零后端依赖，克隆即用。

## 这个包给你什么

| 目录 | 内容 |
|---|---|
| `skills/` | 6 个 WorkBuddy Skill，封装 `tools/` 脚本与项目坑位 |
| `agent/sdgoods-dev.md` | 领域 Agent 定义（内嵌 `AGENTS.md` 硬约束 + 绑定 6 个 Skill） |
| `mcp/mcp-config.example.json` | 连接开放平台 MCP 的客户端配置样例（**不含任何密钥**） |
| `install.sh` | 一键装到 WorkBuddy |

## 6 个 Skill

| Skill | 封装的动作 | 关键回退 |
|---|---|---|
| `sdgoods-check-env` | 跑 `check_env.py`，解析 MUST/WARN | 缺项给安装指引 |
| `sdgoods-new-app` | `new_app.py` 生成骨架并自动注册 | 不删 `>>>` 标记 |
| `sdgoods-build-flash` | `idf.py build` + `flash` | 含 env 坑（unset 三变量、不 rm -rf build） |
| `sdgoods-screenshot` | 串口一键截屏 + 自动打开 PNG 给 AI「看图」 | 单张约 2~25s，UI 改动必做 |
| `sdgoods-fonts` | 改中文后重跑 `gen_fonts.py` + `font_metrics.py` | 漏字=方框，圆屏弦宽校验 |
| `sdgoods-publish` | 提交固件到开放平台 | **优先 MCP，否则回退 `sdgoods_publish.py`** |

## 安装到 WorkBuddy（优先）

```bash
bash sdgoods-ai/install.sh
```

把 `skills/*` 复制到 `~/.workbuddy/skills/`，重启 WorkBuddy 后，AI 在涉及本设备开发时会自动调用。
领域 Agent 可手动作为 Expert 包加载 `agent/sdgoods-dev.md`。

## 其它平台（下一步：全平台通用）

本包 Skill 用通用 Markdown 格式，不绑定 WorkBuddy 私有语法。后续会提供：
- `claude mcp add` / `.mcp.json` 配置；
- Cursor / Codex 的 `agents/` 接入说明。
（逻辑一致，仅配置外壳不同。）

## 连接开放平台（MCP）

谷仓 SDGOODS 开放平台是**不开源的后端**，MCP server 由其服务端实现。
本仓库**只放客户端配置样例**（`mcp/mcp-config.example.json`）与协议说明，**不含 server 代码、不含密钥**。

- 连上 MCP 后，`sdgoods-publish` 会优先调用 `mcp__sdgoods__upload_firmware`，登录走邮箱验证码/OAuth 交互。
- 未连 MCP 时，回退到 `tools/sdgoods_publish.py`（同样邮箱验证码登录，凭据只存本机 `~/.sdgoods/credentials.json`，600）。

### ⚠️ 安全红线（务必遵守）
- **绝不在本仓库提交 / 回显任何 API 密钥、accessToken、refreshToken。**
- MCP 配置样例里只允许出现 `SDGOODS_API_BASE` 这类公开基地址，不得写入内部地址或密钥。
- 凭据只在用户本机或 MCP 会话内，AI 不读取、不写仓库。

## 配套文档
- 开发硬约束：根目录 [`AGENTS.md`](../../AGENTS.md)
- 架构：`docs/ARCHITECTURE.md`
- 发布：三种方式 `docs/PUBLISHING.md`
