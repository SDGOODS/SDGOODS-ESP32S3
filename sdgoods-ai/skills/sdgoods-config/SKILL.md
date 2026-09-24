---
name: sdgoods-config
description: 谷仓次元屏（SDGOODS-ESP32S3）固件「配置文件」集中索引——每张配置项在哪个文件、各控制什么、哪些能改/不能改，以及常见改法（改工程名/产品名、开关截屏能力、改 GPIO 引脚、改 Flash/PSRAM/槽位数、加 Kconfig 选项）。改任何配置后必须整编。Use when changing board pins / sdkconfig / CMakeLists / partition table / project name / enabling screenshot, or asking "where is X configured".
agent_created: true
---

# SDGOODS 固件配置（配置文件索引）

## 何时使用
- 用户要改工程名 / 产品名、改板级引脚、开关某个功能（如截屏）、调 Flash/PSRAM/CPU、增减应用槽位、或加一个编译期开关。
- 用户问「XX 配置在哪」「这个文件能不能改」「改完为什么没生效」。
- 本 Skill 是**配置索引**，不负责编译烧录——改完配置后用 `sdgoods-build-flash` 整编验证。

## 关键事实（先读这段，避免改错文件）
- **`sdkconfig` 是生成物，禁止手改**。改默认配置改 `sdkconfig.defaults`，交互式改用 `idf.py menuconfig`（写回 `sdkconfig`）。
- **`platform/` 下的引导层（bootloader.bin / partition-table.bin / `partitions.csv`）由平台统一下发，开发者禁止修改、禁止提交改动**。本地调试烧录用 `tools/flash_local.sh`，它会自动烧入 `platform/prebuilt/` 的官方引导层。改了 `partitions.csv` 不会生效，还会让分区表与平台不一致导致 OTA 失败。
- **平台层唯一鼓励你改的文件是 `components/sdgoods_board/include/board_pins.h`**（所有 GPIO/引脚常量都在这里）。应用层不要复制引脚常量，统一从这里取。
- **两层边界**：除 `board_pins.h` 外，一般不要碰 `components/sdgoods_board/` 其它源码；新应用只放 `main/apps/`。

## 配置文件地图（各自控制什么）

| 文件 | 控制什么 | 能改？ | 备注 |
|---|---|---|---|
| `CMakeLists.txt`（根） | `project(SDGOODS_EBADGE)` → 决定产物名 `SDGOODS_EBADGE.bin`（烧到 `0x10000`），以及关于页显示的应用身份 | ⚠️ 改需联动 | 改名必须同步 `tools/flash_local.sh`、`tools/pack_app.py`、`tools/check_env.py` 里硬编码的产品名 |
| `version.txt`（根） | 应用版本号（如 `1.0.0`），关于页与发布版本同源 | ✅ | 改源码后按发布铁律 **版本号 +1** |
| `sdkconfig.defaults` | **默认配置的总入口**：目标芯片、Flash 32MB、PSRAM 8MB Octal、CPU 240MHz、`CONFIG_SDGOODS_APP_SLOTS=4`、`LV_COLOR_16_SWAP=y`、FATFS 长文件名、BT/BLE 等 | ✅ | 改默认项就改这里；`LV_COLOR_16_SWAP=y` 不可关（ST77916 RGB565 字节序） |
| `sdkconfig` | 当前构建的完整配置（由 defaults + menuconfig 合并生成） | ❌ 手改 | 用 `sdkconfig.defaults` + `idf.py menuconfig` 管理 |
| `main/CMakeLists.txt` | 应用层源码清单 `SRCS`（加新 `.c` 在这里）、`INCLUDE_DIRS`、`REQUIRES`、自动版本戳、LVGL 补丁应用 | ✅ | 新增应用必须在 `SRCS` 加一行；补丁在 `main/patches/` |
| `components/sdgoods_board/include/board_pins.h` | **所有板级引脚/GPIO**（LCD SPI、I2C、触摸、电源键、MIC/SPK I2S、电池、背光、功放 PA）与少量显示参数（`BOARD_LCD_RGB565_TX_BYTE_SWAP`、`BOARD_LCD_RGB_ORDER_BGR`） | ✅（唯一允许改的平台文件） | 硬件接线变了才改；不要在此加业务逻辑 |
| `components/sdgoods_board/Kconfig` | `CONFIG_SDGOODS_SCREENSHOT`（bool，默认 y）：是否带串口截屏能力，暴露 `SDGOODS_CAP_SCREENSHOT` 能力位（网页上传页会先 `?` 探测） | ✅ 经 menuconfig | 关掉可缩小固件；关后网页会提示重新开启 |
| `components/sdgoods_launcher/Kconfig` | `CONFIG_SDGOODS_APP_SLOTS`（默认 4，范围 1–16）、`CONFIG_SDGOODS_APP_SLOT_BYTES`（默认 3MB） | ✅ 经 menuconfig | 槽位数 / 槽大小必须与 `partitions.csv` 的 `ota_N` 分区严格一致 |
| `platform/partitions.csv` | 多应用分区表（launcher@0x10000、otadata、ota_0..3 各 3MB、appdata 16MB） | ❌ 禁止 | 平台托管；改了不生效且会 OTA 失败 |

