---
name: sdgoods-publish
description: 编译谷仓次元屏固件并发布到「谷仓 SDGOODS 开放平台」，以及平台上固件的下架/删除。交互式流程：用户说发布/提审 → **第一步就问「MCP 发布（推荐）」还是「手动发布」**（⓪）：选 MCP→查本机是否已有开发者令牌，无则请用户粘贴令牌、跳过则降级为手动流程；选手动→有令牌仍走原采集流程(拉平台旧记录当草稿)，无令牌则 AI 按 app 内容直接起草不查平台；令牌就绪后 AI 自动（重编+版本+1 若改码 → 探测设备 → 搜集/起草 app 信息并询问是否更新/修改 → 截图前先能力检查项目代码(是否带截图功能/是否带控制中心)：无截图功能→整条走AI生成路线(不烧固件、按发布状态复用旧图或AI生成像素风图)、有控制中心(has_cc=true)才截关于页；有截图功能才进入设备分流→有设备时先问「手动截图(推荐,更快)/自动截图」，手动=固定 4 张且不套内容框架、由用户自选有代表性的画面翻页、逐张问「我切好页面了，开始截图 / 跳过」，若 4 张全部跳过则 AI 按内容生成 1 张像素风封面提交，自动=按现有调试键流程走→按「是否发布过×是否连设备」决定图从哪来——已发布+无设备→复用市场旧图、未发布+无设备→AI 生成像素风图、有设备→先切单应用模式并比对固件最新(非最新先重刷)后按内容规则截(首页必截+二级菜单或游戏运行各2张+有控制中心必截关于页)）→ 按 ⓪ 已定的通道执行（不再重复问）。两者都上传同一个 app.bin 单应用包（平台自动补引导层、落 0x10000，不会变砖）；手动发布 = AI 把 app.bin + 截图 + 信息草稿打包成 release 文件夹交用户自行登录平台填表提交，MCP 发布 = 代码用 sdg_ 令牌 upload_firmware 直推。⚠️ 重新发布 = 更新优先：用平台 MCP **`update_firmware`**（`mcp-update <id>`，吃 `sdg_` 令牌，不需要登录态）**原地改掉上架固件**（保住 id/downloads/flashes/审核时间线），再走 MCP `submit_firmware` **重新提审**；只有更新通道不可用时才降级删旧建新（禁两个同名）。另含官方恢复包（走后台 admin 接口，四段 bootloader@0x0 + 分区表@0x8000 + 启动器@0x10000 + otadata@0x310000）。Use when asked to 发布 / 提交 / 上传 / publish / upload firmware，在平台上下架 / 撤下 / 删除固件（unpublish / delist / delete firmware），或要做 / 更新官方恢复包 / 救援包 / recovery firmware。
agent_created: true
---

⚠️ **三条发布铁律（用户 2026-09-23 硬性规定，② 于 2026-09-27 修订）**：① 改源码必重编、版本号必 +1，连设备则必重刷最新 + 重截；② 重新发布**已上架/已有下载数据的** app 时，**优先「原地更新」**（MCP `update_firmware` / CLI `mcp-update <id>`，保住 id / downloads / flashes / 审核时间线；2026-09-27 起令牌通道直接可用，**不需要登录态**），**只有更新通道不可用时才降级为删旧建新**（`mcp-replace`），且降级前必须向用户明示「下载数据会清零」并确认 —— 详见下方「🔄 更新优先」一节；同一 app 始终只有一条、禁止两个同名项目；③ 平台支持**审核中取消**（`POST /api/firmwares/:id/cancel-review` + 个人中心「取消审核」按钮），作者撤回后改稿再提交即重新审核。

📦 **发布脚本**：`tools/sdgoods_publish.py`（就在本仓库内）。它同时维护在多份**派生工程**里、内容逐字节一致 —— 改脚本时记得所有副本一起改。

# 编译并发布固件到开放平台（交互式）

## ⛔ 开发者上传形态：只传「一个 app.bin 单应用包」，绝不要传 bootloader/分区表

平台的刷机逻辑在 2026-09-17（v2.40.0）/ v2.41.0 改造后，**开发者账号只能提交「一个应用包」**：

- **应用包 = 单个 `app.bin`，不带烧录地址** → 平台按**文件内容**判别落点（`0x20` 处是 `esp_app_desc` → 判定为应用包 → 落 `0x10000`），并在用户刷机时（`/api/firmwares/:id/download`）**自动从 `SystemAsset` 拼上官方引导层** `bootloader@0x0 + partition-table@0x8000 + otadata@0x310000`，回执 4 段、`declared:false`（落点由平台算，开发者写不歪地址）。
- **开发者一旦传 `bootloader@0x0` / 分区表 / 多段 `parts` → 直接 403**（`developer_single_package_only` 或 `system_package_staff_only`）。引导层只有官方能传。
- 三处同源实现：服务端 `server/src/lib/firmwarePolicy.ts`（权威安全边界）、`js/store.js` 的 `classifySingleBin`（刷机落点）、`upload-firmware.html` 的 `readFirmwareHead`（提交前提示）。前端两份只负责提前告知，不是安全边界。

> ## 🔄 重新发布 = 「更新」优先，不是「删旧建新」（2026-09-27 用户新规）

已上架 / 已有下载数据的固件要重新发布时，**走原地更新**，**不要先删旧记录** —— 删掉会把
`downloads` / `flashes` / 刷机事件 / 审核时间线**级联删除、不可恢复**。
**两条通道都能原地更新**（MCP 更新工具 `update_firmware` 已于 2026-09-27 上线 ⇒ 令牌通道不再需要降级）：
- 🔑 **令牌通道（生产默认，推荐）**：MCP **`update_firmware`**（CLI `mcp-update <id>`，吃 `sdg_` 令牌）
  改信息/换 bin/换截图 → MCP `submit_firmware` 重新提审。**这条是默认路径，别再走删旧建新。**
- 🖥 **登录态通道（仅本地栈，备选）**：`publish --id <旧id> ...` → REST `PATCH /api/firmwares/:id`。
完整分支见 `2c.` 与下文「🔄 更新优先」节。

⛔ **唯一会砖机的不是「平台发布」，而是本地 `esptool write_flash` 把 app.bin 错烧到 0x0** —— 那是本地刷写事故（见「事故处置」），与平台发布无关。平台发布单应用包是安全的：落点由平台按内容判别，且引导层由平台托管，开发者写不歪地址。

### 为什么不要被旧的「单 bin 写 0x0 会砖」说法误导

旧文档曾写「把 app 镜像当单文件传 → 平台从 0x0 写 → 砖机」。那是 **v2.40.0 之前**的行为。现在平台已按内容判别：含 `esp_app_desc`（0x20 处）的包一律当应用包落 `0x10000`，**不会**落到 0x0。所以：
- ✅ **开发者正确做法**：只传 `pack_app.py` 导出的 `dist/<项目>_app.bin`（单文件 / 应用包）。
- ❌ **开发者错误做法**：自己拼 `parts` 带 `bootloader@0x0` —— 既多余（平台会再补一份）又会 403 被拒。
- 服务端仍留了一道兜底闸门（单文件模式查 `0x8000` 处是否有分区表魔数），但那是防整机镜像误传，不是逼开发者传 parts。

### 三类包：带不带地址，决定它是「通用」还是「绑机器」

- **应用包 = 单个 `app.bin`，不带地址** → 落点由**设备的分区表**在刷机时决定（app 镜像存虚拟地址、物理偏移经 MMU 映射，故同一份包在 `factory@0x10000` 设备与多 OTA 槽设备上都能跑）。这是**给所有开发者**的形态。
- **装机包 / 整机包 = `parts`，自带地址** → 只由**官方账号**发布（`Firmware.official` / `POST /admin/firmwares`），例如官方恢复包（见文末）。

**工程侧硬约束（已落地在参考工程 SDGOODS-ESP32S3 里）**：`tools/pack_app.py` 是唯一导出入口 ——
六项校验（① `0x8000` 处**不是**分区表魔数 ② 首字节 `0xE9` ③ `0x20` 处 `0xABCD5432`
④ 偏移 `0x0C` 的 chip_id == `0x0009`(ESP32-S3) ⑤ 体积 ≤ 槽上限 **2.9 MB**
⑥ **app 身份不是模板默认名**，见下），
通过才导出 `dist/<项目>_app.bin`（附 `.json` 元数据）。

⚠️ **#173 新增第 ⑥ 项：app 身份（app_id）必须改过**。app 身份 = `esp_app_desc_t.project_name`
（**绝对偏移 0x50** = desc 起点 0x20 + 字段偏移 0x30，拿真机固件核对过）。它决定设备上的
`appdata/<app_id>/` 目录名，卸载也按它删 ⇒ 撞名 = 数据互串 + 卸载互删。
克隆/派生工程后不改 `CMakeLists.txt` 的 `project()` 是**默认结果**（手动复制文件创建应用时不会被提醒改工程名；`new_app_project.py` 已自动改名），
`pack_app.py` 会直接拒绝（急着用可加 `--allow-template-name`，第三方不该需要它），
服务端也会回 `400 app_id_is_template_default`（官方账号豁免）。⚠️ 连带检查：工程里若曾写死 `#define APP_ID "..."` 也要改成动态取
`esp_app_get_description()->project_name` —— 否则身份变两份（数据目录用常量、卸载用 project_name）。

⚠️ **#174 体积口径已归一为 2.9 MB**（`pack_app.py` 默认原为 3 MB）。洗口径的原因：
三处各写一份（pack_app.py 3MB / devices.ts 2.9MB / 刷机弹窗文案）导致「本地校验通过 →
过审上架 → 用户点刷机才发现装不进槽」。服务端唯一常量是
`server/src/lib/firmwarePolicy.ts` 的 `SLOT_APP_MAX_BYTES`。`tools/sdgoods_publish.py publish`
在**登录前**用同一套判据再校验一次（`--no-verify` 可跳过，**不推荐**）。`merged.bin` 只用于救砖/空片首次烧录，**禁止提交平台**。

⚠️ **端序坑（实测踩过）**：分区表魔数是 **`0x50AA`**（小端读 uint16；文件头两字节的字节序是 `AA 50`），
写成 `0xAA50` 判不出来 → 「合并镜像」会**漏判**成普通 app 镜像。

