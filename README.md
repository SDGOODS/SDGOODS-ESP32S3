# SDGOODS-ESP32S3 · 圆屏次元桌面设备

[![Build](https://github.com/USERNAME/SDGOODS-ESP32S3/actions/workflows/build.yml/badge.svg)](https://github.com/USERNAME/SDGOODS-ESP32S3/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

一块 **ESP32-S3 + 360×360 圆形触摸屏** 桌面设备的完整固件：开机动画 → 主页 → 一组小游戏和工具页。
从零手写的 UI 框架与游戏逻辑，包含**双人蓝牙联机对战的飞机大战**、贪吃蛇、俄罗斯方块，
以及一套**串口一键截屏**调试链路。

**这份代码同时是「谷仓 SDGOODS 开放平台」的二次开发基础**：你可以在此基础上加自己的应用、
改玩法、换界面。用 AI 编程助手来改的话，请先让它读 [`AGENTS.md`](AGENTS.md) ——
里面写了编译方式、代码约定、以及几条**不遵守就会出 bug** 的硬约束。

> [!NOTE]
> 这是针对**特定硬件**开发的固件，硬件不匹配会白屏或反复重启，动手前请先看
> [硬件要求](#-硬件要求重要)。

| 主页 | 应用列表 |
|---|---|
| ![home](docs/images/screenshot-home.png) | ![home2](docs/images/screenshot-home-2.png) |

---

## ✨ 功能

**界面与系统**
- 开机 GIF 动画（帧数据烘进固件，PSRAM canvas 播放）
- 主页 + 应用列表 + 工具页（硬件信息 / WiFi 扫描 / 蓝牙扫描 / 录音回放）
- 统一的应用外壳：顶部下滑出菜单（音量 ± / 截屏 / 退出），底部上滑或电源键收起
- 触摸拖拽交互、电量与关机流程、内置开机音效

**小游戏**
- **飞机大战**：道具系统（火力叠加 1~4 列 / 生命心 / 全屏炸弹）、生命与无敌闪烁、
  难度递增、**双人蓝牙联机对战**（两台设备共享同一套敌机与道具）
- **贪吃蛇**、**俄罗斯方块**

**调试工具链**
- 串口一键截屏：设备端 LVGL 快照 → base64 分块 → 电脑端 `tools/screenshot_recv.py` 还原 PNG
- `tools/plane_stat.py`：连双串口边玩边统计道具掉落分布、火力档位、受击次数
- `tools/gen_fonts.py`：按源码字符集重新生成中文子集字体（带缺字校验）

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

引脚定义集中在 [`main/board/board_pins.h`](main/board/board_pins.h)，换板子改这一个文件。

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
git clone https://github.com/USERNAME/SDGOODS-ESP32S3.git
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

| 你想做的事 | 看哪里 |
|---|---|
| 用 AI 改代码（**强烈建议先读**） | [`AGENTS.md`](AGENTS.md) —— 编译方式、代码约定、硬约束、提交前检查清单 |
| 加一个新的应用页面 | [`docs/ARCHITECTURE.md` §3](docs/ARCHITECTURE.md) —— `ui_app_shell` 四步接入 |
| 改界面 / 加中文文案 | 改完必须跑 `tools/gen_fonts.py`，否则出方框 |
| 改飞机玩法 / 联机逻辑 | [`docs/ARCHITECTURE.md` §6](docs/ARCHITECTURE.md) —— **联机同步四条铁律**，违反会让两台设备敌机分叉 |
| 改引脚 / 换屏幕 | `main/board/board_pins.h` + `main/lcd_driver/` |
| 改完怎么自己验证 | `tools/screenshot_recv.py` 一键截屏看画面（AI 也能直接看图） |

几个最容易踩的坑（**都在 `AGENTS.md` 里展开了**）：

1. 中文是**子集字体**，新增文案不重新生成 → 屏幕出现方框；
2. LVGL 是单线程的，跨线程调 LVGL API → 随机崩溃；
3. 内部 SRAM 很小，大缓冲要显式申请 PSRAM，但 **LVGL 绘制缓冲不能放 PSRAM**；
4. 飞机联机的确定性同步有四条硬约束 + 同 tick 语句顺序要求，改玩法前必读。

---

## 📁 目录结构

```
.
├── AGENTS.md                 # ★ 给 AI 编程助手的开发约定（二次开发先读这个）
├── main/                     # 所有源码（编译单元都在这里）
│   ├── main.c                # app_main：初始化各子系统并启动 LVGL 循环
│   ├── board/board_pins.h    # 引脚定义（换板子改这里）
│   ├── lcd_driver/           # ST77916 QSPI 面板驱动
│   ├── lvgl_port.c           # LVGL 移植层（绘制缓冲、tick、异步 flush）
│   ├── ui_*.c                # 各页面：开机 / 主页 / 应用 / DEMO / 工具页
│   ├── ui_app_shell.c        # 应用外壳：统一的下滑菜单、退出与暂停回调
│   ├── ui_plane.c            # 飞机大战（含道具、生命、难度）
│   ├── plane_net.c           # 双人蓝牙 GATT 联机
│   ├── cn_font_*.c           # 中文子集字体（由 tools/gen_fonts.py 生成）
│   ├── boot_anim_gif.c       # 开机动画帧数据
│   ├── screenshot.c          # 串口截屏
│   └── patches/              # ★ 对第三方组件（LVGL）的补丁，随源码提交、自动应用
├── tools/
│   ├── screenshot_recv.py    # 电脑端：接收串口数据还原 PNG
│   ├── plane_stat.py         # 飞机道具体验数据统计
│   └── gen_fonts.py          # 重新生成中文子集字体
├── docs/
│   ├── BUILD.md              # 编译 / 烧录 / 打包分发
│   └── ARCHITECTURE.md       # 代码结构、联机同步设计、调试链路
├── sdkconfig / sdkconfig.defaults
└── partitions.csv
```

---

## 📄 许可证与第三方

本项目代码以 **MIT** 许可证发布，见 [LICENSE](LICENSE)。

| 依赖 | 许可证 | 说明 |
|---|---|---|
| [LVGL 8.3](https://github.com/lvgl/lvgl) | MIT | 由 ESP-IDF 组件管理器自动下载，未随本仓库分发；对其中 GIF 解码器的改动见 `main/patches/` |
| [lv_font_conv](https://github.com/lvgl/lv_font_conv) | MIT | 生成子集字体的工具（开发期依赖） |
| ESP-IDF | Apache-2.0 | 乐鑫官方 SDK |

> [!IMPORTANT]
> `main/cn_font_*.c` 是**字体子集**（扫描源码字符集后取字形生成），不是完整字库。
> 当前仓库内的字体子集由 macOS 自带的 `Arial Unicode.ttf` 生成，**该字体的再分发授权不明确**。
> 若你要把本固件用于对外发布的产品，请改用 SIL OFL 1.1 授权的字体
> （如[思源黑体](https://github.com/adobe-fonts/source-han-sans) / Noto Sans SC）重新生成：
> `python3 tools/gen_fonts.py --font <OFL字体路径>`。
>
> `main/boot_anim_gif.c` 是开机动画的帧数据，请确认你拥有其素材的分发权，或替换为你自己的动画。

---

## 🙏 致谢

- [LVGL](https://lvgl.io/) —— 嵌入式图形库
- [ESP-IDF](https://github.com/espressif/esp-idf) —— 乐鑫官方 SDK
- [gifdec](https://github.com/lecram/gifdec) —— GIF 解码（LVGL 内置，用于开机动画）
