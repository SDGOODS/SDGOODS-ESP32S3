# MCP / 平台发布契约（随仓库维护，平台后端不开源也能对齐）

> 目的：把「AI 助手 / 开发者把固件提交到谷仓 SDGOODS 开放平台」的**字段契约**和**两条 MCP 通道**
> 集中写在一处，随仓库维护。平台后端不开源没关系——本仓库是**客户端**，只调用公开 REST API，
> 不实现服务端逻辑、不含任何密钥。
>
> 配套：[`docs/PUBLISHING.md`](./PUBLISHING.md)（两条对外发布通道 + 邮箱 REST 内部参考 + curl 逐字段示例）、
> [`tools/sdgoods_publish.py`](../tools/sdgoods_publish.py)（REST 客户端，唯一事实来源）、
> [`sdgoods-ai/mcp/sdgoods-mcp-server/server.py`](../sdgoods-ai/mcp/sdgoods-mcp-server/server.py)
> （本地 MCP server，包装上面的脚本）。

---

## 0. 先分清：两条 MCP 通道（别混）

| 通道 | 谁实现 | 给谁用 | 入参形态 | 在不在本仓库 |
|---|---|---|---|---|
| **(A) 本地 stdio MCP server** | 本仓库 `sdgoods-ai/mcp/sdgoods-mcp-server/server.py` | 开发者 / AI 编程助手（Cursor / Claude / WorkBuddy） | **本地文件路径** `file` + `shots:[本地路径]` | ✅ 在 |
| **(B) 平台托管 MCP（生产令牌通道）** | 开放平台服务端 | 开发者 / AI 编程助手（`sdg_` 开发者令牌直连） | `contentBase64` + `shots:[base64]` + `status` | ❌ 不在（平台托管，端点 `https://sdgoods.ai/api/mcp`） |

- 开发者走 **(A)**。它把本地 `.bin` 读出来 → presign 直传 → `POST /firmwares`，**与 `tools/sdgoods_publish.py` 同一个 REST 契约**。
- (B) 是平台自己托管的另一套 MCP，入参用 base64 内联；**它不在本仓库**，本文只点出区别，避免把两条通道的字段搞混。
- ⚠️ 两条通道最终都落到同一个服务端 `POST /firmwares`，**关键不变量完全一致**（见 §3）。

---

## 1. REST 契约（A 通道的唯一事实来源）

本仓库的 `server.py` **只包装** `sdgoods_publish.py`，不直接写 HTTP——所以改 REST 逻辑只改一处即可。

### 1.1 鉴权（邮箱验证码）

```
POST /auth/code            body: {email}                  → 发 4 位码到邮箱（无论是否注册都同样响应，防枚举）
POST /auth/verify          body: {email, code}            → refreshToken（HttpOnly cookie 或响应体兜底）
POST /auth/refresh         body: {refreshToken}           → accessToken（Bearer，AI 不碰）
GET  /auth/me              header: Authorization: Bearer  → 当前用户
```

- 凭据缓存在本机 `~/.sdgoods/credentials.json`（权限 600），与 `server.py` / CLI 共用。
- **AI 不读取、不回显、不写仓库**任何 token。

### 1.2 上传（presign → PUT → POST）

```
POST /uploads/presign   body: {kind:"firmware"|"shot", contentType, sizeBytes, filename}
                       → {uploadUrl, publicUrl, headers}
PUT  <uploadUrl>        raw bytes（带返回的 headers）→ 直传对象存储
（截图最多 4 张，每张走一遍 presign+PUT）
POST /firmwares         body: {name, category, version, hardware, status, visible,
                              fileName, fileUrl, fileSha256, sizeBytes,
                              descZh, descEn, tags, shots, githubUrl}
                       → {firmware:{id, ...}}
```

