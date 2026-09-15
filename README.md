# SDGOODS-ESP32S3 · 圆屏次元桌面设备

[![Build](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml/badge.svg)](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml)
[![Platform: Apache-2.0](https://img.shields.io/badge/platform-Apache--2.0-blue.svg)](LICENSE)
[![Apps: PolyForm NC](https://img.shields.io/badge/apps-PolyForm%20NC-orange.svg)](LICENSING.md)

一块 **ESP32-S3 + 360×360 圆形触摸屏** 桌面设备的完整固件：开机动画 → 主页 → 应用启动台 → 小游戏与工具页。
从零手写的 UI 框架与游戏逻辑，核心是**双人蓝牙联机对战的飞机大战**，以及一套**串口一键截屏**调试链路。

> [!IMPORTANT]
> **这个工程是什么、属于谁**
>
> 本工程是 **谷仓共创计划（SDGOODS Co-creation Program）** 的基础工程，对应产品是
> **谷仓 SDGOODS 开放平台** 与 **谷仓次元屏（谷仓电子徽章）** 设备。
> 上述计划、平台、设备与本基础代码的**著作权及相关权利，均归深圳希德创新网络有限公司（SDGOODS）所有**。
>
> 你可以按本仓库的许可自由使用代码；但**项目名、产品名与 SDGOODS 标识不在代码许可的授权范围内**，
> 详见 [TRADEMARK.md](TRADEMARK.md)。

**这份代码同时是「谷仓 SDGOODS 开放平台」的二次开发基础。** 工程刻意分成了两层，
连许可也是分开的：

| 层 | 目录 | 你要不要动 | 能商用吗 |
|---|---|---|---|
| **平台层**（板级支持包） | `components/sdgoods_board/` | 一般**不用动** —— 屏驱动、LVGL 移植、触摸、音频、应用框架、中文字体都在这 | ✅ Apache-2.0，随便用、可闭源 |
| **应用层** | `main/apps/` | ★ **你的应用写在这里** | ⚠️ 个人免费，商用需授权 |

加一个自己的应用只要一条命令：

```bash
python3 tools/new_app.py my_app "我的应用"    # 生成骨架 + 自动注册到启动台和构建
```

用 AI 编程助手来改的话，请先让它读 [`AGENTS.md`](AGENTS.md) —— 里面写了编译方式、
两层边界、字体生成流程、以及几条**不遵守就会出 bug** 的硬约束。

> [!NOTE]
> 这是针对**特定硬件**开发的固件，硬件不匹配会白屏或反复重启，动手前请先看
> [硬件要求](#-硬件要求重要)。

| 主页 | 应用启动台 |
|---|---|
| ![home](docs/images/screenshot-home.png) | ![home2](docs/images/screenshot-home-2.png) |

---

## ✨ 功能

**界面与系统**
- 开机 GIF 动画（帧数据烘进固件，PSRAM canvas 播放）
- 主页 + 应用启动台 + DEMO 页（录音回放 / WiFi 扫描 / 蓝牙扫描 / 硬件信息 / 关于）
- 统一的应用外壳（`ui_app_shell`）：顶部下滑出菜单（音量 ± / 截屏 / 退出），
  底部上滑或电源键收起 —— **任何应用接三行代码就自动拥有这套交互**
- 触摸拖拽交互、电量与关机流程、内置开机音效

**小游戏**
- **飞机大战**：道具系统（火力叠加 1~4 列 / 生命心 / 全屏炸弹）、生命与无敌闪烁、
  难度递增、**双人蓝牙联机对战**（两台设备共享同一套敌机与道具）
- **小鸟**：Flappy 玩法的入门示例（结构最简单，适合照着改成自己的游戏）

**给二次开发者的脚手架**
- `tools/new_app.py`：一条命令生成新应用骨架，自动插进 `CMakeLists.txt` 与启动台
- 启动台按钮**按数量自动排布并在圆屏内居中**（1~3 个单行、4~6 个两行），加应用不必手改坐标
- `main/apps/app_template.c`：可直接编译的完整参考实现，新应用骨架就是照抄它。
  它本身没有界面入口，留在构建里的意义是**保证模板始终能编译**

**调试工具链**
- 串口一键截屏：设备端 LVGL 快照 → base64 分块 → 电脑端 `tools/screenshot_recv.py` 还原 PNG
- `tools/plane_stat.py`：连双串口边玩边统计道具掉落分布、火力档位、受击次数
- `tools/gen_fonts.py`：按源码字符集重新生成中文子集字体（带缺字校验）
- `tools/font_metrics.py`：**离线核对文字宽度** —— 不烧板子就能判断某个文案会不会溢出按钮/圆屏

---

## 🧰 硬件要求（重要）

固件按下列硬件编译，**必须一致**，否则白屏、花屏或启动失败：

| 项目 | 要求 |
|---|---|
| 主控 | **ESP32-S3**（带 USB-Serial-JTAG，一根 Type-C 即可烧录） |
| PSRAM | **8MB Octal（8 线）PSRAM**，如 ESP32-S3-WROOM-1-N16R8 / N8R8<br>⚠️ 四线（Quad）PSRAM 的板子**不能用** |
| Flash | **32MB**（分区表把 factory 分区声明到 31MB，小于 32MB 的板子需改 `sdkconfig` + `partitions.csv` 重新编译） |
| 屏幕 | 圆形 **360×360**，**ST77916** 驱动，**QSPI** 接口 |
| 触摸 | 电容触摸（与屏一体） |
| 音频 | 板载功放 + 喇叭（没有也能跑，只是静音） |

引脚定义集中在 [`components/sdgoods_board/include/board_pins.h`](components/sdgoods_board/include/board_pins.h)
—— 这是平台层里**唯一鼓励你改**的文件，换板子改它一个就够。

---

## 🚀 快速开始

### 方式一：直接烧固件（不用编译）

去本仓库的 **Releases** 页下载 `..._merged.bin`，然后：

```bash
pip install esptool
esptool.py --chip esp32s3 --port <你的串口> --baud 921600 \
  write_flash 0x0 SDGOODS_firmware_merged.bin
```

合并固件已含 bootloader + 分区表 + 应用，一次写入 `0x0` 即可。
（首次想更干净可加 `--erase-all`，会全片擦除。）

### 方式二：从源码编译

需要 **ESP-IDF v5.5**（[安装指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html)）：

```bash
git clone https://github.com/SDGOODS/SDGOODS-ESP32S3.git
cd SDGOODS-ESP32S3
idf.py set-target esp32s3
idf.py build
idf.py -p <你的串口> flash monitor
```

首次 `build` 会自动从组件管理器拉取 LVGL 8.3.11（版本锁在 `dependencies.lock`），
并自动应用 `main/patches/` 里的 LVGL 补丁（日志里会打印 `[lvgl-patch] 已应用: ...`）。
详细说明与分发打包见 [`docs/BUILD.md`](docs/BUILD.md)。

---

## 🧩 二次开发

### 加一个自己的应用

```bash
python3 tools/new_app.py my_app "我的应用"
```

脚本会做三件容易漏掉的事：生成 `main/apps/ui_my_app.c/.h`、把源文件插进
`main/CMakeLists.txt`、把应用注册进启动台（`main/apps/apps_registry.c` 的 `s_apps[]` 表）。
然后你只需要改 `ui_my_app.c` 里的界面代码。

启动台会按应用数量自动排布按钮（1~3 个单行居中、4~6 个两行居中，最多显示 6 个），
界面栅格用平台提供的 `SDG_UI_*` 常量（已避开圆边裁切），
顶部下滑菜单 / 音量 / 退出 / 暂停 / 截屏都由 `ui_app_shell` 自动接好。

### 常见任务索引

| 你想做的事 | 看哪里 |
|---|---|
| 用 AI 改代码（**强烈建议先读**） | [`AGENTS.md`](AGENTS.md) —— 编译方式、两层边界、硬约束、提交前检查清单 |
| 加一个应用 / 改启动台按钮 | `python3 tools/new_app.py`，或 [`docs/ARCHITECTURE.md` §3](docs/ARCHITECTURE.md) |
| 改界面 / 加中文文案 | 改完**必须**跑 `tools/gen_fonts.py`，否则出方框；用 `tools/font_metrics.py` 先看会不会超宽 |
| 改飞机玩法 / 联机逻辑 | [`docs/ARCHITECTURE.md` §6](docs/ARCHITECTURE.md) —— **联机同步四条铁律**，违反会让两台设备敌机分叉 |
| 改引脚 / 换屏幕 | `components/sdgoods_board/include/board_pins.h` + `components/sdgoods_board/lcd/` |
| 改完怎么自己验证 | `tools/screenshot_recv.py` 一键截屏看画面（AI 也能直接看图） |
| **想拿去做商业产品 / 要授权** | [`LICENSING.md`](LICENSING.md) —— 平台层本来就可免费商用，只有应用层需要谈 |

几个最容易踩的坑（**都在 `AGENTS.md` 里展开了**）：

1. 中文是**子集字体**，新增文案不重新生成 → 屏幕出现方框；
2. LVGL 是单线程的，跨线程调 LVGL API → 随机崩溃；
3. 内部 SRAM 很小，大缓冲要显式申请 PSRAM，但 **LVGL 绘制缓冲不能放 PSRAM**；
4. 飞机联机的确定性同步有四条硬约束 + 同 tick 语句顺序要求，改玩法前必读；
5. 回调别取 `on_exit` 这种名字 —— 与 libc 符号冲突会直接编译失败。

---

## 📁 目录结构

```
.
├── AGENTS.md                       # ★ 给 AI 编程助手的开发约定（二次开发先读这个）
├── LICENSING.md                    # ★ 授权范围 / 商业授权申请 / 什么算商业用途（FAQ）
├── TRADEMARK.md                    # SDGOODS 标识与商标使用规范
├── NOTICE                          # 第三方组件归属声明
├── LICENSE                         # 应用层许可：PolyForm Noncommercial 1.0.0
│
├── components/sdgoods_board/       # === 平台层（Apache-2.0，可自由商用）===
│   ├── LICENSE                     #   Apache-2.0 全文
│   ├── include/                    #   应用层可见的全部接口
│   │   ├── sdgoods_board.h         #   ← 一行 include 拿到全部平台能力
│   │   ├── sdgoods_ui.h            #   圆屏 UI 栅格常量 SDG_UI_*
│   │   ├── sdgoods_hooks.h         #   平台↔应用 的注册接口（见 ARCHITECTURE §2）
│   │   ├── board_pins.h            #   ★ 引脚定义（换板子改这里）
│   │   ├── ui_app_shell.h          #   应用外壳：统一菜单/退出/暂停
│   │   ├── lvgl_port.h  st77916.h  touch_input.h  power_off.h
│   │   └── audio_recplay.h  wifi_scan.h  ble_scan.h  hw_info.h  screenshot.h
│   ├── src/                        #   实现（LVGL 移植 / 触摸 / 音频 / 框架 / 开机流程…）
│   ├── lcd/                        #   ST77916 QSPI 面板驱动 + 厂商初始化序列
│   ├── fonts/                      #   中文子集字体（gen_fonts.py 生成，SIL OFL 1.1）
│   └── CMakeLists.txt
│
├── main/                           # === 应用层（PolyForm NC，二次开发主要在这里）===
│   ├── main.c                      #   装配点：初始化平台 + apps_register() 接线
│   ├── apps/
│   │   ├── apps_registry.c/.h      #   ★ 应用清单（启动台显示什么、谁被轮询）
│   │   ├── app_template.c/.h       #   ★ 新应用模板（无界面入口，只保证模板能编译）
│   │   ├── ui_home.c               #   主页
│   │   ├── ui_app_page.c           #   应用启动台（表驱动 + 按数量自适应居中）
│   │   ├── ui_demo_page.c          #   DEMO 页：录音 / WIFI / 蓝牙 / 其他 / 关于
│   │   ├── ui_about.c              #   「关于」：归属 / 品牌 / 版本 / 授权
│   │   ├── ui_flappy.c             #   小鸟
│   │   ├── ui_plane.c  plane_net.c #   飞机大战 + 双人蓝牙联机
│   │   └── ui_scan_page.c  ui_rec_page.c  ui_other_page.c
│   ├── patches/                    # ★ 对 LVGL 的补丁；沿用 MIT，不受应用层非商业条款约束
│   └── CMakeLists.txt
│
├── tools/
│   ├── new_app.py                  # ★ 一条命令生成新应用并自动接线
│   ├── fetch_fonts.py              # 下载 OFL 源字体（Noto Sans SC）
│   ├── gen_fonts.py                # 重新生成中文子集字体（带缺字校验）
│   ├── font_metrics.py             # 离线核对文字宽度（会不会溢出/缺字）
│   ├── screenshot_recv.py          # 电脑端：接收串口数据还原 PNG
│   └── plane_stat.py               # 飞机道具体验数据统计
│
├── docs/
│   ├── BUILD.md                    # 编译 / 烧录 / 打包分发
│   └── ARCHITECTURE.md             # 分层设计、钩子机制、联机同步、调试链路
├── sdkconfig / sdkconfig.defaults
└── partitions.csv
```

---

## 📄 许可证与第三方

**本工程是「源码开放（source available）」，不是 OSI 意义上的开源软件** —— 因为应用层
限制了商业使用领域。我们把这件事说清楚，所以文档里不用"开源"二字。

许可**按层分级**。一句话：**平台层随便商用，应用层个人免费、商用需授权。**

| 部分 | 许可证 | 商业使用 |
|---|---|---|
| `components/sdgoods_board/`（平台层：驱动 / LVGL 移植 / 触摸 / 音频 / 外壳 / 字体机制） | [Apache-2.0](components/sdgoods_board/LICENSE) | ✅ 免费，无需授权 |
| `tools/`（构建与调试脚本） | Apache-2.0 | ✅ 免费，无需授权 |
| `main/`（应用层：导航页 / 小游戏 / 工具页） | [PolyForm NC 1.0.0](LICENSE) | ⚠️ 需事前书面授权 |
| `main/patches/`（LVGL 补丁） | MIT | ✅ 免费（LVGL 原许可，不可更改） |
| `components/sdgoods_board/fonts/`（子集字体） | SIL OFL 1.1 | ✅ 免费（字体原许可，不可更改） |

**为什么这么分？** 我们希望你毫无顾虑地拿这块板子做产品 —— 所以平台层给的是
Apache-2.0，可以闭源、可以卖钱、不必回馈代码。应用层是我们演示"这块屏能玩什么"
的代码，个人随便玩，商业用途请先谈一声。

- 商业授权怎么申请、什么算商业用途 → **[LICENSING.md](LICENSING.md)**（含 FAQ）
- SDGOODS 标识与商标怎么用 → **[TRADEMARK.md](TRADEMARK.md)**
- 第三方组件与归属声明 → **[NOTICE](NOTICE)**

> [!NOTE]
> `main/patches/` 是**对 LVGL 源代码的衍生作品**，因此继续以 **MIT** 分发 ——
> 即使根 `LICENSE` 限定了应用层的商业使用，**该限制不适用于此目录**。
> 我们无权对别人的 MIT 代码追加限制，也不想在一个文件里塞两套互相冲突的许可。
>
> `components/sdgoods_board/fonts/*.c` 是**字体子集**（扫描源码字符集后只取用到的字形），
> 不是完整字库。字形来自 **Noto Sans SC**（SIL OFL 1.1，版权 © 2014-2021 Adobe，
> 保留字体名 'Source'），每个生成文件头部都带了完整的版权与许可声明 —— 这是 OFL 的要求，请勿删除。
> 要换成别的字体：`python3 tools/gen_fonts.py --font <你的字体>`（记得确认对方的再分发授权）。
>
> `components/sdgoods_board/src/boot_anim_gif.c` 是开机动画的帧数据，
> 请确认你拥有其素材的分发权，或替换为你自己的动画。

### SDGOODS 标识

每个源文件头部都带版权行与 `SPDX-License-Identifier`；应用层文件还多一行
`Required Notice:` —— **这不是装饰**：PolyForm Noncommercial 的 Notices 条款要求任何分发
都必须把许可条款和这行声明一并传递下去，去掉即失去使用授权。

固件层面也有品牌露出：首页页脚、`build_version.h` 里的品牌宏、开机串口日志的品牌横幅，
以及 **DEMO 页里的「关于」**（谷仓共创计划 / 开放平台 / 设备名 / 公司与授权状态）。
这样即使有人只拿到一颗烧好的芯片，也能看出它的源头与归属。

---

## 🙏 致谢

- [LVGL](https://lvgl.io/) —— 嵌入式图形库
- [ESP-IDF](https://github.com/espressif/esp-idf) —— 乐鑫官方 SDK
- [Noto Sans SC](https://github.com/notofonts/noto-cjk) —— 中文字形
- [gifdec](https://github.com/lecram/gifdec) —— GIF 解码（LVGL 内置，用于开机动画）
