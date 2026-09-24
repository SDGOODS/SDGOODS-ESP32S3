# 编译、烧录与打包分发

## 0. 目录速查（改代码前先看这里）

工程分两层，**改动前先判断你要动的是哪一层**：

```
SDGOODS-ESP32S3/
├── components/sdgoods_board/     ← 平台层（底板驱动 BSP）
│   ├── include/                  对外头文件：sdgoods_board.h 是总入口
│   │   ├── sdgoods_board.h       应用层只需 include 这一个
│   │   ├── sdgoods_ui.h          圆屏栅格常量 SDG_UI_*
│   │   ├── sdgoods_hooks.h       平台 ⇄ 应用 注册接口
│   │   └── board_pins.h          ★ 引脚定义，唯一鼓励改的文件
│   ├── lcd/                      ST77916 QSPI 屏驱动（vendor 初始化序列）
│   ├── fonts/                    生成的子集字体（勿手改）
│   └── src/                      显示/触摸/音频/电源/BLE/WiFi/截屏/应用外壳
├── main/                         ← 应用层（你写的东西放这里）
│   ├── main.c                    入口，只做装配
│   ├── apps/                     所有界面与游戏
│   │   ├── apps_registry.c       ★ 应用注册表（唯一接线点）
│   │   ├── app_template.c        新应用模板（无界面入口，照抄它写自己的应用）
│   │   └── ui_*.c                主页 / 启动台 / 各游戏
│   └── patches/                  LVGL 补丁（GIF canvas 走 PSRAM）
└── tools/                        辅助脚本（字体生成、新建应用、联机分析）
```

| 你的需求 | 改哪里 |
|---|---|
| 加一个新应用 / 小游戏 | 本仓库内加演示应用见第 6 节；**自己的应用请用** `python3 tools/new_app_project.py <name>` 派生独立工程（见 skill sdgoods-new-app） |
| 改启动台按钮文字、顺序 | `main/apps/apps_registry.c` 的 `s_apps[]` |
| 改某个界面布局 | `main/apps/ui_*.c` |
| 改引脚（接自己的板子） | `components/sdgoods_board/include/board_pins.h` |
| 改音频时序 / 功放控制 | `components/sdgoods_board/src/sdgoods_audio.c` |
| 改屏初始化序列 | `components/sdgoods_board/lcd/esp_lcd_st77916/` |

> **别动**：`components/sdgoods_board/src/sdgoods_lvgl.c` 的绘制缓冲配置
> （双缓冲 8 行、放 INTERNAL SRAM）—— 改动容易导致屏上出现黑条/红线。
> 理由见 `docs/ARCHITECTURE.md` 的显示链路一节。

## 1. 环境

| 项目 | 版本 | 说明 |
|---|---|---|
| ESP-IDF | **v5.5**（v5.5.0 验证过） | [安装指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html) |
| Python | 3.9+ | IDF 自带要求 |
| LVGL | 8.3.11 | 由组件管理器自动下载，版本锁在 `dependencies.lock` |
| Node.js + npm | 任意较新版本 | **仅生成字体时**需要（安装 `lv_font_conv`） |

首次编译会自动拉取 LVGL 到 `managed_components/`（该目录**不入库**，见 `.gitignore`）。
拉取后，`main/CMakeLists.txt` 会在 configure 阶段自动应用 `main/patches/` 里的补丁
（GIF canvas 走 PSRAM），日志里会看到：

```
-- [lvgl-patch] 已应用: lvgl__lvgl/src/extra/libs/gif/gifdec.c, ...
```

如果这一步报 `LVGL 补丁应用失败`，请读 [`../main/patches/README.md`](../main/patches/README.md)。

## 2. 编译

```bash
idf.py set-target esp32s3      # 仅首次
idf.py build
```

产物：

| 文件 | 烧录地址 |
|---|---|
| `build/bootloader/bootloader.bin` | `0x0` |
| `build/partition_table/partition-table.bin` | `0x8000` |
| `build/SDGOODS_EBADGE.bin` | `0x10000` |

