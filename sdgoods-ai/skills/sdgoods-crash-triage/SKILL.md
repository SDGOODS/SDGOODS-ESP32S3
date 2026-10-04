---
name: sdgoods-crash-triage
description: 定位谷仓次元屏（SDGOODS / ESP32-S3 + LVGL 8.3）固件的**间歇性崩溃 / 内存被写坏 / 刷完完全不启动**一类问题——不靠猜、不靠加日志刷屏，而是走「addr2line 定代码行 → 写只读探针 → 按变量做 N 轮复现率二分 → 做"控制组"排除无辜模块 → 改一处再测复现率」这条闭环。封装 addr2line 命令、LVGL 定时器链表探针、串口复现率测试骨架（session/reset/drain/send/alive）、以及三条最容易踩的方法论陷阱（探针没命中说明什么、判据假阴性、复位没生效）。含「读到 ROM banner 后彻底静默」的分层排除法（静默≠没启动 / 排除 otadata / 换 bootloader 做 A/B / 比对包内文件与编译产物）。Use when 崩溃/重启/Guru Meditation/InstructionFetchError/StoreProhibited/LoadProhibited/看门狗/内存被写坏/刷完起不来/串口无输出，或"功能偶发失效但看不出原因".
agent_created: true
---

# SDGOODS 固件崩溃定位（间歇性 / 内存被写坏）

## 何时用
- 串口日志出现 `Guru Meditation Error`、`InstructionFetchError`、`StoreProhibited`、
  `LoadProhibited`、`***ERROR*** A stack overflow`、看门狗复位、或**无任何日志的静默重启**。
- **刷完/上电后读到 ROM banner 就彻底没输出了**（连二级 bootloader 的日志都没有）
  —— 走 §5b 那条分层排除，别一上来就重刷。
- 「偶发」问题 > 直觉判断：**间歇性 bug 的第一件事是量化复现率**，不是读代码。

## 铁律（按顺序执行，别跳）
1. **先解析回溯，拿到"崩在哪一行"**，再决定方向。`PC` 落在哪一段是**最强的方向性证据**：
   - `0x42xxxxxx` = IROM 代码 → 真在代码里崩；
   - `0x4037xxxx` = IRAM 代码 → 同上；
   - `0x3cxxxxxx`（ESP32-S3） = **PSRAM，不是代码** ⇒ 一定是**函数指针/跳转目标被写坏**，
     方向立刻收敛到"某个结构体被踩"。
2. **写只读探针**验证"坏值是不是现成的"，见下「探针」。
3. **量化复现率 + 做控制组**（拆变量），见下「复现率二分」。
4. **改一处、再测复现率**：修复的判据是"N 轮全稳"，不是"看着像好了"。

---

## 1. addr2line（一行命令定代码位置）

```bash
# ELF 在构建目录下（换工程时把 <PROJECT> 换成实际工程名）
ELF="$(ls build*/<PROJECT>.elf | head -1)"
A2L="$IDF_TOOLS_PATH/tools/xtensa-esp-elf/esp-*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line"
"$A2L" -pfiaC -e "$ELF" 0x42077245 0x420772f2 0x42017db8 0x82077248
```
- 工具链位置：`source "$HOME/esp/esp-idf/export.sh"` 后 `$IDF_TOOLS_PATH` 即 `.espressif`；
  也可 `find ~/.espressif -name 'xtensa-esp32s3-elf-addr2line'` 直接定位。
- 把 `Backtrace:` 那一串 **和** `A0`（返回地址）一起喂进去；返回值带 `file:line` 就是答案。
- ⚠️ **ELF 必须与设备上跑的固件同一次编译**：`ELF file SHA256` 要对得上日志里的那行。
  改完代码重编再复现，旧回溯就失效了。
- ⚠️ 地址带 `0x8…` 高位（如 `0x82077248`）时 addr2line 可能解析失败——去掉最高位
  （`0x42077248`）再试。

## 2. 只读探针（回答"坏值是不是现成的"）

在**可疑循环的入口**加一段只读校验，把"待会儿会用到的东西"提前体检一遍。
LVGL 定时器链表的现成探针（放在 LVGL 主循环里 `lv_timer_handler()` **之前**）：