### 1.3 `POST /firmwares` 字段表

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `name` | string | ✅ | 名称，≤60 |
| `category` | string | ✅ | 分类 slug，必须平台已存在（先 `GET /categories` 核对，否则 400） |
| `version` | string | — | 默认 `v1.0.0`，≤20 |
| `hardware` | enum | — | `cyb1` / `sdgoods` / `both`，默认 `sdgoods` |
| `status` | enum | — | `draft` / `reviewing`（**默认**）/ `published` / `rejected` |
| `visible` | bool | — | 是否公开，默认 true |
| `fileName` | string | — | 文件名 |
| `fileUrl` | string | — | 固件直传后的 `publicUrl`（**不带地址**的纯 app 镜像） |
| `fileSha256` | string | — | 固件 SHA-256（设备端 OTA 校验用） |
| `sizeBytes` | int | — | 固件字节数 |
| `descZh` / `descEn` | string | — | 中/英文简介，各 ≤2000（⚠️ 是 `descZh`/`descEn`，不是 `desc`） |
| `tags` | string[] | — | ≤12，每项 ≤24 |
| `shots` | string[] | — | 截图 URL，**≤4**（来自 presign 的 `publicUrl`；B 通道则为 base64） |
| `githubUrl` | string | — | GitHub 地址 |

> 多分区固件用 `parts` 数组（`[{address, fileName, fileUrl, fileSha256, sizeBytes}]`）代替顶层 `fileUrl`。

### 1.4 读取 / 更新（重新发布用）

```
GET  /firmwares?scope=mine[&q=<关键词>&page=&pageSize=]   → {items:[toApiFw...], total, page, pageSize}
GET  /firmwares/:id                                     → {firmware: toApiFw}
PATCH /firmwares/:id    header: Authorization: Bearer    body: 同 §1.3（字段均可选）
                                                            → {firmware: toApiFw}
```

- 列表 `scope=mine` 按 `ownerId` 过滤（需登录；`authenticateOptional` 有合法 token 才挂 `user`）。
- `toApiFw` 返回的字段（**注意：列表/详情都不含 app 身份字段**）：
  `id, name, category, status, version, hardware, descZh, descEn, tags, shots[], fileName, fileUrl, fileSha256, sizeBytes, githubUrl, parts[], createdAt, updatedAt, publishedAt, author…`
- 重新发布的匹配：`scope=mine` 后用**工程名**（`project()` 名，对应应用包 `fileName=<工程名>_app.bin`）比对 `fileName` 或 `name` 命中（见 `tools/publish_wizard.py published`）。
- `PATCH` 走 `loadEditable` 强约束：**只能改自己的固件，且 `status=reviewing` 时拒绝写**（审核中不可改）。
- 重新发布时：把旧记录的 `name/descZh/descEn/category/tags/github` 当草稿，**`version` 用最新 `version.txt`、`shots` 用本次最新截图**，其余覆盖。



---

## 2. 本地 MCP 工具清单（`server.py` 暴露）

| 工具 | 入参 | 说明 |
|---|---|---|
| `login` | `email`, `code?`, `api_base?` | 不带 `code` 发码；带 `code` 完成登录。凭据缓存本机。 |
| `list_categories` | `api_base?` | 取合法分类 slug（提交前核对）。 |
| `upload_firmware` | `file`, `name`, `category`, `desc_zh?`, `desc_en?`, `version?`, `hardware?`, `tags?`, `shots?`, `github?`, `draft?`, `firmware_id?` | 内部 presign+PUT+POST（无 `firmware_id`）或 PATCH（传 `firmware_id` 走重新发布）。 |
| `list_my_firmwares` | `q?`, `page?`, `page_size?`, `api_base?` | `GET /firmwares?scope=mine`，发布前判断是否已发布过、拿可复用的 id。 |
| `get_firmware` | `firmware_id`, `api_base?` | `GET /firmwares/:id`，重新发布前取旧记录当草稿。 |
| `whoami` | `api_base?` | 当前登录用户。 |
| `logout` | — | 清本机凭据。 |

- `draft=true` → `status:"draft"`；否则默认 `status:"reviewing"`（进审核队列）。
- `shots` 最多 4 张本地路径，逐个 presign+PUT。
- `firmware_id` 非空 → `PATCH /firmwares/:id` 更新已有记录（重新发布），否则 `POST` 新建。
- 接入方式：让 AI 平台以 **stdio** 启动 `python3 sdgoods-ai/mcp/sdgoods-mcp-server/server.py`，并设置 `SDGOODS_API_BASE`。

