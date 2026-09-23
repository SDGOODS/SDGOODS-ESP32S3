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

#pragma once

#include <stdbool.h>

/*
 * sdgoods_hooks.h —— 应用层「注册」给平台层的少数几个回调
 *
 * 为什么要这么一层
 * ----------------
 * 平台层（components/sdgoods_board）负责屏 / 触摸 / 音频 / 应用框架，
 * 它**不知道主页长什么样、也不知道有哪些应用**。但有三处必须回调应用层：
 *
 *   1. LVGL 主循环每轮要推进各应用的定时器（精灵、物理、蓝牙收包）；
 *   2. 开机动画播完，要创建并进入首屏；
 *   3. 应用菜单点「退出」、或游戏中短按电源键，要回到主页 / 应用页。
 *
 * 早期写法是平台层直接 #include 应用的头文件（sdgoods_lvgl.c 引了 9 个 ui_*_poll），
 * 结果是「底板反过来依赖上层」：任何人改应用都得碰平台代码，没法单独替换。
 * 现在改成注册制 —— 平台层只认这几个函数指针。
 *
 * 装配点只有一个：main/apps/apps_registry.c，由 main.c 调用。
 * 没注册时全部退化为空操作，不会空指针崩溃。
 */

typedef void (*sdgoods_cb_t)(void);

/* ---- 1) 应用轮询 ----------------------------------------------------------
 * sdgoods_lvgl_loop() 每轮（约 2ms）调用一次。各应用的 *_poll() 由应用层自己汇总。 */
void sdgoods_apps_set_poll(sdgoods_cb_t fn);

/* ---- 2) 屏幕导航 ---------------------------------------------------------- */
typedef struct {
    /* 开机动画结束：创建主页并显示（首次进入用，不得为 NULL） */
    sdgoods_cb_t home_create_show;
    /* 仅切到主页（主页已创建）。菜单「退出」/ 游戏中短按电源键回这里 */
    sdgoods_cb_t home_show;
    /* 切换回「应用」启动台页。菜单「退出」默认回这里 */
    sdgoods_cb_t apps_show;
} sdgoods_nav_t;

void sdgoods_ui_set_nav(const sdgoods_nav_t *nav);

/* ---- 3) 电源键短按「消费」钩子 -------------------------------------------
 * 启动器可注册一个回调：返回 true 表示上层已处理该次短按（例如关闭控制中心的浮层），
 * 平台层不再执行默认导航（关 app 菜单 / 回主页 / 熄屏低功耗）。返回 false 则走默认逻辑。
 * 未注册则为 NULL，平台层照常执行默认导航。 */
typedef bool (*sdgoods_power_short_cb_t)(void);
void sdgoods_set_power_short_handler(sdgoods_power_short_cb_t fn);
/* 平台层内部：poll 调用，转调已注册的钩子；无钩子时返回 false。 */
bool sdgoods_ui_power_short(void);

/* ---- 5) 多应用模式判定（电源键导航用） -----------------------------------
 * 弱默认在 sdgoods_board（返回 false）；平台层 components/sdgoods_launcher
 * 的强符号覆盖它，按 sdgoods_device_mode() 返回是否处于多应用（启动器）模式。
 * 用途：电源键短按在「应用主页」的落点——
 *   · 多应用模式：返回到启动器（应用主页 → 启动器）；
 *   · 派生/单应用模式：已在应用主页，直接熄屏进入低功耗。
 * 不含 sdgoods_launcher 的精简固件（或单应用模式）走弱默认 false ⇒ 熄屏，
 * 不会误调到不存在的启动器。 */
bool sdgoods_device_is_multi_app_mode(void);

/* 请求「退出到启动器」（仅被启动器管理的 app 有能力执行）。
 * 弱默认在 sdgoods_board（返回 false = 本固件没有回启动器能力，调用方继续走深睡）；
 * 平台层 components/sdgoods_launcher 的强符号覆盖它：被管理 app 时调
 * sdgoods_return_to_launcher()（写 otadata 指回 factory + 重启，OK 路径不返回）。
 * 用途：电源键短按在「应用主页」时——
 *   · 被启动器管理的 app：返回 true（设备已重启进启动器）；
 *   · 其余（单应用 / 派生 / 启动器宿主主页）：返回 false ⇒ 调用方熄屏 + 深睡。
 * 返回 false 还覆盖 return_to_launcher 被闸门拒绝的边界（理论上先判过 is_managed_app
 * 不会再拒），保证任何情况下短按都有确定行为。 */
bool sdgoods_multi_app_exit_to_launcher(void);

/* ---- 4) 槽清单变化通知（2026-09-19） --------------------------------------
 * 为什么平台层要发这个事件：平台层的**串口二进制注入通道**（见 sdgoods_console.c 顶部，
 * 命令 'X'）能直接把 app.bin 写进某个 ota 槽 —— 这是「设备侧联网」做完之前，槽安装链路
 * 在真机上唯一的调用者。写完槽之后**主页那张图标网格就过期了**：它是在开机时按当时的
 * manifest 建出来的，不会自动重排（实测：注入成功后截图，新 app 的图标要等重启才出现）。
 *
 * ⚠️ 回调是在**平台层的串口接收任务**里被调用的（不是 LVGL 线程）⇒ 实现方**必须**
 *    自己投递到 LVGL 线程（lv_async_call）再碰任何 UI 对象。这一条不是洁癖：
 *    在 console 任务里同步建 LVGL 浮层会直接 `stack overflow in task bsp_console`（已踩）。
 * 未注册时空操作。 */
typedef enum {
    SDGOODS_SLOT_EV_INSTALLED = 0,   /* 某个槽刚装好一个 app；清单已更新，UI 该重建了 */
    SDGOODS_SLOT_EV_SLOTS_FULL = 1,  /* 安装请求失败：没有空闲槽 ⇒ 该弹「删哪个」（决策 #5） */
} sdgoods_slot_event_t;

typedef void (*sdgoods_slot_event_cb_t)(sdgoods_slot_event_t ev);
void sdgoods_set_slot_event_handler(sdgoods_slot_event_cb_t fn);
/* 平台层内部：串口注入通道调用。 */
void sdgoods_slot_event_notify(sdgoods_slot_event_t ev);

/* ---- 以下为平台层内部使用（应用层不需要调用） ----------------------------- */
void sdgoods_apps_poll(void);            /* 由 sdgoods_lvgl_loop() 调用 */
void sdgoods_ui_home_create_show(void);  /* 由 sdgoods_boot.c 在动画结束时调用 */
void sdgoods_ui_home_show(void);         /* 由 sdgoods_app_shell.c 调用 */
void sdgoods_ui_apps_show(void);         /* 由 sdgoods_app_shell.c 调用 */