```c
static void lvgl_timer_probe(void) {
    static const lv_timer_t *reported = NULL;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) {
        const uintptr_t cb = (uintptr_t)t->timer_cb;
        if (cb < 0x40000000UL || cb > 0x44000000UL) {   /* 代码只可能在 IROM/IRAM */
            if (reported == t) return;                  /* 同一颗只报一次 */
            reported = t;
            ESP_LOGE("probe", "BAD TIMER %p cb=%p period=%u repeat=%d user_data=%p",
                     t, t->timer_cb, (unsigned)t->period, (int)t->repeat_count, t->user_data);
        }
    }
}
```
`period` / `user_data` 能**指认归属**（30ms=触摸读、100ms=tap 换屏看门狗、150/100ms=截屏、
5000ms=主页电量、800ms=控制中心存盘…），比"某个定时器坏了"有用得多。
`lv_timer_t` 的字段在 `lv_timer.h` 里是**公开**的（`period/last_run/timer_cb/user_data/repeat_count/paused`）。

> 🔑 **探针"没命中"本身就是结论**，别以为写错了：
> - 探针在 `X` 之前跑且从未报警 ⇒ `X` 之前一切正常；
> - 崩在 `X` 里面，且探针就在 `X` 之前 ⇒ **坏值是 `X` 执行期间产生的**（或由并发写者在此期间写坏）。
> 这条推理把"很久以前就坏了"直接排除掉，通常立刻缩掉一半可能性。

## 3. 复现率二分（间歇性 bug 的核心手段）

模板脚本：`scripts/crash_triage_harness.py`（本 skill 自带，直接改 `CASES`）。
核心结构**只有三点**：

```python
s = Session()          # 开串口 + 复位 + drain + send + alive()
s.reset()              # 每轮跑之前复位，保证同一初始状态
run_case(...)          # 跑 N 轮，任一轮出现 Guru/Rebooting 就计一次崩溃并停
```

- **一次开串口跑完 N 轮**：反复开关串口会诱发幻触（USB-Serial-JTAG 老问题），也会让"复位"变得不可靠。
- **判崩溃**只看两个串：`Guru Meditation` / `Rebooting...`（`boot: ESP-IDF` 也可当重启证据）。
- **控制组纪律**（本轮真正找到根因的关键）：
  1. 列出"必须同时出现才崩"的候选因子，**一次只留一个变量**，各跑 N 轮；
  2. 再做一个**"把可疑模块换成已知安全的等价实现"**的实验（例如让新路径调用与老路径
     **完全同一个函数、同一个参数**）——崩 ⇒ 可疑模块**无罪**，锅在它周围的上下文。
     这一步非常省时间，别省。
- 结论写法：「A 稳定 / B 稳定 / A+B 崩 ⇒ 差异在 X」。

## 4. 串口验收脚本的假阴性（高频坑）

判据脚本里**只允许一个 `recv()` 出口**：任何"等 marker"的 `wait_for()` 内部读到的字节
**必须同时追加进日志缓冲**。否则它把关键日志吃掉了，判据集体假阴性
（典型症状：刚写完的功能 4 项判据"失败"，其实全绿）。
同理：**判据脚本要先自证"设备处于预期初始状态"**——起手复位 + 截屏/日志确认，
别默认复位成功（DTR 脉冲会间歇性失效，见 skill `sdgoods-build-flash`）。

## 5. 本工程已知的真凶（别再重复排查）

| 症状 | 根因 |
|---|---|
| `InstructionFetchError`，PC 在 PSRAM，addr2line → `lv_timer.c:313` | **跨任务 `lv_async_call`** 与 LVGL 线程同时改那条无锁定时器链表。改用「volatile 环形队列 + LVGL 线程 poll」 |
| `***ERROR*** A stack overflow in task bsp_console` | console RX 任务栈只有 4KB，在里面同步建 LVGL 浮层。改成投递到 LVGL 线程 |
| **`assert failed: block already marked as free`**（`tlsf_free` tlsf.c:630）+ 随后 `rst:0xc (RTC_SW_CPU_RST)`，栈里有 `prvDeleteTCB ← vPortFree ← heap_caps_free ← multi_heap_free_impl ← tlsf_free` | **某任务栈越界写坏了相邻堆块的 tlsf 头**，只在任务自删（`vTaskDelete(NULL)`）释放栈块时暴露。**完整判据/量法/修法见 §5d** |
| `draw_bitmap FAILED 0x101` + 画面残缺/黑带 | 内部 DMA 弹跳缓冲预算不足 —— **完整判据与修法见 §5c** |
| lv_gif 分配失败 → 开机动画死循环重启 | `CONFIG_SPIRAM_USE_MALLOC` 没开（见 skill `sdgoods-build-flash`） |

