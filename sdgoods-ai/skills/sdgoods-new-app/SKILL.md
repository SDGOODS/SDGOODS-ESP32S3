---
name: sdgoods-new-app
description: 在谷仓次元屏（SDGOODS-ESP32S3 开源工程）上「从零创建一个新的应用工程」：一键封装 tools/new_app_project.py，复制开源工程 → 改名（不加 SDGOODS_ 前缀）→ 裁剪成单应用直启固件（删启动台/演示 app、生成唯一起始 app ui_<app>.c/.h、重写 apps_registry.c 与 CMakeLists.txt）→ 写 app_meta.json → 可选 --run 自动编译+烧录（不备份设备）+ 截主页。产物一开机直入你的 app，是「新用户读代码上手」的标准入口。**创建时第一步必问「中英文应用名」**（中文名=市场展示名，只进 app_meta.json；英文名=展示名+工程名+设备 app_id，必须 ASCII），再问内容/需求（带建议、可跳过→最小应用 Hello<英文名>!）。**中文名不能直接当工程名**：只给中文名会被脚本拒绝（退出码 1）并建议英文名，必须写成 `--name-zh <中文名> --name-en <英文名>`。Use when asked to 生成应用 / 新建应用 / 开发一个应用 / 创建新项目 / generate a new app / scaffold a standalone app / derive from SDGOODS-ESP32S3.
agent_created: true
---

# 从开源工程派生一个新的应用工程（单应用直启）

## 何时使用
- 用户说「生成一个应用 / 开发一个应用 / 新建一个项目 / 从零做一个自己的 app」。
- 目标产物是一台 **「单应用机」**—— 一开机就直接进你的 app，没有主页 / 演示 / 启动台。

> 📌 关键前提：**种子工程就是本开源仓库 `SDGOODS-ESP32S3`**。
> `new_app_project.py` 是从这个开源工程派生、再裁成**单应用直启**结构
> （开机直接进你的 app，没有主页/演示/启动台）。**不要**去找某个"现成的单应用工程"来拷贝 ——
> 它不在开源范围内。

## 唯一入口命令（从本仓库根目录）

```bash
# 派生 MYAPP（不加 SDGOODS_ 前缀）→ 生成 ../MYAPP/，单应用直启
python3 tools/new_app_project.py myapp

# 中文名 + 英文名（中文名只进 app_meta.json 供市场展示；英文名 = 工程名 = app_id）
python3 tools/new_app_project.py --name-zh 贪吃蛇 --name-en SNAKE

# ⚠️ 只给中文名会被脚本拒绝（退出码 1）—— 见下面「交互规则」
#     python3 tools/new_app_project.py 贪吃蛇     ← ✗ 报错，并建议英文名 SNAKE

# 只预览将要做的改动，不落盘
# ⚠️ dry-run 局限：文件未真正复制，末尾会提示「! 找不到 main/CMakeLists.txt（dry-run 未复制文件），跳过 SRCS 改写
#    （锚点已在种子预检里确认）」—— 这是预览期的正常提示，别据此判脚本坏了。
#    真正的「锚点对不对」由 precheck_seed() 在复制前就验过（不通过会直接中止）。
python3 tools/new_app_project.py myapp --dry-run

# 生成后若检测到设备，自动编译+烧录（不备份设备固件）+ 截主页
python3 tools/new_app_project.py myapp --run
```

- 默认种子 = 脚本所在仓根 = 本开源工程 `SDGOODS-ESP32S3`。
- `--seed-dir <dir>` 指定其它种子（一般不用）；`--dest-dir <dir>` 指定输出父目录。

### 主页默认文案规则（最小应用）

| 用户怎么说的 | 主页显示 | 工程名（= app_id） |
|---|---|---|
| 「生成/创建一个应用」（**没给名、没给需求**） | `Hello MYAPP!`（居中） | `MYAPP` |
| 「生成一个名为 ABC 的应用」 | `Hello ABC!`（居中） | `ABC` |
| 「做一个 XXX 功能的应用」（**给了具体需求**） | 按需求实现（脚本只生成最小骨架，AI 据需求改写 `ui_<name>.c`） | 按名或默认 |

- 名称一律指**英文名**（= 工程名 = 设备端 app_id，ASCII）；中文名只进市场展示，不进设备 UI。
- 问候语填的是**清洗后的英文名**，不是输入原文：`my app` → 屏上 `Hello MYAPP!`（旧版是 `Hello my app!`）。
- 问候语是纯 ASCII（无需重跑字体子集）；若按需求改成中文文案，记得重跑 `tools/gen_fonts.py`。

## 交互规则（信息缺失时主动询问）

创建应用前，先解析用户的话里有没有「应用名（中英文）」和「应用内容/需求」。**缺什么就问什么**（用 AskUserQuestion，一次一个问题、带建议选项）；都齐了再跑脚本。

