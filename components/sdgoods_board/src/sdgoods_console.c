/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（板级支持包 BSP）
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于平台层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

/**
 * @file sdgoods_console.c
 * @brief BSP 串口控制台：常驻命令分发。
 *
 * 直接轮询 USB-Serial-JTAG 的 RX FIFO 实现命令触发（不装 usb_serial_jtag driver，
 * 因为该外设被次级 console 占用，driver_install 会被拒；但 RX/TX FIFO 由我们自己
 * 读写是安全的）。本任务只做命令分发，真正的截屏抓帧在 LVGL 线程完成。
 */

#include "sdgoods_console.h"
#include "sdgoods_caps.h"
#include "sdgoods_hooks.h"        /* 槽清单变化通知（注入成功 / 槽满 → 应用层重建 UI） */
#include "sdgoods_screenshot.h"   /* sdgoods_screenshot_capture（仅在能力启用时调用） */
#include "sdgoods_tap.h"          /* sdgoods_tap_synth：弱默认 ext_cmd 的平台内置手势自检 */
#include "sdgoods_lcd.h"          /* LCD_WIDTH / LCD_HEIGHT：自检手势的默认起手点 */
#include "sdgoods_power.h"        /* sdgoods_power_suspend_toggle / sdgoods_power_off_preview：电源键调试 */
#include "sdgoods_input.h"        /* sdgoods_power_key_short_action：调试键 'P' 走真实短按路径 */

#include "esp_err.h"             /* esp_err_t：模式闸门自测（'r' / 'L'）的返回类型 */
#include "esp_log.h"
#include "esp_rom_sys.h"         /* esp_rom_delay_us：二进制注入的空闲短忙等 */
#include "esp_system.h"          /* esp_restart：串口重启命令 'R' */
#include "esp_timer.h"           /* esp_timer_get_time：注入超时判定 */
#include <stdarg.h>              /* va_list：bin_reply 的可变参数回传 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 槽安装 API（流式）——**弱引用**：BSP 不依赖 sdgoods_launcher 组件（分层：BSP 不 include
 * 业务/应用层），未链接时这些符号为 NULL（下面判空后再调）。 */
extern esp_err_t sdgoods_launcher_install_begin(int slot_idx, size_t total_len,
                                               const char *app_id, const char *version,
                                               const uint8_t sha256[32]) __attribute__((weak));
extern esp_err_t sdgoods_launcher_install_write(const void *data, size_t len) __attribute__((weak));
extern esp_err_t sdgoods_launcher_install_end(void) __attribute__((weak));
/* 自动挑空闲槽（头部里 slot 写 -1 时用）：只读查询，任何固件都能调。 */
extern int sdgoods_launcher_find_free_slot(void) __attribute__((weak));

/* 模式语义自测用的**弱引用**：BSP 不依赖 sdgoods_launcher 组件（分层：BSP 不 include
 * 业务/应用层），但任何 app 都会链接它（控制中心就在那个组件里），所以这里用弱符号引用，
 * 未链接时地址为 NULL（下面判空后再调）。这样 'r' / 'L' 两个自测命令**零 app 代码**可用。 */
extern esp_err_t sdgoods_return_to_launcher(void) __attribute__((weak));
extern esp_err_t sdgoods_launcher_launch_slot(int idx) __attribute__((weak));
/* 控制中心二级页直开（'C'/'D'/'B'）：app 里**没有别的办法**点到 CC 的 Data 按钮，
 * 而数据页的显示内容需要按模式给真值（SINGLE 显示 App 名、MULTI 显示 Slot），
 * 所以「离线核验非首屏」这条能力必须暴露到串口上。 */
extern void sdgoods_cc_debug_open(int which) __attribute__((weak));

/* 关闭控制台 VFS 对 TX 的 CRLF 转换：截屏二进制里的 0x0A 不能被膨胀成 0x0D 0x0A，
 * 否则破坏帧边界、挤掉 JPEG EOI。改为 LF 即「不修改」，二进制才能逐字节对齐。 */