## 5c. 「看不到主页 / 画面不动」= 整屏 flush 拿不到内部 DMA（2026-09-29 定案）

症状三连，**必须同时成立才是这个 bug**：
1. 用户说「看不到主页 / 画面停在某一帧 / 安装应用时显示也有问题」；
2. 串口有 `lcd: draw_bitmap FAILED x:0 y:0 w:360 h:360 err=0x101 ext=1 bytes=259200`
   + 紧跟 `lcd: internal DMA largest_free=N bytes free=M bytes`；
3. **截图完全正常** —— ⚠️ 截屏走的是 `lv_snapshot_take_to_buf`（LVGL 帧缓冲快照），
   **不是面板回读** ⇒ **截图证明不了面板在显示**。别拿截图正常去否定这条。

### 机理（一句话）
PSRAM 里的全屏绘制缓冲经 `spi_master` 发出去时，**每个在途事务**都要一块内部 DMA
弹跳缓冲，大小 = `spi_bus_get_max_transaction_len()` = **4092B**（`LCD_SPI_MAX_TRANSFER_SIZE`
写 512 也没用 —— 描述符粒度向上取整到 4092，缩宏是无效动作）。
`trans_queue_depth=2` ⇒ **需 ≥8192B 连续内部 DMA**。拿不到 ⇒ 分片循环 `break`
⇒ 整帧被丢 ⇒ 面板停在上一帧。

### 🔴 三条判据纪律（本轮踩出来的）
1. **只看 largest（最大连续），不看 free。** 实测 `free=6139B` 但 `largest=2304B`
   ⇒ 照样全失败。
2. **必须按时间序列采样，不能只看开机那一刻。** 只看 `sdgoods_lvgl_init()` 会看到
   `largest=22528B` 得出「余量充足」的**错误结论**；真凶在 1.2s~6.2s 之间吃掉它。
   手法：`heap_probe(tag)`（里程碑）+ 每 500ms 的 `heap_watch_task`（时序）。
   ⚠️ 采样任务**栈 ≥4096**：2048 实测在 GIF 解码最紧那一刻 `A stack overflow in task
   heap_watch` —— 诊断任务自己把设备搞崩。
3. **冷启动 ≠ 软复位，测错路径会判成「不复现」。**
   - `read_boot.py` 发的 `'R'` = `esp_restart()` ⇒ `ESP_RST_SW` ⇒ `sdgoods_boot_show()`
     走**快路径**（无开机动画，主页 ~1.4s 建好，此时 Wi-Fi 还没占内存）⇒ **看起来一切正常**；
   - **上电 / 硬复位**（`rst:0x15 USB_UART_CHIP_RESET`）⇒ 走**开机动画路径**，
     `sdgoods_boot_show()` 阻塞 4s 才建主页（~5.8s），Wi-Fi 已连上、内存已被吃掉
     ⇒ 首帧就失败。**用户口中的「刷完机器第一次启动」= 这条。**
   - 硬复位手法：pyserial **DTR 脉冲**（`DTR False→True→False`，RTS 脉冲无效），
     串口全程不关 ⇒ 不漏 bootloader 头。脚本 `tools/verify_shots/devpage/read_cold.py`。

### 真凶与修法（启动器）
`sdgoods_ble_init()` 在**启动器**里是纯白养：全工程无一处调 `sdgoods_ble_list()`
（BLE 扫描页只在开源 app 工程 `SDGOODS-ESP32S3/main/apps/ui_home.c`；启动器是宿主固件，
进 app 后跑的是那个 app 自己的固件）。它**一口吃掉 60KB 内部 DMA**：
```
after-wifi-init  largest=32768 free= 98799
after-ble-init   largest=26624 free= 38163   ← −60KB
6.0s (Wi-Fi 连上后)          largest= 7168   ← <8192 ⇒ 每帧失败
```
⇒ 删掉启动器的 `sdgoods_ble_init()`（`sdgoods_ble_list()` 内部本就是懒初始化，不会坑后来人）。
修后 6.0s 稳在 **largest=31744**，十轮整屏重绘压力 **零 `draw_bitmap FAILED`**，
固件还小 432KB。**同根因的连带症状**：冷启动 `E sd_cmd: cannot create command task`
（8KB 内部栈分不出来）也一并消失。