## 常见改法（照抄步骤）

### 1) 改工程名 / 产品名（app 身份）
```bash
# 1. 改根 CMakeLists.txt 的 project(SDGOODS_EBADGE) → project(<新名>)
# 2. 同步硬编码产品名（否则 flash_local.sh / pack_app.py / check_env.py 会找不到产物）：
grep -rn "SDGOODS_EBADGE" tools/   # 逐个改
# 3. 整编
idf.py build
```
> 衍生独立工程（`sdgoods-new-app`）的改名由脚本自动完成，无需手改。

### 2) 开关串口截屏能力
```bash
idf.py menuconfig
# SDGOODS Board (BSP)  →  Enable screenshot capability (serial 's')  [y/n]
# 等价于在 sdkconfig 里切 CONFIG_SDGOODS_SCREENSHOT
```
关掉后 `SDGOODS_CAP_SCREENSHOT` 不再暴露，网页上传页会提示「先在 BSP 启用再重烧」。

### 3) 改 GPIO / 板级引脚
只改 `components/sdgoods_board/include/board_pins.h` 里的对应 `#define`（如 `BOARD_I2C_SCL`、`BOARD_TOUCH_GPIO_INT`、`BOARD_AUDIO_PA_EN_GPIO`）。改完整编；引脚冲突会在编译/运行期暴露。

### 4) 改 Flash / PSRAM / CPU / 槽位等
在 `sdkconfig.defaults` 改（`CONFIG_ESPTOOLPY_FLASHSIZE_*`、`CONFIG_SPIRAM_*`、`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`、`CONFIG_SDGOODS_APP_SLOTS`）。
- 槽位数 / 槽大小还要与 `platform/partitions.csv` 的 `ota_N` 分区个数与大小一致（但 partitions.csv 本身**不能改**——硬件/平台布局固定为 4×3MB）。
- 这些参数与真实硬件绑定（32MB Flash + 8MB Octal PSRAM），**除非换了硬件否则不要改**。

### 5) 新增一个编译期开关（Kconfig 选项）
```bash
# 在对应组件的 Kconfig 里加：
config SDGOODS_MY_FEATURE
    bool "My feature"
    default n
    help
      ...
# 源码里用 #ifdef CONFIG_SDGOODS_MY_FEATURE / CONFIG_SDGOODS_MY_FEATURE 消费
```
放 `components/<comp>/Kconfig`（不是根目录），组件下 `CMakeLists.txt` 用 `target_compile_options` 或源码里 `#ifdef` 读取。

## 改配置后的铁律（务必遵守）
1. **整编，不要信增量**：改了 `sdkconfig.defaults` / `Kconfig` / `board_pins.h` / `CMakeLists.txt` 后，必须 `idf.py build` 整编（Kconfig 改动需要重新 configure；怀疑没生效就 `touch` 全部 `.c` 或换一个 `-B build_xxx` 目录，但**绝不要 `rm -rf build`**）。
2. **改源码 + 要发布** → 先 `version.txt` +1 再整编（发布三铁律之一）。
3. **不要碰**：`platform/`（引导层+分区表）、`managed_components/`（组件管理器会覆盖）。
4. 烧录用 `tools/flash_local.sh`（自动带官方引导层）；验证画面用 `sdgoods-screenshot`。

## 与其它 Skill 的关系
- 改完配置 → `sdgoods-build-flash` 整编 + 烧录验证。
- 派生新工程时的改名 → 由 `sdgoods-new-app` 自动完成（见上「常见改法 1」）。
- 改中文文案不属于配置 → 走 `sdgoods-fonts`。
