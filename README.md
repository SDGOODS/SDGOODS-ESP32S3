# 谷仓次元屏 · SDGOODS-ESP32S3

[![Build](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml/badge.svg)](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml)
[![平台层: Apache-2.0](https://img.shields.io/badge/platform-Apache--2.0-blue.svg)](LICENSE)
[![应用层: PolyForm NC](https://img.shields.io/badge/apps-PolyForm%20NC-orange.svg)](LICENSING.md)

> [!IMPORTANT]
> **这是什么、属于谁**
> 本工程是 **谷仓 SDGOODS 开放平台** 与 **谷仓次元屏（谷仓电子徽章）** 设备的二次开发基础工程，
> 著作权及相关权利归 **深圳希德创新网络有限公司（SDGOODS）** 所有。
> 代码按本仓库许可自由使用；但 **项目名、产品名与 SDGOODS 标识不在代码许可授权范围内**（见 [TRADEMARK.md](TRADEMARK.md)）。
> 官网 [https://sdgoods.ai](https://sdgoods.ai) · 邮箱 `zhangzuoliang321@126.com`

一块 **ESP32-S3 + 360×360 圆形触摸屏** 桌面设备的完整固件：开机动画 → 主页 → 应用启动台 → 小游戏与工具页。
从零手写的 UI 框架与游戏逻辑，核心是**双人蓝牙联机对战的飞机大战**，以及一套**串口一键截屏**调试链路。
这份代码同时是「谷仓 SDGOODS 开放平台」的二次开发基础。

| 主页 | 应用启动台 |
|---|---|
| ![home](docs/images/screenshot-home.png) | ![home2](docs/images/screenshot-home-2.png) |

---

## 仓库怎么组织

工程刻意分成两层，连许可也是分开的——**平台层随便商用，应用层个人免费、商用需授权**。

| 层 | 目录 | 你要不要动 | 许可 |
|---|---|---|---|
| **平台层**（板级支持包） | `components/sdgoods_board/` | 一般**不用动**（屏驱动 / LVGL 移植 / 触摸 / 音频 / 应用框架 / 中文字体都在这） | Apache-2.0，可闭源、可商用 |
| **应用层** | `main/apps/` | ★ **你的应用写在这里** | PolyForm NC，个人免费、商用需授权 |

加一个自己的应用只要一条命令（生成骨架 + 自动注册到启动台和构建）：

```bash
python3 tools/new_app.py my_app "我的应用"
```

用 AI 编程助手来改，请先让它读 [`AGENTS.md`](AGENTS.md)——那里是编译方式、两层边界、字体流程与几条**不遵守就出 bug** 的硬约束。

---

## 硬件能力契约

下表是 `main` 已提供的**已验证能力**，不是芯片数据手册里所有可能的能力。应用代码应使用对应接口，不要绕过 BSP 直接操作硬件。

| 能力 | 已确认实现 | 应用接口 | 必须遵守的边界 |
|---|---|---|---|
| 显示 | 圆形 **360×360**，**ST77916**，QSPI，RGB565 | `SDG_UI_*` 栅格 / LVGL | LVGL 绘制缓冲**不能放 PSRAM**；圆边裁切靠栅格常量避开 |
| 主控 | **ESP32-S3-R8**，8MB Octal PSRAM，32MB Flash | — | 四线（Quad）PSRAM 的板子**不能用**；Flash<32MB 需改 `sdkconfig`+`partitions.csv` |
| 输入 | 电容触摸（与屏一体） | `sdgoods_input` / 滑动手势 | 触摸在 LVGL 线程；跨线程调 LVGL 会崩 |
| 音频 | 板载功放 + 喇叭 | `sdgoods_audio` | 无喇叭也能跑（静音）；PCM 读写放工作任务 |
| 存储 | NVS 掉电保存（语言 / 设置） | — | — |
| 无线 | WiFi 扫描 / BLE（飞机联机） | `sdgoods_wifi` / `sdgoods_ble` | 联机确定性同步有硬约束（见 `docs/ARCHITECTURE.md` §6） |
| 截屏 | 板载 JPEG 编码 → 串口 → 电脑 `.jpg` | `sdgoods_screenshot` + `tools/screenshot_recv.py` | 需 `CONFIG_SDGOODS_SCREENSHOT`；无能力固件会被网页识别并提示 |
| 开机动画 | 240² GIF 居中 15fps | `sdgoods_boot` | canvas 走 PSRAM，勿全局开 `CONFIG_LV_MEM_CUSTOM` |
| 双语 | 出厂默认英文，一键切换 | `SDG_T("中文","English")` | 新增文案走 `SDG_T`；否则需重建字体 |

所有引脚、面板参数只在 [`components/sdgoods_board/include/board_pins.h`](components/sdgoods_board/include/board_pins.h) 定义，应用层不得复制这些常量。

---

## 用一句需求开始开发

简单需求可以直接交给 AI 助手（它改代码前会先跑 `tools/check_env.py` 检查环境）：

```
请为谷仓次元屏开发一个 <你的应用>。
使用 360×360 圆形触摸屏与电容触摸，界面文案用 SDG_T("中文","English") 写双语。
从 main 开始，创建 feature/<your-app> 分支开发。
遵守 AGENTS.md 与 docs/ARCHITECTURE.md；保持硬件逻辑在 components/sdgoods_board、应用逻辑在 main/apps，
用 tools/new_app.py 生成应用骨架，完成可运行实现与测试，
最后分别报告构建结果、未执行的真机项目和逐项验收方法。
```

需求越具体越容易一次实现正确：用户流程、按键/手势做什么、是否掉电保存、体验目标、验收标准。
若细节没给全，AI 可在不改变产品方向的前提下采用保守默认值，但需在交付里列出假设。

> 本 README 只描述产品与仓库，不含给 AI 的执行说明。**AI 开始开发前请先读 `AGENTS.md`。**

---

## 二次开发

### 加一个自己的应用

```bash
python3 tools/new_app.py my_app "我的应用"
```

脚本会自动做三件容易漏的事：生成 `main/apps/ui_my_app.c/.h`、插进 `main/CMakeLists.txt`、
注册进启动台（`apps_registry.c` 的 `s_apps[]` 表）。你只需改 `ui_my_app.c` 的界面代码。
启动台按应用数量自动排布按钮（圆屏内居中），顶部下滑菜单 / 音量 / 退出由 `sdgoods_app_shell` 自动接好。

### 现有应用（可作参考，不是成品 UI）

| 应用 | 值得复用的模式 |
|---|---|
| 飞机大战 `ui_plane.c` + `plane_net.c` | 双人蓝牙联机、确定性同步、道具系统 |
| 小鸟 `ui_flappy.c` | 最简游戏循环，适合照着改成自己的游戏 |

新应用应**重新设计并实现**页面与交互，不能直接照搬 demo 界面；BSP API、生命周期模式与纯逻辑可按需复用。

---

## 快速开始

### 方式一：直接烧固件（不用编译）

去仓库 **Releases** 页下载 `..._merged.bin`，然后：

```bash
pip install esptool
esptool.py --chip esp32s3 --port <你的串口> --baud 921600 \
  write_flash 0x0 SDGOODS_firmware_merged.bin
```

合并固件已含 bootloader + 分区表 + 应用，一次写入 `0x0` 即可（首次想更干净可加 `--erase-all`）。

### 方式二：从源码编译

需要 **ESP-IDF v5.5**（[安装指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html)）：

```bash
git clone https://github.com/SDGOODS/SDGOODS-ESP32S3.git
cd SDGOODS-ESP32S3
idf.py set-target esp32s3
idf.py build
idf.py -p <你的串口> flash monitor
```

首次 `build` 会自动拉取 LVGL 8.3.11（版本锁在 `dependencies.lock`）并应用 `main/patches/` 里的补丁。
详细说明与分发打包见 [`docs/BUILD.md`](docs/BUILD.md)。

---

## 提交到谷仓 SDGOODS 开放平台

做出来的固件可以提交到 **谷仓 SDGOODS 开放平台**（开发者上传、他人下载 / 烧录的广场）。三种方式任选，详见 [`docs/PUBLISHING.md`](docs/PUBLISHING.md)：

1. **网页手动**：平台上传固件，填信息、传截图（可点「从设备截图」连真机抓图）、传 `.bin`。
2. **命令行 / AI 助手**：`python3 tools/sdgoods_publish.py login 邮箱` 一次，`publish` 即可交——纯标准库，AI 也能直接调用。
3. **让 AI 直调 REST API**：照 `docs/PUBLISHING.md` 的 curl 示例（鉴权 → presign 直传 → 创建记录）。

> 网页「从设备截图」会先发 `?` 探测固件能力（`SDGOODS-CAPS:SHOT`），**无截屏能力的固件会弹窗提示**
> 「请让 AI 在 BSP 中启用截屏能力（`CONFIG_SDGOODS_SCREENSHOT`）后重烧」，而不是盲抓出颜色错乱的图。

---

## 项目结构

```
components/sdgoods_board/   # 平台层（Apache-2.0，可商用）：驱动/LVGL/触摸/音频/外壳/字体
  └── include/board_pins.h  # 引脚定义（换板子改这里）
main/apps/                  # 应用层（PolyForm NC，二次开发主要在这里）
  ├── apps_registry.c       # 应用清单（启动台显示什么、谁被轮询）
  ├── app_template.c        # 新应用模板
  ├── ui_home.c  ui_app_page.c  ui_about.c
  ├── ui_flappy.c           # 小鸟
  └── ui_plane.c  plane_net.c  # 飞机大战 + 双人蓝牙联机
tools/                      # 脚手架与调试脚本（Apache-2.0）
  ├── new_app.py            # 一条命令生成新应用并自动接线
  ├── check_env.py          # 开发环境检查（AI 第一步必跑）
  ├── gen_fonts.py          # 重新生成中文子集字体
  ├── font_metrics.py       # 离线核对文字宽度
  ├── screenshot_recv.py    # 电脑端接收串口截屏
  └── sdgoods_publish.py    # 命令行提交固件到开放平台
docs/                       # BUILD / ARCHITECTURE / ENVIRONMENT / PUBLISHING
AGENTS.md                   # ★ AI 编程助手必读入口
LICENSING.md  TRADEMARK.md  NOTICE  LICENSE
```

---

## 文档索引

| 文档 | 内容 |
|---|---|
| [`AGENTS.md`](AGENTS.md) | ★ AI 改代码的编译方式、两层边界、硬约束、提交前检查清单 |
| [`docs/ENVIRONMENT.md`](docs/ENVIRONMENT.md) | 开发环境要求与安装（Python / ESP-IDF / esptool） |
| [`docs/BUILD.md`](docs/BUILD.md) | 编译 / 烧录 / 打包分发 |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | 分层设计、钩子机制、联机同步、调试链路 |
| [`docs/PUBLISHING.md`](docs/PUBLISHING.md) | 提交固件到开放平台（网页 / CLI / API） |
| [`LICENSING.md`](LICENSING.md) | 授权范围 / 商业授权申请 / 什么算商业用途（FAQ） |
| [`TRADEMARK.md`](TRADEMARK.md) | SDGOODS 标识与商标使用规范 |
| [`NOTICE`](NOTICE) | 第三方组件与归属声明 |

---

## 许可证（简要）

**本工程是「源码开放（source available）」，不是 OSI 意义上的开源**——因为应用层限制了商业使用领域。
按层分级，一句话：**平台层随便商用，应用层个人免费、商用需授权。**

| 部分 | 许可证 | 商业使用 |
|---|---|---|
| `components/sdgoods_board/`（平台层） | Apache-2.0 | ✅ 免费 |
| `tools/`（脚本） | Apache-2.0 | ✅ 免费 |
| `main/`（应用层） | PolyForm NC 1.0.0 | ⚠️ 需事前书面授权 |
| `main/patches/`（LVGL 补丁） | MIT | ✅ 免费（LVGL 原许可） |
| `components/sdgoods_board/fonts/`（子集字体） | SIL OFL 1.1 | ✅ 免费（字体原许可） |

商业授权申请、商标使用见 [LICENSING.md](LICENSING.md) / [TRADEMARK.md](TRADEMARK.md)。

---

## 联系

- **官网**：[https://sdgoods.ai](https://sdgoods.ai)（谷仓 SDGOODS 开放平台）
- **邮箱**：`zhangzuoliang321@126.com` —— 商业授权、报 bug、合作都走这个邮箱

---

## 致谢

- [LVGL](https://lvgl.io/) · [ESP-IDF](https://github.com/espressif/esp-idf) · [Noto Sans SC](https://github.com/notofonts/noto-cjk) · [gifdec](https://github.com/lecram/gifdec)
