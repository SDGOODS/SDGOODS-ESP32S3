# 谷仓次元屏 · AI 辅助开发工具包（SDGOODS AI Toolkit）

> 让 AI（WorkBuddy / Claude Code / Cursor / Codex 等）克隆本仓库后，立刻能按规范
> 在**谷仓次元屏（SDGOODS Electronic Badge / 谷仓电子徽章）**上二次开发、加应用、编译烧录、
> 并一键提交到**谷仓 SDGOODS 开放平台**。
>
> 本包是「纯文档 + 配置」，零后端依赖，克隆即用。

## 这个包给你什么

| 目录 | 内容 |
|---|---|
| `skills/` | 6 个**跨平台** Skill，封装 `tools/` 脚本与项目坑位 |
| `agent/sdgoods-dev.md` | 领域 Agent 定义（内嵌 `AGENTS.md` 硬约束 + 绑定 6 个 Skill） |
| `mcp/` | 连接开放平台 MCP 的**本地 server 实现**（`sdgoods-mcp-server/`，纯标准库客户端）+ 各平台配置样例（**不含任何密钥**） |
| `install.sh` | 一键装到 WorkBuddy / Claude Code / Cursor（多平台） |

## 6 个 Skill（跨平台通用）

| Skill | 封装的动作 | 关键回退 |
|---|---|---|
| `sdgoods-check-env` | 跑 `check_env.py`，解析 MUST/WARN | 缺项给安装指引 |
| `sdgoods-new-app` | `new_app_project.py` 从开源工程派生 PLANE 形单应用直启工程（改名 + 裁剪 + 可选 --run 烧录截屏） | 种子=本开源仓；不含 SDGOODS_ 前缀 |
| `sdgoods-build-flash` | `tools/build.sh`（封装 env 坑）一键构建/烧录/监视 | 底层仍是 `idf.py build`+`flash`，但 env 坑（unset 三变量、保留 SESSION_ID、不 rm -rf build）已固化进脚本，AI/开发者一行即可 |
| `sdgoods-screenshot` | 串口一键截屏 + 自动打开 PNG 给 AI「看图」 | 单张约 2~25s，UI 改动必做 |
| `sdgoods-fonts` | 改中文后重跑 `gen_fonts.py` + `font_metrics.py` | 漏字=方框，圆屏弦宽校验 |
| `sdgoods-publish` | 提交固件到开放平台 | 单 app.bin 应用包（平台自动补引导层）；能力检查定截图来源、逐条草稿问字段、三铁律（重编+1 / 删旧建新 / 审中可取消） |

Skill 正文是平台无关的 Markdown，仅在安装时按目标平台落到不同位置、并生成对应形态的
领域 Agent（Claude 的 `agents/` subagent、Cursor 的 `rules/` rule）。

## 安装（多平台）

```bash
# 探测已装平台并全部安装（全局，仅装到本机用户目录）
bash sdgoods-ai/install.sh

# 指定平台
bash sdgoods-ai/install.sh --platform=workbuddy
bash sdgoods-ai/install.sh --platform=claude
bash sdgoods-ai/install.sh --platform=cursor
bash sdgoods-ai/install.sh --platform=all

# 装到「当前仓库」（项目级 .claude/ .cursor/ .mcp.json，可随仓库提交）
bash sdgoods-ai/install.sh --platform=all --scope=local

# 同时落 MCP 配置（见下方「连接开放平台」）
bash sdgoods-ai/install.sh --platform=all --with-mcp

# 只预览会做什么、不改文件
bash sdgoods-ai/install.sh --dry-run
```

### 各平台落点

| 平台 | Skill 落点 | 领域 Agent 形态 | MCP 落点（`--with-mcp`） |
|---|---|---|---|
| **WorkBuddy** | `~/.workbuddy/skills/` | 可作为 Expert 包加载 `agent/sdgoods-dev.md` | 合并进 `~/.workbuddy/mcp.json` |
| **Claude Code**（全局） | `~/.claude/skills/` | `~/.claude/agents/sdgoods-dev.md`（自动带 frontmatter） | `claude mcp add sdgoods -- sdgoods-mcp` 或并入 `~/.claude.json` |
| **Claude Code**（项目） | `./.claude/skills/` | `./.claude/agents/sdgoods-dev.md` | `./.mcp.json`（脚本自动生成） |
| **Cursor**（全局） | `~/.cursor/skills/` | `~/.cursor/rules/sdgoods-ai.mdc`（自动带 frontmatter） | 并入 `~/.cursor/mcp.json` |
| **Cursor**（项目） | `./.cursor/skills/` | `./.cursor/rules/sdgoods-ai.mdc` | `./.cursor/mcp.json`（脚本自动生成） |
| **通用 / Codex 等** | 不装——直接读仓库根 `AGENTS.md` + 本包 `skills/` 即可 | 读 `agent/sdgoods-dev.md` | 按平台粘贴 `mcp/` 样例 |

