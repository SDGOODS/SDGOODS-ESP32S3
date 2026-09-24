# 把固件提交到「谷仓 SDGOODS 开放平台」

你在这块谷仓次元屏（谷仓电子徽章）上做出来的固件，可以提交到
**谷仓 SDGOODS 开放平台**（官网 **https://sdgoods.ai**）——开发者上传固件、其他用户浏览/下载/烧录的广场。
本文给你三种提交方式，从「点网页」到「让 AI 一条命令帮你交」。

## AI 发布流程（权威，照 `skills/sdgoods-publish/SKILL.md` 走）

不想记下面三种方式？直接让 AI 助手按
[`sdgoods-ai/skills/sdgoods-publish/SKILL.md`](../sdgoods-ai/skills/sdgoods-publish/SKILL.md)
（v5.1 流程，本仓库发布唯一权威）走，全程自动：

1. **改码必重编 + 版本 +1**（铁律①）：动了源码就重编 `build_pub`，`version.txt` 末位 +1；已连设备则重刷最新 + 重截。
2. **探测设备**：`ls /dev/cu.usbmodem*`。
3. **能力检查（静态读源码，不烧、不卡）**：`has_screenshot`（grep `screenshot`/`SHOT`/`sdgoods_screenshot`）、`has_cc`（grep `sdgoods_cc`/`control_center`/`cc_set_app_ctx`）。
   - `has_screenshot=false` → 整条走 **AI 生成路线**：不烧固件、不连设备截；已发布复用市场旧图 / 未发布 AI 生成 1 张像素风图。
   - `has_cc=false` → 真机截图也**跳过「关于」页**。
4. **逐条搜集信息（带草稿，逐字段问）**：按 名称 → 简介中 → 简介英 → 分类 → GitHub 顺序，每一条单独带草稿问「是否修改」，不改用草稿、改了填新值跳下一条；**版本号自动取最新固件版本，不询问**（已发布则用旧记录当草稿）。
5. **截图（自动，按「发布状态 × 设备」矩阵）**：
   - 有设备 → 先切**单应用模式**（多应用先烧 `factory@0x10000`，数据不备份直接覆盖、截完不还原），比对最新固件（非最新先重刷），再按内容规则截：首页必截 + 二级菜单/游戏运行各 2 张 + 仅 `has_cc=true` 截「关于」页。
   - 无设备 → 已发布复用市场旧图 / 未发布 AI 生成像素风图。
6. **问通道**：全部就绪后问「手动发布」还是「MCP 发布」（不预设默认）。
7. **执行**：
   - 手动 → AI 把 `app.bin` + 截图 + 信息草稿打包成 `release/` 文件夹交你自行登录平台填表提交（代码不代推）。
   - MCP → 用 `sdg_` 开发者令牌走 `upload_firmware` 直推单应用包。

> ⚠️ **重新发布 = 删旧建新（铁律②）**：市场上已有同名记录时，先检测旧记录、拿旧信息当草稿、再**先删旧记录再 POST 新建**，平台上同一 app 始终只有一条、禁止两个同名。旧记录即使已上架/审核中也能删（`DELETE /api/firmwares/:id` 不限状态、只校验归属）。
> ⚠️ **审核中可取消（铁律③）**：个人中心「我的固件」里审核中的固件可「取消审核」→ 回草稿，改稿后再提交即重新审核。

> 🛠 **工具分工**：`tools/sdgoods_publish.py` 是提交/删除/替换的 CLI（`login` / `publish` / `publish --replace` / `delete` / `set-token <sdg_>` / `firmwares` / `whoami`）；`tools/pack_app.py` 导出单应用包 `dist/<项目>_app.bin`；真机烧录用 `esptool`、截屏用 `tools/screenshot_recv.py`。`tools/publish_wizard.py` 是**旧版可选助手**，已不推荐作为主流程。

