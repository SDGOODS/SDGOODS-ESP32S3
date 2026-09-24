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
| `draw_bitmap FAILED 0x101` + 画面残缺/黑带 | 内部 DMA 弹跳缓冲预算不足（PSRAM 剩余空间不够，需腾出内部 DMA 缓冲） |
| lv_gif 分配失败 → 开机动画死循环重启 | `CONFIG_SPIRAM_USE_MALLOC` 没开（见 skill `sdgoods-build-flash`） |

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
   `build_*/bootloader/bootloader.bin`。曾有真实案例：市场官方装机包里的 bootloader 与
   本地所有编译产物**段数/字节数无一命中** ⇒ 是打包时混入了坏文件；而设备上的 ROM banner
   （`load:` 那几行的地址/长度）**与这份坏 bootloader 逐字吻合**，一眼就能把"设备当前跑的
   是谁"对上号。
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