1. **【第一步·必问】应用名称（中英文各一）** → 问「这个应用的**中文名**、**英文名**分别叫什么？」，提问时必须说明两者用途：
   - **中文名 = 展示名**：只用于市场/开放平台上架展示（可纯中文，如「飞机大战」），**不进设备 UI**；
   - **英文名 = 展示名 + 工程名 + app_id**：除作市场英文名外，还直接用作固件工程名（根 `CMakeLists.txt` 的 `project(<英文名>)`）和设备端应用身份 `app_id`（`esp_app_desc.project_name`，launcher 首页图标下方显示的就是它）。
   - 英文名硬约束（提问时同步告知）：
     - 必须 **ASCII**（launcher 首页只显示 ASCII app_id，中文名会渲染成方框）；
     - **全局唯一**：决定设备 `appdata/<app_id>/` 目录名，撞名 = 数据互串 + 卸载互删；
     - 不能用模板默认名（`pack_app.py` 会拒）；约定**不加 `SDGOODS_` 前缀**；
     - 空格 / 连字符等非法字符会被自动清洗成 `project()` 合法标识符（如 `my app` → `MYAPP`）。
   - 建议选项：`MyApp` / `Hello` / 让用户用「其他」一起填中英文名。
   - **只给了中文名、没给英文名** → 按中文含义建议一个 ASCII 英文名（如「贪吃蛇」→ `SNAKE`），AskUserQuestion 让用户确认或改。
   - **只给了英文名** → 视为已确认；中文名不单独打断，留到发布时（`sdgoods-publish` 的「名称」字段会逐条问）再补。
   - 🔴 **脚本不接受「中文名直接当工程名」**：`python3 tools/new_app_project.py 贪吃蛇` 会
     **报错退出（退出码 1）**并打印建议英文名（`贪吃蛇`→`SNAKE`、`飞机大战2`→`PLANE_WAR2`）。
     这是**有意为之**：旧版会把「贪吃蛇」静默清成工程名 `APP`、把「飞机大战2」清成 `2`，
     产出坏工程却退出码 0。所以必须用**两个显式参数**把中英文分开传：

     ```bash
     python3 tools/new_app_project.py --name-zh 贪吃蛇 --name-en SNAKE
     # 也可以：python3 tools/new_app_project.py SNAKE --name-zh 贪吃蛇
     ```

     - `--name-zh` → 落进 `<新工程>/app_meta.json` 的 `name_zh`（**只用于市场展示，不进设备 UI**）；
     - `--name-en` → 工程名（根 `CMakeLists.txt` 的 `project()`）+ 设备端 app_id，必须 ASCII。
     - 顺带：`app_meta.json` 里还有 `name_en` / `project` / `app_id` / `version`，
       `tools/pack_app.py` 会读它、把中英文名带进 `dist/*_app.json`，发布时**不用再问一遍名字**。
2. **缺应用内容 / 需求** → 问「想创建一个怎样的应用？我可以给些建议」
   - 建议选项（每个选项一句话说明，让用户直接选）：
     - 计时器 / 倒计时
     - 图片浏览器 / 相册
     - 小游戏（贪吃蛇 / 打地鼠）
     - 时钟 / 天气显示
     - 待办清单
     - 其他 / 我来描述
     - **跳过（生成最小应用）** ← 选它就走最小应用
3. **用户选「跳过」或没填内容** → 生成最小应用：主页居中只显示 `Hello <英文名>!`（纯 ASCII，无需重跑字体子集）。即跑
   `python3 tools/new_app_project.py --name-zh <中文名> --name-en <英文名>`（没有中文名就只给英文名）。
4. **用户给了具体需求** → 先跑上面的命令生成最小骨架，再据需求改写 `main/apps/ui_<name>.c`（界面 / 逻辑 / 手势）；新增中文文案必须重跑 `tools/gen_fonts.py`，否则屏上出方框。

> 一句话：**第一步先问齐中英文应用名**（中文=展示名；英文=展示名+工程名+app_id）；内容能问就问（带建议）；问不到或用户跳过内容 → 最小应用 `Hello <英文名>!`；给了需求 → 按需求实现。

## 它到底做了什么（机械改名 + 裁剪，全自动）

1. **复制**本仓库（跳过 `.git` / `dist` / `__pycache__` / `build*`）。
   ⚠️ `managed_components`（LVGL 等）**不跳过**：本机已装过就直接带过去，省掉首次构建重新拉 ~98MB；
   第三方刚 clone 时本来就没有它，首次构建会自己下载。
2. **改名**：根 `CMakeLists.txt` 的 `project()` → `<英文名>`（**不加 `SDGOODS_` 前缀**，沿用 HELLO 约定 `project(HELLO_3)`）；`version.txt` → `1.0.0`；写 `app_meta.json`（中英文名 + 版本，供发布链读）。
3. **裁成单应用直启**：
   - 删 `ui_home / ui_scan_page / ui_rec_page / ui_other_page / ui_flappy` 五个启动台/演示 app；
   - 从 `app_template` 派生**唯一**起始 app `main/apps/ui_<name>.c/.h`（函数 `ui_<name>_start` / `_poll`），主页居中显示 `Hello <英文名>!`（最小应用，无计数/按钮）；
   - 重写 `apps_registry.c`：`home_create_show / home_show / apps_show` 全部指向 `ui_<name>_start`（开机直入、退出也回本 app）；
   - 裁剪 `main/CMakeLists.txt` 的 `SRCS`，只留 `app_template.c + ui_<name>.c`。
