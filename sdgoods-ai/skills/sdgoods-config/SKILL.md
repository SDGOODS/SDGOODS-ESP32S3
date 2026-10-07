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
| `components/sdgoods_launcher/Kconfig` | `CONFIG_SDGOODS_APP_SLOTS`（**默认 16**，范围 1–16）、`CONFIG_SDGOODS_APP_SLOT_BYTES`（默认 3MB，**2.0 起标为 Legacy：名义槽大小**） | ✅ 经 menuconfig | 2.0 动态槽下：槽数 = 表里 `ota_N` 的**条数**，槽大小 = 设备安装时在槽区内部自切 ⇒ 这里的 3MB 只当兜底名义值，**别再拿它算落点** |
| `platform/partitions.csv` | 多应用分区表（**2.0 形态**：`launcher@0x10000` / `otadata@0x310000` / `ota_0..15` **16 条全部压在 `0x320000` 且 `size=0`** / `pt_bak@0x0FF0000` / `appdata@0x1000000`） | ❌ 禁止 | 平台托管；改了不生效且会 OTA 失败。**1.0 形态是「4 条 ota 各 3MB」，两种表别混读** |

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
- 槽位数 / 槽大小还要与 `platform/partitions.csv` 的 `ota_N` 分区个数与大小一致（但 partitions.csv 本身**不能改**——硬件/平台布局由平台托管；⚠️ **别再写「固定为 4×3MB」**：那是 1.0 口径，2.0 是 16 条 `size=0` 动态槽）。
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

## 分区表 / 槽布局：IDF 到底管什么（2026-10-06 在 IDF v5.5.0 源码里逐条核实）

评估「能不能把 4×3MB 固定槽改成按固件大小动态分配」时推过一遍，结论可复用，**不用再翻 IDF 源码**：

| 事实 | 出处（`~/esp/esp-idf`） |
|---|---|
| 分区表校验**只有三条**：每项 `offset+size ≤ 芯片容量`、恰好一个 MD5 且校验和匹配、有一个终止项。**没有重叠检查 / 没有按偏移排序的要求 / 没有槽编号连续性的检查** | `bootloader_support/src/flash_partitions.c:14-77` |
| 表占 1 个扇区 `0x1000`，`MAX_LEN 0xC00`、每项 32 B ⇒ **最多 96 条**；槽数硬上限 **16** | `include/esp_flash_partitions.h:60-64` |
| 选槽**按 subtype**，不看地址：bootloader `bs->ota[subtype & 0x0F]`；设备侧 `esp_partition_find_first(APP, OTA_0+idx)` | `src/bootloader_utility.c:194`、`slot_manifest.c:68` |
| `ota_slot = (ota_seq−1) % app_count`，`app_count` = OTA app 条目**个数** | `bootloader_utility.c:195,364` |
| `esp_ota_set_boot_partition` 里 `SUB_TYPE_ID(subtype) >= ota_app_count` 即拒 | `app_update/esp_ota_ops.c:560-563` |
| 🔴 **没有「镜像必须小于所在分区」的检查**（只查 `part->size > 16MB`）⇒ 单应用模式 app 落 `0x10000` 实际能长到 ≈15.94 MiB（会物理覆盖 otadata + 全部 4 槽） | `src/esp_image_format.c:165` |
| 跨 16MB 取指需 `CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH`（实验特性，真机已验证可行） | `MULTI_APP_DYNAMIC_SLOTS.md` §12.7/§12.8 |

**两条设计红线**
- 🔴 **`ota_0..ota_{N−1}` 编号必须连续无洞**：有洞时 `ota_seq % app_count` 会取到**从未赋值的** `bs->ota[i]`
  （初值 offset=0）⇒ 去读 `0x0`（bootloader 自己）⇒ 启动失败。**这条只与编号有关，与偏移无关。**
- ✅ **槽的偏移与大小可以任意**（可不排序、可大小不一）⇒「动态尺寸」的机制基础就在这。

**改动分区表时必须同步的清单**（少一处就会"改了没效果"）

