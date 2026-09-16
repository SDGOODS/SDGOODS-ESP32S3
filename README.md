# 谷仓次元屏 · SDGOODS-ESP32S3
# SDGOODS Cyber-Badge · SDGOODS-ESP32S3

[![Build](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml/badge.svg)](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml)
[![平台层: Apache-2.0](https://img.shields.io/badge/platform-Apache--2.0-blue.svg)](LICENSE)
[![应用层: Apache-2.0](https://img.shields.io/badge/apps-Apache--2.0-blue.svg)](LICENSING.md)

> [!IMPORTANT]
> **这是什么、属于谁 / What this is, and who owns it**
> 本工程是谷仓次元屏（谷仓电子徽章） 设备的二次开发基础工程，
> This repo is the base firmware/SDK for the SDGOODS Cyber-Badge (谷仓电子徽章) device,
> 著作权及相关权利归 **深圳希德创新网络有限公司（SDGOODS）** 所有。
> copyright and related rights belong to **Shenzhen SDGOODS Innovation Network Co., Ltd. (SDGOODS)**.
> 代码按本仓库许可自由使用，但 **项目名、产品名与 SDGOODS 标识不在代码许可授权范围内**（见 [TRADEMARK.md](TRADEMARK.md)）。
> The code is free to use under this repo's license, but **the project name, product name and SDGOODS marks are NOT covered by the code license** (see [TRADEMARK.md](TRADEMARK.md)).
> 官网 [https://sdgoods.ai](https://sdgoods.ai) · 邮箱 `zhangzuoliang321@126.com`
> Site [https://sdgoods.ai](https://sdgoods.ai) · Email `zhangzuoliang321@126.com`

这是一块 **ESP32-S3 + 360×360 圆形触摸屏** 设备的完整固件示例工程：开机动画 → 主页 → DEMO和示例。
This is a complete firmware example for an **ESP32-S3 + 360×360 round touchscreen** device: boot animation → home → DEMO & samples.
还包含从零手写的 UI 框架与游戏逻辑，以及一套串口一键截屏调试链路等。
It also ships a from-scratch UI framework, game logic, and a one-command serial screenshot debug pipeline.
基于这份代码，你能轻松使用 AI 进行二次开发。
With this code, you can easily do AI-assisted secondary development.

| 主页 / Home | 应用启动台 / App launcher |
|---|---|
| ![home](docs/images/screenshot-home.png) | ![home2](docs/images/screenshot-home-2.png) |

---

## DEMO 与参考应用 / DEMO & Reference Apps

固件里预置了几个 demo，既用来直观展示这块屏「能做什么」，也是给 AI 当参考模板的现成代码。下面**不是硬件全量清单**（全量见下方「硬件能力」），而是挑几个代表性 demo，看代码怎么用硬件；每一行也标了它适合给 AI 当模板参考什么：
The firmware ships several demos that both show what the badge can do and serve as ready-made reference templates for AI. The list below is **not the full hardware catalog** (see "Hardware" below); it picks representative demos to show how the code uses the hardware, and each row notes what it is good to copy as a template:

| Demo / 参考文件 / Demo or reference file | 演示的硬件能力 / Hardware demonstrated | 给 AI 当模板参考什么 / What to copy as a template |
|---|---|---|
| 主页 / 应用启动台（`ui_home.c`） / Home & app launcher | 圆形 360×360 触摸屏、电容触摸滑动手势、开机动画 / Round 360×360 touchscreen, capacitive swipe gestures, boot animation | 外壳 / 启动台写法 / Shell & launcher structure |
| 演示页（`ui_demo_page.c`） / Demo page | 基础 UI 控件、双语文案、屏与触摸的综合调用 / Basic UI widgets, bilingual text, combined screen+touch calls | 页面布局、控件与硬件调用的写法 / Page layout, widgets, hardware calls |
| 小鸟（`ui_flappy.c`） / Flappy bird | 触摸控制 + 定时器游戏循环 + 音频播放 / Touch control + timer game loop + audio | 简单游戏：触摸输入 + 定时刷新 + 绘制 / Simple game: input + timed refresh + draw |
| 飞机大战（`ui_plane.c` + `plane_net.c`） / Plane shooter | 触摸 / 陀螺仪操控，以及 **WiFi + BLE 双人蓝牙联机对战** / Touch/gyro control, plus **WiFi + BLE two-player Bluetooth co-op** | 完整游戏 + 双人蓝牙联机同步逻辑 / Full game + two-player BT sync |
| 最小骨架（`main/apps/app_template.c`） / Minimal skeleton | 最小可运行应用（用 `tools/new_app.py` 一键生成） / Minimal runnable app (one-shot `tools/new_app.py`) | **新应用从这里改**：改它就成新应用 / **Start new apps here**: edit it into your app |
| 串口一键截屏 / Serial screenshot | 连电脑即抓当前画面（调试链路） / Grab current screen from a PC (debug pipeline) | — |

> AI 读完这些参考 + [`AGENTS.md`](AGENTS.md) + [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)，
> After reading these references + [`AGENTS.md`](AGENTS.md) + [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md),
> 用 `python3 tools/new_app.py my_app "我的应用"` 生成骨架，就能照着写你自己的应用。
> run `python3 tools/new_app.py my_app "我的应用"` to scaffold, then write your own app following them.

---

## 硬件能力 / Hardware

谷仓次元屏（谷仓电子徽章）的板载硬件：
On-board hardware of the SDGOODS Cyber-Badge:

| 部件 / Part | 型号 / 规格 / Model / spec |
|---|---|
| 主控 SoC / SoC | ESP32-S3-R8（双核 Xtensa LX7，内置 **8MB Octal PSRAM**）/ dual-core Xtensa LX7, **8MB Octal PSRAM** |
| 存储 / Storage | **32MB Flash**（QSPI） |
| 显示屏 / Display | 圆形 **360×360**，**ST77916** 驱动，QSPI 接口，RGB565 / Round **360×360**, **ST77916** driver, QSPI, RGB565 |
| 触摸 / Touch | **CST816** 电容触摸（与屏一体，I2C，支持滑动手势）/ **CST816** capacitive touch (I2C, swipe gestures) |
| 惯性传感 / IMU | **QMI8658** 六轴 IMU（3 轴加速度计 + 3 轴陀螺仪，I2C）/ **QMI8658** 6-axis IMU (3-axis accel + 3-axis gyro, I2C) |
| 音频输出 / Audio out | 板载 Class-D 功放 + 喇叭（I2S）/ On-board Class-D amp + speaker (I2S) |
| 音频输入 / Audio in | 数字麦克风（I2S）/ Digital mic (I2S) |
| 物理按键 / Button | 1 颗电源键（GPIO6）/ 1 power button (GPIO6) |
| 电池 / Battery | **500mAh** 锂电池 + 电池检测 / 供电管理（GPIO7）/ **500mAh** Li-Po + fuel gauge / PMU (GPIO7) |
| 无线 / Wireless | 2.4GHz **WiFi** + **Bluetooth 5（BLE）**，支持双人蓝牙联机对战 / 2.4GHz **WiFi** + **Bluetooth 5 (BLE)**, two-player BT co-op |
| 接口 / Interface | USB（USB-Serial-JTAG，用于烧录 / 调试 / 串口截屏）/ USB (USB-Serial-JTAG: flash / debug / serial screenshot) |

**外观与佩戴 / Look & wear**：圆形机身，直径 **58mm**、厚度 **9mm**；背面带**磁吸**，可吸附在金属表面；另设**挂绳孔**与**别针**（badge pin）两种佩戴方式，可作胸牌 / 挂饰。
Round body, **58mm** diameter, **9mm** thick; back has a **magnet** to stick on metal; also a **lanyard hole** and a **badge pin** for two wear styles (lapel badge / pendant).

所有引脚与面板参数只在 [`components/sdgoods_board/include/board_pins.h`](components/sdgoods_board/include/board_pins.h) 定义，应用层不得复制这些常量。
All pin and panel constants live only in [`components/sdgoods_board/include/board_pins.h`](components/sdgoods_board/include/board_pins.h); the app layer must not copy them.

---

## 🚀 用一句话开始开发 / Start building in one sentence

想给谷仓次元屏做一个新应用？把需求直接交给 **AI 编程助手** 即可——它会先跑 `tools/check_env.py` 检查环境，再按本仓库规范开发。
Want a new app for the badge? Just hand the requirement to your **AI coding assistant** — it runs `tools/check_env.py` to check the environment first, then develops per this repo's rules.

```
请读取 https://github.com/SDGOODS/SDGOODS-ESP32S3 的代码和文档，
为谷仓次元屏开发一个 <你的应用> 应用。
使用触摸操控，界面文案使用中英双语。
遵守 AGENTS.md 与 docs/ARCHITECTURE.md；
完成运行实现与测试，
并给报告和建议。

Read the code and docs at https://github.com/SDGOODS/SDGOODS-ESP32S3,
and develop a <your-app> app for the SDGOODS badge.
Use touch controls; UI text in both Chinese and English.
Follow AGENTS.md and docs/ARCHITECTURE.md;
complete a runnable implementation and tests,
and give a report with suggestions.
```

需求越具体越容易一次实现正确：用户流程、按键/手势做什么、是否掉电保存、体验目标、验收标准。
The more specific the requirement, the more likely it is correct on the first try: user flow, what buttons/gestures do, whether to persist across power loss, experience goal, acceptance criteria.
若细节没给全，AI 可在不改变产品方向的前提下采用保守默认值，但需在交付里列出假设。
If details are missing, the AI may adopt conservative defaults without changing the product direction, but must list its assumptions in the delivery.

> ★ 本 README 只描述产品与仓库。**AI 开始开发前请先读 `AGENTS.md`**——那里是编译方式、两层边界、字体流程与几条「不遵守就出 bug」的硬约束。
> ★ This README only describes the product and repo. **AI must read `AGENTS.md` before coding** — it holds the build method, the two-layer boundary, the font flow, and several hard rules that break things if ignored.

---

## 提交到谷仓 SDGOODS 开放平台 / Publish to the SDGOODS Open Platform

做出来的固件可以提交到 **谷仓 SDGOODS 开放平台**（开发者上传、他人下载 / 烧录的广场）。三种方式任选，详见 [`docs/PUBLISHING.md`](docs/PUBLISHING.md)：
Built firmware can be submitted to the **SDGOODS Open Platform** (a plaza where developers upload and others download/flash). Three ways, see [`docs/PUBLISHING.md`](docs/PUBLISHING.md):

1. **网页手动 / Web UI**：平台上传固件，填信息、传截图（可点「从设备截图」连真机抓图）、传 `.bin`。
   Upload on the web, fill info, send a screenshot (use "screenshot from device" to grab from real hardware), upload the `.bin`.
2. **命令行 / AI 助手 / CLI or AI assistant**：`python3 tools/sdgoods_publish.py login 邮箱` 一次，`publish` 即可交——纯标准库，AI 也能直接调用。
   `python3 tools/sdgoods_publish.py login <email>` once, then `publish` — pure stdlib, AI can call it directly.
3. **让 AI 直调 REST API / AI calls the REST API**：照 `docs/PUBLISHING.md` 的 curl 示例（鉴权 → presign 直传 → 创建记录）。
   Follow the curl examples in `docs/PUBLISHING.md` (auth → presign upload → create record).

> 网页「从设备截图」会先发 `?` 探测固件能力（`SDGOODS-CAPS:SHOT`），**无截屏能力的固件会弹窗提示**
> The web "screenshot from device" first sends `?` to probe capability (`SDGOODS-CAPS:SHOT`); **firmware without screenshot support pops a notice**
> 「请让 AI 在 BSP 中启用截屏能力（`CONFIG_SDGOODS_SCREENSHOT`）后重烧」，而不是盲抓出颜色错乱的图。
> "ask the AI to enable screenshot in the BSP (`CONFIG_SDGOODS_SCREENSHOT`) and reflash", instead of grabbing a wrong-colored image blindly.

---

## 项目结构 / Project layout

```
components/sdgoods_board/   # 平台层（Apache-2.0，可商用）：驱动/LVGL/触摸/音频/外壳/字体
                            # Platform layer (Apache-2.0, commercial OK): drivers/LVGL/touch/audio/shell/fonts
  └── include/board_pins.h  # 引脚定义（换板子改这里）/ Pin defs (edit here for a new board)
main/apps/                  # 应用层（Apache-2.0，二次开发主要在这里）/ App layer (Apache-2.0, where you hack)
  ├── apps_registry.c       # 应用清单（启动台显示什么、谁被轮询）/ App registry (launcher + poll list)
  ├── app_template.c        # 新应用模板 / New-app template
  ├── ui_home.c  ui_app_page.c  ui_about.c
  ├── ui_flappy.c           # 小鸟 / Flappy bird
  └── ui_plane.c  plane_net.c  # 飞机大战 + 双人蓝牙联机 / Plane shooter + BT co-op
tools/                      # 脚手架与调试脚本（Apache-2.0）/ Scaffolds & debug scripts (Apache-2.0)
  ├── new_app.py            # 一条命令生成新应用并自动接线 / One command to scaffold + wire an app
  ├── check_env.py          # 开发环境检查（AI 第一步必跑）/ Env check (AI runs first)
  ├── gen_fonts.py          # 重新生成中文子集字体 / Regenerate Chinese subset fonts
  ├── font_metrics.py       # 离线核对文字宽度 / Offline text-width check
  ├── screenshot_recv.py    # 电脑端接收串口截屏 / PC side of serial screenshot
  └── sdgoods_publish.py    # 命令行提交固件到开放平台 / CLI publish to Open Platform
docs/                       # BUILD / ARCHITECTURE / ENVIRONMENT / PUBLISHING
AGENTS.md                   # ★ AI 编程助手必读入口 / ★ Must-read entry for AI assistants
LICENSING.md  TRADEMARK.md  NOTICE  LICENSE
```

---

## 文档索引 / Docs index

| 文档 / Doc | 内容 / Contents |
|---|---|
| [`AGENTS.md`](AGENTS.md) | ★ AI 改代码的编译方式、两层边界、硬约束、提交前检查清单 / ★ Build method, two-layer boundary, hard rules, pre-commit checklist |
| [`docs/ENVIRONMENT.md`](docs/ENVIRONMENT.md) | 开发环境要求与安装（Python / ESP-IDF / esptool）/ Env requirements & install |
| [`docs/BUILD.md`](docs/BUILD.md) | 编译 / 烧录 / 打包分发 / Build / flash / package |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | 分层设计、钩子机制、联机同步、调试链路 / Layering, hooks, co-op sync, debug pipeline |
| [`docs/PUBLISHING.md`](docs/PUBLISHING.md) | 提交固件到开放平台（网页 / CLI / API）/ Publish firmware (web / CLI / API) |
| [`LICENSING.md`](LICENSING.md) | 授权范围 / 商业授权申请 / 什么算商业用途（FAQ）/ License scope / commercial FAQ |
| [`TRADEMARK.md`](TRADEMARK.md) | SDGOODS 标识与商标使用规范 / SDGOODS marks & trademark rules |
| [`NOTICE`](NOTICE) | 第三方组件与归属声明 / Third-party components & attributions |

---

## 许可证（简要）/ License (summary)

本工程（平台层与应用层）**统一以 Apache-2.0 发布**：商业与非商业均免费，可闭源、可修改、可再分发。
This repo (platform + app layers) is **uniformly released under Apache-2.0**: free for commercial and non-commercial, can be closed-source, modified, and redistributed.
许可只授权代码，**不含商标**（见 [TRADEMARK.md](TRADEMARK.md)）。
The license covers code only, **not trademarks** (see [TRADEMARK.md](TRADEMARK.md)).

| 部分 / Part | 许可证 / License | 商业使用 / Commercial |
|---|---|---|
| `components/sdgoods_board/`（平台层）/ Platform layer | Apache-2.0 | ✅ 免费 / Free |
| `main/`（应用层）/ App layer | Apache-2.0 | ✅ 免费 / Free |
| `tools/`（脚本）/ Scripts | Apache-2.0 | ✅ 免费 / Free |
| `main/patches/`（LVGL 补丁）/ LVGL patches | MIT | ✅ 免费（LVGL 原许可）/ Free (LVGL's license) |
| `components/sdgoods_board/fonts/`（子集字体）/ Subset fonts | SIL OFL 1.1 | ✅ 免费（字体原许可）/ Free (font license) |

授权范围与商标使用见 [LICENSING.md](LICENSING.md) / [TRADEMARK.md](TRADEMARK.md)。
License scope and trademark use: see [LICENSING.md](LICENSING.md) / [TRADEMARK.md](TRADEMARK.md).

---

## 联系 / Contact

- **官网 / Site**：[https://sdgoods.ai](https://sdgoods.ai)（谷仓 SDGOODS 开放平台 / SDGOODS Open Platform）
- **邮箱 / Email**：`zhangzuoliang321@126.com` —— 商业授权、报 bug、合作都走这个邮箱 / for commercial licensing, bug reports, and partnerships

---

## 致谢 / Acknowledgements

- [LVGL](https://lvgl.io/) · [ESP-IDF](https://github.com/espressif/esp-idf) · [Noto Sans SC](https://github.com/notofonts/noto-cjk) · [gifdec](https://github.com/lecram/gifdec)