> **版本号（手动改，不自动 +1）**：本工程编译时 `components/sdgoods_launcher/tools/gen_app_info.py`
> 会读根目录 `version.txt`，**透传**（不做末位 +1）为 `SDGOODS_APP_VERSION`，与二进制里的
> `esp_app_desc.version`（ESP-IDF `project()` 时读同一份 `version.txt` 注入）**同源、永远一致**。
> 所以**发新版本时由开发者手动改 `version.txt` 再重新编译**（发布铁律①：改码必重编 + 版本号 +1）。
> 发布时版本号一律取 `version.txt` / 二进制内 `esp_app_desc.version` 的当前值，**不询问用户、不沿用旧记录版本**；编译时间随构建自动更新。

> 🔌 **AI / MCP 字段契约**：REST 端点、鉴权、字段表、两条 MCP 通道的区别，集中在
> [`docs/MCP_CONTRACT.md`](docs/MCP_CONTRACT.md)（随仓库维护，平台后端不开源也能对齐）。

> 📌 **上架前，先读端侧 SDK 硬性要求**：`docs/APP_SDK.md`。
> 两条否决项——① 多应用模式 app **必须提供「返回启动器」入口并调用 `sdgoods_return_to_launcher()`**；
> ② 持久数据**只能走 `sdgoods_appdata_*`（高 16M，按 app_id 隔离）**，不得写低 16M。
> 这两条没接好，提交会被打回。本文只讲「怎么把固件交到平台」，端侧代码怎么写看 `APP_SDK.md`。

> 本文里的 **API 基地址**统一记作 `https://你的平台域名/api`，
> 对应网页端的环境变量 `SDGOODS_API_BASE`。生产环境即 `https://sdgoods.ai/api`。

> 📦 **交上去的是什么**：一份**不带地址**的纯应用镜像 —— 构建目录里的
> `build/SDGOODS_EBADGE.bin`（此处 `SDGOODS_EBADGE` 是官方模板默认名；你的工程名 =
> 根 `CMakeLists.txt` 的 `project(<名>)`，产物即 `build/<你的工程名>.bin`）。烧录地址由
> **设备上的分区表**决定，包本身不携带地址
> （这样同一份包才能落到旧设备的 `0x10000`、或已装启动器设备的空槽）。
> 派生独立工程后，`tools/new_standalone_project.py` 会把这些字面量自动替换成你的工程名，无需手改。
> 上传前先校验并导出：
>
> ```bash
> python3 tools/pack_app.py      # → dist/SDGOODS_EBADGE_app.bin（附 sha256 与元数据）
> ```
>
> ⛔ **不要提交 `merged.bin`**（`merge_bin` 生成的合并镜像）：它自带地址、从 `0x0` 起写，
> 会覆盖用户设备上的 bootloader 与分区表，刷完无法启动。详见 `docs/BUILD.md` 第 4 节。
>
> 🔒 **平台会直接拦**：凡是会写到 `0x10000` 以下的包（`merged.bin`、bootloader、分区表，
> 以及将来的启动器升级包）只有**官方账号**能上传 —— 开发者账号提交会拿到 `403`
> 与一句「引导层只能由官方发布」。开发者能交的永远只是**一个应用包**，
> 这也正是上面要你先跑 `pack_app.py` 的原因：它在本地就把这件事挡在联网之前。
>
> 🔑 **发布前必须先改 app 身份（工程名）** —— 这条忘记得最多、后果也最隐蔽：
> app 的身份 = `esp_app_desc_t.project_name`，由工程根 `CMakeLists.txt` 的 `project(...)` 决定。
> 它在设备上就是 **app_id**：私有数据目录是 `appdata/<app_id>/`，卸载也按它删。
> 克隆本工程后**不改名**的话，你的 app 会和所有同样忘了改名的作者的 app
> **共用同一个数据目录** —— 数据互串，而且用户卸载其中一个时会连带删掉另一个的数据。
> （`tools/new_app_project.py` 派生时已自动改好工程名；若你是手动复制文件创建应用，则**不会**被提醒改工程名，漏改是默认结果。）
>
> 改法（改完必须**重新编译**才会生效）：
>
> ```cmake
> # 工程根 CMakeLists.txt
> project(my_awesome_app C)   # ← 这个名字就是你的 app_id，请换成你自己的
> ```
>
> 两道闸门都会拦：`tools/pack_app.py` 校验时拒绝模板默认名（急着用可加 `--allow-template-name`，
> 但第三方不该需要它）；绕过本地工具直传也一样，服务端会回 `400 app_id_is_template_default`。