> Claude Code / Cursor 的 skill 目录与 WorkBuddy 格式一致（同为 `SKILL.md` + frontmatter），
> 平台会忽略不认识的字段（`agent_created`），无需转换。

## 连接开放平台（MCP）

谷仓 SDGOODS 开放平台是**不开源的后端**，但本仓库**自带一个本地 stdio MCP server**
（`mcp/sdgoods-mcp-server/server.py`，纯标准库**客户端**，包装 `tools/sdgoods_publish.py` 调公开 REST API）。
它也**不含服务端代码、不含密钥**——平台若再托管自己的 operator MCP（base64 内联形态），那是另一套通道，
详见 `docs/MCP_CONTRACT.md` 的「两条 MCP 通道」。本目录放的是**客户端配置样例**与协议说明。

`install.sh --with-mcp` 会按平台把对应样例落到正确位置：

| 平台 | 配置样例文件 | 目标位置 |
|---|---|---|
| WorkBuddy | `mcp/mcp-config.example.json` | `~/.workbuddy/mcp.json`（手动合并/粘贴） |
| Claude Code | `mcp/claude-mcp.example.json` | `.mcp.json`（项目）或 `claude mcp add`（全局） |
| Cursor | `mcp/cursor-mcp.example.json` | `.cursor/mcp.json`（项目）或 `~/.cursor/mcp.json`（全局） |

- 连上 MCP 后，`sdgoods-publish` 会优先调用 `mcp__sdgoods__upload_firmware`，登录走邮箱验证码/OAuth 交互。
- 未连 MCP 时，回退到 `tools/sdgoods_publish.py`（同样邮箱验证码登录，凭据只存本机 `~/.sdgoods/credentials.json`，600）。

### ⚠️ 安全红线（务必遵守）
- **绝不在本仓库提交 / 回显任何 API 密钥、accessToken、refreshToken。**
- MCP 配置样例里只允许出现 `SDGOODS_API_BASE` 这类公开基地址，不得写入内部地址或密钥。
- 凭据只在用户本机或 MCP 会话内，AI 不读取、不写仓库。

## 一键配置页与 Agent 市场（Phase 4）

| 文件 | 作用 |
|---|---|
| `catalog.json` | 工具包**机器可读清单**：6 Skill + Agent + MCP server（5 工具）+ 3 平台的落点 / 安装命令 / MCP 配置模板。开放平台「Agent 市场」与「复制 MCP 配置」按钮的**单一数据源**。 |
| `setup/index.html` | 自包含离线页（`file://` 直接打开）：平台选择 + MCP 配置预览 + **「复制 MCP 配置」按钮**（剪贴板）+ 安装一行命令 + 可浏览的 Agent 市场卡片。同时是开放平台网页端的**客户端参考实现**。 |
| `setup/gen_setup.py` | 从 `catalog.json` 重新生成 `setup/index.html`（改了清单后跑一次，勿手改 index.html）。 |
| `setup/gen_config.py` | CLI：按平台输出 MCP 配置 JSON 与安装命令（`--platform=claude --json`）。开放平台后端可直接调用，拿到与前端一致的配置。 |

```bash
# 改了 catalog.json 后重新生成 setup 页
python3 sdgoods-ai/setup/gen_setup.py

# 后端取某平台的 MCP 配置（机器可读 JSON）
python3 sdgoods-ai/setup/gen_config.py --platform=claude --json
```

**开放平台后端怎么接**：「Agent 市场」渲染与「复制 MCP 配置」按钮直接消费
`catalog.json`（或调用 `gen_config.py` 生成 per-platform 配置）。本仓库不存放任何
服务端代码，也不含密钥；平台侧只需读取清单并按平台渲染。

## 配套文档
- 开发硬约束：根目录 [`AGENTS.md`](../../AGENTS.md)
- 架构：`docs/ARCHITECTURE.md`
- 发布：三种方式 `docs/PUBLISHING.md`
- 发布契约（REST / 字段表 / 两条 MCP 通道）：`docs/MCP_CONTRACT.md`
