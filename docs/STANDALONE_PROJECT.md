# 派生独立应用工程（Standalone Project）

> 本文专治一个问题：**把本仓库「fork / 复制」成你自己的独立产品固件**——
> 开机就可能直接进你的应用、固件身份是你自己的名字、但仍**保留手势、控制中心、
> 开发边界与开发注意事项**。
>
> 本文专治「fork / 复制成你自己的独立产品并上架」。若你只是想**从开源工程派生成一个自己的 app**
> （开机直入、独立命名、独立上架，类似 PLANE），直接用 `tools/new_app_project.py <name>`（见 skill `sdgoods-new-app`）从本仓库派生即可，**不要**按本文改。

---

## 0. 先选路（路由）

| 你的目标 | 走哪条 | 入口 |
|---|---|---|
| **复制出一份你自己的工程**（推荐，开机直入你的 app、独立命名、独立 git、可直接上架） | **独立工程（本文）** | 下面 Step 1–8 |
| 只是想上架一个 app 包到开放平台 | 打包分发 | `docs/PUBLISHING.md` |

> 📌 普通用户上平台优先走「独立工程」：产出的就是可独立上架的固件。
> 若你只是想**从开源工程派生成一个自己的 app**（开机直入、独立上架，类似 PLANE），用 `tools/new_app_project.py <name>`（见 skill `sdgoods-new-app`）即可，这不属于本文范围。
>
> 同一份 `app.bin` 既能当 MULTI 上架（市场装进 ota_N，用户机器上仍是「启动器 + 你的 app」），
> 也能当 SINGLE 主机固件（本地写 0x10000 + 擦 otadata，开机直入你的 app）。
> **市场安装不会让别人的设备「开机直入你的 app」**——那只是本地 SINGLE 直刷的效果。详见文末「SINGLE vs MULTI」。

---

> 💡 **一键派生（推荐）**：本文 Step 1–8 的「复制仓库 → 改名 → 瘦注册表 → 删 demo →
> 替换硬编码」已全部封装进 `tools/new_standalone_project.py`。一条命令即可生成独立工程：
>
> ```bash
> python3 tools/new_standalone_project.py MY_PRODUCT --boot-direct --prune
> #   --boot-direct  开机直入（不再走主页启动台）
> #   --prune        删掉其它 demo 页面源码并同步 main/CMakeLists.txt 的 SRCS
> #   --app <名>     保留注册表里的某个 app（默认 flappy）
> #   --dry-run      只打印将要做的改动，不落盘
> ```
> 生成的工程已自动把 `project()`、烧录脚本 `APP_BIN`、文档里的 `SDGOODS_EBADGE`
> 全部替换成你的工程名（仅 `pack_app.py` 的「模板默认名拒绝集」故意保留，
> 使派生工程仍会拒绝发布没改名的官方模板）。下面 Step 1–8 是它背后的手动机理，供读懂/排错。

---

## 1. 你「免费得到」什么（只要保留平台组件）

独立工程**不需要重写**这些能力，保留 `components/` 下的两个平台组件即可：

| 能力 | 来自 | 怎么拿到 |
|---|---|---|
| **手势**（顶部下滑出控制中心 / 上滑回主页 / 左滑返回 / 点按） | `components/sdgoods_board` | `sdgoods_app_shell_bind(scr)` 自动接管设备级导航；应用级手势用 `sdgoods_gesture.h` |
| **控制中心**（音量 ± / 亮度 / 数据 / 电量 / 第 5 键 Power\|Exit / 截屏） | `components/sdgoods_launcher`（`sdgoods_cc.*`） | 接 `sdgoods_app_shell_bind(scr)` 后**顶部下滑自动弹出**，无需自己画 |
| 音频（BGM / 音效）、LVGL 移植、触摸、电源键 | `components/sdgoods_board` | 一行 `#include "sdgoods_board.h"` |
| 中文字体子集、字宽度量 | `components/sdgoods_board/fonts` | 改文案后跑 `tools/gen_fonts.py` |
| 应用外壳（暂停/退出回调、菜单开关） | `components/sdgoods_board` | `sdgoods_app_shell.h` |
| 编译 / 烧录 / 截屏 / 字体工具 | `tools/` | 见下 |

