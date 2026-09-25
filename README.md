[![中文](https://img.shields.io/badge/lang-中文-red)](README.md) [![English](https://img.shields.io/badge/lang-English-blue)](README_EN.md)

# 谷仓次元屏 · SDGOODS-ESP32S3

[![Build](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml/badge.svg)](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml)
[![平台层: Apache-2.0](https://img.shields.io/badge/platform-Apache--2.0-blue.svg)](LICENSE)
[![应用层: Apache-2.0](https://img.shields.io/badge/apps-Apache--2.0-blue.svg)](LICENSING.md)

> [!IMPORTANT]
> 本工程是谷仓次元屏（谷仓电子徽章）设备的二次开发基础工程，
> 著作权及相关权利归 **深圳希德创新网络有限公司（SDGOODS）** 所有。
> 代码按本仓库许可自由使用，但 **项目名、产品名与 SDGOODS 标识不在代码许可授权范围内**（见 [TRADEMARK.md](TRADEMARK.md)）。
> 官网 [https://sdgoods.ai](https://sdgoods.ai) · 邮箱 `karl@sdgoods.ai`。

---

## 项目目标

> **让任何人（通过自己手上的 AI 编程助手）都能在 5 分钟内，从 GitHub 克隆这份代码 → 派生出自己的应用 → 开发 → 发布到谷仓开放平台，全程不需要先成为嵌入式专家。**

这份仓库被设计成「**AI 可读、AI 可改、AI 可发**」：

- **AI 读得懂**：`AGENTS.md` 是 AI 改代码前的必读规范；`docs/` 是分层架构、编译、发布、SDK 的完整说明；`sdgoods-ai/` 是一套可一键安装的 AI 开发工具包（Skill + Agent + 本地 MCP server）。
- **AI 改得动**：内置从零手写的 UI 框架与多个示例（小鸟游戏 / 主页启动台 / 扫描·识别示例页），既是「板子能做什么」的展示，也是 AI 照着改的现成模板。
- **AI 发得出**：配套发布链路把「开发 → 打包 → 上传到谷仓开放平台」闭合成一条命令 / 一次 MCP 调用。

你只要把需求用自然语言交给 AI，它就会按本仓库的规范把应用做出来、并帮你上架。

---

## 谷仓电子徽章（硬件）

这是一块 **ESP32-S3 + 360×360 圆形触摸屏** 的随身设备（badge），可当胸牌 / 挂饰佩戴。板载硬件：

| 部件 | 型号 / 规格 |
|---|---|
| 主控 SoC | ESP32-S3-R8（双核 Xtensa LX7，内置 **8MB Octal PSRAM**） |
| 存储 | **32MB Flash**（QSPI） |
| 显示屏 | 圆形 **360×360**，**ST77916** 驱动，QSPI，RGB565 |
| 触摸 | **CST816** 电容触摸（I2C，支持滑动手势） |
| 惯性传感 | **QMI8658** 六轴 IMU（3 轴加速度计 + 3 轴陀螺仪，I2C） |
| 音频输出 | 板载 Class-D 功放 + 喇叭（I2S） |
| 音频输入 | 数字麦克风（I2S） |
| 物理按键 | 1 颗电源键（GPIO6） |
| 电池 | **500mAh** 锂电池 + 电池检测 / 供电管理（GPIO7） |
| 无线 | 2.4GHz **WiFi** + **Bluetooth 5（BLE）**，支持双人蓝牙联机对战 |
| 接口 | USB（USB-Serial-JTAG，用于烧录 / 调试 / 串口截屏） |

**外观与佩戴**：圆形机身，直径 **58mm**、厚度 **9mm**；背面带**磁吸**，可吸附在金属表面；另设**挂绳孔**与**别针**（badge pin）两种佩戴方式。

| 正面 · 1.85 寸圆屏 | 侧面 · 9mm 轻薄机身（含挂绳孔） |
|---|---|
| ![正面 1.85 寸圆屏](docs/hardware/device-front.png) | ![侧面 9mm 机身](docs/hardware/device-side.png) |
| **双核主控 · 蓝牙 + WiFi** | **内部结构（爆炸图）** |
| ![屏幕 双核+蓝牙+WiFi](docs/hardware/device-dualcore.png) | ![内部结构爆炸图](docs/hardware/device-exploded.png) |

> 为什么这些参数重要：360×360 的**圆**意味着四角会被切掉——所有 UI 必须落在圆屏安全矩形内（平台提供 `SDG_UI_SAFE_X/Y/W/H` 栅格常量）；8MB PSRAM 让 LVGL 全量帧缓冲和较大图片素材都放得下。应用层**不要**复制 `components/sdgoods_board/include/board_pins.h` 里的引脚常量。

### 实机效果（本工程内置示例：主页 + 小鸟 Flappy，单人小游戏）

| 主页 | 小鸟 Flappy |
|---|---|
| ![主页](screenshot/home.jpg) | ![小鸟 Flappy](screenshot/game-flappy.jpg) |

---

## 谷仓开放平台是什么

[谷仓开放平台](https://sdgoods.ai) 是这块徽章的 **应用市场 / 广场**：

- **对开发者**：你上传自己编译好的应用固件（一个 `.bin`），填名称 / 简介 / 分类 / 截图，提交后由平台**审核**，过审即**上架**，其他用户就能搜到、装到自己的徽章上。
- **对终端用户**（买了徽章的人）：在平台上浏览应用、一键下载、通过 Web 串口或平台工具烧录到自己的徽章；一台徽章支持**多应用插槽**，可同时装多个 app 自由切换。
- **对 AI / CI**：平台提供 **MCP 开发者接口**（拿一个 `sdg_` 开发者令牌即可），让你的 AI 助手把「开发完的固件」直接推送到平台，无需人工填表。

几个关键约定（开发者必知）：

- **你只传「纯应用镜像」**：平台会自动在底部拼接官方引导层（bootloader / 分区表），所以你上传的 `.bin` **不带地址**、不含出厂区。
- **审核机制**：提交后进入审核队列，过审后公开上架；同一应用任何时候平台上只应有一条记录（重新发布 = 删旧建新）。
- **多应用 vs 单应用**：同一份 `app.bin` 既能作为「多应用模式」的一个 app 上架，也能刷成「单应用模式」主机固件直启——由设备运行时判定，不需要两套编译（详见 `docs/SINGLE_APP_FIRMWARE.md`）。

---

## 🚀 从零到上架：四步走

下面四步，每一步都给两种做法，**结果完全一样，选一条跟着走即可**：

- **👤 普通用户**：不碰命令行。你只需要一个 AI 编程助手（WorkBuddy / Claude Code / Cursor 任一），把话说给它，它替你完成。
- **💻 开发者**：熟悉终端，直接敲命令。

---

> [!IMPORTANT]
> **开发前第一步：先把「谷仓电子徽章」用一根「带数据功能的 Type-C 线」连上电脑。**
> 这一步必须在写任何代码 / 让 AI 动手之前完成——只有连着设备，AI 才能替你烧录固件、串口截屏、在真机上验证效果。
> 连好之后，下面四步你只要跟着 AI 走就行。

## 第 1 步 · 配置环境

**👤 普通用户**

你需要的只有：一台电脑（macOS / Windows / Linux）、一根**带数据功能的 Type-C 线**（普通充电线可能只供电、不能传数据，挑能连电脑传文件的那种）、一个 AI 编程助手。把这句话发给 AI：

```
请先克隆这个仓库：https://github.com/SDGOODS/SDGOODS-ESP32S3
然后按里面 docs/ENVIRONMENT.md 的要求，
检查并配置这台电脑的开发环境。缺什么装什么，每一步都告诉我在做什么；
如果我在国内，直接按 docs/ENVIRONMENT.md 第 6 节「国内用户一条龙」来：
走乐鑫国内镜像装 ESP-IDF，并下载那个 24MB 的 LVGL 离线包解压到工程根目录，
别直接走 GitHub 下工具链和组件（会很慢或断流）。
装完跑一次环境检查，把结果汇总给我。
```

AI 会自动检查并安装 Python、ESP-IDF 工具链与 USB 驱动，最后给你一份环境检查报告。过程中弹出的「是否允许」确认即可。

> **国内用户**：装环境要下约 1.2 GB，工具链默认走 GitHub Releases，国内常常几 KB/s。
> 让 AI 先读 `docs/ENVIRONMENT.md` 第 6 节「国内用户一条龙」——走乐鑫国内镜像 +
> 24MB 的 LVGL 离线包，能省下大把等待时间。

**💻 开发者**

```bash
git clone https://github.com/SDGOODS/SDGOODS-ESP32S3
cd SDGOODS-ESP32S3
python3 tools/check_env.py     # 检查 Python / ESP-IDF / esptool，缺啥提示啥
bash sdgoods-ai/install.sh     # 可选：装 AI 开发工具包，AI 助手自动套用本仓库规范
```

版本要求与手动安装步骤见 [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md)；**国内用户直接看其中的第 6 节「国内用户一条龙」**（镜像 + 离线包，全程不必直连 GitHub）。

---

## 第 2 步 · 生成第一个应用

**👤 普通用户**

**先跑通最小例子（推荐）**：把这句话发给 AI——

```
请先克隆这个仓库：https://github.com/SDGOODS/SDGOODS-ESP32S3
然后按里面的 README 与 AGENTS.md 规范，
用 tools/new_app_project.py 生成一个叫 HelloApp 的独立应用工程，
把屏幕问候语改成 Hello SDGOODS!，编译后烧进连着的谷仓电子徽章，烧完截屏给我看效果。
```

生成的就是一个**最小应用**：整块圆屏中央显示 **`Hello SDGOODS!`**。烧进真机的效果：

| 第一个应用 · 真机实拍 | 顶部下滑 · 控制中心（模板自带，不用写一行代码） |
|---|---|
| ![第一个应用 Hello SDGOODS!](screenshot/first-app-hello.jpg) | ![控制中心](screenshot/first-app-cc.jpg) |

看到左边这个画面，说明你已经跑通了「生成 → 编译 → 烧录 → 真机验证」整条链路（全程 10 分钟内）；右边的控制中心是平台模板自带的——音量、亮度、截屏、关于页都能用，你的应用自动拥有它们。

**💻 开发者**

```bash
git clone https://github.com/SDGOODS/SDGOODS-ESP32S3
cd SDGOODS-ESP32S3
python3 tools/new_app_project.py MyApp   # 一键派生独立应用工程，生成在本仓库的上一级目录 ../MYAPP/
cd ../MYAPP                              # 进入新生成的工程（关键：编译要在新工程里跑，不是在仓库里）
idf.py set-target esp32s3
idf.py -B build build                    # 编译
```

派生出来的 `MYAPP/`（与本仓库同级）是一个**完整可编译、开机直入你的应用**的独立工程：没有主页、没有启动台、没有 demo，一上电就进你的界面。

> ★ 本 README 只讲产品与流程。**AI 开始改代码前必须先读 `AGENTS.md`**——那里是编译方式、两层边界、字体流程与几条「不遵守就出 bug」的硬约束。

---

## 你的第一个应用，已经会这些

`new_app_project.py` 生成的最小应用来自仓库里的 `app_template.c`，它**不是空白壳**，而是已经接好了平台全部标准能力。开箱即用的能力清单：

| 能力 | 说明 | 你改哪里 |
|---|---|---|
| **开机直入你的 app** | 没有主页 / 启动台 / demo，上电就进你的界面 | `ui_<name>.c` |
| **圆屏安全区** | 自带 `SDG_UI_SAFE_X/Y/W/H` 栅格，控件不会被圆边切掉 | 布局时引用常量 |
| **触摸交互** | 点按钮、点屏幕任意处、左滑返回，全部走平台成熟原语（不让你自己写手势检测） | `sdgoods_app_on_tap` / `sdgoods_app_on_gesture` |
| **顶部下滑菜单** | 顶部下滑唤出控制中心：音量 ± / 亮度 / 截屏 / 退出 | 平台层自动提供 |
| **控制中心 / 关于页** | 音量、亮度、设备信息（含固件版本）——开发者无需自己写 | 平台层自动提供 |
| **平台音效** | 调用 `sdgoods_audio_sfx_flap()` 即播提示音，安全无副作用 | 事件回调里调用 |
| **中英双语文案** | 用 `SDG_T("中文", "English")` 写文案，用户可在设置里切换语言 | 所有界面字符串 |
| **串口一键截屏** | 连电脑即可抓当前画面（调试 / 发布配图都用它） | `tools/screenshot_recv.py` |
| **每帧推进循环** | `ui_<name>_poll()` 由主循环每轮调用，做动画 / 游戏逻辑 | `poll` 函数 |
| **生命周期** | pause（菜单盖屏时冻结）/ resume / exit（切走时清理资源）回调 | 对应回调 |
| **掉电持久化** | 通过 `appdata` 分区做数据存储（平台已留好） | `APP_SDK.md` |

> ⚠️ **唯一不能忘的一条**：界面里只要出现**新的中文文案**，就必须重跑字体子集工具，否则那些字在屏上是方框（tofu）：
> ```bash
> python3 tools/fetch_fonts.py   # 首次需要：下载 OFL 源字体
> python3 tools/gen_fonts.py
> ```

---

## 第 3 步 · 修改第一个应用

**👤 普通用户**

**第一个修改，从改一句话开始**：把这句话发给 AI——

```
把 HelloApp 主页的 "Hello SDGOODS!" 改成 "你好，世界！"（英文 "Hello, World!"），
重新编译烧进设备，截屏给我看。
```

改完屏幕上的字就换了——这就是改应用的全部体验：**用大白话说需求，AI 改代码、编译、烧录、截屏给你看**。

**然后把它改成你想要的应用**：还是把需求发给 AI——

```
请把 HelloApp 改造成一个 <你的应用，例如：倒计时器 / 计步器 / 小游戏>。
需求：<用大白话描述你想做什么、点按/手势做什么、要不要存数据>。
界面文案用中英双语。改完编译烧进设备，截屏给我看。
```

需求越具体越容易一次做对。AI 会自己找到对应文件（界面都在 `main/apps/ui_<名字>.c` 里）改好并重新编译；每句改动都可以带上「**烧到徽章里截屏给我看**」，不用碰设备就能在电脑上看到真机画面。

对生成的代码好奇？直接问 AI「带我看看这个应用的代码，教我怎么改」，它会一段一段讲给你听。

**💻 开发者**

- 界面与逻辑都在派生工程的 `main/apps/ui_<name>.c`：`ui_<name>_start` 建界面、`ui_<name>_poll` 每帧推进，触摸走平台回调（`sdgoods_app_on_tap` / `sdgoods_app_on_gesture`）。
- 平台能力（触摸原语 / 音效 / 双语文案 / 掉电持久化）的 API 说明见 [docs/APP_SDK.md](docs/APP_SDK.md)。
- 编译 / 烧录 / 串口截屏命令见 [docs/BUILD.md](docs/BUILD.md)。
- ⚠️ 新增了中文文案，记得重跑上面的字体子集工具，否则新字在屏上是方框。

---

## 第 4 步 · 提交发布第一个应用

开发完成、真机验证通过就可以发布。两条官方路径**结果一样，选一条即可**。

**👤 普通用户**

**让 AI 帮你发（推荐）**：把这句话发给 AI——

```
请帮我把 HelloApp 发布到谷仓开放平台。
我会在平台开发者设置里生成一个 sdg_ 开头的开发者令牌发给你，
你负责打包固件、填名称 / 简介 / 分类、配好截图并提交审核，完成后把结果告诉我。
```

AI 会先引导你到平台开发者设置生成一个 `sdg_` 开头的开发者令牌（粘贴给 AI 一次即可），然后自动完成打包 → 上传 → 填名称 / 简介 / 截图 → 提交审核，过审即上架。

**自己在网页发**：先跟 AI 说「帮我把 HelloApp 打包成能上传的 app.bin」，然后打开 [sdgoods.ai](https://sdgoods.ai) → 上传打包好的 `dist/<名字>_app.bin` → 填名称 / 简介 / 分类 → 上传截图（可点「从设备截图」连真机抓图）→ 提交审核。

**💻 开发者（MCP 令牌直推，适合 AI / CI）**

1. 在谷仓开放平台开发者设置里生成一个 `sdg_` 开头的开发者令牌。
2. 先打包出纯应用镜像（平台会自动拼引导层）：
   ```bash
   idf.py -B build build
   python3 tools/pack_app.py -b build      # 产出 dist/<name>_app.bin
   ```
3. 保存开发者令牌（只需一次），然后用令牌直推（AI 也可直接调 MCP server）：
   ```bash
   # 保存令牌（或临时用环境变量：export SDGOODS_DEV_TOKEN="sdg_你的令牌"）
   python3 tools/sdgoods_publish.py set-token "sdg_你的令牌"

   python3 tools/sdgoods_publish.py mcp-upload \
     --file dist/<name>_app.bin \
     --name "MyApp" --category tool \
     --desc-zh "..." --desc-en "..." \
     --shots shot1.jpg shot2.jpg shot3.jpg shot4.jpg
   ```
   提交后进入审核，过审即上架。重新发布时先 `mcp-replace <旧id>` 删旧再上传。

其他令牌通道子命令：`mcp-firmwares`（列出自己提交的固件）、`mcp-unpublish`（下架）、`mcp-delete`（删除草稿/被拒记录）、`mcp-download <id> --check-sha256 <本地sha256>`（拉平台刷机清单做防砖校验）。全部见 `python3 tools/sdgoods_publish.py --help` 与 [docs/PUBLISHING.md](docs/PUBLISHING.md)。

> 网页「从设备截图」会先发 `?` 探测固件能力（`SDGOODS-CAPS:SHOT`）；**无截屏能力的固件会提示你先在 BSP 启用 `CONFIG_SDGOODS_SCREENSHOT` 再重烧**，而不是盲抓出颜色错乱的图。

---

## 🤖 AI 怎么读这个仓库

除了 `AGENTS.md`（AI 必读规范），仓库附带一套 **AI 开发工具包** [`sdgoods-ai/`](sdgoods-ai/README.md)：8 个跨平台 Skill（环境检查 / 新建应用 / 编译烧录 / 截屏 / 字体 / 配置 / 崩溃内存排查 / 发布，支持 WorkBuddy / Claude Code / Cursor）+ 一个领域 Agent 定义 + 一个**本地 MCP server 实现**（纯标准库，把「提交固件」闭合成「AI 开发 → 一键上传到开放平台」）+ 各平台 MCP 配置样例。

- **装上即用**：`bash sdgoods-ai/install.sh` 把 Skill 装到 WorkBuddy，AI 在涉及本设备开发时自动调用。
- **安全**：开放平台不开源，本仓库只含客户端配置样例与协议说明，**不含 server 代码与任何密钥**。

---

## 项目结构

```
components/sdgoods_board/   # 平台层（Apache-2.0，可商用）：驱动/LVGL/触摸/音频/外壳/字体
  └── include/board_pins.h  # 引脚定义（换板子改这里）
main/apps/                  # 应用层（Apache-2.0，二次开发主要在这里）
  ├── apps_registry.c       # 应用清单（主页显示什么、谁被轮询）
  ├── app_template.c        # 新应用模板（new_app_project.py 派生时复制它）
  ├── ui_home.c             # 主页（单应用模式首屏）
  ├── ui_scan_page.c  ui_rec_page.c  ui_other_page.c   # 示例页面
  └── ui_flappy.c           # 小鸟（游戏 demo）
tools/                      # 脚手架与调试脚本（Apache-2.0）
  ├── new_app_project.py    # 从零派生独立应用工程（开机直入、独立上架，见 skill sdgoods-new-app）
  ├── check_env.py          # 开发环境检查（AI 第一步必跑）
  ├── gen_fonts.py          # 重新生成中文子集字体
  ├── screenshot_recv.py    # 电脑端接收串口截屏
  ├── pack_app.py           # 打包纯应用镜像（供发布）
  └── sdgoods_publish.py    # 命令行 / MCP 提交固件到开放平台
platform/                   # ★ 平台托管层：开发者勿改、勿提交改动
  ├── partitions.csv        # 多应用分区表（平台安装时用官方版覆盖）
  └── prebuilt/             # 本地调试用的预编译引导层
docs/                       # BUILD / ARCHITECTURE / ENVIRONMENT / PUBLISHING / APP_SDK ...
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
| [`docs/APP_SDK.md`](docs/APP_SDK.md) | app 侧 SDK 与多/单应用模式检测 |
| [`docs/SINGLE_APP_FIRMWARE.md`](docs/SINGLE_APP_FIRMWARE.md) | 把应用当主机固件直启（单应用模式） |
| [`docs/STANDALONE_PROJECT.md`](docs/STANDALONE_PROJECT.md) | 独立工程完整改造流程（不派生、手工裁剪时用） |
| [`docs/MULTI_APP_DYNAMIC_SLOTS.md`](docs/MULTI_APP_DYNAMIC_SLOTS.md) | 多应用插槽与槽地址/封面块布局 |
| [`docs/PUBLISHING.md`](docs/PUBLISHING.md) | 提交固件到开放平台（网页 / CLI / MCP） |
| [`docs/MCP_CONTRACT.md`](docs/MCP_CONTRACT.md) | MCP / 平台发布的字段契约（本地 stdio server 与平台托管 MCP） |
| [`LICENSING.md`](LICENSING.md) | 授权范围 / 商业授权申请 / FAQ |
| [`TRADEMARK.md`](TRADEMARK.md) | SDGOODS 标识与商标使用规范 |
| [`NOTICE`](NOTICE) | 第三方组件与归属声明 |

---

## 许可证（简要）

本工程（平台层与应用层）**统一以 Apache-2.0 发布**：商业与非商业均免费，可闭源、可修改、可再分发。许可只授权代码，**不含商标**（见 [TRADEMARK.md](TRADEMARK.md)）。

| 部分 | 许可证 | 商业使用 |
|---|---|---|
| `components/sdgoods_board/`（平台层） | Apache-2.0 | ✅ 免费 |
| `main/`（应用层） | Apache-2.0 | ✅ 免费 |
| `tools/`（脚本） | Apache-2.0 | ✅ 免费 |
| `main/patches/`（LVGL 补丁） | MIT | ✅ 免费（LVGL 原许可） |
| `components/sdgoods_board/fonts/`（子集字体） | SIL OFL 1.1 | ✅ 免费（字体原许可） |

---

## 联系

- **官网 / 开放平台**：[https://sdgoods.ai](https://sdgoods.ai)
- **邮箱**：`karl@sdgoods.ai` —— 商业授权、报 bug、合作都走这个邮箱

---

## 致谢

- [LVGL](https://lvgl.io/) · [ESP-IDF](https://github.com/espressif/esp-idf) · [Noto Sans SC](https://github.com/notofonts/noto-cjk) · [Source Han Sans](https://github.com/adobe-fonts/source-han-sans) · [gifdec](https://github.com/lecram/gifdec)