> 上交开放平台的是**最后一行** —— `build/SDGOODS_EBADGE.bin`，一份**不带地址**的纯应用
> 镜像。不要用 `merge_bin` 生成合并镜像去提交，那会让用户设备刷完无法启动（详见第 4 节）。

正常体积约 **1.72 MB**（app 分区 31 MB，用掉约 5%），编译结束应看到：

```
SDGOODS_EBADGE.bin binary size 0x1a3c00 bytes. ... 0x1d5c400 bytes (95%) free.
```

> **改了 `components/` 或 `main/` 后如果行为像没生效**：确认源码确实在
> 工程目录内（IDF 只从工程根下的 `components/` 与 `main/` 找文件），
> 并注意把新文件加进 `main/CMakeLists.txt` 的 `SRCS` 才会被编译 —— 没加进 `SRCS` 的文件不会被编译。

### 2.1 sdkconfig 里有两条不能动（appdata 靠它们才能用）

本工程已设好，**新建工程时要照抄**：

```
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=64
```

原因：`appdata` 分区按 `app_id` 建目录（`/appdata/<app_id>/`），而 `app_id` 取自 IDF 的
`esp_app_desc_t.project_name`（**最长 32 字符**），本工程就是 `SDGOODS_EBADGE`（14 字符）——
**远超 FAT 的 8.3 短名限制**。IDF 默认是 `CONFIG_FATFS_LFN_NONE=y`，此时
`mkdir("/appdata/SDGOODS_EBADGE")` 会拿到 `FR_INVALID_NAME` ⇒ VFS 报 `EINVAL(22)`
⇒ 目录建不出来 ⇒ **app 的持久化数据静默落空**（只有 ≤8 字符的 app_id 看着正常，
所以短名字的示例很容易「验证通过」）。真机日志特征：

```
E sdg_app_sdk: mkdir('/appdata/SDGOODS_EBADGE') failed: errno=22
```

⚠️ **改 `sdkconfig.defaults` 不够，必须同时改 `sdkconfig`**：IDF 只在「该符号没有出现在
`sdkconfig` 里」时才套用 defaults；`CONFIG_FATFS_LFN_NONE=y` 已经写在 `sdkconfig` 里了。

