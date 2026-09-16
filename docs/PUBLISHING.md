# 把固件提交到「谷仓 SDGOODS 开放平台」

你在这块谷仓次元屏（谷仓电子徽章）上做出来的固件，可以提交到
**谷仓 SDGOODS 开放平台**（官网 **https://sdgoods.ai**）——开发者上传固件、其他用户浏览/下载/烧录的广场。
本文给你三种提交方式，从「点网页」到「让 AI 一条命令帮你交」。

> 本文里的 **API 基地址**统一记作 `https://你的平台域名/api`，
> 对应网页端的环境变量 `SDGOODS_API_BASE`。生产环境即 `https://sdgoods.ai/api`；
> 本地起后端调试时换成 `http://localhost:3000/api`。

---

## 方式一：网页手动提交（最直观）

1. 打开「谷仓 SDGOODS 开放平台」→ **上传固件**。
2. 填名称、分类、中英文简介、版本、适用硬件、标签。
3. **截图**：可以本地选图，也可以**把设备用 USB 连上电脑，点「从设备截图」**
   ——网页通过浏览器 Web Serial 向设备发 `?` 探测能力、再发 `s` 抓图（详见下方「能力探测」）。
4. 选固件 `.bin`（多分区固件可加多个 bin 与各自信烧录地址）。
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

```bash
python3 tools/sdgoods_publish.py publish \
  --file build/SDGOODS_EBADGE.bin \
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

| 参数 | 说明 | 约束 |
|---|---|---|
| `--file` | 固件 `.bin`（必填） | — |
| `--name` | 名称（必填） | ≤60 字 |
| `--desc-zh` / `--desc-en` | 中/英文简介 | 各 ≤2000 字 |
| `--category` | 分类 slug（必填） | 必须是平台已有分类，见下 |
| `--version` | 版本号 | 默认 `v1.0.0`，≤20 字 |
| `--hardware` | 适用硬件 | `sdgoods`（默认）/ `cyb1` / `both` |
| `--tags` | 标签，可多个 | ≤12 个，每个 ≤24 字 |
| `--shots` | 截图路径，可多个 | ≤4 张 |
| `--github` | GitHub 仓库地址 | 可选 |
| `--draft` | 加此参数则存为草稿，否则提交审核 | — |

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
