---
name: sdgoods-publish
description: 把谷仓次元屏（SDGOODS-ESP32S3）上做好的固件提交到「谷仓 SDGOODS 开放平台」（https://sdgoods.ai）。优先走本地 MCP server（sdgoods-ai/mcp/sdgoods-mcp-server/server.py，暴露 mcp__sdgoods__login / list_categories / upload_firmware / whoami / logout）；否则回退 tools/sdgoods_publish.py（邮箱验证码登录 + 一键 publish，纯标准库）。Use when asked to publish / 提交 / upload / 上传 firmware to the SDGOODS open platform.
agent_created: true
---

# 提交固件到开放平台

## 两种路径（优先级）

1. **MCP（优先）**：若用户已连接谷仓 SDGOODS 开放平台的 MCP server（仓库已随附本地实现
   `sdgoods-ai/mcp/sdgoods-mcp-server/server.py`，纯标准库、stdio；接入方式见该目录 README 与
   `sdgoods-ai/README.md`），直接调用下面工具，AI 不碰 token：
   - `mcp__sdgoods__login(email)`：发验证码；用户给码后再 `mcp__sdgoods__login(email, code="1234")` 完成登录。
   - `mcp__sdgoods__list_categories()`：取合法分类 slug。
   - `mcp__sdgoods__upload_firmware(file, name, category, desc_zh?, desc_en?, version?, hardware?, tags?, shots?, github?, draft?)`：
     内部 presign+PUT+POST，返回固件 id。
   - `mcp__sdgoods__whoami()`：当前登录用户。
   - `mcp__sdgoods__logout()`：清本机凭据。
   - 登录只需一次，之后 `accessToken` 由本机缓存的 `refreshToken` 自动续期。
2. **CLI 回退**：未连 MCP 时，用仓库自带 `tools/sdgoods_publish.py`（纯标准库，无需 pip）。

## CLI 用法（从仓库根目录）
```bash
export SDGOODS_API_BASE=https://sdgoods.ai/api     # 或用户给的本地后端

# 一次性登录（邮箱收 4 位码；refreshToken 缓存在 ~/.sdgoods/credentials.json，权限 600）
python3 tools/sdgoods_publish.py login 你的邮箱@example.com

# 提交（审核队列）
python3 tools/sdgoods_publish.py publish \
  --file build/SDGOODS_EBADGE.bin \
  --name "我的固件" --desc-zh "一句话介绍" --desc-en "One-line intro" \
  --category game --version v1.0.0 --hardware sdgoods \
  --tags 飞机 联机 --shots shot1.png shot2.png --github https://github.com/you/your-fw
```
- 分类 slug 必须平台已存在：`curl -s $SDGOODS_API_BASE/categories` 先核对，否则 400。
- 其他：`whoami`（当前用户）、`logout`（清本机凭据）。
- 服务端字段名 `descZh`/`descEn`（非 `desc`）。多分区固件用 `parts` 数组。详见 `docs/PUBLISHING.md`。

## ⚠️ 安全（开放平台不开源，务必遵守）
- **绝不在本仓库提交 / 回显任何 API 密钥、accessToken、refreshToken**。凭据只在用户本机
  `~/.sdgoods/credentials.json`（600）或 MCP 会话里，AI 不读取、不回显、不写进仓库。
- MCP server（`sdgoods-ai/mcp/sdgoods-mcp-server/server.py`）是开放平台后端的**客户端**，
  只调用公开 REST API，**不含 server 端代码、不含密钥**；接入配置样例在 `sdgoods-ai/mcp/`。
- 不要把 `SDGOODS_API_BASE` 之外的内部地址、密钥写进 README/提交。
- 截图（`--shots`）建议先用 skill `sdgoods-screenshot` 抓真实设备画面，再随固件提交。

## 提交前自检
- 固件已编译（`build/SDGOODS_EBADGE.bin` 存在）。
- 改过中文则字体已重生成（skill `sdgoods-fonts`）。
- 至少 1 张截图能展示核心玩法。