> ⚠️ **控制中心的实现不在应用层，而在 `components/sdgoods_launcher`**。
> 很多 AI 会误以为「独立工程要自己实现控制中心」——**不需要**。只要
> `main/CMakeLists.txt` 的 `REQUIRES` 里有 `sdgoods_launcher` 且没删这个组件，
> 控制中心就跟着 `sdgoods_app_shell_bind()` 自动到位。

---

## 2. 前置条件

- 已按 [`docs/ENVIRONMENT.md`](../docs/ENVIRONMENT.md) 配好环境（ESP-IDF 5.5、Python、esptool）。
- **先在原始仓库里完整跑通一次** `tools/build.sh` + `tools/flash_local.sh`，确认环境无误再派生。
  （环境坑都固化在 `tools/build.sh` 里了，直接用它，别手写 idf.py 命令。）

---

## 3. 步骤

### Step 1 — 复制仓库到你的目录，并 git 初始化

```bash
# 不要直接在原仓库里改；复制出独立目录
cp -r SDGOODS-ESP32S3 MyProduct
cd MyProduct
rm -rf .git          # 去掉原仓库历史，成为你自己的工程
git init && git add -A && git commit -m "init: fork from SDGOODS-ESP32S3"
```

> `managed_components/` 不入库（由组件管理器下载），`.gitignore` 已忽略；别提交它。

### Step 2 — 改名（固件身份 + 产物名同源）

改 **根 `CMakeLists.txt`** 第 9 行：

```cmake
# 改前
project(SDGOODS_EBADGE)
# 改后
project(MY_PRODUCT)
```

这一行同时决定两件事：
1. **产物名**：`build_pub/MY_PRODUCT.bin`（由 `tools/build.sh` 的构建目录 `build_pub` + 工程名拼出）。
2. **设备身份**：`esp_app_desc.project_name` 烧进 flash `0x10050`，
   控制中心「关于 / About」页、平台 `Firmware.id` 都读它。
   （某次机型回读 `read_flash 0x10050 32` 看到的字符串就是这里的值。）

> 🔴 **改名坑**：全仓还有约 35 处把 `SDGOODS_EBADGE` 写死在 `tools/`、`docs/`、`AGENTS.md`
> 和 `sdgoods-ai/` 的脚本/文档里（如 `flash_local.sh`、`pack_app.py`、`check_env.py`、
> `PUBLISHING.md`）。手动改会漏——**直接用 `tools/new_standalone_project.py` 一键派生**，
> 它会把 `project()`、`APP_BIN`、文档里的 `SDGOODS_EBADGE` 全部替换成你的工程名
> （仅 `pack_app.py` 的「模板默认名拒绝集」故意保留）。若不用工具，至少把
> `tools/flash_local.sh` 里的 `APP_BIN`、`tools/pack_app.py` 的默认名、以及
> `docs/PUBLISHING.md` 里出现 `SDGOODS_EBADGE.bin` 的字面量改掉，否则烧录/打包会去找一个不存在的文件。

### Step 3 — 决定首屏：保留主页网格，还是开机直入你的 app

#### Option A：保留主页启动台（多 app 变单 app）

适合「你的工程未来还想加更多 app」的情况。只瘦注册表：

`main/apps/apps_registry.c`：