✅ **平台侧已改造（2026-09-17 / v2.40.0）** —— 这两处是**一起**改的，别再按旧行为判断：
- **落点按文件内容判别**（三路）：`0x8000` 处分区表 → 整机镜像落 `0x0`；`0x20` 处 app_desc → 应用包落 `0x10000`；都没有 → 拒。
  三份同源实现：服务端 `server/src/lib/firmwarePolicy.ts`（**权威，安全边界**）、
  `js/store.js` 的 `classifySingleBin`（刷机落点）、`upload-firmware.html` 的 `readFirmwareHead`（提交前提示）。
  前端两份只负责提前告知，**不是安全边界**（浏览器里用户能改）。
- 🔒 **引导层只有官方能传**：凡是会写到 `0x10000` 以下的包（`merged.bin` / bootloader / 分区表 / 将来的启动器升级包），
  开发者账号提交 → **403**（`system_package_staff_only`）。开发者能交的只有「一个应用包」；
  官方交的这类包自动 `official=true`。两道闸门缺一不可：
  ① **段数与地址**（开发者多段 → 403 `developer_single_package_only`）；
  ② **内容形态** —— 地址是**客户端声明的**，只查地址就能被"声明 `0x10000`、实际传整机镜像"绕过，
     而刷机端按**内容**落点，照样写到 `0x0`。预签名直传的包由服务端 Range GET 读回包头（几十 KB）核对，
     读不到 → 400 `firmware_object_missing`（以前会静默建出一条"文件其实是空的"固件）。
- `PATCH /api/firmwares/:id` 也拦（否则"先传应用包、再改地址为 `0x0`"就能绕）；
  只有 `POST /api/admin/firmwares`（staff 专属）不受限。

📦 **刷机时平台会自动补上官方引导层（v2.41.0）** —— 开发者**不用传、也不用管**
bootloader 与分区表：它们不属于任何固件，由平台托管（`SystemAsset`，按硬件型号存一套，
后台「默认文件」页，只有官方能改）。用户刷你的应用包时，服务端在 `/api/firmwares/:id/download`
把它们拼进刷机清单**一并下发**（回执的 `assets` 字段会写补了什么）。
⚠️ 因此刷应用包语义已从「只替换 app 槽」变为**整机刷写**（会覆盖设备原有的 bootloader/分区表）。
固件本身是**整机镜像**或**多 bin 装机包**时不补（自带引导层，再补就是两段写同一片 flash）。
- 回归：`cd server && npx vitest run`（112 条，含 `firmwarePolicy.test.ts` 18 条）
  \+ `node scripts/e2e-firmware-policy.mjs`（18 条端到端，跑完自清理固件与测试用户）。

⛔ **刷机写入白名单**（平台侧须按设备当前布局硬编码）：

| 设备状态 | 允许写 | 禁止 |
|---|---|---|
| 无启动器（`factory@0x10000` 单应用） | app 段 `0x10000` 起 | `0x0` / `0x8000`（除官方整机包） |
| 已装启动器（多 OTA 槽） | 槽区 `0x20000` 起 | `0x0` / `0x8000` / 启动器段 / `otadata` / 高 16 MB（除官方启动器升级包） |

把 5 段式整机包刷到已装启动器的设备上，会连 bootloader + 分区表一起换回单应用布局 → **退化回单应用**（不砖，但多应用能力丢失）。

## 触发
用户说"发布固件 / 提交到平台 / 上传 firmware / 提审 / publish"。本 skill 覆盖「⓪ 先问发布方式(MCP 推荐/手动) → 自动准备（编译→探测设备→搜集信息→截图）→ 上传/交付」全流程。
🔴 **⓪ 通道确认是第一件事**（用户 2026-09-24 新规）：用户一开口说发布，**立刻**问「MCP 发布（推荐）还是手动发布」，据此决定是否要令牌、草稿从哪来；不要再等到全部就绪后才问。
⚠️ **2026-09-27 实测踩过**：首次发布 CALC 时把「通道确认」排在了「名称→简介中→简介英→分类→GitHub」五条信息采集**之后**才问，违反本条 —— 无论新发还是重发，⓪ 都必须排在搜集信息之前；搜集信息阶段的草稿一律 AI 起草即可，不必等通道确定。

## 🔁 端到端顺序（用户 2026-09-23 确认版）

```
用户说「发布」/「提审」/「publish」
   │
   ├─⓪ 【第一步就问，2026-09-24 新规】发布方式：MCP 发布（**推荐**） 还是 手动发布？
   │     （AskUserQuestion：第一个选项 = MCP 发布并标注「推荐」；第二个 = 手动发布）
   │       │
   │       ├─ 选 **MCP 发布**
   │       │     └─ 检查本机是否已存开发者令牌（CLI `mcp-token` / 凭据 `api_token` 是否 `sdg_` 开头）
   │       │           ├─ **已存** → 走原采集流程（③ 拉平台旧记录当草稿）
   │       │           └─ **未存** → 请用户把开发者令牌粘贴进输入框（AskUserQuestion「其他」自由输入）
   │       │                 ├─ 填了 `sdg_...` → `set-token` 保存（**不回显、不写仓库**）→ 走原采集流程
   │       │                 └─ **跳过** → 降级为「手动发布」流程（按下方手动分支走）
   │       │
   │       └─ 选 **手动发布**
   │             ├─ **已存开发者令牌** → 仍走**原采集流程**（用令牌拉平台旧记录当草稿，信息更准）
   │             └─ **未存令牌** → **不查平台**，AI 直接按 app 内容生成草稿，逐字段问用户
   │
   ├─① 改过源码？ ──是──► 重编 + 版本号 +1（铁律①），产物 = build_pub/*
   │
   ├─② 探测设备：ls /dev/cu.usbmodem*  （端口随时变，每次现查）
   │
   ├─②.5 能力检查（静态读源码，不烧、不卡用户）
  │       ├─ has_screenshot?  全仓库（含 components/）搜截图符号（screenshot / SHOT / sdgoods_screenshot / 截图调试键 `s`）
  │       └─ has_cc?          看平台层：build*/<PROJECT>.map 含 sdgoods_cc_open，或仓库含 components/sdgoods_launcher/src/sdgoods_cc.c（WHOLE_ARCHIVE）。app 源码无 sdgoods_cc 字面量 ≠ 没有控制中心
   │
   ├─③ 逐条搜集 app 信息（带草稿，逐字段问，不一次性列全）
   │     草稿来源（由 ⓪ 决定）：
   │       ├─ **有令牌**（MCP 已存/刚填、或手动但本机已存）→ 已发布过：拉平台旧记录(name/descZh/descEn/category/githubUrl)；没发布过：AI 生成
   │       └─ **无令牌**（手动且本机未存 / MCP 但用户跳过填令牌）→ **不查平台**，AI 直接按 app 内容生成
   │     版本号：自动取最新固件版本(version.txt/二进制)，不询问
   │     逐字段循环（名称→简介中→简介英→分类→GitHub）：
   │        └─ AskUserQuestion 带该字段草稿问「是否修改」（⚠️ 长草稿见 §2「草稿展示规则」：先正文/文件展示，不塞 option description）
   │              ├─ 不修改 → 用草稿，跳下一条
   │              └─ 修改 → 用户在空白填写，跳下一条
   │
   ├─④ 截图（能力检查先分流）
   │     ├─ 无截图能力 (has_screenshot=false) → 整条走「AI 生成路线」，不烧固件、不连设备截
   │     │     ├─ 已发布 → 复用市场已有旧图
   │     │     └─ 未发布 → AI 生成 1 张像素风图
   │     └─ 有截图能力 (has_screenshot=true)
   │           ├─ 无设备 → 按「发布状态 × 设备」矩阵（见 2b ①）：已发布→复用旧图 / 未发布→AI 生成像素风
   │           └─ 有设备
   │                 ├─ 先切单应用模式（多应用先烧 factory@0x10000，见 2b ②）
   │                 ├─ 最新固件比对：非最新先重刷（单应用模式）
   │                 └─ ④-0 问一次：手动截图 还是 自动截图？（**推荐手动**，更快；默认推荐项写手动）
   │                       ├─ 自动截图 → 按原流程自动切页（调试键）+ 逐张截（内容规则见 2b ③）
   │                       └─ 手动截图 → **固定 4 张、不套内容框架**，只提示「挑 4 张有代表性的画面」，
   │                             **由用户在真机上自己翻页**（AI 不规定每张必须是什么画面）
   │                             逐张 AskUserQuestion：「第 k 张 / 共 4 张 —— 我切好页面了，开始截图 / 跳过」
   │                               ├─ 我切好页面了，开始截图 → 立刻截当前画面（screenshot_recv.py -t），保存并自检，进入下一张
   │                               └─ 跳过 → 不截这张，直接进入下一张
   │                             逐张循环直到 4 张走完；**若 4 张全部跳过（0 张）→ AI 按 app 内容生成 1 张像素风封面提交**（用户 2026-09-23 新规，不留空图）
   │
   └─⑤ 执行（⓪ 已定好通道，此处不再问）
         ├─ 手动发布 → AI 把 app.bin + 截图 + 信息草稿 打包成 release 文件夹 → 交用户自行登录平台填表提交（代码不代推）
         ├─ 🔄 旧记录已上架 / 有下载数据 **且有网站登录态** → `publish --id <旧id>` **原地更新**（id、downloads、flashes 全保留，重新提审）
         ├─ 🗑 同上但**没有登录态** → 用 AskUserQuestion 问「现在登录走更新（推荐）/ 仍删旧建新」；
         │     选删除才走 `mcp-replace <旧id>` + `mcp-upload`（先明说下载数据会清零）
         └─ MCP 发布 → 代码用 sdg_ 令牌 upload_firmware 直推（单应用包 app.bin + shots + 信息）
```

⚠️ **全程只在四处打断用户**（2026-09-24：⓪ 提到最前）：
⓪ 发布方式（MCP 推荐 / 手动）+ 未存令牌时请用户粘贴令牌、③ 搜集信息后的「是否更新/修改」、
④-0 有设备时的「手动截图 vs 自动截图」及手动模式下的逐张「我切好页面了，开始截图 / 跳过」。
🔴 **第 5 处条件性打断（2026-09-27 新增）**：重新发布且旧记录已上架/有下载数据、**但没有网站登录态**时，
必须先问「现在登录走更新（推荐） / 仍删旧建新（下载数据清零）」——这一处只在 2c 的 0. 分支里触发，
其余任何时候都不要为了「要不要更新」多问。
重编/重刷/自动截图都自动完成，不询问；**⑤ 执行前不再问通道**（已在 ⓪ 定完）。

