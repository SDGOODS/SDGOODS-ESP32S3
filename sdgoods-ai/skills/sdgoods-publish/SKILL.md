---
name: sdgoods-publish
description: 编译谷仓次元屏固件并发布到「谷仓 SDGOODS 开放平台」（sdgoods.ai），以及平台上固件的下架/删除/重新发布。交互式流程：用户说发布/提审 → 第一步就问「MCP 发布（推荐）还是网页手动发布」；MCP 用 sdg_ 开发者令牌直推（令牌缺失/失效时必须向用户索取，绝不改用其他通道绕过）；手动发布则打包 release 文件夹交用户自己在网页提交。覆盖：打包纯应用镜像（平台自动补引导层）、逐字段确认发布信息、截图采集（真机截图 / AI 生成像素风兜底）、防砖校验、删旧建新。Use when asked to 发布 / 提交 / 上传 / publish / upload firmware，或在平台下架 / 删除固件（unpublish / delete firmware）。
agent_created: true
---

# 发布固件到谷仓 SDGOODS 开放平台（交互式）

> 本文件随开源仓库 `SDGOODS-ESP32S3` 维护，面向**第三方开发者 / AI 编程助手**。
> 平台端点只有生产一个：`https://sdgoods.ai`（MCP：`https://sdgoods.ai/api/mcp`）。

## ⚠️ 三条发布铁律（无例外、不询问跳过）

1. **改源码必重编 + 版本号 +1**：动了源码就必须重新编译（`idf.py -B build build`，见 `docs/BUILD.md`），并把 `version.txt` 升一位（二进制内 `esp_app_desc.version` 与关于页显示同源，改一处即两处同步）。若**检测到设备已连接**（`ls /dev/cu.usbmodem*` 有输出），重编后必须重刷设备并按下方截图规则**重截最新画面**——不重刷/不重截就发布 = 平台挂的是过时固件/旧截图。
2. **重新发布 = 删旧建新**：该 app 之前发布过时，先找到旧记录、拿旧信息当草稿问用户，确认后**先删旧再传新**（CLI：`mcp-replace <旧id>`）。平台上同一应用**永远只有一条**，禁止出现两个同名项目。
3. **审核中可撤回**：提交后进入审核（`status:"reviewing"`）。作者可在平台「个人中心 → 我的固件」对审核中的固件点「取消审核」回到草稿，改稿后重新提审。已上架的只能下架（`mcp-unpublish`）回草稿。

⛔ **模糊指令 ≠ 跳过确认**：用户中断提问后回「继续 / 直接发」= 继续走流程，**不是**「跳过确认直接提交」。发布信息（名称/简介/分类）必须在提交前逐字段确认完。

## ⛔ 开发者上传形态：只传「一个 app.bin 单应用包」

- **正确**：`tools/pack_app.py` 导出的 `dist/<项目名>_app.bin` —— 单文件、**不带烧录地址**。平台按文件内容判别落点（`0x20` 处是 `esp_app_desc` → 落 `0x10000`），并在用户刷机时**自动拼上官方引导层**（bootloader / 分区表）。开发者写不歪地址，不会变砖。
- **错误**：自己拼多段 `parts` 带 `bootloader@0x0` —— 多余且会被平台拒绝（403 `developer_single_package_only`）。引导层只有平台能托管。
- **`pack_app.py` 六项校验**（不通过就拒绝导出）：非整机镜像（`0x8000` 处不是分区表魔数 `0x50AA`）、首字节 `0xE9`、`0x20` 处 `0xABCD5432`、chip_id `0x0009`(ESP32-S3)、体积 ≤ **2.9MB**（槽上限）、**app 身份不是模板默认名**。
- **app 身份** = `esp_app_desc_t.project_name`（绝对偏移 `0x50`）。它决定设备上 `appdata/<app_id>/` 数据目录名与卸载目标 ⇒ 撞名 = 数据互串。用 `tools/new_app_project.py` 派生工程会自动改名；手动克隆工程后必须改根 `CMakeLists.txt` 的 `project()`，否则 `pack_app.py` 拒绝打包（`--allow-template-name` 仅供官方参考工程自用，第三方不需要）。
- **改码后重发前必核对**：`pack_app.py` 只打包不重编，`dist/<项目>_app.json` 里的 `version`/`buildTime` 是上次编译嵌入的值——必须与 `version.txt` 一致才允许发布（不一致 = 拿旧 bin 顶替，会发过时固件）。