### 排查顺序（固定）
1. 先抓**冷启动**日志，看 `internal DMA largest_free` 是否 ≥8192（队列 2）；
2. 不够 ⇒ 查 Wi-Fi 缓冲档位（`CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=3` /
   `DYNAMIC_RX_BUFFER_NUM=16` / `DYNAMIC_TX_BUFFER_NUM=16`，**根 `sdkconfig` 与实际生效的那份都要改**）；
3. 还不够 ⇒ 查**启动器里是否白养了 BLE**（本节）；
4. 最后才考虑把 `LCD_SPI_TRANS_QUEUE_SZ` 降到 1（需求减半到 4096B）。
   **缩 `LCD_SPI_MAX_TRANSFER_SIZE` 永远无效。**

### 回归脚本（已留仓）
- `tools/verify_shots/devpage/read_cold.py` —— DTR 硬复位 + 边读边等（冷启动路径）。
- `tools/verify_shots/devpage/read_stress.py` —— 冷启动 + N 轮整屏重绘，只报错误/诊断行。
  判据：`(无 —— 本次压力下显示链路干净)`。

## 5d. `tlsf_free: block already marked as free` = **某个任务栈越界**（2026-10-04 定案）

### 指纹（认准这一串，别去查业务逻辑）
```
assert failed: block already marked as free      (tlsf_free tlsf.c:630)
Backtrace: panic_abort <- esp_system_abort <- __assert_func <- tlsf_free
           <- multi_heap_free_impl <- heap_caps_free <- vPortFree
           <- prvDeleteTCB <- prvCheckTasksWaitingTermination (IDLE)
rst:0xc (RTC_SW_CPU_RST)
```
机理：栈**向下**增长，越界是写到**栈块下方相邻堆块**去，只把它的 **tlsf 块头** 写坏
（`!block_is_free(block)` 断言失败）。只有当**那块后来被 free/alloc**时才炸 ——
最常见的就是**任务自删（`vTaskDelete(NULL)`）时 `prvDeleteTCB` 释放自己的栈块**。

### 🔴🔴 最重要的一条方法学：**"吃多少栈"是「任务」的属性，不是「函数」的**
所以**不能**用"新起一个临时任务调同一个函数"来量真机栈 —— 临时任务少一层调用帧，
量出来的数**偏小**，会得出"够用"的**错结论**。
2026-10-04 实测同一个 `ui_push_install_remote()`：

| 量的对象 | 栈 | hwm 读数 | 峰值 |
|---|---|---|---|
| 临时调试任务 | 12288 | `4036 B free` | 8252 B ← **偏小，会误判** |
| 真机常驻任务 `sd_cmd` | 10240 | `1628 B free` | **8612 B** ← 真值 |

⇒ 真机原来 8192 是 **−420 B（负数）**：**每次远程安装都在悄悄越界**，
但**偶发崩溃、不是必崩**（要等相邻块被回收），所以用户只报功能异常、从不报崩溃。

### 量法（可复制的动作）
1. 给**真机那个任务**加一条常驻日志：`uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)`，
   打印 `N B free of <本任务栈常量>B`。
   ⚠️ 要放在**最深那段之后**（例如回执 TLS 之后），hwm 是运行期最小值、越晚读越准。
2. 想在无人值守下触发真机路径 ⇒ 加一个"把指令**投进真机任务自己的队列**"的串口入口
   （本项目：`{cm<slot>,<idx>!` → `sdgoods_cmd_debug_install_now()` → `sdgoods_cmd_submit()`）。
   **只加"新起临时任务"的入口（`{ri`）量不出真机数**，这是关键区别。
3. 加完重编烧录跑一次，把 `N` 与峰值记进注释。

### 两条硬约束（都会让你白改一轮）
- 🔴 **内部堆紧张时"加栈"本身会失败**：`xTaskCreatePinnedToCore(…, 16384, …)` 直接返回失败
  （栈走**内部 RAM**，要一块**连续**的内部块；下载中 `heap_int≈33 KB` 时给不出 16 KB）。
  ⇒ 别一次翻倍，先试 12288 这类"够但不大"的值，并在失败分支把
  `heap_caps_get_free_size / get_largest_free_block(MALLOC_CAP_INTERNAL)` 打出来。