## ⏱ 进度播报规范（每步必报，禁止静默串完再一次性汇报）

用户说「发布」后，整条链路短则 3 分钟、长则十几分钟（含等用户翻页）。
**中间任何一步连续 30 秒没有输出，用户就会以为卡死。** 因此：

**① 开场先给一张路线图**（一句话 + 总预估 + 会打断几次）：
> 📋 发布流程共 **8 步**，预计 3–8 分钟，中途需要你确认 3 次（发布通道 / app 信息 / 截图方式）。

**② 每步开始/结束各播一条**，格式固定：

| 时机 | 格式 | 示例 |
|---|---|---|
| 开始 | `⏳ [k/8] <步骤> —— 预计 ~<时长>` | `⏳ [2/8] 重新编译固件 —— 增量约 50 秒，全新约 3–6 分钟` |
| 成功 | `✅ [k/8] <步骤> —— <关键结果>` | `✅ [2/8] 编译完成 —— 1,683,856 字节，v1.0.45` |
| 失败 | `❌ [k/8] <步骤> —— <原因>；已停在 xx，不继续` | `❌ [7/8] 打包校验 —— 版本号与 version.txt 不一致，发布中止` |

**③ 8 步清单与预估**（按端到端顺序图编号映射）：

| # | 步骤 | 预估 | 备注 |
|---|---|---|---|
| 1 | ⓪ 发布通道确认（MCP / 手动）+ 令牌 | 等你选，秒级 | 带令牌分支说明 |
| 2 | ① 重编 + 版本号 +1 | 增量 ~50s / 全新 3–6 分钟 | 未改码则跳过并说明「源码未变，跳过重编」；**内部子进度按 `sdgoods-build-flash` 的 6 步播报**（不必重复念开场白） |
| 3 | ② 探测设备 | <1s | `ls /dev/cu.usbmodem*`，有/无都要报一句 |
| 4 | ②.5 能力检查（截图 / 控制中心） | <5s | 报出 `has_screenshot` / `has_cc` 判定结果 |
| 5 | ③ 搜集信息（名称→简介中→简介英→分类→GitHub） | 等你 5 次 | **子进度**：`[5/8] 搜集信息 2/5 —— 简介中文` |
| 6 | ④ 截图 | 手动：等你翻页；自动：开机 15s + 每张 ~10s | **子进度**：`[6/8] 截图 2/4`；无设备/无能力时说明走了哪条路线 |
| 7 | 打包 `pack_app.py` + 版本核对 | <10s | 必报 `dist` 与 `version.txt` 是否一致 |
| 8 | ⑤ 上传 + 防砖校验 | 20–60s | 报固件 id / 状态 / 4 段落点 / 截图数 |

**④ 长步骤必须后台执行**：第 2 步（编译，细节见 `sdgoods-build-flash`「进度播报规范」）、第 8 步（上传）通常 >60 秒 → 用后台任务，
启动后立刻说一句「已开始，跑完我会回报」，跑完按 ③ 的格式回报结果。
⚠️ 不要为了「少说话」把编译输出全丢掉：至少保留末 15 行，并说明正在编译。

**⑤ 播报是单向输出，不算打断用户**（不要求用户回答）。
🔴 **不得**因为要播报进度而改用 `AskUserQuestion` —— 打断次数仍严格按「全程只在四处打断」的红线执行。
唯一例外：打断提问本身要带进度前缀（如 `[5/8] 搜集信息 2/5`），让用户知道整体走到哪了。

**⑥ 收尾给一张汇总表**（id / 状态 / 版本 / 截图数 / 落点 4 段 / 下一步是什么），不要只回一句「已提交」。
⚠️ **2026-09-27 实测**：首次发布 CALC 时**没走这套播报**（缺开场路线图与 `[k/8]` 子进度），用户事后反馈「不知道用的是不是最新流程」。
此后发布一律先给路线图 + 每步 `[k/8]` 前缀；中途长时间无输出时补一句「正在 xx，预计 ~yy」。

## ⚠️ 发布通道（⓪，用户说「发布」后第一件事就问；推荐 MCP）

🔴 **顺序已改（用户 2026-09-24 新规）：通道确认从「最后一步」提到「第一步」。** 用户一说「发布/提审应用」，
**先**用 `AskUserQuestion` 问「MCP 发布（**推荐**）」还是「手动发布」，再按所选分支决定要不要令牌、草稿从哪来。
不要再等到全部就绪后才问，也不要因为本 skill 默认写「MCP 推送」就跳过这一问。

🔴 **MCP 令牌失效/缺失的处理红线（用户 2026-09-24 重申）**：选了 MCP 发布后，若本机令牌**缺失**、或调用 MCP 返回 **-32001（令牌无效/不被该环境接受）**，必须**立刻停下来向用户索取有效的 `sdg_` 开发者令牌**，**绝不允许**改走 REST 登录态通道（`publish`/`delete`/`firmwares`/`firmware`）去绕过。真实生产环境只有 MCP（无登录态），REST 登录态只是本地栈遗留物、不可作为发布通道；即便本地栈能登录，也只在「用户明确选手动发布且该通道」时用于采集信息，绝不能拿它代推固件。令牌确属「用户跳过提供」时，按手动发布流程（AI 打包 release 交用户自传），而不是偷偷改用 REST 直推。

**分支逻辑（严格按此执行）**：

1. **选 MCP 发布**
   - 先查本机有没有开发者令牌：
     ```bash
     python3 tools/sdgoods_publish.py mcp-token        # 输出 SAVED / NOT_SAVED，绝不回显令牌明文
     ```
   - **已存（SAVED）** → 走**原采集流程**（③ 用 `mcp-firmwares --name <app名>` 拉平台旧记录当草稿 → 逐字段确认）。
   - **未存（NOT_SAVED）** → 用 `AskUserQuestion` 请用户**把开发者令牌粘贴到输入框**（选「其他」自由输入；
     正文里说明「个人中心 → 开发者令牌 生成，明文只显示一次」）：
     - 用户**填了** `sdg_...` → `python3 tools/sdgoods_publish.py set-token <令牌>` 保存 → 走**原采集流程**。
     - 用户**跳过** → **降级为手动发布流程**（按下面第 2 条「未存令牌」分支走：AI 按内容起草，不查平台）。
2. **选 手动发布**
   - **已存开发者令牌** → 仍然走**原采集流程**（拉平台旧记录当草稿，字段更准，用户改起来省事）。
   - **未存开发者令牌** → **不查平台**（没令牌查不了），由 **AI 按 app 内容直接生成草稿**（名称/简介中英/分类/GitHub），
     然后逐字段问用户是否修改。
3. **令牌处理红线**：不回显全文、不写进仓库、不在聊天复述；只存 `~/.sdgoods/credentials.json` 的 `api_token`。
- **手动发布（普通用户视角）**：AI **不代推**。AI 把以下三样东西放进一个 **release 文件夹**（如 `release/<app名>-v<版本>/`）：
  1. `app.bin` —— `tools/pack_app.py` 导出的单应用包；
  2. `shots/` —— ④ 步得到的截图（无设备时是 AI 生成的像素风图）；
  3. `info.md` / `info.json` —— AI 起草的发布信息草稿（名称 / 简介中英文 / 版本 / 分类 slug / GitHub / 截图说明）。
  然后**交用户本人登录平台**（`sdgoods.ai` 或本地 `127.0.0.1:8080`），在「提交固件」表单里手动填表、选文件、点提交。代码不参与上传。
- **MCP 发布（开发者令牌）**：开发者令牌 `sdg_...` 走 `POST /api/mcp` 的 `upload_firmware`；令牌经 `tools/sdgoods_publish.py set-token <sdg_>` 存入凭据（**不回显、不写仓库、不在聊天复述**）。上传内容同样是那个 `app.bin` 单应用包 + 截图 + 信息。
- **重新发布分通道执行（2026-09-27 修订：更新优先）**：
  - 🔄 **更新（推荐，保住下载数据）· 令牌通道（生产默认）**：MCP `update_firmware`（`id` + 要改的字段）
    → **id / downloads / flashes / 审核时间线全部保留**；只改信息就只传信息字段，换 bin 就再带
    `contentBase64`（或 `parts`），换截图就带 `shots`（**整组替换**）。改完 `submit_firmware` 重新提审。
  - 🔄 **更新 · 登录态通道（仅本地栈）**：`publish --id <旧id> ...` → `PATCH /api/firmwares/:id`，同样原地更新。
  - 🗑 **删旧建新（降级，仅在更新真的走不通时）· 令牌通道**：`mcp-replace <旧id>` —— 先 `unpublish_firmware`（published/reviewing → draft）再 `delete_firmware`
    （因为 MCP 的 `delete_firmware` **只收 draft/rejected**，直接删线上记录会报错）。删完再 `mcp-upload` 建新。
    仅在「用户确认接受下载数据清零」时才走 —— **但先试 `update_firmware`，它现在吃 `sdg_` 令牌**。
  - **登录态通道（仅本地栈）**：`publish --replace <旧id>`（REST `DELETE /api/firmwares/:id` 不限状态、只校验归属，一步到位）。
  - **手动通道**：由**用户在平台上自己删/改**旧记录，再把新 release 提交上去（AI 在 info 草稿里提示「市场已有同名旧版，请先在个人中心更新或删除旧记录」）。

## 🔑 凭据矩阵：采集信息 vs 上传（2026-09-24 实测，别混用）

采集信息（③ 拉旧记录当草稿、查状态/版本）**两条凭据都能做，但生产只有开发者令牌** —— 登录态走 REST
（`~/.sdgoods/credentials.json` 的 `refresh_token` → `POST /api/auth/refresh` 换 access token），
令牌走 MCP `my_firmwares`。两条凭据**互不通用**（实测）：

