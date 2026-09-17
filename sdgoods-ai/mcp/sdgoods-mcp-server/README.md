# 谷仓 SDGOODS 开放平台 · 本地 MCP server

一个**纯标准库、stdio 传输**的 Model Context Protocol server，让 WorkBuddy / Claude Code /
Cursor 等 AI 编码助手以子进程方式启动它，直接把固件上传到**谷仓 SDGOODS 开放平台**
（https://sdgoods.ai），完成「AI 开发 → 一键上传」闭环。

> **它是什么、不是什么**
> - 它是开放平台后端的**客户端**：只调用平台公开 REST API（与 `tools/sdgoods_publish.py`
>   同一套端点），**不含任何密钥、不含服务端代码**。
> - 开放平台后端（MCP 的服务端实现）是**不开源**的；本文件是「先在本地实现」的客户端版本，
>   协议与未来平台提供的 server 一致，工具名前缀 `mcp__sdgoods__`。

## 运行要求

- Python 3.8+（系统自带即可，无需 `pip install`）。
- 网络可访问 `SDGOODS_API_BASE`（默认 `https://sdgoods.ai/api`）。
- 由 AI 平台以 stdio 方式拉起，**不要**手动在前台跑（它会一直读 stdin 等消息）。

## 工具清单（server name = `sdgoods`）

| MCP 工具 | 说明 |
|---|---|
| `login(email, code?)` | 邮箱验证码登录（两步：先不带 `code` 发码，收到后带 `code` 再调）；凭据缓存在 `~/.sdgoods/credentials.json`（600） |
| `list_categories()` | 列出平台合法分类 slug（提交前核对） |
| `upload_firmware(file, name, category, desc_zh?, desc_en?, version?, hardware?, tags?, shots?, github?, draft?)` | presign 直传 + 创建固件记录，返回固件 id |
| `whoami()` | 当前登录用户 |
| `logout()` | 清除本机凭据 |

工具在客户端呈现为 `mcp__sdgoods__login` / `mcp__sdgoods__upload_firmware` 等。

## 接入方式（AI 平台）

把下面配置加进对应平台的 MCP 配置（路径换成你仓库里 `server.py` 的绝对路径）：

```json
{
  "mcpServers": {
    "sdgoods": {
      "command": "python3",
      "args": ["/abs/path/to/sdgoods-ai/mcp/sdgoods-mcp-server/server.py"],
      "env": { "SDGOODS_API_BASE": "https://sdgoods.ai/api" }
    }
  }
}
```

- **WorkBuddy**：设置 → MCP 连接器，粘贴上述 JSON（或使用 `sdgoods-ai/install.sh --with-mcp` 生成）。
- **Claude Code**：项目级 `.mcp.json` 或全局 `claude mcp add sdgoods -- python3 /abs/path/server.py`。
- **Cursor**：项目级 `.cursor/mcp.json`。

更省事：`bash sdgoods-ai/install.sh --platform=all --with-mcp`，脚本会把真实 `server.py` 路径
自动写进生成的 MCP 配置（`PATH_TO_SERVER` 占位符会被替换）。

## 典型对话流（AI 侧）

1. 用户：「帮我把刚编译的固件传到开放平台。」
2. AI 调 `mcp__sdgoods__login(email="你@example.com")` → 返回「验证码已发送」。
3. 用户把收到的 4 位码告诉 AI；AI 再调 `mcp__sdgoods__login(email=..., code="1234")` → 登录成功。
4. AI 调 `mcp__sdgoods__list_categories()` 取分类 slug。
5. AI 调 `mcp__sdgoods__upload_firmware(file="build/...bin", name=..., category=..., desc_zh=..., desc_en=..., shots=[...])` → 返回固件 id。

登录只需一次，之后提交的 `accessToken` 由本机缓存的 `refreshToken` 自动续期。

## ⚠️ 安全红线

- **绝不**在仓库 / 回复里提交或回显 accessToken / refreshToken。
- 凭据只在用户本机 `~/.sdgoods/credentials.json`（0600）或 MCP 会话内。
- 本文件不含任何内部地址或密钥；不要把 `SDGOODS_API_BASE` 之外的地址写死。