```c
#include "apps_registry.h"
#include <stddef.h>
#include "sdgoods_board.h"

/* 删掉 ui_scan_page / ui_rec_page / ui_other_page / ui_flappy 的 include */
#include "ui_home.h"          /* 主页网格保留 */
#include "ui_myapp.h"         /* 你的应用 */

static const sdgoods_app_t s_apps[] = {
    { .label_zh = "我的应用", .label_en = "My App", .icon = "myapp",
      .show = ui_myapp_start, .poll = ui_myapp_poll },
    /* 新应用插到这里（保持缩进即可） */
};
const sdgoods_app_t *const g_sdgoods_app   = s_apps;
const int                  g_sdgoods_app_count = (int)(sizeof(s_apps) / sizeof(s_apps[0]));
/* apps_poll / home_create_show / home_show / apps_show / apps_register 原样保留 */
```

`main.c` **不用改**——它本来就是 `apps_register()` → `sdgoods_ui_home_create_show()`，
主页网格里点你的 app 进入。

#### Option B：开机直入你的 app（无主页、无启动台）

适合「这就是个单一功能设备」。改两处：

`apps_registry.c`：把 `apps_register()` 里的导航落点改成你的 app：

```c
static void apps_show(void)   { ui_myapp_start(); }   /* 退出后回到你的 app，而非主页 */
static void home_show(void)   { ui_myapp_start(); }
static void home_create_show(void) { ui_myapp_start(); }
```

`main.c`：把首屏创建换成你的 app 启动（其余不动）：

```c
    apps_register();
    /* 原：sdgoods_ui_home_create_show(); */
    ui_myapp_start();          /* 开机直入你的应用 */
    lv_refr_now(NULL);
    sdgoods_lcd_set_backlight(80);
```

> 若选 Option B 且彻底不要主页，可连 `ui_home.c` 一起删（见 Step 4）。

### Step 4 — 删除 demo 源码，并同步 `main/CMakeLists.txt`

从 `main/apps/` 删除你不需要的 demo 源文件（**保留** `app_template.c` 当脚手架、
`app_data_store.c`、`apps_registry.c`）：

```bash
# 典型要删的 demo（按你的选型保留所需）
rm main/apps/ui_flappy.c  main/apps/ui_flappy.h \
   main/apps/ui_scan_page.c main/apps/ui_scan_page.h \
   main/apps/ui_rec_page.c  main/apps/ui_rec_page.h \
   main/apps/ui_other_page.c main/apps/ui_other_page.h
# Option B 且不要主页时再加：
# rm main/apps/ui_home.c main/apps/ui_home.h
```

并在 **`main/CMakeLists.txt`** 的 `SRCS` 里**同步删掉对应行**（否则链接报错）：

```cmake
SRCS
    "main.c"
    "app_data_store.c"
    "apps/apps_registry.c"
    # "apps/ui_home.c"            ← 不需要就注释/删
    # "apps/ui_scan_page.c"
    # "apps/ui_rec_page.c"
    # "apps/ui_other_page.c"
    "apps/app_template.c"        ← 保留脚手架
    # "apps/ui_flappy.c"
    "apps/ui_myapp.c"            ← 你的应用
```

> 🔴 **务必同步 `main/CMakeLists.txt`**：只删 `.c` 文件不删 SRCS，idf 会报
> `cannot find source file`；只删 SRCS 不删 `.c`，文件仍被编译但无人引用（无害但占空间）。

### Step 5 — 🔴 保留两个平台组件，别删

```text
components/sdgoods_launcher/   ← 必须留：控制中心 sdgoods_cc + 应用 SDK + 设备模式
components/sdgoods_board/      ← 必须留：手势 / 音频 / LVGL / 应用外壳 / 字体
```

`main/CMakeLists.txt` 的 `REQUIRES` 里必须**同时**有：

```cmake
REQUIRES
    sdgoods_board
    sdgoods_launcher
    ...
```

> 这两个组件是「独立工程仍拥有手势 + 控制中心 + 开发边界」的根本。
> 它们的许可都是 Apache-2.0（见根 `README.md` 许可证表），可商用、可闭源、可再分发。

### Step 6 — 编译 / 烧录 / 真机验证