| 端点 / 用途 | 登录态 JWT（网站账号） | 开发者令牌 `sdg_` |
|---|---|---|
| REST `/api/firmwares?scope=mine`（**列自己的**） | ✅ 200 | ❌ **401** |
| REST `GET /api/firmwares/:id`（**单条详情**，含 downloads/flashes） | ✅ 200 | ✅ **200（2026-09-27 实测）** |
| REST `publish` / `delete` / `cancel-review` / `download`（防砖验证） | ✅ | ❌ 401 |
| MCP `/api/mcp`（`upload_firmware` 直推、`my_firmwares`、`whoami`） | ❌ `-32001` 仅接受开发者令牌 | ✅ |

🔑 **令牌一律别问用户要，直接本机取**：
```bash
# GitHub（username 固定 x-access-token，口令=令牌）
printf 'protocol=https\nhost=github.com\n\n' | git credential fill | grep '^password='
# Gitee（返回 32 位 password）
printf 'protocol=https\nhost=gitee.com\n\n' | git credential fill | grep '^password='
```
⚠️ **别用 `security find-internet-password -a git -s gitee.com -w`**（2026-09-27 实测**取不到**，长度 0、
后续 `sync_gitee.sh` 直接空跑失败）—— 只有 `git credential fill` 这条路能拿到 Gitee 令牌。
拿到后喂给同步脚本：`env -u http_proxy -u https_proxy -u ALL_PROXY GITEE_TOKEN="$TOKEN" bash tools/sync_gitee.sh main`。

- 无凭据（不带 `Authorization`）→ REST 一律 **401**。
- 登录态从哪来：`python3 tools/sdgoods_publish.py login`（邮箱验证码；本地栈看 Mailpit）。令牌：`set-token <sdg_>`。

🔴 **生产环境默认没有登录态（用户 2026-09-24 指出）**：正式上线时用户**只给开发者令牌 `sdg_`，绝不会提供
refresh_token / 邮箱验证码**，`~/.sdgoods/credentials.json` 里的登录态只是**本地开发栈**的遗留物。
⇒ **流程一律以「令牌通道」为默认**，登录态只当作本地栈的可选加速路径，**禁止假设生产可登录**。

| 发布动作 | 🔑 令牌通道（生产默认） | 登录态通道（仅本地栈可选） |
|---|---|---|
| 采集信息：拉旧记录当草稿 | `mcp-firmwares [--name <app名>]`（`my_firmwares`，字段含 name/status/version/descZh/descEn/category/githubUrl/shots，**够当草稿**） | `firmwares` / `firmware --id` |
| 上传新固件 | `mcp-upload`（`upload_firmware`） | `publish` |
| **更新已有固件（铁律②首选）** | **MCP `update_firmware`**（`id` + 要改的字段；保住 id/downloads/flashes） | `publish --id <旧id>`（`PATCH /api/firmwares/:id`） |
| 删旧（降级，先试上面那行） | **`mcp-replace <旧id>`**（`unpublish`→`delete`；因 `delete_firmware` 只收 draft/rejected，必须先下架） | `publish --replace <旧id>` / `delete` |
| 下架 / 撤回 | `mcp-unpublish <id>`（published、reviewing 都行） | `cancel-review`（仅 REVIEWING） |
| 防砖验证（拉刷机清单） | **`mcp-download <id> --check-sha256 <本地sha256>`**（平台 2026-09-24 新增 MCP 工具 `firmware_download`，自带 4 段/sha256 校验） | `POST /api/firmwares/:id/download`（需登录态） |

⚠️ **CLI 命名有坑**：`publish`（含 `--replace` / `--id`）走的是 **REST 登录态**，不是 MCP；真正吃 `sdg_` 令牌的是
**`mcp-upload` / `mcp-update` / `mcp-submit` / `mcp-unpublish` / `mcp-replace` / `mcp-firmwares` / `mcp-download`**。
⑤ 用户选令牌通道就全程用 `mcp-*` 系列。
- ⚠️ **`mcp-firmwares --name` 是精确匹配、大小写敏感**（2026-09-24 ALLINONE 踩过）：库里记录名是 `AllinOne`，
  传 `--name ALLINONE` 会返回 `[]` —— 别据此误判「旧记录不存在/不属于本账号」；先**不带 `--name` 全列一遍**，
  看 `owner:"me"` 里有没有同名记录再下结论。
- ⚠️ **`mcp-replace` 的 id 是位置参数**：`mcp-replace <旧id>`，传 `--id <旧id>` 会报 `unrecognized arguments`。
- ⚠️ **重发前核对 dist 导出与当前 build 一致**：烧录/增量重链会让 `build*/<项目>.bin` 的 `app_elf_sha256` 变化
  （大小/版本/编译时间字段不变）⇒ 旧 `dist/<项目>_app.bin` 的 sha 可能已 ≠ 当前 build（曾出现 dist=平台旧记录
  sha、build=另一 sha 的三方不一致）。上传前重跑 `python3 tools/pack_app.py -b <build目录>` 重导出 dist，
  保证 dist=build=设备 三处同 sha。
✅ **防砖验证不再有缺口**：平台已新增 MCP 工具 **`firmware_download`**（2026-09-24，令牌通道可用），
对应本 CLI `mcp-download <id> --check-sha256 <本地 app.bin 的 sha256>` —— 自动核对「4 段齐全 + app@0x10000
的 sha256 与本地一致 + declared=false」。⚠️ 副作用：与网页刷机一样会累加一次下载/刷机计数（无法避免，
因为它复用了同一条 REST 路由以保证清单一致）。

## 关键事实（与真实后端一致）
- **MCP 端点（远程，非 stdio）**：开发栈 `http://127.0.0.1:8080/api/mcp`；生产 `https://sdgoods.ai/api/mcp`。无状态 Streamable HTTP，`POST` + `Content-Type: application/json`。
- **鉴权**：开发者令牌 `sdg_...`（个人中心 → 开发者令牌 tab 生成，明文仅显一次）。放在 `Authorization: Bearer <sdg_>`。`list_categories` 公开，其余需鉴权。
- **9 个工具**（2026-09-27 逐个 `tools/list` 核对，无 nextCursor）：`list_categories`（取合法 slug）、
  `whoami`、`my_firmwares`、`upload_firmware`、**`update_firmware`**（原地更新已发布固件）、
  `submit_firmware`、`unpublish_firmware`（下架）、`delete_firmware`（删除，仅 draft/rejected）、
  `firmware_download`（拉刷机清单做防砖验证；2026-09-24 新增，CLI 封装为 `mcp-download`）。
- **`upload_firmware` 字段**（开发者走「单应用包」模式）：
  - **`contentBase64`（开发者唯一正确用法）**：应用包 `build_pub/<项目>.bin` / `dist/<项目>_app.bin`，**不带地址**，由服务端按内容判别落 `0x10000` 并自动补引导层。这是给所有开发者的形态。
  - ⛔ **不要传 `parts`（带 `bootloader@0x0` 的多段）** —— 开发者账号会 403 `developer_single_package_only`。`parts` 多段仅官方账号 / 恢复包（`/admin`）可用。
  - 其它可选：`hardware`(sdgoods|cyb1|both，默认 sdgoods)、`name`(≤60，**不含版本号**)、`version`(如 v1.0.5，自动补 v 前缀，**独立字段勿并入 name**)、`descZh`(≤2000)、`descEn`(≤2000)、`category`(必须 list_categories 返回的 slug)、`githubUrl`、`fileName`、`shots`(截图≤4 张，元素为图片 base64 或 `data:image/...;base64,`；服务端直传 shots 桶并关联，**发布即出现在详情页**)、`status`(draft|reviewing)。
  - ⚠️ 截图**不走**「前端 presign REST」（`/uploads/presign` + `PATCH /firmwares/:id` 只吃网站 JWT，15min 过期）；MCP 的 `shots` 由服务端直传 S3，吃 `sdg_` 令牌，是**唯一能用开发者令牌提交截图的通道**。
  - ⚠️ **不询问 tags** —— 该字段已从流程移除，默认不传。

- **`update_firmware` 字段**（2026-09-27 上线，原地更新已发布固件；**重新发布一律走它**）：
  - 🔑 **唯一入参必填 = `id`**，其余**全部可选** ⇒ 语义是 PATCH「没给的字段不被清空」，
    **不要传一堆无关字段**（传了就要清空原值，例如 `shots`）。
  - 可改：`name` / `category` / `hardware`(cyb1|sdgoods|both) / `version`（平台自动补 `v` 前缀，
    可写 `1.0.46` 或 `v1.0.46`）/ `descZh` / `descEn` / `tags` / `githubUrl`，
    以及**内容**（`contentBase64` 单应用包，或 `parts` 整组替换）与**截图** `shots`。
  - ⚠️ **`shots` 是「整组替换」，三种语义别搞混**：
    不传 = 完全不动（**通常就想要这个**）；传一组 = 用这组覆盖；传 `[]` = 清空截图。
  - ⚠️ **审核中（`reviewing`）不可改** —— 平台会拒绝，必须先 `mcp-unpublish <id>` 撤回提审。
  - ⚠️ **已上架（`published`）改内容会立刻对市场与 OTA 生效**（与网页改稿一致）。
    想让新版本重新过审 ⇒ 改完后 `mcp-unpublish` → `mcp-submit`（两步把状态从 published 拉回 reviewing）。
  - ⚠️ **它不改状态** —— 上下架与审核状态一律交给 `submit_firmware` / `unpublish_firmware`。
  - ✅ 更新后 **id 不变**，`downloads` / `flashes` / 审核时间线 / `publishedAt` 全部保留
    （实测只跳一下 `updatedAt` 时间戳，内容零改动则市场无变化）。
- 本地有 `HTTP_PROXY` 指向沙箱代理 → 调本地 MCP 必须用 Python `urllib` 配 `ProxyHandler({})` 直连，或 curl `--noproxy '*'`。
- **改后端后必须重建**：`docker compose up -d --build api`。⚠️ 沙箱会拦 buildx 写 `.docker/buildx/`（`operation not permitted`），需**非沙箱模式**跑，并加 `DOCKER_BUILDKIT=0 COMPOSE_DOCKER_CLI_BUILD=0`。

## 流程

### 0. 预检
- cyb2-dev-site docker 栈在跑（api 127.0.0.1:8080）。先调 `list_categories` 探活并取合法 slug。
- 设备串口：`ls /dev/cu.usbmodem*`（端口会变，MAC 绑定）。