#include "esp_vfs_common.h"                       /* esp_line_endings_t / ESP_LINE_ENDINGS_LF */
void usb_serial_jtag_vfs_set_tx_line_endings(esp_line_endings_t mode);
#include "hal/usb_serial_jtag_ll.h"

static const char *TAG = "bsp-console";

/* 诊断扩展点：应用层（如启动器）可覆盖此弱符号，注册额外串口命令。
 * 不破坏平台/应用分层（BSP 不 include 应用层）。仅用于调试。
 *
 * ★ 弱默认实现 = **平台内置自检**，两类能力：
 *   ① 手势自检：'1'..'5' 接到 sdgoods_tap_synth()，于是任何 app（哪怕一行调试代码都没写）
 *      都能在串口上验证手势判别是否正确。为什么放进平台层：手势行为没法用肉眼远程观察，
 *      而「点按 vs 滑动」是最容易写错、也最难回归的一类逻辑；让固件自己跑一遍 + 看日志断言，
 *      是唯一可靠手段。
 *   ② 模式闸门自检：'r' 请求返回启动器、'L' 请求启动槽 0（见各自的 case 注释）。
 *      单应用固件自更新后误判模式会表现为「刚更新的固件被退回去」，屏上看不出区别，
 *      同样只能靠日志断言。
 *   ③ 非首屏直开：'C'/'D'/'B' 直接打开控制中心一/二/三级页 —— 截屏只能拿当前那一屏，
 *      而 CC 二级页要先开 CC 再点中按钮，app 侧没有能做到的合成手势。
 *   启动器覆盖了本符号（main/launcher_main.c，含 A..H / j / k / 6..9 等更细的用例、
 *   它自己的 'r' 与小写 c/d/b），所以启动器行为不受本默认实现影响。 */