4. 产物 = 一台「单应用机」：**一开机就进你的 app**，没有主页 / 演示 / 启动台。

> 🔴 **改裁剪规则后必须同步锚点**：`SRCS` 的裁剪靠 `main/CMakeLists.txt` 里
> 「`# --- 导航页 / 工具页 ---`」…「`"apps/ui_flappy.c"`」这段锚定（正则常量 `CMAKE_SRCS_RE`）。
> 脚本现在会**先预检锚点、再动任何文件**：锚点对不上就退出码 1 中止（旧版只打一行日志、
> 照样删掉 5 个 `ui_*.c`，交付一个 SRCS 引用已删文件、**编译不过**的工程）。

## ⚠️ 圆形屏约束（最重要的一条，已写进生成模板）

设备是 **360×360 的「圆」屏**，四角是被圆边切掉的盲区。所有文字 / 图片 / 按钮（含 border / 阴影）都必须落在安全矩形内：

- `x, y ∈ [SDG_UI_SAFE_X, SDG_UI_SAFE_X + SDG_UI_SAFE_W)` —— 即 `x, y ∈ [60, 300)`、宽高不超 240；
- 用 `sdgoods_ui_in_safe_area(x, y)` 程序化判断越界；控件尺寸要把 border / 阴影算进去，否则圆边会把它们切掉；
- 完整说明见 `components/sdgoods_board/include/sdgoods_ui.h`。生成的起始 app 顶部已带这条约束的醒目注释。

## 下一步：写你自己的应用

- 编辑 **`main/apps/ui_<name>.c`**（生成的骨架已接好：建屏 → 画 UI → 接 `sdgoods_app_shell` 标准框架 → 应用级手势 → 每帧 `poll`）。
- **游戏需要四向滑动**：平台高层手势只给「左滑返回 + 点按」（上滑被 app_shell 占作回主页）——请在 `poll()` 里用底层轮询触摸 API 自判方向：`sdgoods_touch_get_state()` + `sdgoods_touch_get_point()`，取按下→松开位移的主轴、超过阈值（≈24px）才算滑动（API 见 `components/sdgoods_board/include/sdgoods_input.h`）。
- 双语文案写 `SDG_T("中文", "English")`；**新增中文文案 → 必须重跑 `tools/gen_fonts.py`**，否则屏上出方框（tofu）。
- 编译 / 烧录 / 截屏见 skill `sdgoods-build-flash`；字体见 skill `sdgoods-fonts`；发布见 skill `sdgoods-publish`。

## `--run` 行为（生成后真机一键）

- 检测到设备（`/dev/cu.usbmodem*` 等）→ **直接编译 + 烧录，不备份设备固件**。
- 烧录后检测代码是否**保留截图功能**（grep `screenshot` / `SHOT` / `sdgoods_screenshot`）：保留 → 自动截主页存 `screenshot_home.png`（工程根）给你看；已移除 → 跳过。
- 无设备 → 仅编译产出 `.bin` 不烧录。

## 与 `new_standalone_project.py` 的区别（别混）

| | `new_app_project.py`（本 skill，**新用户首选**） | `new_standalone_project.py` |
|---|---|---|
| 意图 | **从零新建一个 app**，干净的单应用直启 | 把**整个仓库**派生成「你自己品牌的独立产品」 |
| 起始 app | 从 `app_template` 派生全新骨架 `ui_<name>.c` | 保留某个现有 demo（默认 `flappy`）当基底 |
| 改名 | `project()` → `<NAME>`（无 `SDGOODS_` 前缀） | 全局替换 `SDGOODS_EBADGE` 字面量 + git init |
| 圆屏约束 | 模板已写死 | 沿用原 demo 既有代码 |
| 自动烧录/截屏 | `--run` 一步到位 | 无，需手跑 `tools/build.sh` / `flash_local.sh` |
| 适用 | 我想做个自己的 app 上架 | 我要以某 demo 为基底、用自己产品名发独立整机 |

`new_standalone_project.py` 的用法与手动 Step 详见 [`docs/STANDALONE_PROJECT.md`](../../docs/STANDALONE_PROJECT.md)。

## 硬约束（来自 AGENTS.md）

- 应用层只改 `main/`；平台层 `components/sdgoods_board`、`components/sdgoods_launcher` 是契约，**改了就不再是标准固件、失去上架兼容性**。
- **不要删 `main/apps/app_template.c`**（脚手架，新工程始终编译以便参考）。
- 改了中文文案 → 必须重跑 `tools/gen_fonts.py`。
- 跨任务碰 LVGL 只走无锁 SPSC 环形队列 + 每帧 poll，禁 `lv_async_call()`。
- 音量 / 亮度跨 app 只过 NVS，落盘存「用户意图值」不是瞬时值（否则开机黑屏）。