### 1. 编译（见 skill `sdgoods-build-flash`）
```bash
cd <SDGOODS-ESP32S3 repo>
source "$HOME/esp/esp-idf/export.sh"
env -u CODEBUDDY_SAFE_DELETE_SANDBOX -u CODEBUDDY_BROKERED_FS_HOOK_ENABLED -u CODEBUDDY_SAFE_DELETE_ENABLED \
  "$HOME/.espressif/python_env/idf5.5_py3.13_env/bin/python" \
  "$HOME/esp/esp-idf/tools/idf.py" -p /dev/cu.usbmodemXXXX -B build_pub build
```
产物经 `tools/pack_app.py` 导出单应用包（**开发者只交这一份**）：
- `dist/<PROJECT_NAME>_app.bin`（本项目为 `SDGOODS_EBADGE_app.bin`，约 1.7MB；含正确 app 身份、≤2.9MB）

- 改过中文文案 → 先跑 `sdgoods-fonts` 重生成字体子集，否则新字变方框。

⚠️ **改码必重编 · 版本号铁律（用户硬性规定，无例外、不询问跳过）**：
- 只要动了**源码**，就必须**重新编译**出新的 `build_pub` 产物，不能用旧 bin 顶替发布。
- 重新编译 → **版本号必须 +1**（`version.txt` 改高一位）。三仓同源：二进制内 `esp_app_desc.version` 与关于页 `SDGOODS_APP_VERSION` 都来自 `version.txt`（已统一透传、不再差 1），改一处即两者同步。
- 若**检测到设备已连接**（`ls /dev/cu.usbmodem*` 有输出），重编后**必须**确保设备上是最新固件（见 ④ 截图规则里的「最新固件比对」），并在发布前按 ④ 重截**最新**画面。不重刷、不重截就发布 = 平台上挂的是过时固件/旧截图。
- **发布前必核对**：`pack_app.py` 只打包**不重编**，`dist/<项目>_app.json` 里的 `version`/`buildTime` 是**上次编译嵌入的旧值**——必须与 `version.txt` 一致才允许发布（实测踩过：bin=1.0.41 vs version.txt=1.0.42，差点把旧版本号发上去）。
- **build_pub 半截恢复**：后台构建被杀（SIGTERM）在 cmake configure 中途 ⇒ `build_pub` 无 `project_description.json`，后续 `idf.py build` 直接报错。**删 `build_pub/CMakeCache.txt` 强制重新 configure 即可**（对象缓存保留，远快于全清重建）。

### 2. ⭐ 字段确认（全自动准备，仅在「是否更新/修改」时打断一次）
**不要在用户说发布前就弹问。** 用户说「发布」后，**先弹 ⓪ 通道确认**（MCP 推荐 / 手动，见上一节），再按顺序自动跑 ①编译②探测设备③搜集信息④截图，期间只在以下节点用 `AskUserQuestion` 打断：
- ⚠️ **「继续 / 直接发 / 别问了」这类模糊指令 ≠ 跳过确认**（2026-09-23 事故）：只有用户**明确**说「沿用草稿」「不改」「跳过询问」才可跳过信息采集与通道确认；否则 ③ 信息采集与 ⑤ 通道**必须在提交前逐项确认完**。事故经过：AI 弹出采集提问 → 用户中断 → 回一句「继续」→ AI 误读为「不必确认直接提交」，结果未经确认就提审，只能走 `cancel-review` 回草稿重来。**中断提问后的「继续」默认是「继续走流程」，不是「跳过这一步」。**
- **搜集信息时（③）逐条问，不一次性列全**：按「名称 → 简介中文 → 简介英文 → 分类 → GitHub」顺序，**每一条单独**用 `AskUserQuestion` 带该字段的**草稿值**问用户「是否要修改？」：
  - **不修改** → 直接采用草稿，跳到下一条；
  - **要修改** → 用户在问题下方的空白（自由输入 / Other）填写新值，采用后跳下一条。
  - **草稿来源（取决于 ⓪ 的通道与令牌有无）**：
    - **有令牌**（MCP 已存/刚填，或手动但本机已存）→ 已发布过：拉平台旧记录 name/descZh/descEn/category/githubUrl 当草稿；没发布过：AI 生成。
    - **无令牌**（选手动且本机未存，或选 MCP 但用户跳过填令牌）→ **不查平台**，一律由 **AI 按 app 内容生成草稿**。
  - **草稿如何展示（重要 UI 限制）**：`AskUserQuestion` 的选项 `description` 在聊天前端**会被截断**，长文本用户看不到完整内容（但工具回传的 JSON 是完整的，提交/发布不丢数据，所以「提交没问题、只是看不见」）。处理方式：
    - 短字段（名称、分类 slug、GitHub 通常较短）→ 草稿可直接放进 option `description`。
    - **长字段（简介中/英，可能很长）→ 禁止把全文放进 `description`**：在调用 `AskUserQuestion` 的**同一条消息里先以普通文本把该草稿完整打印**（或写入 `release/draft_desc_zh.md` / `draft_desc_en.md` 并在消息里给路径），`AskUserQuestion` 的 `description` 只写一句「完整草稿见上方/文件，是否沿用？」，`label` 用简短的「不改 / 要修改」。
    - 通用判据：某字段草稿**超过约 60 字符**即按「长字段」处理（先正文/文件展示，再短确认）。
  - **版本号不询问**：自动取最新编译产物（`version.txt` / 二进制内 `esp_app_desc.version`，已按铁律① +1），直接填入；截图取 ④ 步结果。两者都不进逐条询问循环。
- **选通道时**（⓪，**第一步**，不是全部就绪后）：问「MCP 发布（推荐）」还是「手动发布」，并按所选分支查/要令牌。

AI 起草依据：`git log --oneline -5` + `main/apps/apps_registry.c`（含飞机/小鸟/贪吃蛇 → 偏 game；综合 launcher → tool）。字段清单：名称(≤60，不含版本号) / 简介中英文(各≤2000) / 版本号(独立字段 `vX.Y.Z`) / 分类(从 slug 选) / GitHub 地址(有则填) / 截图(见 ④)。（**版本号与截图为自动项，不进逐条询问；逐条询问仅覆盖 名称/简介中/简介英/分类/GitHub**）

### 2b. 截图规则（自动：先能力检查，再判定发布状态，决定图从哪来，绝不询问用户）

**🔍 能力检查（在「最新固件比对 / 设备分流」之前做，静态读源码，不烧、不卡用户）**：
- `has_screenshot`（项目代码**是否带截图功能**）：在**整个仓库（含 `components/`）**搜索截图相关符号（`screenshot` / `SHOT` / `sdgoods_screenshot` / 截图调试键 `s` 的注册逻辑）。单应用项目的截图能力往往也在平台组件里，**别只扫 `main.c` / `ui_*.c`**。若工程根本没编译进截图模块 → 判定 `false`。
- `has_cc`（项目代码**是否带控制中心**）：控制中心是**平台层组件**，判定**不能只看 app 自己的源码**——
  - 实现在 `components/sdgoods_launcher/src/sdgoods_cc.c`，且该组件以 `WHOLE_ARCHIVE` 整档链接，是**每个 app 的默认能力**（即使没有任何 app 代码引用它，也会进链接）；
  - 调用入口在平台 `sdgoods_board` 组件的 `sdgoods_app_shell.c`（`sdgoods_app_shell_init()` → `sdgoods_cc_open()`），**app 自己的代码通常不直接出现 `sdgoods_cc` 字面量**。
  - 判定方法（任选其一，**构建产物优先**——能力检查在 ① 重编之后，`build*/<PROJECT>.map` 必然存在）：
    1. **构建产物（权威）**：`grep -q "sdgoods_cc_open" build*/<PROJECT>.map` 命中 ⇒ `has_cc=true`；
    2. **源码（无构建也能用）**：在**整个仓库**搜 `sdgoods_cc_open` / `sdgoods_cc_set_app_ctx` / `sdgoods_cc.c`，或确认 `components/sdgoods_launcher/src/sdgoods_cc.c` 存在且以 `WHOLE_ARCHIVE` 编译 ⇒ `has_cc=true`。
  - ⚠️ **单应用模式（factory@0x10000 直启）下控制中心照样编译进固件、照常可用**（上边下划手势唤出），**不要因「app 源码里没写 `sdgoods_cc`」就误判 false**——ALLINONE、MYAPP 这类由开源工程派生的单应用都带控制中心。
- **效果**：
  - `has_screenshot=false` → **整条走「AI 生成路线」**：不探测、不连设备、不烧固件；图源仍按发布状态：已发布→复用市场旧图、未发布→AI 生成 1 张像素风图（见下方 ① 的无设备分支）。
  - `has_cc=false` → 即使后续有设备、走真机截图，也**跳过「关于」页**（见 ③ 末张规则）。

**⓪ 手动截图 / 自动截图（有设备时必问一次；推荐手动 —— 用户 2026-09-23 新规）**

进入真机截图前（已完成「切单应用模式 + 最新固件比对」之后）用 `AskUserQuestion` 问一次：
⚠️ **2026-09-27 实测踩过**：首次发布 CALC 时（有设备、has_screenshot=true）**漏了这一问**，直接按自动路线截了三张。
无论首次发布还是重新发布，只要「有设备 + has_screenshot=true」，这一问都**必须弹**，不得因为「的信息内容简单/图少」而跳过。

- 选项 1（**放在第一个、标注「推荐」**）：**手动截图** —— 固定 **4 张**（手动模式**不套用下方 ③ 的内容框架**，见下），
  由**用户在真机上自己挑有代表性的画面**翻页，AI 逐张问、用户说好了就截。
  - 优点：**快得多**（不用 AI 猜调试键、不用等合成触摸/动画、不会因按钮命中区偏差截错页 —— 曾有一次按钮间 8px 空隙点不动，就是自动路线的典型坑）。
- 选项 2：**自动截图** —— 完全按现有流程走：AI 用调试键（`--pre <char>` / 合成触摸）自己切页并逐张截（严格按 2b ③ 内容规则 + 调试键序列）。

**手动截图循环（逐张询问，不一次性列全）**：

🔴 **手动模式不套用 ③ 的内容框架**（用户 2026-09-24 新规）：**固定 4 张**，不强制「首页必截 / 二级菜单 2 张 / 关于页必截」那套顺序。
只提示用户「挑 **4 张有代表性**的画面」即可——能体现这个 app 是干什么的、最好玩/最好看的界面（首页、主要玩法、设置、控制中心/关于……用户自己定，
**不要替他规定每张必须是什么**）。用户翻到哪个画面就截哪个，不校验「这一张应该是首页」、也不要求补首页/关于页。