---

## 方式一：网页手动提交（最直观）

1. 打开「谷仓 SDGOODS 开放平台」→ **上传固件**。
2. 填名称、分类、中英文简介、版本、适用硬件、标签。
3. **截图**：可以本地选图，也可以**把设备用 USB 连上电脑，点「从设备截图」**
   ——网页通过浏览器 Web Serial 向设备发 `?` 探测能力、再发 `s` 抓图（详见下方「能力探测」）。
4. 选**纯应用镜像** `build/SDGOODS_EBADGE.bin`（**不带地址**；建议先用 `tools/pack_app.py`
   校验导出）。**不要选 `merged.bin`** —— 那是带地址的整机镜像，会让用户刷完无法启动。
5. 提交 → 进入审核队列，通过后上架市场。

网页端提交走的就是本文档后面的同一套 REST API（见 `js/store.js`）。

---

## 方式二：用命令行工具（AI 友好，推荐给开发者）

仓库自带一个**纯标准库 Python** 小工具 `tools/sdgoods_publish.py`，无需 `pip install`。
它完整复刻了网页端的提交链路，AI 助手也能直接调用它。

### 一次性登录

```bash
# 设置 API 基地址（和网页端同一个变量；生产环境见下方）
export SDGOODS_API_BASE=https://sdgoods.ai/api

python3 tools/sdgoods_publish.py login 你的邮箱@example.com
# → 邮箱收到 4 位验证码，输入后即登录
# → refreshToken 缓存在 ~/.sdgoods/credentials.json（权限 600）
```

登录只需一次。之后提交时工具会用缓存的 refreshToken 自动换新 accessToken，**无需再验证码**。

### 提交固件

先校验并导出应用包（**不带地址**），再提交：

```bash
python3 tools/pack_app.py                    # → dist/SDGOODS_EBADGE_app.bin

python3 tools/sdgoods_publish.py publish \
  --file dist/SDGOODS_EBADGE_app.bin \
  --name "我的固件" \
  --desc-zh "一句话介绍这个固件能玩什么" \
  --desc-en "One-line intro of what this firmware does" \
  --category game \
  --version v1.0.0 \
  --hardware sdgoods \
  --tags 飞机 联机 \
  --shots shot1.png shot2.png \
  --github https://github.com/you/your-fw
```

> 工具会在**上传前**校验应用包（与 `pack_app.py` 同一套判据）：带地址的合并镜像
> `merged.bin`、bootloader、别的芯片的固件、超过槽大小的固件，都会被直接拒绝。
> 确实要跳过校验（**不推荐**）加 `--no-verify`。

| 参数 | 说明 | 约束 |
|---|---|---|
| `--file` | 固件 `.bin`（必填） | 必须是**不带地址**的纯应用镜像 |
| `--name` | 名称（必填） | ≤60 字 |
| `--desc-zh` / `--desc-en` | 中/英文简介 | 各 ≤2000 字 |
| `--category` | 分类 slug（必填） | 必须是平台已有分类，见下 |
| `--version` | 版本号 | 默认 `v1.0.0`，≤20 字 |
| `--hardware` | 适用硬件 | `sdgoods`（默认）/ `cyb1` / `both` |
| `--tags` | 标签，可多个 | ≤12 个，每个 ≤24 字 |
| `--shots` | 截图路径，可多个 | ≤4 张 |
| `--github` | GitHub 仓库地址 | 可选 |
| `--max-size` | 槽上限字节数（默认 2.9 MB，以 `firmwarePolicy.ts` 的 `SLOT_APP_MAX_BYTES` 为准） | 校验应用包体积 |
| `--no-verify` | 跳过应用包校验 | **不推荐** |
| `--draft` | 加此参数则存为草稿，否则提交审核 | — |

> 📝 **标签（tags）默认不传**：v5.1 发布流程已把 `tags` 从用户字段清单移除，AI 提交时不询问、默认不传。CLI 仍支持 `--tags`，仅在你明确要打标签时手动加。

