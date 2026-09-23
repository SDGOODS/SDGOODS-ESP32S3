---
name: sdgoods-new-app
description: 在谷仓次元屏（SDGOODS-ESP32S3 开源工程）上「从零创建一个新的应用工程」：一键封装 tools/new_app_project.py，复制开源工程 → 改名（不加 SDGOODS_ 前缀）→ 裁剪成 PLANE 形单应用直启固件（删启动台/演示 app、生成唯一起始 app ui_<app>.c/.h、重写 apps_registry.c 与 CMakeLists.txt）→ 可选 --run 自动编译+烧录（不备份设备）+ 截主页。产物像 PLANE 那样一开机直入你的 app，是「新用户读代码上手」的标准入口。创建时若用户没给应用名/需求会主动询问（带建议、可跳过→最小应用 Hello<名>!）。Use when asked to 生成应用 / 新建应用 / 开发一个应用 / 创建新项目 / generate a new app / scaffold a standalone app / derive from SDGOODS-ESP32S3.
agent_created: true
---

# 从开源工程派生一个新的应用工程（PLANE 形单应用直启）

## 何时使用
- 用户说「生成一个应用 / 开发一个应用 / 新建一个项目 / 从零做一个自己的 app」。
- 目标产物是一台 **「单应用机」**—— 像 PLANE 那样一开机就直接进你的 app，没有主页 / 演示 / 启动台。

> 📌 关键前提：**种子工程是本开源仓库 `SDGOODS-ESP32S3`**（用户能读到的只有它）。
> `SDGOODS-PLANE` 是不开源的内部固件，**不能当种子**。`new_app_project.py` 是从开源工程
> 派生、再「形状」裁成和 PLANE 一样的单应用直启结构 —— **不是直接拷贝 PLANE**。

## 唯一入口命令（从本仓库根目录）

```bash
# 派生 MYAPP（不加 SDGOODS_ 前缀）→ 生成 ../MYAPP/，PLANE 形单应用直启
python3 tools/new_app_project.py myapp

# 只预览将要做的改动，不落盘
python3 tools/new_app_project.py myapp --dry-run

# 生成后若检测到设备，自动编译+烧录（不备份设备固件）+ 截主页
python3 tools/new_app_project.py myapp --run
```

- 默认种子 = 脚本所在仓根 = 本开源工程 `SDGOODS-ESP32S3`。
- `--seed-dir <dir>` 指定其它种子（一般不用）；`--dest-dir <dir>` 指定输出父目录。

### 主页默认文案规则（最小应用）

| 用户怎么说的 | 主页显示 | 工程名 |
|---|---|---|
| 「生成/创建一个应用」（**没给名、没给需求**） | `Hello SDGOODS!`（居中） | `SDGOODS_APP` |
| 「生成一个名为 ABC 的应用」 | `Hello ABC!`（居中） | `ABC` |
| 「做一个 XXX 功能的应用」（**给了具体需求**） | 按需求实现（脚本只生成最小骨架，AI 据需求改写 `ui_<name>.c`） | 按名或默认 |

- 不给名也能跑：`python3 tools/new_app_project.py`（等同于上面的 `myapp` 省略）。
- 问候语用纯 ASCII（无需重跑字体子集）；若按需求改成中文文案，记得重跑 `tools/gen_fonts.py`。

## 交互规则（信息缺失时主动询问）

创建应用前，先解析用户的话里有没有「应用名」和「应用内容/需求」。**缺什么就问什么**（用 AskUserQuestion，一次一个问题、带建议选项）；都齐了再跑脚本。

1. **缺应用名称** → 问「这个应用叫什么名字？」
   - 建议选项：`MyApp` / `Hello` / 让用户用「其他」填自己的名字。
   - 名称规则：不加 `SDGOODS_` 前缀；空格 / 连字符等非法字符会被自动清洗成 `project()` 合法的标识符（如 `my app` → `MYAPP`）。
2. **缺应用内容 / 需求** → 问「想创建一个怎样的应用？我可以给些建议」
   - 建议选项（每个选项一句话说明，让用户直接选）：
     - 计时器 / 倒计时
     - 图片浏览器 / 相册
     - 小游戏（贪吃蛇 / 打地鼠）
     - 时钟 / 天气显示
     - 待办清单
     - 其他 / 我来描述
     - **跳过（生成最小应用）** ← 选它就走最小应用
3. **用户选「跳过」或没填内容** → 生成最小应用：主页居中只显示 `Hello <应用名>!`（纯 ASCII，无需重跑字体子集）。即跑 `python3 tools/new_app_project.py <name>`。
4. **用户给了具体需求** → 先跑上面的命令生成最小骨架，再据需求改写 `main/apps/ui_<name>.c`（界面 / 逻辑 / 手势）；新增中文文案必须重跑 `tools/gen_fonts.py`，否则屏上出方框。

> 一句话：能问就问（带建议）；问不到或用户跳过内容 → 最小应用 `Hello <名>!`；给了需求 → 按需求实现。

## 它到底做了什么（机械改名 + PLANE 形裁剪，全自动）

1. **复制**本仓库（跳过 `.git` / `dist` / `build*` / `managed_components`）。
2. **改名**：根 `CMakeLists.txt` 的 `project()` → `<NAME>`（**不加 `SDGOODS_` 前缀**，沿用 HELLO 约定 `project(HELLO_3)`）；`version.txt` → `1.0.0`。
3. **裁成 PLANE 形单应用**：
   - 删 `ui_home / ui_scan_page / ui_rec_page / ui_other_page / ui_flappy` 五个启动台/演示 app；
   - 从 `app_template` 派生**唯一**起始 app `main/apps/ui_<name>.c/.h`（函数 `ui_<name>_start` / `_poll`），主页居中显示 `Hello <名或 SDGOODS>!`（最小应用，无计数/按钮）；
   - 重写 `apps_registry.c`：`home_create_show / home_show / apps_show` 全部指向 `ui_<name>_start`（开机直入、退出也回本 app）；
   - 裁剪 `main/CMakeLists.txt` 的 `SRCS`，只留 `app_template.c + ui_<name>.c`。
4. 产物 = 一台「单应用机」：**一开机就进你的 app**，没有主页 / 演示 / 启动台。

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
- 烧录后检测代码是否**保留截图功能**（grep `screenshot` / `SHOT` / `sdgoods_screenshot`）：保留 → 自动截主页存 `screenshot_home.jpg` 给你看；已移除 → 跳过。
- 无设备 → 仅编译产出 `.bin` 不烧录。

## 与 `new_standalone_project.py` 的区别（别混）

| | `new_app_project.py`（本 skill，**新用户首选**） | `new_standalone_project.py` |
|---|---|---|
| 意图 | **从零新建一个 app**，干净 PLANE 形直启 | 把**整个仓库**派生成「你自己品牌的独立产品」 |
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