```bash
tools/build.sh                 # 一键编译（已固化 env 坑、自动 source IDF）
tools/flash_local.sh           # 一键烧录（用 platform/prebuilt 引导层）
python3 tools/screenshot_recv.py -t   # 连真机抓当前画面，确认 UI 正确（别漏 -t）
```

> 验证三条红线：
> 1. 开机画面是你要的首屏（主页网格 or 直入 app）。
> 2. **顶部下滑能出控制中心**（音量 ± / 亮度 / 第 5 键）。
> 3. 串口回读 `read_flash 0x10050 32` 看到的是你的工程名（如 `MY_PRODUCT`）。

### Step 7 — git 提交

```bash
git add -A && git commit -m "standalone: rename to MY_PRODUCT, slim to single app"
```

### Step 8 — 发布到开放平台

指 [`docs/PUBLISHING.md`](../docs/PUBLISHING.md) 两条对外通道（MCP 令牌直推 / 网页手动）；
**发布前务必先跑 `python3 tools/publish_wizard.py check` 确认 bin 是最新编译版本**，再按
[发布向导流程](../../sdgoods-ai/skills/sdgoods-publish/SKILL.md) 走：
先自动准备提交截图（**在选发布方式之前，全程不询问用户**）——有设备且截图功能开启则 `flash` 烧最新固件 + `shot` 截首页；
无设备 / 截图功能未开则 AI 直接生成一张像素风封面、存进 `screenshot/cover.png`；然后再问「手动发布」还是「MCP 发布」——
手动则 `publish_wizard.py release` 生成 `release/` 包自行上传；MCP 则每个字段先由 AI 生成草稿让用户确认、
截图直接复用第 1 步备好的图。
REST 端点、字段表、两条 MCP 通道的区别见 [`docs/MCP_CONTRACT.md`](../docs/MCP_CONTRACT.md)。
- 当 **MULTI app 包**上架：`tools/pack_app.py` 产出 `dist/MY_PRODUCT_app.bin`，平台写 ota_N。
- 当 **SINGLE 主机固件**：本地 `flash_local.sh` 直刷；平台上架整机包走官方引导层拼接。

---

## 4. 开发边界 & 红线速查（独立工程照样要守）

独立工程没有豁免权，下面这些**和在参考仓库里开发一模一样**，务必遵守：
（详细看 [`AGENTS.md`](../AGENTS.md)）

- **两层边界**：应用层只改 `main/`；平台层 `components/sdgoods_board` 与 `components/sdgoods_launcher`
  是「平台契约」，**改了就不再是标准固件、会失去上架兼容性**。需要新平台能力走「能力登记表 + SDGOODS-CAPS」。
- **改了中文文案 → 必须重跑 `tools/gen_fonts.py`**，否则屏上出方框（tofu）。
- **内存顺序红线**：`sdgoods_lvgl_init` 必须在 `sdgoods_wifi_init / sdgoods_ble_init / sdgoods_audio_init`
  之前；`app_data_store_init` 也别提前。顺序错了会「整屏不更新 / 下滑没有控制中心」。
- **不要用 `lv_async_call()`**：跨任务碰 LVGL 只走无锁 SPSC 环形队列 + 每帧 poll。
- **不要直接改 `managed_components/`**：那是组件管理器下载的第三方代码。
- **音量/亮度跨 app 只过 NVS**，落盘存「用户意图值」不是瞬时值（否则开机黑屏）。
- **提交前检查清单**见 `AGENTS.md` §9。

---

## 5. 控制中心 & 手势 怎么接（写 app 必看）

应用骨架 `main/apps/app_template.c` 已经接好，照抄即可：