1. 先用正文告诉用户：
   > 本次共需 **4 张**，请挑 **4 个有代表性的画面**（能体现这个 app 是做什么的就行，顺序和内容你定）。
   > 在设备上翻到想截的画面后点「我切好页面了，开始截图」，我就截当前屏幕；不想截这张就点「跳过」。
2. 对第 k 张调一次 `AskUserQuestion`（header 写「第 k 张」）：
   - `label`：**「我切好页面了，开始截图」**（固定文案，用户 2026-09-24 指定，不要写成「切换好了」「好了」等）
     → **立刻截当前画面**：`screenshot_recv.py -t -p <port> -o shots/<app>_k_<name>.jpg`，随后 Read 自检（非空白/无缺字），进入第 k+1 张。
   - `label`：「跳过」→ **不截这张**，直接进入第 k+1 张（不重复追问原因）。
   - 两个选项都要在 `description` 里写清当前进度（如「第 2 张 / 共 4 张，当前画面截下来作为第 2 张」）。
3. 循环到 **4 张**走完。**跳过产生的空缺不补位**（最终可能是 1/2/3 张），按实际张数提交。
4. 截到的图同样要 Read 自检（非空白、无缺字/方框）。
5. 🔴 **全部跳过（0 张）的兜底（用户 2026-09-23 新规）**：不能空图提交 —— **由 AI 按 app 实际内容生成 1 张像素风（pixel-art）封面**再提交（内容与「无设备 + 未发布」那条 AI 生成规则同款：小鸟游戏画像素小鸟+管道、扫描器画像素雷达、飞机大战画像素战机+敌机）。
   - 生成后同样要 Read 自检，并按 1 张图提交（MCP `shots` 传这一张；手动发布就把这张放进 `release/shots/`）。
   - **不要**因为「用户跳过了」就再回头追问是否补截、也不要改走真机重截 —— 直接生成并提交，流程继续往下走（通道早在 ⓪ 定完，⑤ 执行前不再问）。
   - 部分跳过（≥1 张真机图）时**不触发**这条兜底，按已有的真机图提交即可。

⚠️ 前置条件：**手动截图只在「有设备 + has_screenshot=true + 已切单应用模式并确认固件最新」时出现**。
无设备 / 无截图能力 → 直接走 ① 的矩阵或 AI 生成路线，**不要弹这个选择**（没人能翻页、也没法截）。

**① 截图来源（取决于「是否发布过」×「是否连接设备」）**：

| 是否发布过 \ 设备 | 有设备 | 无设备 |
|---|---|---|
| **已发布过** | 重截新图（走下方「真机截图」） | **复用市场上已有的旧截图**（不重新生成、不询问） |
| **未发布过** | 截新图（走下方「真机截图」） | AI 按 app 内容生成 1 张像素风（pixel-art）图 |

- **无设备 + 未发布** → AI 直接生成 1 张像素风图，内容按 app 实际功能画（小鸟游戏画像素小鸟+管道，扫描器画像素雷达）。不为缺图卡住流程、也不询问。
- **无设备 + 已发布** → 直接复用该 app 在平台上已有的 `shots`（市场详情页那些图），不要重画也不要问——用户想换图就走「有设备重截」。

**② 真机截图（仅「有设备」时执行）**：

先做两件事，再按「内容规则」截：
  1. **⚠️ 模式检查（多应用模式必须先切单应用）**：若设备**已装启动器**（多 OTA 槽 / `factory@0x10000` 是启动器），显示被启动器接管、首屏是启动器首页，**直接截只会截到启动器**而非目标 app。截图前必须先把 app **刷成「单应用模式」**——`write_flash` 把刚 `build` 的 app 烧到 `factory@0x10000` 直启（无启动器）。⚠️ **设备数据无需备份**：直接 `write_flash` 覆盖原固件/原 app 即可；发布流程**不还原设备到原状**（下次要用再重刷）。单应用模式只碰 `0x10000` 起的 app 区 + 分区表，`不动 0x0 bootloader`（除非确有必要）。
  2. **最新固件比对**：设备当前 app 是否已是本次最新编译产物（同 `app_id` + 同 `version` / 同 git 哈希）？**否** → 先 `write_flash` 烧最新固件（单应用模式）；**是** → 直接截。等开机动画过完（约 15s，`SHOT` 能力位才注册好），再按内容规则截。

**③ 内容规则（决定截哪些界面、共几张）** —— ⚠️ **只约束「自动截图」路线**；**手动截图不适用本框架**（手动固定 4 张、由用户自选有代表性的画面，见 ⓪）。
- **第 1 张 = 首页/封面，必须截**（无论什么 app）。
- **第 2、3 张 = 二选一，取决于 app 类型**：
  - app **有二级菜单** → 截 **2 个不同二级菜单页面**；
  - app **是游戏类型** → 截 **2 张运行中画面**（如小鸟游戏截 2 张不同关卡/状态）。
- **最后一张（末张）**：
  - app **有控制中心（能力检查 has_cc=true）** → **必须截「控制中心 → 关于」页**；能力检查 false 则跳过（即使在真机路径）；
  - app **无控制中心** → 不截，结束。

⇒ 实际张数：有控制中心（且含二级菜单或游戏）→ **4 张**；有控制中心但纯单页 → **2 张**（首页 + 关于）；无控制中心但含二级菜单/游戏 → **3 张**；无控制中心且纯单页 → **1 张**（仅首页）。

- 用调试键（见 `sdgoods-screenshot`）自动切到目标界面并截屏：`python3 tools/screenshot_recv.py -t -p <port> [--pre <char>] -o shotN.png`。
- 用 Read 打开每张图 AI 自检（布局/字体/缺字），确认非空白后随固件提交。
- ⛔ **禁止**用 `AskUserQuestion` 询问「是否截图 / 是否切到某界面 / 是否在设备旁」——这是旧流程的错误做法，会导致用户被打断、流程变慢。

### 2c. 重新发布（app 之前已发布过）：**更新优先**（2026-09-27 修订），禁止两个同名项目
用户硬性规定：当这个 app **之前已经发布过**（市场上已有同名/同身份记录），重新发布时：

0. **🔄 先判「更新 vs 删旧建新」**（这一步决定后面 1~5 走哪条，**先于下面所有步骤**）：
   - 【要不要更新】看旧记录 `status`（`mcp-firmwares --name <app名>` 或 `GET /firmwares/:id`，
     令牌通道就能取单条）与 `downloads / flashes`：**只要它是 published/reviewing、或 downloads>0**，
     就**必须走更新** —— 删掉它，那些下载/刷机计数、审核时间线、用户刷机记录会**随记录级联删除**、
     **不可恢复**（MCP `delete_firmware` 的语义就是「分区、截图、审核与下载记录会一并级联删除」）。
     实测 2026-09-27：飞机大战 v1.0.44 上架后 downloads=2 / flashes=2，删了就没了。
   - 【能不能更新】**能，令牌通道一条就够**（MCP `update_firmware` 已于 2026-09-27 上线，
     从此更新不再依赖网站登录态）：
     · 🔑 **令牌通道（生产默认，推荐）**：MCP **`update_firmware`**（吃 `sdg_` 令牌，服务端直传对象存储，
       AI 只需把 `app.bin` 读成 base64 塞进 `contentBase64`）。**这条不需要登录态**；
     · 🖥 **登录态通道（仅本地栈）**：REST `PATCH /api/firmwares/:id`
       （前端「编辑固件」页「替换文件」就是它：presign → PUT 直传 → PATCH 带 `fileUrl/fileSha256/sizeBytes`）。
   - 【分支】**默认走令牌通道的 `update_firmware`** → 跳到第 3 步（信息）后更新，结束后从本节第 5 步继续
     （id 不变，防砖校验仍用同一个 id）。
       ⚠️ 旧记录若是 `reviewing` → `update_firmware` 会拒绝（审核中不可改，与网页一致）：
       先 `mcp-unpublish <旧id>` 撤回提审，再更新、再 `mcp-submit`。
       ⚠️ 若旧记录是 draft/rejected 且 downloads=0（没人下载过），可直接删旧建新，不必问。
   - ⚠️ `mcp-update` / `update_firmware` **不改状态** ⇒ 想让新版重新过审，改完显式
     `mcp-unpublish <旧id>` → `mcp-submit <旧id>`（顺序不能反，`submit_firmware` 只收 draft）。

1. **先检测**：**默认用令牌通道** `mcp-firmwares --name <app名>`（`my_firmwares`；生产无登录态，REST `firmwares?scope=mine` 打不通）。
   按名称/app 身份找到已有的那条（published / reviewing / rejected / draft 都算）。
   ⚠️ 只想核对单条（含 downloads/flashes）时用**令牌通道直接 `GET /api/firmwares/:id` 也能通**（2026-09-27 实测
   200，`sdg_` 令牌可用；但列表 `?scope=mine` 仍是 401）。
2. **拿旧信息当草稿**：`mcp-firmwares --name <app名>` 的返回里已含 name/descZh/descEn/category/githubUrl/status/version/shots，
   **直接当草稿**（无需再拉详情，MCP 没有单条详情工具）；把其中的 **version / 固件 bin / 截图** 替换成最新编译产物（版本号按 1. 规则 +1）。
   登录态栈才用 `firmware --id <旧>`。