## 3. 烧录

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor    # macOS 示例
idf.py -p COM5 flash monitor                    # Windows 示例
```

串口名会变 —— ESP32-S3 原生 USB-Serial-JTAG 的 `usbmodem` 编号与芯片 MAC 绑定，
换板子/换 USB 口就会变，**脚本里不要写死**。找不到串口时先确认没有别的程序占用：

```bash
ls /dev/cu.usbmodem*
lsof /dev/cu.usbmodemXXXX      # 有输出说明被占用（常见于浏览器 Web Serial 页面没关）
```

烧录后应看到：开机动画（约 4 秒）→ 主页。

### 3.1 `flash` 刷三件套，`app-flash` 只刷应用

`idf.py flash` 一次会刷**三样**：

| 段 | 地址 | 由什么生成 |
|---|---|---|
| bootloader | `0x0` | IDF 按 `sdkconfig.defaults` 编译 |
| 分区表 | `0x8000` | `partitions.csv` 生成 |
| app | `0x10000` | `build/SDGOODS_EBADGE.bin` |

**所以 `idf.py flash` 会改写设备上的分区表。** 只改了应用代码、没动分区布局时用
`app-flash` 就够了 —— 快得多，也不会碰引导层：

```bash
idf.py -p <串口> app-flash monitor
```

> ⚠️ **改了 `partitions.csv` 必须同步平台。** 平台「默认文件」里那份分区表与本仓库的
> `partitions.csv` 是**同一套布局的两个副本**：用户在平台上刷应用包时刷的是平台那份，
> 你本地 `idf.py flash` 刷的是这份。两边任何一边改了而另一边没跟上，症状都是
> **「刷完起不来」** —— 平台把 app 写到 `0x10000`，bootloader 却按另一张表去别处找。
>
> 当前布局两边一致：`nvs@0x9000` / `phy_init@0xF000` / `factory@0x10000`（31MB）。
> 要改就同时改，并在改完重新上传平台的默认文件（平台侧会校验应用分区的落点，
> 与平台落点对不上的分区表会被拒绝）。

## 4. 打包：交付物是「不带地址」的应用包

**约定：烧录地址由设备上的分区表决定，包本身不携带地址。**
所以本工程要交出去的东西只有一份 —— 构建目录里的纯应用镜像：

| 产物 | 是什么 | 去处 |
|---|---|---|
| `build/SDGOODS_EBADGE.bin` | **纯应用镜像（app），不带地址** | **交给平台**（或给别人升级） |
| `build/bootloader/bootloader.bin` | 引导程序（烧 `0x0`） | 只在救砖 / 空片首次烧录时用 |
| `build/partition_table/partition-table.bin` | 分区表（烧 `0x8000`） | 同上 |

用仓库自带的工具**校验并导出**（产物落到 `dist/`，并顺手生成一份 `.json` 元数据）：

```bash
python3 tools/pack_app.py              # → dist/SDGOODS_EBADGE_app.bin
python3 tools/pack_app.py --json       # 结构化输出，给 AI 助手 / CI 用
python3 tools/pack_app.py --no-emit    # 只校验当前构建产物，不导出
```

五项校验，任何一项不过都**拒绝导出**：

| # | 判据 | 拦下来的是什么 |
|---|---|---|
| 1 | `0x8000` 处**不是**分区表魔数 | `merged.bin`（带地址的合并镜像）—— 上架后用户一刷就砖 |
| 2 | 首字节 `0xE9` | 选错文件（截图、文档、压缩包……） |
| 3 | `0x20` 处 == `0xABCD5432` | bootloader、分区表、或被截断的文件 |
| 4 | 偏移 `0x0C` 的 chip_id == `0x0009` | 别的芯片的固件（ESP32 / S2 / C3…） |
| 5 | 体积 ≤ 槽上限（默认 2.9 MB，槽物理 3 MB 留余量） | 会写穿到相邻槽的超大固件 |
| 6 | app 身份不是模板默认名（`SDGOODS_EBADGE` 等） | 否则 app_id 撞车、数据目录互串 |

### 4.1 为什么必须「不带地址」

镜像里存的是**虚拟地址**（IROM `0x42000000` / DROM `0x3C000000`），真正的物理偏移
由 bootloader 通过 MMU 页表在启动时决定 —— **同一份 `app.bin` 写到任何位置都能跑**。
所以地址不该由包来定：

| 对方设备 | 平台 / 工具写到哪 |
|---|---|
| 还没有启动器（旧布局，只有一个 `factory`） | `0x10000` |
| 已装启动器（多应用） | 某个空槽 |

一旦包里带了地址，平台就会**照着包里的地址写**：`merged.bin` 从 `0x0` 起，
第一件事就是覆盖 bootloader 与分区表 —— 用户刷完设备直接起不来。
（这不是假设：谷仓徽章2 首版就是把 app 镜像当整包传，造成了砖机。）

### 4.2 `merged.bin`（合并镜像）：只用于救砖 / 空片首次烧录

只有**自己救砖**、或给**从没烧过固件的空片**刷机时才需要它 —— 单个文件、写 `0x0`：

```bash
cd build
python -m esptool --chip esp32s3 merge_bin \
    --flash_mode dio --flash_freq 80m --flash_size 32MB \
    -o merged.bin \
    0x0    bootloader/bootloader.bin \
    0x8000 partition_table/partition-table.bin \
    0x10000 SDGOODS_EBADGE.bin