```c
/* app_template.c 里的标准四步（你的 app 照抄） */
static lv_obj_t *s_scr;
void ui_myapp_start(void) {
    if (s_scr) return;                 /* 守卫：已在前台就别重建 */
    s_scr = lv_obj_create(NULL);
    /* ... 画你的 UI ... */
    sdgoods_app_shell_bind(s_scr);                 /* 1. 顶部下滑出控制中心 + 上滑回主页 */
    sdgoods_app_shell_set_exit_cb(on_menu_exit);  /* 2. 退出/清理回调（切屏后才调） */
    /* 应用级手势 */
    sdgoods_app_on_gesture(s_scr, SDGOODS_GESTURE_BACK, my_back_cb);  /* 左滑返回 */
    lv_scr_load(s_scr);
}
void ui_myapp_poll(void) {            /* 每帧推进，非前台立刻 return，不阻塞 */
    if (!sdgoods_app_shell_is_app_active()) return;
    /* ... 你的游戏/逻辑 ... */
}
```

**关键 API**（完整签名看头文件）：

| 头文件 | 符号 | 作用 |
|---|---|---|
| `sdgoods_app_shell.h` | `sdgoods_app_shell_bind(scr)` | 接上 → 自动获得设备级导航 + **控制中心** |
| `sdgoods_app_shell.h` | `sdgoods_app_shell_set_exit_cb/pause_cb/resume_cb` | 退出/暂停/恢复钩子 |
| `sdgoods_app_shell.h` | `sdgoods_app_shell_leave(bool to_home)` | 离开应用（返回主页或退出） |
| `sdgoods_app_shell.h` | `sdgoods_app_shell_is_app_active()` | poll 里判断是否前台 |
| `sdgoods_cc.h` | `sdgoods_cc_open()/close()/is_open()` | 控制中心开关（一般不用手调） |
| `sdgoods_cc.h` | `sdgoods_cc_set_app_ctx(SDGOODS_CC_CTX_DEFAULT\|BIRD)` | 自定义控制中心按钮集（如小鸟隐藏 Data/Battery/About） |
| `sdgoods_cc.h` | `sdgoods_cc_brightness_get()` | 取当前亮度（跨 app 只过 NVS） |
| `sdgoods_gesture.h` | `sdgoods_app_on_gesture(scr, SDGOODS_GESTURE_UP/BACK, cb)` | 应用级手势 |
| `sdgoods_gesture.h` | `sdgoods_app_on_tap(scr, ctx, on_tap, ud)` | 应用级点按（带位移守卫，非 CLICKED） |

> 控制器第 5 键语义随**设备模式**变：MULTI（被启动器托管）是 **Exit**（回启动器）；
> SINGLE（自己就是主机）是 **Power**。默认可不关心，用 `SDGOODS_CC_CTX_DEFAULT` 即可。

---

## 6. SINGLE vs MULTI 澄清（避免误解）

| 维度 | MULTI（多应用 / 上架 app 包） | SINGLE（独立主机固件） |
|---|---|---|
| 开机进哪 | 启动器 → 主页 → 点 app | **直接进你的 app**（本文 Option B） |
| 落点 | 平台写 `ota_N` | 本地写 `0x10000` + 擦 `otadata` |
| 控制中心第 5 键 | Exit（回启动器） | Power |
| 别人装了会怎样 | 他的设备「启动器 + 你的 app」 | 仅本地直刷才生效，不影响市场其它用户 |
| 适用 | 想进开放平台广场 | 做自己的成品设备 |

同一份代码，编译出的 `app.bin` 既能按 MULTI 上架，也能本地当 SINGLE 直刷——
区别只在「写到 flash 哪个位置 / 是否擦 otadata」，不在代码本身。详见
[`docs/SINGLE_APP_FIRMWARE.md`](../docs/SINGLE_APP_FIRMWARE.md) 与 [`docs/PUBLISHING.md`](../docs/PUBLISHING.md)。

---

## 7. 参考蓝本

本仓库历史上曾从同一份代码派生出独立工程 **SDGOODS-PLANE**（开机直入飞机、保留双人蓝牙联机、
只留一个 app、改名 `SDGOODS_PLANE`）。它严格走了本文 Step 1–8，可作为「改完长什么样」的实物对照。