> 🔴 **别照清单人工核对 —— 跑一条命令**（2026-10-07 补，任务 #298）：
> 下面 1~3 项（尤其「7 份表逐字节同源」）**已经有会红的判据**，而且已经接进 git：
> 只要分期表/出厂资产进暂存区，`pre-commit` 就会跑同一套检查、不同源直接**阻断提交**。
> ```bash
> cd <平台仓>/cyb2-dev-site
> SDGOODS_WORKSPACE=<固件工作区根> IDF_PATH=$HOME/esp/esp-idf \
>   node scripts/check-partition-sources.mjs --strict      # 0 通过 / 1 真失败 / 3 无法判定
> bash scripts/install-git-hooks.sh --check                # 看 hook 装没装
> bash scripts/negctl-partition-hook.sh                    # 验证闸门本身还灵不灵（对照实验）
> ```
> ⚠️ **退出码 3 = 没跑，不是通过**（本机没有固件工作区时）。hook 在 3 下**放行但大声告警**
> —— 拦了只会让人学会 `--no-verify`，于是真正漂移那天也会被跳过。
> ⚠️ **覆盖边界**：固件侧 7 个工程里只有 4 个是 git 仓（LAUNCHER / -ESP32S3 / -gitee / -PLANE），
> **HELLOAPP / 2046 / CALC 不是 git 仓** ⇒ 它们永远不会通过 hook 被拦，只能靠上面这条命令手动跑。

1. 🔴 **设备侧 `slot_manifest.c:463` 的体积校验**：现在用**全局** `SDGOODS_SLOT_SAFE_BYTES`(2.9MB)
   ⇒ 不改成「`part->size − SDGOODS_SLOT_COVER_RESERVE_BYTES`」，**分区表改多大都装不进 >2.9MB 的 app**（最易漏）
2. 🔴 **共 7 份 `partitions.csv`**（1 真身 `SDGOODS_LAUNCHER/partitions.csv` + 6 派生
   `SDGOODS-ESP32S3` / `-ESP32S3-gitee` / `-PLANE` / `HELLOAPP` / `2046` / `CALC` 的
   `platform/partitions.csv`），**去注释后数据行必须逐字节同源**；连带的 6 份
   `platform/prebuilt/partition-table.bin` 必须逐字节等于平台资产。
   ⚠️ 2026-10-07 实测这条**已经漂过一次**（6 份派生停在 1.0、平台已是 2.0），**没人发现**
   ⇒ 现在由上面那条命令 + hook 判。
3. 平台 `system-assets/partition-table@0x8000.bin` **和** `pt-bak@0x0FF0000.bin`（两者必须同一个 md5；
   入库为 `SystemAsset(kind=PARTITION_TABLE)` / `OTHER`；
   有内容校验：必须含一个 `offset === 0x10000` 的 app 分区）
4. 平台封面地址**写死 3MiB 的三处**：`server/src/routes/firmwares.ts:55-58`（`COVER_SLOT_SIZE`/`COVER_KNOWN_SLOT_STARTS`）、
   `js/store.js:1749`、`js/flash-flow.js:119` ⇒ 槽大小一变就会**把封面写到别人槽里**
5. 平台体积口径：`firmwarePolicy.ts:60 SLOT_APP_MAX_BYTES` + 三处闸门
   （`devices.ts:2640` install / `:1061` 推送列表 / `:1305` INSTALL 指令）+ `docs.html`(+zh+en) + 6 份 `tools/pack_app.py`
6. 槽区总量是死数：`0x320000..0x0FF0000` = **12.8125 MiB**。
   ⚠️ 别用 `..0x1000000` 算成 12.875 MiB —— 差的 64 KiB 是 `pt_bak`（起点 `0x0FF0000`）那一段，槽区在它之前就结束。
   每个槽尾还要留 `0xE000`(56KiB) 封面 ⇒ 单应用上限 = 槽大小 − 56KiB。
   **突破 12.8125 MiB 只有两条路**：压 `appdata`（需换 bootloader）或缩 launcher。

**不用改的（好消息）**
- 封面读取：设备侧按 `part->size − RESERVE` **相对槽尾**算 ⇒ 任何槽大小都自适应（`slot_cover.c`）
- `js/esp-flasher.js:2572 deviceLayoutMatches()` 判据是 `factory@0x10000` + `appdata@0x1000000`
  ⇒ 只要这两个不动（动态槽正是如此），**「保数据刷机」选项不会失效**

