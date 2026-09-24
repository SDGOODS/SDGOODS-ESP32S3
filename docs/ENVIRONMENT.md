# 开发环境要求（SDGOODS-ESP32S3）

> 这是「谷仓次元屏（谷仓电子徽章）」固件二次开发的**第一道基础**。
> 用 AI 助手或自己改代码前，请先确认环境齐全——缺一项都会在中途卡住。
>
> **项目官网**：[https://sdgoods.ai](https://sdgoods.ai)（谷仓 SDGOODS 开放平台；固件提交广场也在这里）。
> 代码仓库：<https://github.com/SDGOODS/SDGOODS-ESP32S3>。
>
> **最快确认方式**：直接跑本仓库自带的检查脚本，它会逐项告诉你缺什么、怎么装：
>
> ```bash
> python3 tools/check_env.py            # 人类可读表格
> python3 tools/check_env.py --json     # 给 AI / CI 解析的结构化输出
> ```
>
> 退出码 `0` = 可编译；`1` = 有必需项缺失。

---

## 1. 工具清单

| 工具 | 版本要求 | 级别 | 用途 | 怎么检查 | 缺了会怎样 |
|---|---|---|---|---|---|
| **Python 3** | ≥ 3.8（推荐 3.13） | 必需 | ESP-IDF 的运行环境 | `python3 --version` | 装不了 / 跑不了 ESP-IDF |
| **ESP-IDF** | **≥ 5.5**（本工程按 5.5 锁 LVGL 8.3.11） | 必需 | 编译 / 烧录 SDK | `idf.py --version`（需先 `source <idf>/export.sh`） | 无法编译 |
| **esptool** | 随 ESP-IDF 自带 | 必需（烧录） | 烧写 / 合并固件 | `esptool.py --version` | 编出来也烧不到板子 |
| **git** | 任意较新 | 推荐 | 克隆 / 提交代码 | `git --version` | 不影响编译；只是拿不到/交不了代码 |
| **Node.js + npm** | ≥ 16 | 改中文文案时才需 | 跑 `lv_font_conv` 生成子集字体 | `node --version` / `npm --version` | 只能编译、不能改/增中文文案（否则烧出满屏方框） |
| **网络** | — | 必需（首次编译） | 组件管理器拉取 LVGL 8.3.11 | — | 首次 `idf.py build` 会失败（国内用户先看 [2.5 节](#25-国内网络加速国内用户强烈建议先做这一步)） |

**级别说明**
- **必需**：缺了就编译不了 / 烧不了，必须先补齐。
- **改中文文案时才需**：仅当你要新增或改动中文界面文字、或换字体时才装 Node+npm。
  不碰中文文案可以直接跳过，照常编译。
- **推荐 / 信息项**：不影响编译，按需准备。

---

## 2. 安装 ESP-IDF（核心）

ESP-IDF 是乐鑫官方 SDK，本工程基于 **v5.5**。安装方式任选其一：

### 方式 A：官方安装器 / 脚本（推荐）

按官方文档安装：<https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html>

macOS / Linux 装完后，ESP-IDF 通常在 `~/esp/esp-idf`。**每次开新终端都要先激活环境**：

```bash
source ~/esp/esp-idf/export.sh
idf.py --version        # 能看到 ESP-IDF v5.5.x 才算激活成功
```

> ⚠️ 没 source 就跑 `idf.py` 会报「command not found」或 `tools/check_env.py` 报
> 「已安装但未激活」。把这条 source 写进你的 shell 启动脚本（`.zshrc` / `.bashrc`）
> 可一劳永逸。

### 方式 B：用 IDF_PATH（已手动解包时）

```bash
export IDF_PATH=$HOME/esp/esp-idf
source $IDF_PATH/export.sh
```

激活后 `idf.py --version` 应显示 **v5.5.x**；若显示更早版本，请升级：
`cd $IDF_PATH && git fetch && git checkout v5.5.0 && git submodule update --recursive`。

### 2.5 国内网络加速（国内用户强烈建议先做这一步）

装 ESP-IDF 要下约 **1.2 GB**：SDK 源码 ~491 MB + 工具链 ~495 MB + Python 包 ~212 MB。
其中**工具链的绝大部分走 GitHub Releases**（`github.com/espressif/*/releases/download/...`），
国内直连常常几 KB/s 甚至断流，是本环境最耗时的一环。下面的加速手段**已逐条实测**。

**① 工具链：把 GitHub Releases 改走乐鑫国内站（效果最大）**

```bash
export IDF_GITHUB_ASSETS=dl.espressif.cn/github_assets   # 注意：不能带 https://
cd $IDF_PATH && ./install.sh esp32s3
```

这个变量会把 `https://github.com/...` 整体替换成 `https://dl.espressif.cn/github_assets/...`。
已实测该路径返回 **200**（xtensa-esp-elf、xtensa-esp-elf-gdb 均可下载）。

> ⚠️ **两个坑**
> - 值里**不能**出现 `://`，否则 `idf_tools.py` 会直接 `fatal` 退出。
> - **不要**用 `IDF_MIRROR_PREFIX_MAP` 把 `dl.espressif.com` 映射到 `dl.espressif.cn`——
>   实测国内站上没有 `/dl/...` 这份目录（cmake、xtensa 包都返回 404），改完反而全部下不来。
>   国内站只镜像 `github_assets`。

**② 只装 ESP32-S3 需要的工具（省 285 MB）**

`./install.sh` 后面带 target 参数即可，跳过用不到的 RISC-V 工具链：

```bash
./install.sh esp32s3        # 而不是 ./install.sh all
```

**③ Python 包走清华源**

```bash
export PIP_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple
```

**④ Node / npm 走 npmmirror**（只有改中文文案才需要）

```bash
npm config set registry https://registry.npmmirror.com
npm i lv_font_conv
```

**⑤ 工具链离线预置（完全不联网的做法）**

`idf_tools.py` 在下载前会先看 `~/.espressif/dist/` 里有没有**同名压缩包**：有且校验通过就打印
`already downloaded` 直接跳过下载。所以可以把工具链放到自己的网站/内网，让用户下载后丢进这个目录：

```
~/.espressif/dist/
├── xtensa-esp-elf-14.2.0_20241119-aarch64-apple-darwin.tar.xz
├── xtensa-esp-elf-gdb-16.2_20250324-aarch64-apple-darwin21.1.tar.gz
├── esp32ulp-elf-2.38_20240113-macos-arm64.tar.gz
├── openocd-esp32-macos-arm64-0.12.0-esp32-20250422.tar.gz
└── esp-rom-elfs-20241011.tar.gz
```

文件名必须与平台匹配（macOS / Linux / Windows 各一套），照抄自己机器上 `~/.espressif/dist/`
里的现有文件名最保险。

**⑥ ESP-IDF 源码怎么拿更快**

官方仓库在 GitHub，Gitee 上**没有官方镜像**（第三方搬运仓库不可靠）。可选：

- 用 `--depth 1` 浅克隆 + 浅子模块，体积可从 491 MB 降到约 200 MB；
- 或先在一台网络好的机器上 `git clone --recursive` 完整拉下来，打成 tar 包放到自己的
  网站/内网，让国内用户直接下载解压，然后 `export IDF_PATH=<解压路径>`。

**⑦ LVGL 组件（98 MB）**

首次编译时由组件管理器从 `components.espressif.com` 拉取。想完全离线，可以把
`managed_components/lvgl__lvgl/` 整个目录打个包，让用户解压到工程根目录——组件管理器
检测到已存在且 `dependencies.lock` 的 hash 匹配就不会重新下载。

> 本工程对 LVGL 的 GIF 解码器有本地补丁，补丁放在 `main/patches/` 随源码提交，
> 每次 CMake configure 自动覆盖到 `managed_components/`，所以离线拷贝组件也不会丢补丁。

---

## 3. 仅当你要改中文文案：装字体工具链

中文走**子集字体**（只含源码里扫到的字）。新增/改动中文文案后必须重跑字体生成，
否则屏幕上是方框（tofu）。这一步需要 `lv_font_conv`（Node 工具）：

```bash
npm i lv_font_conv                       # 装到本地 node_modules（已 gitignore）
python3 tools/fetch_fonts.py             # 首次：下载 OFL 源字体（不入库）
python3 tools/gen_fonts.py --bin ./node_modules/.bin/lv_font_conv
python3 tools/gen_fonts.py --check       # 校验缺字（很快）
```

> 不碰中文文案则**完全不需要** Node/npm，可直接编译。改完界面建议再用
> `tools/font_metrics.py` 离线量一下文字宽度，避免圆屏边缘被切。

---

## 4. 烧录与串口

| 系统 | 驱动 | 设备路径 | 备注 |
|---|---|---|---|
| macOS | 原生 USB-Serial-JTAG **免驱**；CP210x 需装 SiLabs 驱动 | `/dev/cu.usbmodem*` | 烧录前 `ls /dev/cu.usbmodem*` 确认当前端口 |
| Linux | 通常免驱；权限不足时 | `/dev/ttyACM*` 或 `/dev/ttyUSB*` | `sudo usermod -aG dialout $USER` 后注销重登 |
| Windows | 装 CP210x / USB-Serial-JTAG 驱动 | `COMx` | 建议用 WSL2 + ESP-IDF |

烧录：

```bash
idf.py -p <串口> flash monitor
# 或整机镜像直写（合并镜像，仅救砖 / 空片首次烧录用）：
# esptool.py --chip esp32s3 --port <串口> --baud 921600 write_flash 0x0 merged.bin
```

> ⚠️ `merged.bin` 是**带烧录地址**的整机镜像，只用于救砖或空片；提交到开放平台的
> 必须是**不带地址**的纯应用镜像（`build/SDGOODS_EBADGE.bin`），见 `docs/BUILD.md` 第 4 节。

> 串口号会变（与芯片 MAC 绑定）。如果报 `Resource busy`，多半是浏览器开着 Web Serial
> 页面占着串口，关掉标签页即可。详见 `AGENTS.md` §1。

---

## 5. AI 助手读代码时的第一步

如果你是让 AI（Claude Code / Cursor / Codex / Copilot 等）来改这份代码，
请要求它**开工前先跑一次环境检查**，再决定能做什么：

```bash
python3 tools/check_env.py
```

- 若 `ESP-IDF` 报「已安装但未激活」：让 AI 先 `source <idf>/export.sh` 再编译。
- 若 `node/npm` 报缺失、而你又想改中文文案：让 AI 先 `npm i lv_font_conv` 再改。
- 脚本退出码为 `1` 时，**不要**让 AI 继续「应该能编译」——先补齐环境。

完整开发约定（两层边界、字体流程、联机铁律、提交清单）见 [`AGENTS.md`](../AGENTS.md)。
把固件提交到谷仓 SDGOODS 开放平台见 [`PUBLISHING.md`](PUBLISHING.md)。