分类 slug 取平台运行时的分类表，先查一下有哪些：

```bash
curl -s https://你的平台域名/api/categories
# → {"categories":[{"slug":"game","nameZh":"游戏","nameEn":"Game"}, ...]}
```

其他子命令：`whoami`（看当前登录用户）、`logout`（清本机凭据）。

---

## 方式三：让 AI 直接调 REST API（无需任何工具）

如果你（或你的 AI 助手）不想装工具，直接照下面三步用 `curl`/任意 HTTP 客户端即可。
`tools/sdgoods_publish.py` 就是这套流程的 Python 实现，可作为参考实现。

### 第 1 步：登录拿 token

```bash
# 1a. 请求验证码（无论邮箱是否注册都返回同样响应）
curl -s -X POST https://你的平台域名/api/auth/code \
  -H 'Content-Type: application/json' \
  -d '{"email":"你的邮箱@example.com"}'

# 1b. 用收到的 4 位码换 token
curl -s -X POST https://你的平台域名/api/auth/verify \
  -H 'Content-Type: application/json' \
  -d '{"email":"你的邮箱@example.com","code":"1234"}'
# → {"accessToken":"<JWT>","expiresIn":900,"user":{...}, Set-Cookie: refreshToken=...}
```

- `accessToken`：15 分钟有效，后续请求放 `Authorization: Bearer <JWT>`。
- `refreshToken`：在响应头的 `Set-Cookie: refreshToken=...` 里（HttpOnly，不在响应体）。
  想免交互续期，把它存起来，调 `POST /api/auth/refresh {"refreshToken":"..."}` 换新 accessToken（30 天内有效）。

### 第 2 步：presign 直传文件（截图 + 固件 bin）

文件不走 API 进程，先找平台要一个「预签名地址」，再直接 PUT 到对象存储：

```bash
# 截图（kind=shot，最多 4 张）
curl -s -X POST https://你的平台域名/api/uploads/presign \
  -H 'Authorization: Bearer <JWT>' -H 'Content-Type: application/json' \
  -d '{"kind":"shot","contentType":"image/png","sizeBytes":12345,"filename":"shot1.png"}'
# → {"uploadUrl":"https://...presigned...","headers":{"Content-Type":"image/png"},"publicUrl":"..."}

# 把图片字节 PUT 到上面的 uploadUrl（带返回的 headers）
curl -s -X PUT "<uploadUrl>" -H 'Content-Type: image/png' --data-binary @shot1.png

# 固件 bin（kind=firmware）同理，contentType 用 application/octet-stream
# ⚠ 传的必须是「不带地址」的纯应用镜像（build/SDGOODS_EBADGE.bin），不是 merged.bin
```

每张图 / 每个 bin 都记录下返回的 `publicUrl`，第 3 步要用。

### 第 3 步：创建固件记录

```bash
curl -s -X POST https://你的平台域名/api/firmwares \
  -H 'Authorization: Bearer <JWT>' -H 'Content-Type: application/json' \
  -d '{
    "name": "我的固件",
    "category": "game",
    "version": "v1.0.0",
    "hardware": "sdgoods",
    "status": "reviewing",
    "descZh": "中文简介",
    "descEn": "English intro",
    "tags": ["飞机","联机"],
    "shots": ["<shot1.publicUrl>", "<shot2.publicUrl>"],
    "fileName": "SDGOODS_EBADGE.bin",
    "fileUrl": "<firmware.publicUrl>",
    "fileSha256": "<固件 sha256>",
    "sizeBytes": 1234567,
    "githubUrl": "https://github.com/you/your-fw"
  }'
```