**🔴 两套 `app_count` 计数口径不同 —— 「编号必须连续」的真正根因**
- bootloader：`bs->app_count` = **OTA 条目个数**（`bootloader_utility.c:193-196`）
- app 侧：`esp_ota_get_app_partition_count()` = **从 ota_0 起连续 while 数，遇到缺口就停**（`esp_ota_ops.c:542-550`）
⇒ 表里若有洞（`ota_0, ota_2`），两边得 2 与 1，`(seq−1) % n` 结果不同 ⇒ **启动到错误的槽**。编号连续时两者天然相等。

**🔴 分区表写坏 = 无限重启（无任何回退）**
- 校验失败 ⇒ `bootloader_utility.c:157-161 return false` ⇒ `bootloader_start.c:69-72 INVALID_INDEX` ⇒ `:52-54 bootloader_reset()`
- **只读 `0x8000` 一处**，没有备用偏移、没有降级 ⇒ 表写坏只能 USB 救援
- ⚠️ 所以「运行时重写分区表」必须配安全件。**IDF 留了官方弱钩子**：`bootloader_before_init()` / `bootloader_after_init()`
  （`components/bootloader/subproject/main/bootloader_hooks.h:30,36`，调用点 `bootloader_start.c:27,37`），
  工程内放 `bootloader_components/<comp>/` 即可覆盖，**不用 fork IDF 文件**；支持列表含 ESP32-S3。
  ✅ 用 `bootloader_after_init`（**早于**分区表解析 `:51`，且此时 flash 子系统/cache 已就绪）做「校验 + 从备份恢复」最合适；
  ❌ 别用 `bootloader_before_init`（cache 未开，只能用 ROM 级读写）。bootloader 体积余量：现 ≈20.6 KiB / 上限 32 KiB。

**空槽（`size = 0`）能不能用？——机制上能，但我们自己的解析器要先改**
- IDF 侧合法：verify 不查 `size > 0`；且 `try_load_partition` 对 `size == 0` 直接返回 false（`bootloader_utility.c:472-477`）
  ⇒ 零长分区**永远不会被误启动**，是做"预置空槽位"的理想表示
- 但我们有两处解析器把它当**终止判据**，**必须一起改**（各一行）：
  · 服务端 `cyb2-dev-site/server/src/lib/systemAssets.ts:324` `if (size === 0) break;`
  · 浏览器 `cyb2-dev-site/js/esp-flasher.js:2450` `if (!size || address + size > 0x10000000) break;`
  两处的上一行 `if (magic !== 0x50AA) break;` 已经能兜住「未编程区 0xFFFF」与「MD5 项 0xEBEB」，
  所以删掉 `size===0` 这条即可（浏览器端保留越界兜底）。**不改的症状：槽位凭空少一半且不报错。**
- 替代方案：空槽给非零最小值 4 KiB（代价：最多浪费 64 KiB；且设备侧遍历必须按 size 阈值过滤）

**🆕 预置槽项可免掉 otadata 重写（做动态槽时的关键选择）**
把 `ota_0..ota_{N−1}` **全部预置、只让 offset/size 动态** ⇒ `app_count` 恒定 ⇒ `(seq−1)%app_count` 映射不变
⇒ **改表不需要动 otadata**，也不会出现"表变了、otadata 没变 ⇒ 启动到别的 app"。代价：表项多占 32 B/条
（16 条 = 512 B，表上限 96 条，完全够）。
⚠️ **表里绝不能再出现 `app/test`(subtype=0x20)**：`esp_ota_get_app_partition_count()` 的循环用
`OTA_MIN + n` 索引，`0x10 + 16 = 0x20` 正好撞 TEST 的号，预置满 16 条槽时该函数会数到 16 再进循环
⇒ `assert(n < 16)` 失败 ⇒ **崩溃**（`esp_ota_ops.c:546` + `esp_flash_partitions.h:23`）。

## 与其它 Skill 的关系
- 改完配置 → `sdgoods-build-flash` 整编 + 烧录验证。
- 派生新工程时的改名 → 由 `sdgoods-new-app` 自动完成（见上「常见改法 1」）。
- 改中文文案不属于配置 → 走 `sdgoods-fonts`。