3. **逐条问用户（不一次性列全）**：按「名称 → 简介中文 → 简介英文 → 分类 → GitHub」顺序，每一条单独用 `AskUserQuestion` 带**旧数据当草稿**问「是否要修改？」——不修改用旧值跳下一条；要修改在空白处填新值跳下一条。**长草稿（尤其简介中/英）按上文「草稿展示规则」先以正文/文件完整展示，不要把全文塞进 option `description`（前端会截断、看不见）**。版本号自动取最新固件版本（不询问），截图用本次新截的图。
4. **★ 执行：更新（改信息）+ 重新提审**（走哪条在 0. 已定；此处只负责执行）
   - 【**更新（推荐，保住下载数据）**】令牌通道三步（2026-09-27 起 MCP 已有 `update_firmware`，
     **不需要登录态**，见本节 0. 已定）：
     ```bash
     # ① 原地更新：换 bin 就带 --file，只改信息就只传信息字段；--shots 不传 = 保留旧图
     python3 tools/sdgoods_publish.py mcp-update <旧id> --api <站>/api \
         --file <新bin> --name ... --version ... --category ... \
         --desc-zh ... --desc-en [--shots <新图…>]      # 省略 --shots 即保留市场现有截图
     # ② 重新提审（update_firmware 不改状态）：published 的先退回 draft
     python3 tools/sdgoods_publish.py mcp-unpublish <旧id>
     python3 tools/sdgoods_publish.py mcp-submit <旧id>
     ```
     ✅ **旧 id 不变、downloads / flashes / 审核时间线 / publishedAt 全部保留**；
     ❌ 副作用：更新期间市场会短暂不可见（下架 → 提审）；`updatedAt` 时间戳会跳一次。
     ⚠️ **`mcp-update` 默认先做一次状态预检**（`GET /api/firmwares/:id`，`sdg_` 令牌可通）：
     读到 `reviewing` 会**直接报错退出**并提示先 `mcp-unpublish`（平台本来也会拒）。
     加 `--no-status-check` 可跳过（由平台拦）。预检失败不阻断流程。
     ⚠️ 只想改信息、不想动固件文件时**别传 `--file`**；只想保留旧截图就**别传 `--shots`**。
     （替代方案：登录态通道 `publish --id <旧id>`，它 body 带 `status`、更新已上架记录会顺带重新提审；
     生产默认没登录态，仅本地栈可用。）
   - 【**删旧建新（降级，只在更新通道不可用时）· 令牌通道**】`mcp-replace <旧id>`（unpublish → delete）→ `mcp-upload ...`
   - 【删旧建新 · 登录态通道】`publish --replace <旧id> ...`（REST 先 DELETE 再 POST）。
   - 【手动通道】更新：AI 把新 `app.bin` + 信息草稿交给用户，让他在「编辑固件」页点「替换文件」上传；
     删旧：提示「市场已有同名旧版（id=xxx），请先在个人中心删除旧记录再提交新 release」，代码不代删。
   - ⛔ **绝不**在保留旧记录的同时再 POST 一条新的同名项目——「不能同时存在 2 个一样的项目」。
   - 旧记录即使是 **已上架 / 审核中** 也能删：令牌通道先 `mcp-unpublish` 回 draft 再删；登录态下 REST `DELETE /api/firmwares/:id` 不限状态、只校验归属。
5. 新记录走正常提审（`status:"reviewing"` / 更新后即 `reviewing`）；随后按「第 5 步 验证」用**同一个 id** 跑 `mcp-download --check-sha256`。

## 🔄 更新优先：为什么别急着「删旧建新」（2026-09-27 用户要求）

- **删记录的代价是数据性的、不可恢复**：MCP `delete_firmware` 的语义就是「分区、截图、审核与下载记录会一并级联删除」，
  `downloads` / `flashes` / 刷机事件（「哪台设备刷过」的唯一线索）全部清零。
  实测：飞机大战 v1.0.44（id `cmuilsjh5009fo2018rfkfnl5`）上架后 downloads=2 / flashes=2，删除即归零。
- **平台早就支持原地更新，但以前只有一条暗道**：REST `PATCH /api/firmwares/:id`，和前端「编辑固件」页
  「替换文件」用的是同一个接口。⚠️ 它只吃**网站登录态**（`sdg_` 令牌打 PATCH → 401 **未登录或登录已失效**），
  而生产默认没有登录态 ⇒ 以前在令牌通道上等于更新不了。
- ✅ **2026-09-27 平台上线了 MCP `update_firmware`**（`tools/list` 从 8 个变 9 个，无 nextCursor）
  ⇒ **令牌通道（生产默认）现在能原地更新了**，这也是本节「更新优先」的真正落地方式：
  **改内容/信息 = MCP `update_firmware`（`mcp-update <id>`，吃 `sdg_` 令牌）
  + 重新提审 = MCP `submit_firmware`（`mcp-submit <id>`）**。
  REST `PATCH`（`publish --id`，需登录态）退为本地栈的备选，不再是唯一解。
- ⚠️ 平台文档的隐含约束（写进 `/api/firmwares/:id` 时也一样）：**审核中不可改**；**已上架改内容会立刻对市场与 OTA 生效**；
  **想重新过审 ⇒ 改完 `mcp-unpublish` 再 `mcp-submit`**。
- 更新后 **id 不变**，所以「第 5 步 验证」的防砖校验仍用旧 id，结论直接可比（同一条记录的刷机清单）。
- ⚠️ `submit_firmware` 只收 **draft** ⇒ 更新一条 published 记录后，先 `mcp-unpublish <id>` 退回 draft 再提审
  （下架不改变 downloads/flashes，只清 `publishedAt`）。

### 3. 上传（MCP `upload_firmware`，**单应用包 contentBase64 + version + shots**）
用 Python 发（绕过代理、处理大 base64 最稳）：
```python
import json, base64, os, urllib.request
TOKEN = os.environ["SDG_TOKEN"]           # 用户给的 sdg_ 令牌，别写死在脚本里
REPO  = "<SDGOODS-ESP32S3 repo>"

def b64(p): return base64.b64encode(open(p, "rb").read()).decode()

# ✅ 只传一个应用包（单文件，不带地址）——平台按内容判别落 0x10000 并自动补引导层
APP_BIN = os.path.join(REPO, "dist", "<PROJECT_NAME>_app.bin")
SHOT_PATHS = [...]                        # 第 2b 步截到的图（最多 4 张）

body = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
  "params": {"name": "upload_firmware", "arguments": {
     "name": "<定稿名称>", "category": "<slug>", "hardware": "sdgoods",
     "version": "v1.0.6",
     "descZh": "<定稿简介>", "descEn": "<EN>",
     "githubUrl": "<可选>",
     "contentBase64": b64(APP_BIN),        # ← 单应用包，不是 parts
     "shots": [b64(p) for p in SHOT_PATHS],
     "status": "reviewing"}}}              # 直接进审核；否则默认 draft
req = urllib.request.Request("http://127.0.0.1:8080/api/mcp",
  data=json.dumps(body).encode(),
  headers={"Content-Type": "application/json", "Authorization": "Bearer " + TOKEN})
resp = json.loads(urllib.request.build_opener(
    urllib.request.ProxyHandler({})).open(req, timeout=180).read())
```
- 从 `resp["result"]` 取固件 `id`；`isError:true` 时打印完整响应并停。
- ⛔ 若收到 403 `developer_single_package_only` / `system_package_staff_only` → 说明误传了 `parts`/bootloader，改回只传 `contentBase64` 单应用包。
- **手动发布不走这段代码**：把 `APP_BIN` + `SHOT_PATHS` + 信息草稿写进 `release/` 文件夹交给用户即可。

### 4. 提交审核
仅当第 3 步未带 `status:"reviewing"` 时：调 `submit_firmware`，arguments `{"id":"<id>"}`。

### 5. 验证（必须核对落点，防变砖）
- `mcp-firmwares --name <app名>`（令牌通道即可，无需登录态）确认状态 `reviewing`、`version`、`shots` 数。
- **模拟市场刷机第一步（令牌通道直接用 CLI，无需登录态）**：
  `python3 tools/sdgoods_publish.py mcp-download <id> --check-sha256 $(shasum -a 256 dist/<项目>_app.bin | cut -d' ' -f1)`
  → 打印 `✅ 防砖校验通过：4 段齐全、app@0x10000 的 sha256 与本地一致、declared=False` 才算过。
  等价的 REST 版本（**需网站 JWT**，仅本地栈可用）：
  `POST /api/firmwares/<id>/download` → 应返回 **4 段带 `address`/`sha256`/`url`**：
  `bootloader@0x0` / `partition-table@0x8000` / `app@0x10000` / `otadata@0x310000`，
  且 `declared:false`（落点由平台算）。
- 本地核对 sha256：`shasum -a 256 dist/<项目>_app.bin` 与平台回执里 app 段的 `sha256` **逐位比对**（应一致，证明平台没动你的应用包、只是拼了引导层）。
- 回执给用户：id、状态、版本、分区表（4 段地址+文件名+大小）、截图数。

## 官方恢复包（市场首页「设备救援」入口挂的那份固件）

用户设备被刷坏 / 回不到启动器时，市场首页那条横幅会调 `GET /api/firmwares/recovery`
取一份「一键恢复」用的整机包。它不是普通固件：**必须是「已上架且公开」的多段整机包**
（服务端三条硬门槛：`assertRecoveryEligible`、`recovery_needs_parts`、同板型互斥清零），
所以走 `POST /admin/firmwares`（`recovery:true`）或 `PATCH /admin/firmwares/:id/recovery`，
**不吃开发者令牌、MCP 那套也建不了**。封装好了：

```bash
L=<SDGOODS_LAUNCHER>/build_launcher          # 与平台 SystemAsset 当前一致的那份构建
python3 scripts/publish_recovery_pkg.py \
  --firmware-id <固件id> \
  --name "谷仓电子徽章 · 官方恢复包" --version 2.0.41 \
  --desc-zh "安装回出厂固件，装过的应用与数据会被清空。" \
  --desc-en "Installs the factory firmware — installed apps and data are erased." \
  --part 0x0:$L/bootloader/bootloader.bin \
  --part 0x8000:$L/partition_table/partition-table.bin \
  --part 0x10000:$L/SDGOODS_LAUNCHER.bin \
  --part 0x310000:/path/otadata_8192B_全0xFF.bin \
  --set-recovery
```
脚本自己完成：后台邮箱验证码登录（**运维账号邮箱**，凭据由平台运维提供）→ 四段 presign+PUT → 换 parts →
置恢复包 → 回读 `GET /firmwares/recovery` 与 `POST /firmwares/:id/download` 校验
（`declared` 必须 true、四段地址必须与上传一致、必须过得了整片擦除闸门）。

**四段缺一不可**，尤其别省 `partition-table@0x8000`：
整片擦除的闸门是「覆盖 `0x0` **且** 覆盖 `0x8000`」（`js/esp-flasher.js::coversBootloader`）。
只给 bootloader 不给分区表 ⇒ 判「不带引导层」⇒ 跳过整片擦除 ⇒ 变成「说得狠、其实没清」的假恢复
（用户要的是「清空已装 app 与数据」，没擦就没清）。真实 bootloader 只有 ~20KB，
**不是** ≥32KB，别被旧判据误导。