__attribute__((weak)) void sdgoods_console_ext_cmd(char c)
{
    switch (c) {
    case '0':   /* 点按下排按钮位 (180,218)：控制中心第 5 个按钮 = Power | Exit。
                 * 验「Exit 真能回启动器」就靠它 —— app 里没有别的办法点这个按钮。 */
        sdgoods_tap_synth(LCD_WIDTH / 2, 218, 0, 0, 1);
        break;
    case '1':   /* 点按屏中央：验证按钮命中 / 点按判定 */
        sdgoods_tap_synth(LCD_WIDTH / 2, LCD_HEIGHT / 2, 0, 0, 1);
        break;
    case '2':   /* 顶部下划 75px：打开控制中心（app 外壳手势） */
        sdgoods_tap_synth(LCD_WIDTH / 2, 40, 0, 25, 3);
        break;
    case '3':   /* 底部上划 75px：返回主页 / 关闭系统浮层 */
        sdgoods_tap_synth(LCD_WIDTH / 2, LCD_HEIGHT - 40, 0, -25, 3);
        break;
    case '4':   /* 左缘右滑 75px：返回上级 */
        sdgoods_tap_synth(20, LCD_HEIGHT / 2, 25, 0, 3);
        break;
    case '5':   /* 起手后滑走（上划 75px）：验证「滑动掠过不算点按」 */
        sdgoods_tap_synth(70, 157, 0, -25, 3);
        break;
    case 'r':   /* 请求「返回启动器」——验证**单/多应用模式闸门**（2026-09-19 新增）。
                 * 期望行为按模式分（看下面的日志字符串判断走了哪条）：
                 *   · 被启动器管理的 app（运行 ota_N 且 factory 里是启动器）
                 *     ⇒ 真把下次启动指回 factory 并重启，串口出现
                 *       `Loaded app from partition at offset 0x10000`（即回到启动器）；
                 *   · **单应用固件**（含自更新后运行在 ota_N 的那种）/ 启动器宿主
                 *     ⇒ 打印 `return-to-launcher refused: ...` 且**绝不重启**（这是重点）。
                 * 之所以放进平台层：单应用固件自更新后误判模式会表现为「刚更新的固件被退回去」，
                 * 这种错没法用肉眼在屏上区分，只能让固件自己跑一遍 + 看日志断言。 */
        if (!sdgoods_return_to_launcher) {
            ESP_LOGW(TAG, "'r': launcher component not linked, no-op");
            break;
        }
        ESP_LOGI(TAG, "'r': requesting return-to-launcher (expect reboot if managed app, refusal otherwise)");
        ESP_LOGI(TAG, "'r': sdgoods_return_to_launcher() -> %d (0=OK, 0x103=INVALID_STATE/refused)",
                 (int)sdgoods_return_to_launcher());
        break;
    case 'L':   /* 请求「启动槽 0」——验证**槽管理权限闸门**（2026-09-19 新增）。
                 * 只有启动器宿主该成功；app 里应打印 `launch_slot refused: only the launcher host...`
                 * 且不重启（若 app 能自己切槽，就等于把 otadata 的簿记权交出去了）。
                 * 启动器里用 'l' 启动 slot 0（那条是宿主自己的正路）。 */
        if (!sdgoods_launcher_launch_slot) {
            ESP_LOGW(TAG, "'L': launcher component not linked, no-op");
            break;
        }
        ESP_LOGI(TAG, "'L': requesting launch_slot(0) (expect refusal unless launcher host)");
        ESP_LOGI(TAG, "'L': sdgoods_launcher_launch_slot(0) -> %d (0=OK, 0x103=INVALID_STATE/refused)",
                 (int)sdgoods_launcher_launch_slot(0));
        break;
    /* 'C' / 'D' / 'B'：直接打开控制中心的一级页 / 数据页 / 电量页（离线核验非首屏）。
     * 为什么需要它：截屏只能拿到**当前那一屏**，而 CC 的二级页要先「下划开 CC」再
     * 点中对应按钮 —— app 里没有能点到 Data 按钮的合成手势（'0' 只落在下排按钮位）。
     * ⚠️ 实现内部会**先 close 再 open**，否则 cc_open 见已有浮层会直接返回，
     *   截到的还是上一页。启动器自己也有一套（小写 c/d/b，见 main/launcher_main.c）。 */
    case 'C':
    case 'D':
    case 'B': {
        if (!sdgoods_cc_debug_open) {
            ESP_LOGW(TAG, "'%c': launcher component not linked, no-op", c);
            break;
        }
        const int which = (c == 'C') ? 0 : (c == 'D') ? 1 : 2;
        ESP_LOGI(TAG, "'%c': opening control center page %d (0=main 1=data 2=battery)", c, which);
        sdgoods_cc_debug_open(which);
        break;
    }
    case 'h':   /* 合成点按「控制中心 · Home」键（小鸟上下文位于 (276,169)）：验证「点 Home 返回主页」。
                 * 仅当 CC 已开且处于小鸟上下文时生效；标准上下文该位置无按钮，属安全空操作。 */
        ESP_LOGI(TAG, "'h': synth tap on CC Home button (276,169)");
        sdgoods_tap_synth(276, 169, 0, 0, 1);
        break;
    case 'p':   /* 调试：模拟「主页短按电源键」的生产路径 = 熄屏 + 进入深度睡眠
                 *（最低功耗；再按电源键唤醒 = 冷启动直回主页，不播开机动画）。
                 * 函数只碰背光 + gpio hold + esp_deep_sleep_start，不碰 LVGL 对象树，
                 * 可在 console 任务直接调。深睡后串口断开，需按电源键唤醒后串口才重新枚举。 */
        ESP_LOGI(TAG, "'p': enter deep sleep (simulate home short power press)");
        sdgoods_power_enter_deep_sleep();
        break;
    case 'P':   /* 调试：模拟电源键「短按」的完整生产路径（与真实松手沿共用
                 * sdgoods_power_key_short_action()：钩子 → 菜单 → 应用内分级 →
                 * 应用主页（被管理 app 重启回启动器）/ 深睡）。串口验证电源键
                 * 分级导航用，不需要真手指按键。 */
        ESP_LOGI(TAG, "'P': power key short-press action (production path)");
        sdgoods_power_key_short_action();
        break;
    case 'Z':   /* 调试：画出「Power Off」关机画面（与真实关机同款视觉，但不切电），供串口截屏核验。
                 * 必须经 sdgoods_lvgl_post 在 LVGL 线程画，否则乱摸 LVGL 对象树。 */
        ESP_LOGI(TAG, "'Z': preview Power Off screen (no power cut)");
        sdgoods_power_off_preview();
        break;
    default:
        break;
    }
}