- 🔴 **别无限加栈**：常驻任务每多 1 KB 都在挤压 TLS 等需要内部堆的东西。
  余量不够优先**把大局部数组挪出栈**（本项目 `install_run` 的 `url[512]` 等），
  其次才是加栈。加 2 KB 换来"从负数到 +1628 B"是划算的；再加就该换思路。

## 5b. 特例：设备**完全**不启动（读到 11 行就没了）

症状：串口读到 ROM banner（`ESP-ROM:…` + 4 行 `load:` + `entry 0x…`）之后**彻底静默**，
多次复位可复现。⚠️ **别急着说"设备砖了"、也别急着刷回去** —— 先按下面四步排除，
尤其第 1 步（静默 ≠ 没启动）：

1. **确认"静默"等于"没启动"**：`CONFIG_ESP_CONSOLE_*` 决定日志走哪。
   本设备是 **UART0 主 + USB-Serial-JTAG 副**，而 `bootloader_console_init()`
   在 UART 主分支下**不装副通道** —— 但**实测二级 bootloader 的日志确实出现在 USB 上**
   （比较不同工程的 `sdkconfig` 即可知道有没有差别）。**没有差别时，静默就是真没启动**；
   有差别（比如某工程改成 UART-only）则要接到 UART0 才能下结论。
2. **先排除 otadata，再怀疑 bootloader**（成本从低到高）：
   - 读 `0xf000` 两段各 32B：`entry = ota_seq(4) + seq_label[20] + ota_state(4) + crc(4)`，
     `crc == zlib.crc32(pack('<I',seq), 0xFFFFFFFF)`（**`^0xFFFFFFFF` 是错的**）。
     两条都无效且分区表**没有 factory** 时，IDF 会「试 OTA 0」（源码 `bootloader_utility.c`
     里那条 `No factory image, trying OTA 0`）。所以**先把一条有效 otadata 写进 0xf000 试**：
     起来了 ⇒ 是缺 otadata；**仍然静默 ⇒ otadata 不是原因**（别在这里反复试）。
   - 关键：`ota_state == 0xFFFFFFFF`（UNDEFINED）**不算 invalid**，不要被它误导。
3. **换 bootloader 做 A/B**（一步定生死）：把**已知能启动**的那份 bootloader 写到 `0x0`，
   **其余一个字不动**。起来了 ⇒ 就是 bootloader；还静默 ⇒ 往分区表/时钟/闪存配置查。
4. **比对"包里的文件"与"本地编译产物"**：`cmp` 装机包里的 `bootloader.bin` 与各
   `build_*/bootloader/bootloader.bin`。曾有真实案例（2026-09-21）：市场官方装机包里的
   bootloader 只有 **13792 B / 4 段（含一段仅 4 字节）**，而本地**全部**编译产物都是
   **19744 B / 3 段**、**无一命中** ⇒ 是打包时混入了坏文件；而设备上的 ROM banner
   （`load:` 那几行的地址/长度）**与这份坏 bootloader 逐字吻合**，一眼就能把"设备当前跑的
   是谁"对上号。
   💡 **尺寸 + 段数**是最快的指纹：正常 bootloader 是 **3 段**；出现 **4 段**（尤其带一个
   4 字节的碎段）基本可判坏文件。
   💡 **ROM banner 的 `load:addr,len` 就是 bootloader 的段表** —— 把它抄下来跟候选文件比，
   比反复刷机快得多。
5. 诊断期间用 **`esptool.py` 直读芯片**（`--baud 921600` 很稳）：读 `0x0` 看 `0xe9`、
   `0x8000` 看 `0x50AA`、`0xf000` 看 otadata、app 分区看 `esp_app_desc_t`（app 镜像头是
   **24 字节**，段表从 0x18 起，desc 在段 0 的 0x20 处）。比浏览器侧快一个数量级，
   且不会互相抢串口。
6. 动设备前**先备份**：`read_flash 0x20000 0x600000`（应用区）与 `read_flash 0x0 0x20000`
   （引导层）存到非临时目录（`/tmp` 会被清掉）。

## 6. 收尾（别忘）
- **诊断代码（探针、临时命令、临时 async 结构）必须删干净**，删完重编一次确认没有任何
  `defined but not used` 警告残留——那种警告常常就是"上一条 Edit 没落盘"或"忘了删"的信号。
- 把根因写进项目记忆与对应 skill；**如果旧记忆里的口径被这次结论推翻了，要改掉旧句子**，
  别只追加新句子（曾推翻过"跨任务一律走 lv_async_call"这条旧口径）。