```

刷写：

```bash
esptool.py --chip esp32s3 --port <串口> --baud 921600 write_flash 0x0 merged.bin
```

> ⛔ **这个文件不要提交到开放平台。** 它自带烧录地址、从 `0x0` 起写，平台按它刷机
> 会把用户设备上的 bootloader 与分区表一起覆盖。`tools/pack_app.py` 与
> `tools/sdgoods_publish.py` 都会把它拦下来 —— 被拦到时请改传构建目录里的
> `SDGOODS_EBADGE.bin`。

**两条路别搞混：**

| 场景 | 交什么 |
|---|---|
| 上架开放平台（用户设备上已有引导程序与分区表） | 纯 `app.bin`（**不带地址**） |
| 给朋友 / 空片首次烧录 / 救砖 | `merged.bin`（整机镜像，写 `0x0`） |

### 4.3 合并镜像到底写了哪些区域

| 区域 | 内容 |
|---|---|
| `0x000000` | bootloader |
| `0x008000 ~ 0x009000` | 分区表 |
| `0x009000 ~ 0x010000` | 空隙，合并时填 `0xFF` —— 覆盖 `nvs` 与 `phy_init`，**等于顺手清空 NVS** |
| `0x010000 ~ app 结束` | 应用程序 |

所以常规烧录**不需要** `--erase-all`；`app 结束` 之后的内容不会被写入。
注意分区表把 factory 声明到 31MB，**flash 必须与原板一致为 32MB**。

### 4.4 硬件硬约束（发给别人时必须说清）

- ESP32-S3 **必须带 Octal（8 线）PSRAM** —— Quad PSRAM 的板子跑不起来。
- 屏：**360×360 ST77916 QSPI 圆屏**，带电容触摸。
- Flash：≥ 4 MB（固件约 1.72 MB）。本工程默认按 32 MB 配置。

### 4.5 发固件给别人时：必须随包带上许可文件

这不是可选项。本仓库（平台层与应用层）统一以 **Apache-2.0** 发布，该许可第 4 条要求
**再分发时携带许可全文与 NOTICE 文件**。

所以固件包里至少要有这几份（从仓库根目录直接拷进 `release/fixNN/`）：

```
SDGOODS_EBADGE_app.bin             # 纯应用镜像（不带地址，用 tools/pack_app.py 导出）
LICENSE                            # 根许可（Apache-2.0）
NOTICE                             # 第三方组件归属声明
LICENSING.md                       # 授权范围与商标使用（接收方最该看的一份）
```

`flash.sh` / 包内 `README.md` 里也建议写一行：
「个人免费使用；商业用途需授权，详见 LICENSING.md」。

> 顺带一提：对方烧完固件后，串口开机日志会打印项目归属、品牌与授权横幅，
> DEMO 页的「关于」也能看到同样的信息 —— 不必额外解释。

## 5. 字体：新增中文文案后必须重生成

屏幕上出现**方框（□）**就是子集字体缺字。字体分两套，职责不同：

| 字体 | 用途 | 字符集 |
|---|---|---|
| `cn_font_14` / `cn_font_16` | 全量兜底 | 扫描源码得到的**全部**字符 |
| `si_yuan_black_icon_14` / `_16` | 主页/应用页主字体 | **精选子集**（按钮与菜单文案） |

`si_yuan_*` 通过 `.fallback` 挂到 `cn_font_*`：精选子集里没有的字自动去全量集里找。
所以**通常只要 `cn_font_*` 全就够用**，`si_yuan_*` 只在需要让某个字用主字体时才要补。

### 5.1 三个命令（按顺序）

```bash
# ① 下载授权明确的开源字体（SIL OFL 1.1 的 Noto Sans SC，约 10.5 MB）
python3 tools/fetch_fonts.py

# ② 装字体裁剪工具（官方 lv_font_conv）
npm i lv_font_conv