## ⓪ 发布通道（用户说「发布」后第一件事就问）

用 `AskUserQuestion` 问：**MCP 发布（推荐）** 还是 **网页手动发布**？据此决定要不要令牌、草稿从哪来；⑤ 执行前不再重复问。

### 分支 1 · MCP 发布（开发者令牌 `sdg_`，AI / CI 直推）

1. 查本机是否已存令牌：
   ```bash
   python3 tools/sdgoods_publish.py mcp-token        # 输出 SAVED / NOT_SAVED，不回显令牌明文
   ```
2. **已存（SAVED）** → 走采集流程（用 `mcp-firmwares` 拉平台旧记录当草稿）。
3. **未存（NOT_SAVED）** → 请用户把开发者令牌粘贴进输入框（平台「个人中心 → 开发者令牌」生成，明文只显示一次）：
   - 填了 `sdg_...` → `python3 tools/sdgoods_publish.py set-token "<sdg_令牌>"` 保存（不回显、不写仓库）→ 走采集流程。
   - 跳过 → 降级为**网页手动发布**流程。

### 🔴 令牌红线（必须遵守）

- 选了 MCP 后，若调用返回 **-32001（令牌无效/过期/被吊销）**，必须**立刻停下来向用户索取有效的 `sdg_` 令牌**。
- **绝不**因为令牌失效就改用其他通道（如网站账号登录态的 REST 接口）绕过发布——生产环境开发者的发布通道只有 MCP 令牌与网页手动两条。
- 用户明确跳过提供令牌 → 按手动发布流程（打包 release 交用户自传），而不是偷偷换通道直推。
- 令牌处理：不回显全文、不写进仓库、不在聊天复述；只存 `~/.sdgoods/credentials.json`（由 `set-token` 完成）。

### 分支 2 · 网页手动发布

