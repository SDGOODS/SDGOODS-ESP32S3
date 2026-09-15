# 编译、烧录与打包分发

## 1. 环境

| 项目 | 版本 | 说明 |
|---|---|---|
| ESP-IDF | **v5.5**（v5.5.0 验证过） | [安装指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html) |
| Python | 3.9+ | IDF 自带要求 |
| LVGL | 8.3.11 | 由组件管理器自动下载，版本锁在 `dependencies.lock` |

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

## 3. 烧录

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor    # macOS 示例
idf.py -p COM5 flash monitor                    # Windows 示例
```

串口名会变 —— ESP32-S3 原生 USB-Serial-JTAG 的 `usbmodem` 编号与芯片 MAC 绑定，
换板子/换 USB 口就会变，**脚本里不要写死**。

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

## 5. 只改中文文案时

新增界面文字后必须重新生成子集字体，否则屏幕出现方框：

```bash
npm i lv_font_conv
python3 tools/gen_fonts.py --bin ./node_modules/.bin/lv_font_conv --font <字体路径>
```

脚本会自动扫描 `main/` 全部字符、重新生成 `cn_font_14.c` / `cn_font_16.c`，
并逐字校验有没有漏字（漏了直接报错退出）。

> 请用**授权允许再分发**的字体（推荐 SIL OFL 1.1 的思源黑体 / Noto Sans SC）。
> 仓库内现有字体子集的历史来源见根目录 README 的「许可证与第三方」。