# ③ 扫描源码 → 重新生成四份字体 → 逐字校验
python3 tools/gen_fonts.py --bin ./node_modules/.bin/lv_font_conv
```

只想知道"有没有漏字"、不想重新生成，用：

```bash
python3 tools/gen_fonts.py --check
```

`--check` 会分别校验两套字体的**各自预期字符集**（`cn_font_*` 按源码全量、
`si_yuan_*` 按精选集），并检查 `si_yuan_*` 是否真的挂上了 fallback。

### 5.2 几个必须知道的事实

- **注释里的中文也会计入字符集** —— `gen_fonts.py` 扫描的是源码文本，
  不是只扫 `"..."` 字符串。写一大段中文注释会让字体变大（当前 `cn_font_16`
  约 840 KB）。介意体积就少往注释里塞生僻字。
- **别手动改 `components/sdgoods_board/fonts/*.c`** —— 下次生成会被覆盖。
- **生成文件头部带有 OFL 版权声明，不要删**：SIL OFL 1.1 要求保留版权与许可声明，
  删掉就不合规了。
- 历史来源：早期子集曾用 macOS 自带 `Arial Unicode.ttf` 生成，该字体授权不允许再分发，
  已全部改用 Noto Sans SC 重新生成（见根目录 README 的「许可证与第三方」）。
- 字体源文件 `tools/fonts/NotoSansSC-Regular.ttf` **不入库**（10.5 MB，
  `.gitignore` 已排除），需要时跑 `fetch_fonts.py` 重新下载。

### 5.3 改完必须核对排版（两个手段）

换字体后**字形宽度会变**，圆屏边缘可能溢出、按钮文字可能换行。按下面顺序自证：

**① 离线量宽度（不用烧板子）**

```bash
python3 tools/font_metrics.py                # 用内置的本项目关键文案表
python3 tools/font_metrics.py --strings "谷仓共创计划,按电源键返回"
```

它解析生成字体里的 `glyph_dsc`（前进宽度）与 `cmaps`，直接算出每个文案的渲染宽度，
并标出「缺字」或「超宽」。默认还会跟**仓库首个提交**里的旧字体对比，
一眼看出换字体后哪些文案变宽了：

```
DEMO 按钮    关于         14      28.0    28.0     +0.0  OK
主页页脚     谷仓SDGOODS  14      99.5    94.4     -5.1  OK
```

> 经验：古文/CJK 的前进宽度在思源系与 Arial 系之间**完全一样**（都以全角 em 为基准），
> 所以换字体**不会改变中文排版**，只有拉丁字母会有零点几像素差别。

**② 实机截屏（最终确认）**

有些问题（纵向裁切、颜色、圆边遮挡）只有真机能看出来，仍然要烧一次看画面：

1. `idf.py -p <串口> flash`；
2. 逐个界面走一遍；
3. 电脑端 `tools/screenshot_recv.py -p <串口> -o x.png -n 1 -t`（`-t` 自动向串口发 `s`）；
4. 对着截图确认无方框、无溢出。

> ⚠️ 设备端只有 `'s'` 这一个串口命令，**没有导航命令** —— 手势到不了的界面
> （启动台、菜单等）没法用串口翻过去，只能①先量宽度 + 手动点一遍。

## 6. 新增一个应用

在本仓库（参考固件）里再加演示应用，手动三步（原 `tools/new_app.py` 已移除）：

1. 复制 `main/apps/app_template.c/.h` → `main/apps/ui_my_app.c/.h`，替换应用名与按钮文字；
2. 在 `main/CMakeLists.txt` 的 `SRCS` 里加一行 `"apps/ui_my_app.c"`；
3. 在 `main/apps/apps_registry.c` 加 `#include` 与 `s_apps[]` 表项。

退出回调**不要叫 `on_exit`** —— libc 里有同名函数，会报 `conflicting types`。

完整实现参考 `main/apps/app_template.c`：它本身就可以直接编译，覆盖了建屏、栅格定位、
应用外壳接入、退出清理、每帧轮询。（它没有界面入口 —— 留在仓库里的意义是
保证模板始终能编译。）

> 💡 **要开发你自己的应用并上架**，别在本仓库里加：用
> `python3 tools/new_app_project.py <name>` 派生独立工程（PLANE 形单应用直启）。
> 详见 skill `sdgoods-new-app`。
