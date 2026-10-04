---
name: sdgoods-new-app
description: 在谷仓次元屏（SDGOODS-ESP32S3 开源工程）上「从零创建一个新的应用工程」：一键封装 tools/new_app_project.py，复制开源工程 → 改名（不加 SDGOODS_ 前缀）→ **默认裁成「最小体积固件」**（删蓝牙 BLE、删 Wi-Fi、删中文字库、界面固定英文；**保留控制中心**）→ 裁成单应用直启（删启动台/演示 app、生成唯一起始 app ui_<app>.c/.h、重写 apps_registry.c 与 CMakeLists.txt）→ 写 app_meta.json → 可选 --run 自动编译+烧录（不备份设备）+ 截主页。产物一开机直入你的 app，是「新用户读代码上手」的标准入口。**创建时先走「命名问答」**：没给名字→先问「项目名（必须英文）」再问「中文名」；只给中文名→问「项目名（必须英文）」；只给英文名→问「中文名」；**全部跳过→直接生成 HelloApp 工程**（工程名 HELLOAPP）。中文名=市场展示名（只进 app_meta.json，不进设备 UI）；英文名=工程名+设备 app_id，必须 ASCII。**中文名不能直接当工程名**：只给中文名会被脚本拒绝（退出码 1）并建议英文名，必须写成 `--name-zh <中文名> --name-en <英文名>`。要完整能力（Wi-Fi/BLE 扫描 + 中文界面）加 `--no-trim`。Use when asked to 生成应用 / 新建应用 / 开发一个应用 / 创建新项目 / generate a new app / scaffold a standalone app / derive from SDGOODS-ESP32S3.
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
# 派生 ABC（不加 SDGOODS_ 前缀）→ 生成 ../ABC/，单应用直启 + 最小体积
python3 tools/new_app_project.py ABC

# 中文名 + 英文名（中文名只进 app_meta.json 供市场展示；英文名 = 工程名 = app_id）
python3 tools/new_app_project.py --name-zh 贪吃蛇 --name-en SNAKE

# ⚠️ 只给中文名会被脚本拒绝（退出码 1）—— 见下面「命名问答」
#     python3 tools/new_app_project.py 贪吃蛇     ← ✗ 报错，并建议英文名 SNAKE

# 什么都不给 → 直接生成 HelloApp 工程（工程名 HELLOAPP，主页 Hello HELLOAPP!）
python3 tools/new_app_project.py

# ⭐ 要完整能力（Wi-Fi / BLE 扫描 + 中文界面）→ --no-trim（体积大约 +1MB）
python3 tools/new_app_project.py ABC --no-trim

# 只预览将要做的改动，不落盘
# ⚠️ dry-run 局限：文件未真正复制，末尾会提示「! 找不到 main/CMakeLists.txt（dry-run 未复制文件），跳过 SRCS 改写
#    （锚点已在种子预检里确认）」—— 这是预览期的正常提示，别据此判脚本坏了。
#    真正的「锚点对不对」由 precheck_seed() 在复制前就验过（不通过会直接中止）。
python3 tools/new_app_project.py ABC --dry-run