1. **本机已存令牌** → 仍走采集流程（令牌拉旧记录当草稿，字段更准）。
2. **无令牌** → 不查平台，AI 按 app 内容直接生成草稿，逐字段问用户。
3. 最终 AI **不代推**：把三样东西放进 `release/<app名>-v<版本>/` 文件夹交给用户：
   - `app.bin` —— `pack_app.py` 导出的单应用包；
   - `shots/` —— 截图（无设备时是 AI 生成的像素风图）；
   - `info.md` —— 发布信息草稿（名称 / 简介中英文 / 版本 / 分类 slug / GitHub）。
   用户自己登录 [sdgoods.ai](https://sdgoods.ai) 在「提交固件」表单填表上传。
   - ⚠️ 若市场已有同名旧版，在 `info.md` 里提示用户：**先在个人中心删除旧记录，再提交新 release**（铁律②）。

## 🔁 端到端顺序

```
用户说「发布」/「提审」/「publish」
 ├─⓪ 问发布方式：MCP（推荐）/ 网页手动 → 按分支查/要令牌
 ├─① 改过源码？ ──是──► 重编 + version.txt +1（铁律①）
 ├─② 探测设备：ls /dev/cu.usbmodem*（端口随时变，每次现查）
 ├─②.5 能力检查（静态读源码，不烧不卡用户）
 │      ├─ has_screenshot?  全仓库（含 components/）搜截图符号（screenshot / SHOT / sdgoods_screenshot）
 │      └─ has_cc?          构建产物 grep sdgoods_cc_open build*/<PROJECT>.map（权威），
 │                          或仓库含 components/sdgoods_launcher/src/sdgoods_cc.c（WHOLE_ARCHIVE）
 ├─③ 逐字段搜集信息（带草稿，逐条问，不一次性列全）
 ├─④ 截图（按能力检查 × 发布状态 × 有无设备分流，见下）
 └─⑤ 执行（⓪ 已定通道）：MCP 直推 或 打包 release 交用户
```

全程只在四处打断用户：⓪ 通道/令牌、③ 逐字段确认、④-0 手动 vs 自动截图及手动逐张确认。
重编/重刷/自动截图都自动完成，不询问。

## ③ 逐字段搜集信息

按「名称 → 简介中文 → 简介英文 → 分类 → GitHub」顺序，**每条单独**用 `AskUserQuestion` 带草稿问「是否修改」：
- 不修改 → 用草稿跳下一条；要修改 → 用户在空白填新值跳下一条。
- 草稿来源：**有令牌** → 已发布过用 `mcp-firmwares` 拉平台旧记录（name/descZh/descEn/category/githubUrl），没发布过 AI 生成；**无令牌** → AI 按 app 内容生成（依据 `git log --oneline -5` + `main/apps/apps_registry.c`；游戏类偏 `game`、工具类偏 `tool`）。
- **长草稿展示规则**：`AskUserQuestion` 的选项 `description` 在前端会被截断。草稿超过约 60 字符（尤其简介中/英）时，**先在同一条消息正文里完整打印**（或写入 `release/draft_desc_zh.md` 给路径），选项只写「不改 / 要修改」。
- **版本号不询问**：自动取最新编译产物（`version.txt`）；**截图不询问**：取 ④ 步结果。

## ④ 截图规则

**能力检查先分流（静态读源码）**：
- `has_screenshot=false`（工程没编译进截图模块）→ 整条走「AI 生成路线」：不连设备、不烧固件。图源仍按发布状态：已发布→复用平台旧图；未发布→AI 生成 1 张像素风图。
- `has_cc=false` → 即使走真机截图也**跳过「关于」页**。
- ⚠️ 控制中心是平台层组件（`components/sdgoods_launcher`，WHOLE_ARCHIVE 整档链接，每个 app 默认带），**app 自己的源码没有 `sdgoods_cc` 字面量 ≠ 没有控制中心**——以构建产物 `build*/<PROJECT>.map` 里 `grep sdgoods_cc_open` 为权威。

**截图来源（「是否发布过 × 是否有设备」）**：

| 是否发布过 \ 设备 | 有设备 | 无设备 |
|---|---|---|
| 已发布过 | 重截新图 | 复用平台已有旧截图（不重画、不询问） |
| 未发布过 | 截新图 | AI 按 app 内容生成 1 张像素风图 |

**真机截图前置**（有设备时自动完成，不询问）：
1. **先切单应用模式**：若设备已装启动器（首屏是启动器），直接截只会截到启动器。用 `idf.py -p <PORT> flash`（或 `write_flash` 到 `factory@0x10000`）把最新 app 烧成单应用直启。设备数据无需备份、截完不还原。
2. **最新固件比对**：设备上不是本次最新产物就先重刷；等开机动画过完（约 15s，`SHOT` 能力位注册好）再截。
3. 截图命令：`python3 tools/screenshot_recv.py -t -p <port> -o shotN.jpg`（⚠️ 实际落盘是 `.jpg`）。每张用 Read 自检（非空白、无缺字方框）。

**④-0 手动截图 / 自动截图（有设备时问一次，推荐手动）**：
- **手动（推荐，更快）**：固定 **4 张、不套内容框架**——只提示用户「挑 4 张有代表性的画面」，用户在真机上自己翻页。逐张问：`label` 固定为**「我切好页面了，开始截图」**（就截当前画面）或「跳过」。**4 张全部跳过（0 张）→ AI 按 app 内容生成 1 张像素风封面提交**，不回头追问；部分跳过按实际张数提交。
- **自动**：AI 用调试键自己切页逐张截，按内容规则：第 1 张=首页必截；第 2/3 张=有二级菜单截 2 个二级页 / 游戏类截 2 张运行画面；末张=有控制中心必截「关于」页（无则结束）。⇒ 有 CC 且含二级/游戏=4 张；有 CC 纯单页=2 张；无 CC 含二级/游戏=3 张；纯单页=1 张。

## ⑤ 执行

### MCP 发布（直推）

推荐用 CLI（内部即 MCP `upload_firmware`，单应用包 + 截图 + 信息）：

```bash
python3 tools/sdgoods_publish.py mcp-upload \
  --file dist/<项目名>_app.bin \
  --name "MyApp" \
  --version v1.0.1 \
  --category tool \
  --desc-zh "一句话中文简介" \
  --desc-en "One-line English intro" \
  --github https://github.com/you/your-app \
  --shots shot1.jpg shot2.jpg shot3.jpg shot4.jpg   # ≤4 张，可省
```

- `--version` 独立字段（自动补 `v` 前缀），**不要并进名称**；`--name` ≤60 字符、不含版本号。
- `--category` 必须是平台合法 slug（`list_categories` 返回的，如 `game` / `tool`）。
- 默认存草稿；加 `--submit`（或流程内 `status:"reviewing"`）直接进审核。
- 令牌来源：`set-token` 存的凭据或环境变量 `SDGOODS_DEV_TOKEN`（`mcp-upload` 不接受 `--token` 参数）。

直调 MCP 的等价 HTTP 形态（供 AI / CI 参考，Streamable HTTP，`POST` + JSON）：

```python
import json, base64, os, urllib.request
TOKEN = os.environ["SDGOODS_DEV_TOKEN"]          # sdg_ 令牌走环境变量，别写死
body = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
  "params": {"name": "upload_firmware", "arguments": {
    "name": "MyApp", "category": "tool", "hardware": "sdgoods",
    "version": "v1.0.1", "descZh": "…", "descEn": "…",
    "contentBase64": base64.b64encode(open("dist/MyApp_app.bin","rb").read()).decode(),
    "shots": [base64.b64encode(open(p,"rb").read()).decode() for p in SHOTS],
    "status": "reviewing"}}}
req = urllib.request.Request("https://sdgoods.ai/api/mcp",
  data=json.dumps(body).encode(),
  headers={"Content-Type": "application/json", "Authorization": "Bearer " + TOKEN})
# 本机若配了 HTTP_PROXY，需 build_opener(ProxyHandler({})) 直连
```

**平台 MCP 工具一览**（8 个）：`list_categories`（公开）、`whoami`、`my_firmwares`、`upload_firmware`、`submit_firmware`、`unpublish_firmware`（下架，状态回 draft）、`delete_firmware`（删除，仅 draft/rejected）、`firmware_download`（拉刷机清单做防砖校验）。

### 重新发布（铁律②：删旧建新）

1. **找旧记录**：`python3 tools/sdgoods_publish.py mcp-firmwares`（全列自己账号的）。⚠️ `--name` 是**精确匹配、大小写敏感**——传 `--name ALLINONE` 而库里叫 `AllinOne` 会返回空列表；别据此误判「不存在」，先不带 `--name` 全列再下结论。
2. **拿旧信息当草稿**：`my_firmwares` 返回已含 name/descZh/descEn/category/githubUrl/status/version/shots，直接当草稿逐字段问用户；version / bin / 截图替换成最新产物。
3. **删旧建新**：确认后 `mcp-replace <旧id>`（先 unpublish 回 draft 再 delete，两步合一；位置参数，不是 `--id`），再 `mcp-upload` 建新并提审。

### 下架 / 删除

| 想做 | 命令 |
|---|---|
| 下架（已上架/审核中 → 草稿，市场即不可见） | `mcp-unpublish <id>` |
| 删除（不可恢复；仅 draft/rejected 可删） | `mcp-delete <id>`（或先 unpublish 再 delete） |

- 作者不能自助重新上架——下架后要重新 `submit_firmware` 提审。
- 只对自己的固件有效；查 id 用 `mcp-firmwares`。下架/删除是有副作用的线上操作，先跟用户确认目标 id。

### 防砖校验（发布后必做）

```bash
python3 tools/sdgoods_publish.py mcp-download <固件id> \
  --check-sha256 "$(shasum -a 256 dist/<项目名>_app.bin | cut -d' ' -f1)"
```

通过标准：回执 **4 段齐全**（bootloader@0x0 / partition-table@0x8000 / app@0x10000 / otadata@0x310000）、app 段 sha256 与本地 `dist/<项目名>_app.bin` 逐位一致、`declared:false`（落点由平台按内容算）。⚠️ 该操作与网页刷机一样会累加一次下载计数（复用同一条刷机清单路由，无法避免）。

## 失败排查

- **403 `developer_single_package_only`**：误传了多段 `parts` / bootloader。改回只传 `contentBase64` 单应用包（CLI `mcp-upload --file` 即是）。
- **400 分类错误**：`--category` 不是合法 slug → 用 `list_categories` 查。
- **-32001**：令牌无效/过期/被吊销 → **停下向用户索取新令牌**（见令牌红线），不要换通道。
- **pack_app.py 拒绝打包**：多半是 app 身份还是模板默认名 → 改根 `CMakeLists.txt` 的 `project()` 后重编。
- **dist 与 build 不一致**：烧录/增量重链后 `dist/*.json` 的 version/buildTime 可能是旧值 → 重跑 `python3 tools/pack_app.py -b build` 重导出，保证 dist=build 同 sha。
- **截图没进详情页**：`shots` 最多 4 张；MCP 的 shots 由服务端直传，是开发者令牌提交截图的唯一通道（网页 presign 上传只吃网站登录态）。

## 安全

- 开发者令牌 `sdg_...` 只用于 `Authorization: Bearer`；不写入仓库、不回显全文、不在聊天复述。
- 开放平台服务端不开源；本仓库只含客户端工具与协议说明，不含任何服务端代码与密钥。