/* ================== 串口二进制注入：本地安装通道（调试专用） ==================
 *
 * 为什么需要它（2026-09-19）：动态插槽的**安装链路**（`install_begin/write/end` 流式写 + 精
 * 确擦除 + UPDATING 状态机 + 失败回滚）在设备侧已经落地，但在「设备侧联网」（设计稿 §11 行
 * 10）做完之前，它在真机上**没有任何调用者** —— 平台下发的路径还没有，esptool 手刷又绕过了
 * 这套 API（走的是 `self_check()` 收编）。于是「装 / 卸 / 槽满」三件事没法在真机上完整验证。
 * 本通道用串口把 app.bin 推给设备，让链路有一个真实调用者；它与截屏回传是对称的
 * （文本头 + 裸字节），并且**不需要任何网络**。
 *
 * 协议（主机 → 设备）
 * ------------------
 *   ① 发一个字符 `'X'`                      ⇒ 设备进入二进制接收模式
 *   ② 发一行  `===SLOT-BEGIN <slot> <len> <sha256hex|->`  （以 `\n` 结束）
 *        slot = 槽号；**`-1` = 让设备自己挑空闲槽**（挑不到回 `err=slots-full`，
 *               并发槽事件 `SDGOODS_SLOT_EV_SLOTS_FULL` ⇒ 启动器弹「选一个删掉」
 *               对话框，即设计稿决策 #5 的真实触发点）；
 *        len = 字节数（必须与 bin 实际大小一致）；
 *        sha256hex = 64 位十六进制小写，或 `-` 表示不校验完整性。
 *
 * 收满写完后还会发一次 `SDGOODS_SLOT_EV_INSTALLED`：清单变了，应用层据此重建主页
 * （否则新装的 app 要等重启才会出现在图标网格里 —— 实测过）。两个事件都定义在
 * sdgoods_hooks.h §4，回调发生在**本 console 任务**里，实现方自己投递到 LVGL 线程。
 *   ③ 紧接着发 <len> 个**原始字节**（就是不带地址的 app.bin 本身）
 *
 * 应答（设备 → 主机，普通日志行，可与其他日志交错）
 * ----------------------------------------------
 *   `SLOT-PUSH:ready ...` / `SLOT-PUSH:begin slot=N bytes=N` / `SLOT-PUSH:ok slot=N bytes=N`
 *   `SLOT-PUSH:fail slot=N written=N/M err=<reason>`
 * 主机只需在读到 `SLOT-PUSH:ok` / `SLOT-PUSH:fail` 后收尾。
 *
 * 三条设计约定
 * ----------
 * · **长度驱动，不用结束标记**：设备已知 len，收满即止 ⇒ 少了「结束标记被日志/噪声污染」
 *   这一类失败模式，也不会因为多发一个字节而错位。
 * · **超时即回滚**：HDR/BODY 阶段连续 15s 没收到字节 ⇒ 视为主机中断，走 `install_end()`
 *   回滚（擦净该槽 + manifest 置回 EMPTY）。否则半截镜像会永久占着槽，还会让
 *   `find_free_slot()` 一直跳过它。
 * · **不校验调用方身份**：写槽的闸门在 `install_begin` 内部（只有启动器宿主放行），
 *   本通道只负责搬运字节 —— 非宿主固件会拿到 `SLOT-PUSH:fail ... err=ESP_ERR_INVALID_STATE`。
 *
 * ⚠️ 本通道占用串口字符 `'X'`（仅大写；小写 'x' 不受影响，仍交给应用层扩展命令）。
 * ⚠️ 接收缓冲是**文件作用域静态数组**：console 任务栈仅 3KB，放不下 2KB 的缓冲。
 */