### 创建接口字段表（`POST /api/firmwares`）

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `name` | string | ✅ | 名称，≤60 |
| `category` | string | ✅ | 分类 slug，必须平台已存在（否则 400） |
| `descZh` / `descEn` | string | — | 中/英文简介，各 ≤2000 |
| `version` | string | — | 默认 `v1.0.0`，≤20 |
| `hardware` | enum | — | `cyb1` / `sdgoods` / `both` |
| `status` | enum | — | `draft` / `reviewing`（默认）/ `published` / `rejected` |
| `visible` | bool | — | 是否公开，默认 true |
| `tags` | string[] | — | ≤12，每项 ≤24 |
| `shots` | string[] | — | 截图 URL，≤4（来自 presign 的 `publicUrl`） |
| `fileName` | string | — | 文件名 |
| `fileUrl` | string | — | 固件直传后的 `publicUrl` |
| `fileSha256` | string | — | 固件 SHA-256（设备端 OTA 校验用，可选） |
| `sizeBytes` | int | — | 固件字节数 |
| `githubUrl` | string | — | GitHub 地址 |

> ⚠️ 服务端字段名是 **`descZh` / `descEn`**（不是 `desc`）。多分区固件用 `parts`
> 数组（`[{address, fileName, fileUrl, fileSha256, sizeBytes}]`）代替顶层 `fileUrl`。
>
> **顶层 `fileUrl`（单文件）的语义**：一份**不带地址**的应用镜像 —— 开发者只交
> `build/<项目名>.bin`，由平台**按内容判别落点**（含 `esp_app_desc` 即判为应用包 → `0x10000`），
> 并在用户刷机时（`/api/firmwares/:id/download`）**自动从 `SystemAsset` 拼上官方引导层**
> `bootloader@0x0 + partition-table@0x8000 + otadata@0x310000`。开发者**不用传、也不用管**引导层。
>
> 🔒 **引导层只有官方能传**：凡会写到 `0x10000` 以下的包（`merged.bin` / bootloader / 分区表）开发者账号提交 → **403**（`system_package_staff_only` / `developer_single_package_only`）。开发者能交的只有「一个应用包」；带地址的整机/装机镜像（含 bootloader、分区表）请改用 `parts` 逐段声明地址，且只应由官方账号发布。
>
> ⚠️ **多应用设备刷市场固件会退化回单应用**：刷应用包语义已是**整机刷写**（平台一并下发引导层），会把
> `bootloader@0x0` + `partition-table@0x8000` 一起覆盖。把这份包刷到**已装启动器**的设备上，会把设备
> 换回单应用布局 —— **不砖，但多应用能力丢失**（槽内原 app 不可达，界面不额外提示）。要保留多应用，
> 走 `esptool` 手刷对应槽，或等槽管理 UI 落地。

---

## 能力探测：网页「从设备截图」怎么知道固件支持截屏

网页点「从设备截图」前，会先通过 Web Serial 向设备写 `?`，设备（USB-Serial-JTAG）回一行：

```
SDGOODS-CAPS:SHOT
```

- `SHOT` 在列表里 → 固件含截屏能力，继续发 `s` 抓图。
- 列表为空 / 不含 `SHOT`（含两次重试仍无响应）→ 弹窗提示：
  **「固件缺少『截屏』能力。请让 AI 在 BSP 中启用截屏能力（CONFIG_SDGOODS_SCREENSHOT），重新编译固件并烧录后再截图。」**

这个能力登记表在 `components/sdgoods_board/`（`sdgoods_caps.h/.c`），由常驻串口控制台
`sdgoods_console.c` 应答 `?`。**新增任何 BSP 基础能力**（如 BLE 配对、Wi-Fi 配网），
都应登记到 `sdgoods_caps` 并让控制台应答，网页端就能自动探测、缺失时给出明确指引——
而不是盲发命令导致颜色错乱/静默失败。

PC 端可用 `tools/screenshot_recv.py --caps` 直接验证设备能力：
`python3 tools/screenshot_recv.py --caps -p /dev/cu.usbmodemXXXX` → 打印 `固件能力：SHOT`。

---

## 给 AI 助手的速查

- 开发固件：先读根目录 [`AGENTS.md`](AGENTS.md)（编译、两层边界、硬约束）和 [`README.md`](README.md)。
- 提交固件：本文件三种方式任选；最快是 `tools/sdgoods_publish.py`（login 一次，publish 即可）。
- 不要假设平台 API 基地址，从 `SDGOODS_API_BASE` 环境变量或用户处获取。
- 分类 slug 必须真实存在，先 `GET /api/categories` 核对。
