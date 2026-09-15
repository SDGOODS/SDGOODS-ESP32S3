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
│   │   ├── app_template.c        可编译的示例应用，照着抄
│   │   └── ui_*.c                主页 / 启动台 / 各游戏
│   └── patches/                  LVGL 补丁（GIF canvas 走 PSRAM）
└── tools/                        辅助脚本（字体生成、新建应用、联机分析）
```

| 你的需求 | 改哪里 |
|---|---|
| 加一个新应用 / 小游戏 | `python3 tools/new_app.py my_app "我的应用"`（见第 6 节） |
| 改启动台按钮文字、顺序 | `main/apps/apps_registry.c` 的 `s_apps[]` |
| 改某个界面布局 | `main/apps/ui_*.c` |
| 改引脚（接自己的板子） | `components/sdgoods_board/include/board_pins.h` |
| 改音频时序 / 功放控制 | `components/sdgoods_board/src/audio_recplay.c` |
| 改屏初始化序列 | `components/sdgoods_board/lcd/esp_lcd_st77916/` |

> **别动**：`components/sdgoods_board/src/lvgl_port.c` 的绘制缓冲配置
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
| `build/home_demo.bin` | `0x10000` |

正常体积约 **1.72 MB**（app 分区 31 MB，用掉约 5%），编译结束应看到：

```
home_demo.bin binary size 0x1a3c00 bytes. ... 0x1d5c400 bytes (95%) free.
```

> **改了 `components/` 或 `main/` 后如果行为像没生效**：确认源码确实在
> 工程目录内（IDF 只从工程根下的 `components/` 与 `main/` 找文件），
> 并注意 `main/CMakeLists.txt` 已写好 `# >>> new_app.py: 新应用源文件插到这里 >>>`
> 标记 —— 新文件不加进 `SRCS` 就不会被编译。

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

## 4. 打包给别人（不含源码）

只有一台设备、想让别人也能用，就发**合并固件**（单个文件，写 `0x0` 即可）：

```bash
cd build
python -m esptool --chip esp32s3 merge_bin \
    --flash_mode dio --flash_freq 80m --flash_size 32MB \
    -o merged.bin \
    0x0    bootloader/bootloader.bin \
    0x8000 partition_table/partition-table.bin \
    0x10000 home_demo.bin
```

对方烧录：

```bash
esptool.py --chip esp32s3 --port <串口> --baud 921600 write_flash 0x0 merged.bin
```

地址表（**必须一致**，与 `partitions.csv` 的 factory 分区对应）：
`bootloader → 0x0`、`partition-table → 0x8000`、`app → 0x10000`。

### 合并固件到底写了哪些区域

| 区域 | 内容 |
|---|---|
| `0x000000` | bootloader |
| `0x008000 ~ 0x009000` | 分区表 |
| `0x009000 ~ 0x010000` | 空隙，合并时填 `0xFF` —— 覆盖 `nvs` 与 `phy_init`，**等于顺手清空 NVS** |
| `0x010000 ~ app 结束` | 应用程序 |

所以常规烧录**不需要** `--erase-all`；`app 结束` 之后的内容不会被写入。
注意分区表把 factory 声明到 31MB，**flash 必须与原板一致为 32MB**。

### 硬件硬约束（发给别人时必须说清）

- ESP32-S3 **必须带 Octal（8 线）PSRAM** —— Quad PSRAM 的板子跑不起来。
- 屏：**360×360 ST77916 QSPI 圆屏**，带电容触摸。
- Flash：≥ 4 MB（固件约 1.72 MB）。本工程默认按 32 MB 配置。

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

### 5.3 改完必须实机核对排版

换字体后**字形宽度会变**，圆屏边缘可能溢出、按钮文字可能换行。生成完务必：

1. 编译 → 烧录；
2. 走一遍主页、启动台、每个应用；
3. 顶部下滑菜单里有「截屏」，或串口发 `s` 触发（见 `docs/ARCHITECTURE.md`），
   把画面取回来逐屏看。

## 6. 新增一个应用

一条命令生成骨架（自动建 `.c`/`.h`、改 `main/CMakeLists.txt`、
在 `main/apps/apps_registry.c` 的应用表里插一行）：

```bash
python3 tools/new_app.py my_app "我的应用"
```

会生成 `main/apps/my_app.c` + `main/apps/my_app.h`，并注册到启动台。
命令末尾会打印后续步骤，照做即可。先看计划不落盘：

```bash
python3 tools/new_app.py my_app "我的应用" --dry-run
```

三个注意点：

- 文件名**不要**带 `ui_` 前缀（脚本会自动规范化，`ui_timer` → `timer`）。
- 生成的三处 `>>> new_app.py ... >>>` 标记**不要删** —— 脚本靠它们定位插入点。
- 退出回调**不要叫 `on_exit`** —— libc 里有同名函数，会报 `conflicting types`。

完整实现参考 `main/apps/app_template.c`：它本身就是一个可编译运行的示例应用
（启动台上的「示例」按钮），覆盖了建屏、栅格定位、应用外壳接入、退出清理、每帧轮询。