#define BIN_HDR_MAX          160     /* 头部行最大长度（含结尾 '\0' 的位置） */
#define BIN_RX_BUF_SZ        2048    /* 单次从 FIFO 抽出的字节数（= 一次 install_write 的量） */
#define BIN_IDLE_TIMEOUT_MS  15000   /* 空闲超时：主机中途挂掉时回滚 */

typedef enum { BIN_OFF = 0, BIN_HDR, BIN_BODY } bin_state_t;

static bin_state_t s_bin_st    = BIN_OFF;
static char        s_bin_hdr[BIN_HDR_MAX];
static size_t      s_bin_hdr_len = 0;
static int         s_bin_slot  = -1;
static size_t      s_bin_total = 0;
static size_t      s_bin_left  = 0;
static uint8_t     s_bin_sha[32];
static int64_t     s_bin_last_us = 0;
static uint8_t     s_bin_rx[BIN_RX_BUF_SZ];

static void bin_reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void bin_reply(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static void bin_off(void)
{
    s_bin_st     = BIN_OFF;
    s_bin_slot   = -1;
    s_bin_total  = 0;
    s_bin_left   = 0;
    s_bin_hdr_len = 0;
}

/* 'X'：进入接收模式（先校验组件是否链接上） */
static void bin_enter(void)
{
    if (!sdgoods_launcher_install_begin || !sdgoods_launcher_install_write ||
        !sdgoods_launcher_install_end) {
        bin_reply("SLOT-PUSH:fail err=launcher-not-linked\n");
        return;
    }
    bin_off();
    s_bin_st      = BIN_HDR;
    s_bin_last_us = esp_timer_get_time();
    bin_reply("SLOT-PUSH:ready (send '===SLOT-BEGIN <slot> <len> <sha256hex|->\\n' then raw bytes)\n");
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hex32(const char *s, uint8_t out[32])
{
    if (strlen(s) != 64) {
        return false;
    }
    for (int i = 0; i < 32; i++) {
        int hi = hexval(s[2 * i]);
        int lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/* 失败收尾：BODY 阶段必须回滚（`install_end()` 未收满会擦净该槽），然后回到普通模式。 */
static void bin_fail(const char *why)
{
    if (s_bin_st == BIN_BODY && sdgoods_launcher_install_end) {
        sdgoods_launcher_install_end();   /* 未收满 ⇒ 内部 install_rollback() */
        bin_reply("SLOT-PUSH:fail slot=%d written=%u/%u err=%s\n",
                  s_bin_slot, (unsigned)(s_bin_total - s_bin_left),
                  (unsigned)s_bin_total, why);
    } else {
        bin_reply("SLOT-PUSH:fail err=%s\n", why);
    }
    bin_off();
}

/* 头部行收全（不含 '\n'） */
static void bin_hdr_done(void)
{
    s_bin_hdr[s_bin_hdr_len] = '\0';
    int slot = -1;
    unsigned long len = 0;
    char sha[80] = {0};
    int n = sscanf(s_bin_hdr, "===SLOT-BEGIN %d %lu %79s", &slot, &len, sha);
    if (n < 3 || len == 0 || slot < -1) {
        bin_reply("SLOT-PUSH:fail err=bad-header line='%s'\n", s_bin_hdr);
        bin_off();
        return;
    }
    const bool has_sha = (strcmp(sha, "-") != 0);
    if (has_sha && !hex32(sha, s_bin_sha)) {
        bin_reply("SLOT-PUSH:fail err=bad-sha256 line='%s'\n", s_bin_hdr);
        bin_off();
        return;
    }
    /* slot == -1 ⇒ 自动挑空闲槽（主机不必知道设备的分区布局）。
     * 没有空闲槽就是**决策 #5 的真实触发点**：通知 UI 弹「让用户选删哪个」，不自动替出。 */
    if (slot < 0) {
        slot = sdgoods_launcher_find_free_slot ? sdgoods_launcher_find_free_slot() : -2;
        if (slot < 0) {
            bin_reply("SLOT-PUSH:fail slot=-1 err=slots-full\n");
            ESP_LOGW(TAG, "slot push: no free slot -> notify UI (decision #5: user picks)");
            /* 注册式钩子（见 sdgoods_hooks.h §4）：启动器据此弹「选一个删掉」。
             * 通知在 console 任务上下文里发出，实现方自己投递到 LVGL 线程。 */
            sdgoods_slot_event_notify(SDGOODS_SLOT_EV_SLOTS_FULL);
            bin_off();
            return;
        }
    }
    /* app_id / version 传 NULL：`install_end()` 会以镜像自带 esp_app_desc_t 回填 manifest。
     * 串口这条路上没有平台下发的 Firmware.id，硬编一个反而会把 manifest 写脏。 */
    esp_err_t r = sdgoods_launcher_install_begin(slot, (size_t)len, NULL, NULL,
                                                has_sha ? s_bin_sha : NULL);
    if (r != ESP_OK) {
        bin_reply("SLOT-PUSH:fail slot=%d err=%s\n", slot, esp_err_to_name(r));
        bin_off();
        return;
    }
    s_bin_slot = slot;
    s_bin_total = (size_t)len;
    s_bin_left  = (size_t)len;
    s_bin_st    = BIN_BODY;
    s_bin_last_us = esp_timer_get_time();
    bin_reply("SLOT-PUSH:begin slot=%d bytes=%lu sha=%s\n", slot, len, has_sha ? "yes" : "no");
}

/* 数据块：交给流式安装；收满即收尾 */
static void bin_body(const uint8_t *p, size_t n)
{
    size_t k = (n < s_bin_left) ? n : s_bin_left;
    esp_err_t r = sdgoods_launcher_install_write(p, k);
    if (r != ESP_OK) {
        bin_fail(esp_err_to_name(r));
        return;
    }
    s_bin_left -= k;
    s_bin_last_us = esp_timer_get_time();
    if (s_bin_left == 0) {
        esp_err_t e = sdgoods_launcher_install_end();
        if (e == ESP_OK) {
            bin_reply("SLOT-PUSH:ok slot=%d bytes=%u\n", s_bin_slot, (unsigned)s_bin_total);
            /* 清单变了 ⇒ 主页那张图标网格已经过期（它是开机时按当时 manifest 建的，
             * 不会自动重排）⇒ 通知应用层重建 UI（见 sdgoods_hooks.h §4）。 */
            sdgoods_slot_event_notify(SDGOODS_SLOT_EV_INSTALLED);
        } else {
            bin_reply("SLOT-PUSH:fail slot=%d bytes=%u err=%s\n",
                      s_bin_slot, (unsigned)s_bin_total, esp_err_to_name(e));
        }
        bin_off();
    }
}

/* 串口命令分发：'?' 查能力；'s'/'S' 触发截屏（若能力已登记）；'X' 进入二进制注入模式。 */
static void console_rx_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "ready (send '?' for capabilities, 's' to capture if enabled, "
                  "'X' to push an app image into a slot, 'R' to reboot)");

    for (;;) {
        uint32_t n = usb_serial_jtag_ll_rxfifo_data_available()
                   ? usb_serial_jtag_ll_read_rxfifo(s_bin_rx, sizeof(s_bin_rx)) : 0;
        if (n == 0) {
            if (s_bin_st != BIN_OFF) {
                /* 注入中：空闲超时即回滚；否则短忙等。
                 * ⚠️ 不能用 vTaskDelay(pdMS_TO_TICKS(50))（那是普通模式的做法）：10ms 的
                 *    tick 延迟会把吞吐压到 ~100KB/s 量级，1.7MB 的 app 要传十几秒。 */
                if ((esp_timer_get_time() - s_bin_last_us) / 1000 > BIN_IDLE_TIMEOUT_MS) {
                    bin_fail("idle-timeout");
                } else {
                    esp_rom_delay_us(500);
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            continue;
        }
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t b = s_bin_rx[i];
            if (s_bin_st == BIN_HDR) {
                if (b == '\n' || b == '\r') {
                    if (s_bin_hdr_len) {
                        bin_hdr_done();
                    }
                } else if (s_bin_hdr_len < BIN_HDR_MAX - 1) {
                    s_bin_hdr[s_bin_hdr_len++] = (char)b;
                } else {
                    bin_fail("header-too-long");
                }
                continue;
            }
            if (s_bin_st == BIN_BODY) {
                /* 本缓冲里剩下的连续字节一次性交给 install_write（流式安装的 chunk 语义） */
                bin_body(&s_bin_rx[i], (size_t)(n - i));
                break;
            }

            /* ---- 普通命令模式 ---- */
            if (b == '?') {
                /* 能力查询：静音日志避免与回传行交错，回传 SDGOODS-CAPS: 一行 */
                esp_log_level_set("*", ESP_LOG_NONE);
                printf("SDGOODS-CAPS:%s\n", sdgoods_caps_names());
                fflush(stdout);
                esp_log_level_set("*", ESP_LOG_INFO);
            } else if (b == 's' || b == 'S') {
#ifdef CONFIG_SDGOODS_SCREENSHOT
                if (sdgoods_caps_has(SDGOODS_CAP_SCREENSHOT)) {
                    sdgoods_screenshot_capture();
                } else {
                    ESP_LOGW(TAG, "screenshot capability not enabled");
                }
#else
                ESP_LOGW(TAG, "screenshot not built into this firmware");
#endif
            } else if (b == 'X') {
                bin_enter();
            } else if (b == 'R') {
                /* 平台级：立即重启（等价按一下复位键）。
                 *
                 * 为什么必须是平台级、而不是塞进某个应用的 ext_cmd：
                 *   ① 复位是**与具体应用无关**的能力，和 '?'/'s'/'X' 同一性质；
                 *   ② 应用侧 `sdgoods_console_ext_cmd` 是**弱符号被整体覆盖**的
                 *      —— 启动器覆盖了它，放进弱默认实现就只有 app 能用、启动器反而没有；
                 *      放在这里则**任何固件（启动器 / app / 单应用）行为完全一致**。
                 *
                 * 为什么值得为「调试便利」专门加一个命令：本机 USB-Serial-JTAG 的
                 * DTR/RTS 脉冲复位**并非每次都生效**（实测同一台机器上前几轮有效、
                 * 后几轮静默失效），于是所有需要「重启后再观察」的验证脚本都只能靠运气。
                 * 有了 'R'，脚本可以用「发 'R' + 等 boot banner」把复位变成确定性的。
                 * 对启动失败回滚（救援通道）这类**纯重启行为**的验证尤其关键。 */
                ESP_LOGW(TAG, "reboot requested from console ('R')");
                fflush(stdout);
                vTaskDelay(pdMS_TO_TICKS(20));   /* 让上面那行日志先出 FIFO */
                esp_restart();
            } else if (b >= 32 && b < 127) {
                ESP_LOGI(TAG, "serial rx '%c'", b);
                sdgoods_console_ext_cmd((char)b);   /* 应用层诊断命令（如 'l' 触发启动槽位） */
            }
        }
    }
}

void sdgoods_console_init(void)
{
    static bool inited = false;
    if (inited) {
        return;
    }
    inited = true;

    /* 关闭 TX 的 CRLF 转换（详见文件头说明）。此设置全局，对普通日志无影响。 */
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);

    /* 栈 4KB（原 3KB）：二进制注入模式下本任务会直接跑 `esp_ota_write()`（flash 写 + cache
     * 关闭期间的 IRAM 路径），调用链比只跑 printf 深得多；接收缓冲是文件作用域静态数组，
     * 所以这 4KB 全是留给调用链的。 */
    if (xTaskCreate(console_rx_task, "bsp_console", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "rx task create failed");
    }
}