---

## 3. 平台侧关键不变量（客户端由 `tools/pack_app.py` 强制校验）

这些规则服务端也会拦，但**本地先拦**能省一次联网：

1. **只收「不带地址」的纯 app 镜像**。`merged.bin`、bootloader、分区表、别的芯片固件、超槽上限都会被拒。
   - 判据（与平台一致）：
     - `0x8000` 处 **不能**是分区表魔数 `0x50AA`（`PARTITION_TABLE_MAGIC`）——有则说明是合并镜像。
     - `0x20` 处应是 `esp_app_desc_t` 魔数 `0xABCD5432`。
     - 镜像头 `magic=0xE9`，`chip_id` 匹配。
   - 实现见 `tools/pack_app.py::verify_app_bin`，这是「merged vs app 分水岭」的客户端真相源。
2. **单应用包自动落点**：平台按 `esp_app_desc_t.project_name` 识别这是 app 包，单应用设备写到 `0x10000`（factory）并自动补官方引导层；多应用设备写到空 `ota_N` 槽。包本身不携带地址。
3. **`status` 默认 `reviewing`**：直进审核队列；`draft` 仅存草稿。
4. **`shots` ≤ 4**。

> 改名相关：产物名 = 根 `CMakeLists.txt` 的 `project(<名>)`，编译后 `build_pub/<名>.bin`。
> `pack_app.py` 默认名是 `SDGOODS_EBADGE`（官方模板名），派生独立工程后用
> `tools/new_standalone_project.py` 会把它替换成你的工程名（官方开源参考工程用 `--allow-template-name` 绕过）。

---

## 4. 已核实的对齐状态（写本文时）

- ✅ `server.py` 直接 `import sdgoods_publish as pub`，所有 HTTP 走 `pub._req` / `pub._presign_and_put` / `pub._refresh_access`——**REST 逻辑单一事实来源**，未各自实现。
- ✅ `sdgoods-ai/skills/sdgoods-publish/SKILL.md` 的工具签名与 `server.py` 的 `TOOLS` 一致。
- ✅ `docs/PUBLISHING.md` 的 curl 示例（presign+PUT+POST、`publicUrl`、`status:"reviewing"`）与 `sdgoods_publish.py` 一致。
- ✅ `tools/pack_app.py` 的地址-自由校验（0x8000 ≠ 0x50AA）与平台「merged vs app 分水岭」一致。
- ✅ 重新发布链路已对齐：`GET /firmwares?scope=mine` + `GET /firmwares/:id` + `PATCH /firmwares/:id`（仅 owner 可改、审核中锁定）；客户端由 `publish_wizard.py published` / MCP `list_my_firmwares` + `get_firmware` + `upload_firmware(firmware_id)` 覆盖。

> ⚠️ **漂移防护**：改 REST 字段/端点只改 `sdgoods_publish.py`；`server.py` 保持「纯包装」。
> 若 `pack_app.py` 的校验判据变了，服务端三处（服务端 + store.js + upload-firmware.html）应同步改——
> 客户端以 `pack_app.py::verify_app_bin` 为权威核对点。

---

## 5. 开发者最短路径

```bash
# 让 AI 平台以 stdio 启动本地 MCP server（需先设 API 基地址）
export SDGOODS_API_BASE=https://你的平台域名/api
python3 sdgoods-ai/mcp/sdgoods-mcp-server/server.py   # 由 AI 平台拉起，无需手跑

# 然后在 AI 对话里：
#   login(email) → 用户给码 → login(email, code="1234")
#   list_categories()
#   upload_firmware(file="dist/SDGOODS_EBADGE_app.bin", name="…", category="game", shots=[…])
```

未连 MCP 时回退 `tools/sdgoods_publish.py`（同样的邮箱验证码登录 + 一键 publish，纯标准库）。
