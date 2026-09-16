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
| **网络** | — | 必需（首次编译） | 组件管理器拉取 LVGL 8.3.11 | — | 首次 `idf.py build` 会失败 |

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
# 或合并固件直写：esptool.py --chip esp32s3 --port <串口> --baud 921600 write_flash 0x0 merged.bin
```

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