# 生成后若检测到设备，自动编译+烧录（不备份设备固件）+ 截主页
python3 tools/new_app_project.py ABC --run
```

- 默认种子 = 脚本所在仓根 = 本开源工程 `SDGOODS-ESP32S3`。
- `--seed-dir <dir>` 指定其它种子（一般不用）；`--dest-dir <dir>` 指定输出父目录。

## ⭐ 默认产物 = 最小体积固件（本 skill 的默认行为）

派生出来的工程**默认就是最小体积**，不需要用户额外要求：

| 默认做的事 | 怎么做 | 为什么 |
|---|---|---|
| **删蓝牙（BLE）** | 删 `sdgoods_board/{src,include}/sdgoods_ble.*`、两处 CMakeLists 的 `SRCS`/`REQUIRES`、sdkconfig 里 `CONFIG_BT_*` | Bluedroid 协议栈是体积大头 |
| **删 Wi-Fi** | 删 `sdgoods_board/{src,include}/sdgoods_wifi.*` + `REQUIRES` 的 `esp_wifi`/`esp_netif`/`bt`、sdkconfig 里 `CONFIG_ESP_WIFI_*`/`CONFIG_ESP_COEX_*`/`CONFIG_ESP_PHY_*` | Wi-Fi 协议栈 + PHY + net80211 同样是大头，还白占 ~16KB 内部 RAM |
| **删中文字库** | 删 `fonts/cn_font_14.c` / `cn_font_16.c`（源码合计 ≈1.74MB）；图标字体的 `.fallback` 改 `NULL`；`sdgoods_app_shell.c` 3 处 `cn_font_*` 改用图标字体 | 中文字库是固件里最大的单个资源 |
| **界面固定英文** | `sdgoods_i18n.c` 不读 NVS 的 `lang` 键、`sdg_i18n_set/toggle` 变空实现 | 没有中文字库，中文会渲染成方框 |
| **保留控制中心** ✅ | `components/sdgoods_launcher/src/sdgoods_cc.c` **完全不动** | 用户明确要求；它只用图标字体 `si_yuan_black_icon_14/16`，与中文字库零耦合 |

- **想要完整能力**（Wi-Fi/BLE 扫描 + 中文界面）：加 `--no-trim`，得到与种子工程同等能力的工程（体积大约 +1MB）。
- 🔴 **`--no-trim` 是逃生开关**，只在用户明确说「我要 Wi-Fi / 蓝牙 / 中文界面」时才用；默认别加。
- 图标字体 `si_yuan_black_icon_14/16` 由 `--range 0x20-0x7E` + UI 精选中文子集生成 ⇒ **全 ASCII 都能显示**
  （按钮 `Vol+` / `Vol-` / `Exit`、菜单标题 `Menu`、音量 `Vol: %d%%` 都走它）。

> ⚠️ **裁剪靠一堆锚点**（图标字体的 `.fallback = &cn_font_*` 是**编译期符号引用**，所以裸删中文字库会直接链接失败；
> 还有两处 CMakeLists、`main.c`、`sdkconfig` 的 4 组开关……）。
> 脚本**先预检全部锚点、再动任何文件**：锚点对不上 → **退出码 1 中止、一个文件都不改**；
> `trim_minimal()` 本身也是「**先全部改文本、后统一删文件**」两阶段（旧式做法是先删文件再改引用，
> 失败后留下编不过的工程 —— 本仓库反复踩过的「静默跳过」家族）。

### 主页默认文案规则（最小应用）

| 用户怎么说的 | 主页显示 | 工程名（= app_id） |
|---|---|---|
| 「生成/创建一个应用」（**全部跳过**） | `Hello HELLOAPP!`（居中） | `HELLOAPP` |
| 「生成一个名为 ABC 的应用」 | `Hello ABC!`（居中） | `ABC` |
| 「做一个 XXX 功能的应用」（**给了具体需求**） | 按需求实现（脚本只生成最小骨架，AI 据需求改写 `ui_<name>.c`） | 按名或默认 |

- 名称一律指**英文名**（= 工程名 = 设备端 app_id，ASCII）；中文名只进市场展示，不进设备 UI。
- 问候语填的是**清洗后的英文名**，不是输入原文：`my app` → 屏上 `Hello MYAPP!`（旧版是 `Hello my app!`）。
- 🔴 **本工程没有中文字库**：给自己的 app 加界面文案请**只用 ASCII**（图标字体已含全 ASCII，无需重跑字体子集）。
  真要中文 → 用 `--no-trim` 重新派生（或手工恢复中文字库 + `.fallback`）。

## 交互规则：命名问答（4 条分支，一次只问一个问题）

创建应用前，先解析用户话里有没有「英文名（项目名）」「中文名」「内容/需求」。**命名走下面这张表**；内容/需求能问就问、可跳过。

| 用户当前给了什么 | 你该问什么 |
|---|---|
| **什么都没给**（「生成/创建一个应用」） | ① 先问「项目名叫什么？**必须是英文**」→ 用户答完后 ② 再问「中文名叫什么？」 |
| **给了中文名** | 问「项目名（**必须英文**）是什么？」—— 用 AskUserQuestion **带上按中文含义建议的 ASCII 名**（「贪吃蛇」→ `SNAKE`、「飞机大战2」→ `PLANE_WAR2`），让用户确认或改 |
| **给了英文名** | 问「中文名叫什么？」（可跳过；中文名只用于市场展示，发布前随时能补） |
| **全部跳过** | **不再追问**，**直接生成 `HelloApp` 工程**（工程名 `HELLOAPP`）→ 跑 `python3 tools/new_app_project.py` |

问名字时必须说清两者用途（用户常混）：

- **中文名 = 市场/平台展示名**：只进 `<新工程>/app_meta.json` 的 `name_zh`，**不进设备 UI**，可纯中文。
- **英文名 = 工程名 + 设备端 app_id**：根 `CMakeLists.txt` 的 `project(<英文名>)`，
  也是 `esp_app_desc.project_name`（launcher 首页图标下方显示的就是它）。
- 英文名硬约束（提问时同步告知）：
  - 必须 **ASCII**（launcher 首页只显示 ASCII app_id，中文名会渲染成方框）；
  - **全局唯一**：决定设备 `appdata/<app_id>/` 目录名，撞名 = 数据互串 + 卸载互删；
  - 不能用模板默认名（`pack_app.py` 会拒）；约定**不加 `SDGOODS_` 前缀**；
  - 空格 / 连字符等非法字符会被自动清洗成 `project()` 合法标识符（如 `my app` → `MYAPP`）。

🔴 **脚本不接受「中文名直接当工程名」**：`python3 tools/new_app_project.py 贪吃蛇` 会
**报错退出（退出码 1）**并打印建议英文名（`贪吃蛇`→`SNAKE`、`飞机大战2`→`PLANE_WAR2`）。
这是**有意为之**：旧版会把「贪吃蛇」静默清成工程名 `APP`、把「飞机大战2」清成 `2`，产出坏工程却退出码 0；
`--name-zh` 单独给时更隐蔽 —— 会悄悄退回默认名。所以必须**两个显式参数**分开传：

```bash
python3 tools/new_app_project.py --name-zh 贪吃蛇 --name-en SNAKE
# 也可以：python3 tools/new_app_project.py SNAKE --name-zh 贪吃蛇
```

- `--name-zh` → `<新工程>/app_meta.json` 的 `name_zh`（**只用于市场展示，不进设备 UI**）；
- `--name-en` → 工程名（根 `CMakeLists.txt` 的 `project()`）+ 设备端 app_id，必须 ASCII；
- 顺带：`app_meta.json` 里还有 `name_en` / `project` / `app_id` / `version`，
  `tools/pack_app.py` 会读它、把中英文名带进 `dist/*_app.json`，发布时**不用再问一遍名字**。

### 内容 / 需求（命名问完之后，可跳过）

- 问「想创建一个怎样的应用？我可以给些建议」，建议选项（每个一句话说明，让用户直接选）：
  计时器 / 倒计时 ｜ 图片浏览器 / 相册 ｜ 小游戏（贪吃蛇 / 打地鼠）｜ 时钟 / 天气显示 ｜ 待办清单 ｜ 其他 / 我来描述 ｜
  **跳过（生成最小应用）**
- **用户跳过内容** → 生成最小应用：主页居中只显示 `Hello <英文名>!`（纯 ASCII）。
- **用户给了具体需求** → 先跑命名命令生成最小骨架，再据需求改写 `main/apps/ui_<name>.c`（界面 / 逻辑 / 手势）。

> 一句话：**先走「命名问答」（4 分支，如上表）**；内容能问就问（带建议、可跳过）；
> 全跳过 → 直接 `HelloApp` 工程；给了需求 → 按需求实现（改写 `ui_<name>.c`，界面文案用 ASCII）。

## 它到底做了什么（机械改名 + 裁剪，全自动）

1. **复制**本仓库（跳过 `.git` / `dist` / `__pycache__` / `build*`）。
   ⚠️ `managed_components`（LVGL 等）**不跳过**：本机已装过就直接带过去，省掉首次构建重新拉 ~98MB；
   第三方刚 clone 时本来就没有它，首次构建会自己下载。
2. **改名**：根 `CMakeLists.txt` 的 `project()` → `<英文名>`（**不加 `SDGOODS_` 前缀**，沿用 HELLO 约定 `project(HELLO_3)`）；`version.txt` → `1.0.0`；写 `app_meta.json`（中英文名 + 版本，供发布链读）。
3. **最小体积裁剪**（**默认**；`--no-trim` 跳过）：见上面「⭐ 默认产物 = 最小体积固件」——
   删蓝牙 / 删 Wi-Fi / 删中文字库、界面固定英文，**保留控制中心**。
4. **裁成单应用直启**：
   - 删 `ui_home / ui_scan_page / ui_rec_page / ui_other_page / ui_flappy` 五个启动台/演示 app；
   - 从 `app_template` 派生**唯一**起始 app `main/apps/ui_<name>.c/.h`（函数 `ui_<name>_start` / `_poll`），主页居中显示 `Hello <英文名>!`（最小应用，无计数/按钮）；
   - 重写 `apps_registry.c`：`home_create_show / home_show / apps_show` 全部指向 `ui_<name>_start`（开机直入、退出也回本 app）；
   - 裁剪 `main/CMakeLists.txt` 的 `SRCS`，只留 `app_template.c + ui_<name>.c`。
5. 产物 = 一台「单应用机」：**一开机就进你的 app**，没有主页 / 演示 / 启动台。

> 🔴 **改裁剪规则后必须同步锚点**。现在有**两套**锚点、都在 `precheck_seed()` 里预检：
> · 单应用裁剪：`CMAKE_SRCS_RE` = `main/CMakeLists.txt` 里
>   「`# --- 导航页 / 工具页 ---`」…「`"apps/ui_flappy.c"`」；
> · 最小体积裁剪：`TRIM_PATCHES` / `TRIM_SDKCONFIG` / `TRIM_DELETE_FILES`（见脚本顶部那张表）。
>
> 脚本**先预检全部锚点、再动任何文件**：锚点对不上就**退出码 1 中止、一个文件都不改**
> （旧版只打一行日志、照样删掉 5 个 `ui_*.c`，交付一个 SRCS 引用已删文件、**编译不过**的工程）。

## ⚠️ 圆形屏约束（最重要的一条，已写进生成模板）

设备是 **360×360 的「圆」屏**，四角是被圆边切掉的盲区。所有文字 / 图片 / 按钮（含 border / 阴影）都必须落在安全矩形内：

- `x, y ∈ [SDG_UI_SAFE_X, SDG_UI_SAFE_X + SDG_UI_SAFE_W)` —— 即 `x, y ∈ [60, 300)`、宽高不超 240；
- 用 `sdgoods_ui_in_safe_area(x, y)` 程序化判断越界；控件尺寸要把 border / 阴影算进去，否则圆边会把它们切掉；
- 完整说明见 `components/sdgoods_board/include/sdgoods_ui.h`。生成的起始 app 顶部已带这条约束的醒目注释。

## 下一步：写你自己的应用

- 编辑 **`main/apps/ui_<name>.c`**（生成的骨架已接好：建屏 → 画 UI → 接 `sdgoods_app_shell` 标准框架 → 应用级手势 → 每帧 `poll`）。
- **游戏需要四向滑动**：平台高层手势只给「左滑返回 + 点按」（上滑被 app_shell 占作回主页）——请在 `poll()` 里用底层轮询触摸 API 自判方向：`sdgoods_touch_get_state()` + `sdgoods_touch_get_point()`，取按下→松开位移的主轴、超过阈值（≈24px）才算滑动（API 见 `components/sdgoods_board/include/sdgoods_input.h`）。
- 文案：平台外壳用 `SDG_T("中文", "English")` 按当前语言取词，而**本工程语言固定英文** ⇒ 屏上是英文。
  🔴 **本工程没有中文字库，自己写中文会出方框（tofu）** —— 界面文案请只用 ASCII；真要中文，
  用 `--no-trim` 重新派生（或恢复 `cn_font_14/16.c` + 图标字体 `.fallback`）后重跑 `tools/gen_fonts.py`。
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
- 默认产出的新工程**已按「最小体积」裁剪**：没有 Wi-Fi/BLE、没有中文字库 ⇒ 界面文案只用 ASCII；
  想加回这些能力用 `--no-trim` 重新派生（不要手工往已裁剪的工程里塞源文件，锚点/CMake 都要一起改）。
- （`--no-trim` 或种子工程本体）改了中文文案 → 必须重跑 `tools/gen_fonts.py`。
- 跨任务碰 LVGL 只走无锁 SPSC 环形队列 + 每帧 poll，禁 `lv_async_call()`。
- 音量 / 亮度跨 app 只过 NVS，落盘存「用户意图值」不是瞬时值（否则开机黑屏）。