配套事实（2026-09-21 落地）：内容 = 平台「默认文件」那四份（`SystemAsset` 表，
后台「内容 / 默认文件」页），与其它固件刷机时平台自己拼的引导层同源；
`otadata` 是 8192B 全 `0xFF`（刷完 bootloader 走 factory = 启动器，4 个空槽）。
真机验收：用浏览器 Web Serial 走一次市场「一键恢复」流程，
确认步骤 3 是「清空设备数据 32.0 MB」而不是「已跳过」，且刷完启动器起来、4 槽全空。

## 固件下架 / 删除（MCP 已支持，不必再走网站 JWT）

| 想做 | 调 | arguments |
|---|---|---|
| 从市场下架（撤下已上架 / 撤回提审） | `unpublish_firmware` | `{"id":"<id>","reason":"可选原因"}` |
| 删除（不可恢复） | `delete_firmware` | `{"id":"<id>"}` |

- **下架语义**：`status` 一律回 `draft`，已上架的清空 `publishedAt`，并写一条 `UNPUBLISH` 审核时间线。
  市场可见性 = `status=PUBLISHED && visible=true`，所以回 `draft` 即从市场消失。
  （平台没有独立 OFFLINE 状态；后台「强制改状态」也把下架表示为 DRAFT，保持同一套语义。）
- **作者不能自助重新上架** —— 重新上线必须 `submit_firmware` 提审、审核员通过。
- **删除护栏（区分通道）**：MCP `delete_firmware` historically 只允许删 `draft` / `rejected`，已上架/审核中会报错；但**本 CLI 的 `delete` / `publish --replace` 走 REST `DELETE /api/firmwares/:id`，后端不限状态、只校验归属**——已上架、审核中都能删（用户 2026-09-23 明确：「上架的应用，用户可以删除的」）。所以重新发布替换旧项目时直接 `delete`/`--replace` 即可，不必先下架。
- 只对**自己的**固件有效（管理员除外）。查 id 用 `my_firmwares`。
- ⚠️ 下架/删除是**有副作用的线上操作** —— 先跟用户确认目标 id，别自己挑一条顺手删。

## 审核中取消（作者主动撤回 → 草稿，再改再发）
用户在「个人中心 / 我的固件」里，对**审核中**的固件点「取消审核」即可：
- **后端**：`POST /api/firmwares/:id/cancel-review`（`Authorization: Bearer <登录令牌>`；仅本人或管理员；仅 `REVIEWING` 可撤，其余状态返回 400 `not_reviewing`）。
- **效果**：`REVIEWING → DRAFT`，并写一条 `CANCEL` 审核时间线（ReviewAction 枚举已加 `CANCEL`，含迁移）。
- **之后**：固件回到草稿，个人中心出现「编辑」入口，作者改稿后点「提交审核」即重新走审核。
- 这是平台新能力（2026-09-23 上线），取代了过去「审核中锁死、只能等判完」的体验。
- 本 CLI 暂未封装该命令；命令行调试可 `curl -X POST .../cancel-review -H "Authorization: Bearer <accessToken>"`，accessToken 由 `/api/auth/refresh`（用缓存 refresh_token）换取。

## 事故处置：本地刷写把 app.bin 错烧到 0x0 怎么办（平台发布不会砖）
1. **先救设备**：`idf.py -p <PORT> -B build_xxx flash`（会写 bootloader@0x0 + 分区表@0x8000 + app@0x10000），然后用 `sdgoods-screenshot` 截屏确认已能启动。
2. **下架错固件**（若已误推到平台）：MCP `unpublish_firmware` `{"id":"<id>"}`（吃 `sdg_` 令牌，一步到位）；要彻底清掉则下架后再 `delete_firmware`。
3. **重新按单应用包发布**一份正确的（见第 3 步，只传 `contentBase64` 的 app.bin）。

## ⚠️ 安全
- 开发者令牌 `sdg_...` 仅用于 `Authorization` 头；不写入仓库、不回显全文、不在聊天里复述。
- 不碰开放平台源码/密钥；本 skill 只调其公开 MCP 端点。
- 孤儿草稿清理：现已可用 MCP `delete_firmware`（草稿可直接删），不必再靠 postgres SQL 或 Web UI 手删。

## 🔴 CLI 参数与产物核对（2026-09-26 实测踩坑，实操必看）

1. **`--api` 是「子命令」的选项，不是全局选项**。
   `sdgoods_publish.py --api https://sdgoods.ai/api mcp-upload ...`（写法 1）会直接报
   `error: argument cmd: invalid choice: 'https://sdgoods.ai/api'` —— 看着像 URL 不合法，其实是参数放错了位置：
   ```bash
   python3 tools/sdgoods_publish.py mcp-upload --api https://sdgoods.ai/api --file ... 
   ```
   （`mcp-firmwares` / `mcp-download` / `mcp-whoami` 同理）。`SDGOODS_API_BASE` 环境变量是等价写法。
   ⚠️ 这个报错**一眼看去不像参数问题**，很容易去改 URL 而白折腾一轮。
2. **`mcp-firmwares --name <名>` 是精确匹配**：查不到 ≠ 没发布过。先不带 `--name` 全列一遍看 `owner:"me"`。
3. 🔴 **上传前必须重跑 `pack_app.py` 重导出 dist**（skill 已提示，但这次真踩了）：
   2046 工程里 `dist/2046_app.bin` 的 sha 是 `ee50521b…`，而 `build_2046/2046.bin` 已因增量重链变成
   `194d0c78…` —— 大小/版本/编译时间字段全都一样，只有 `app_elf_sha256` 变了，肉眼看不出来。
   直接上传 = 平台上挂的其实是**另一份字节**。**每次上传前**跑一遍
   `pack_app.py -b <build目录>`，然后 `shasum -a 256` 三方比对 `dist == build == 设备`；
   对不上就重导出。旧 dist 先 `cp` 备份一份再覆盖，别丢。
4. 中文长简介别拼进 shell 命令链的 `$( )`（bash 3.2 会把花括号拆词），写成 `readonly DESC_ZH='...'` 的脚本文件再 `bash` 跑最稳。
5. 🔴 **`mcp-update` 内部会再跑一次 `_check_app_package()`，它不认 `--allow-template-name`**（2026-09-27 实测）：
   「谷仓开源DEMO」这类官方豁免身份 `SDGOODS_EBADGE` 的文件，先用 `pack_app.py -b <build> --allow-template-name`
   成功导出后，传给 `mcp-update --file ...` 仍会被模板名校验拦下 ⇒ 加 **`--no-verify`** 即可
   （上游 pack_app.py 已经过一遍校验，`--no-verify` 在这里只跳过第 ⑥ 项模板名，六项其余检查与平台侧校验都不受影响）。
   不加 `2>/dev/null` 直通 `tail` 时会**输出为空、退出码 0**，看着像成功其实没推上去 —— 重定向到文件再 `tail` 看。
6. 🔴 **`mcp-upload` 没有 `--status` 参数**（2026-09-27 实测）：它的用法里根本没有这个开关，
   传了会直接 `error: unrecognized arguments: --status reviewing`（**看着像参数名写错**，其实是这开关不存在）。
   `mcp-upload` **默认就提交审核**（回执 `status:"reviewing"`），要草稿态请走 `mcp-update` / REST。
7. 🔴 **`mcp-upload` 成功后**「取 id」只看这行日志：
   `提交成功（令牌通道）！固件 id=<id>，状态：REVIEWING（提审中）。`
   别去读回执 JSON 的尾巴（里面还有 shots 的目录 id 等别的 id，容易看串）。
   随后**必须回查列表确认这条记录真的存在**：`mcp-firmwares` 列表里应出现同名记录；
   若 `GET /api/firmwares/:id` 回 404 `firmware_not_found` ⇒ 记录没落库，别拿它去做防砖校验。
   ⚠️ **2026-09-27 实测**：CALC 那次回执日志里的 id `cmuhm41nt…` 直接 404，而实际落库的是**另一个 id**
   `cmuja2b4m…`（`mcp-firmwares` 列表里新出现的那条）。判据只有一条 —— **一切以 `mcp-firmwares` 列表回查结果为准**，
   日志行与列表对不上时**信任列表、别信日志**。
8. 🔴 **「编译没真跑」的假成功**：`... build 2>&1 | tail -25` 的退出码是 `tail` 的。解包版 IDF 用
   `source export.sh` 会静默失效、报 `"cmake" must be available on the PATH`，日志末尾照样有
   `Executing action: all (aliases: build)`，看起来像编译完了。**判据只有两条**：日志里出现
   `Project build complete.`，且目标 `.bin` 的 mtime/size 变了。构建命令见 `sdgoods-build-flash`。

## 失败排查
- **上传报 403 `developer_single_package_only` / `system_package_staff_only`**：误传了 `parts` 或多段（带 bootloader/分区表）。开发者只能传单个 `app.bin` 应用包（`contentBase64`），引导层由平台自动补。改回单文件上传即可。
- **设备刷完无法启动（本地 `write_flash` 事故）**：几乎一定是烧录地址错 —— 直接 `idf.py flash` 救回（它会按分区表写 bootloader@0x0 + 分区表@0x8000 + app@0x10000）。平台发布单应用包不会砖。
- `FST_ERR_CTP_BODY_TOO_LARGE`：bodyLimit 不够 → 确认 `server/src/index.ts` 已加 `bodyLimit:64MB` 且 api 容器已重建。
- docker build 报 buildx `operation not permitted`：用**非沙箱模式** + `DOCKER_BUILDKIT=0 COMPOSE_DOCKER_CLI_BUILD=0`。
- 截图没进 listing：确认后端支持 `shots`（`mcp.ts` 已改且 api 已重建）。
- 分类 400：slug 不在 list_categories 列表。
- 下架/删除报 `-32601 未知工具：unpublish_firmware`：api 容器还是旧代码 → 重建
  （`DOCKER_BUILDKIT=0 COMPOSE_DOCKER_CLI_BUILD=0 docker compose up -d --build api`）后用 `tools/list` 确认 9 个工具都在（含 `update_firmware`）。
- 代理拦本地：用 `ProxyHandler({})` 或 `--noproxy '*'`。
- 令牌 -32001：令牌被吊销/过期 → 重新生成。
