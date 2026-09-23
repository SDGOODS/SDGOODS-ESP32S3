/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于应用层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

/*
 * apps_registry.c —— 应用注册表（应用层）
 *
 * 这是「应用层 ⇄ 平台层」的唯一接线点，同时定义了启动台上显示哪些应用。
 * 平台层（components/sdgoods_board）不知道有哪些应用，它只认 sdgoods_hooks.h
 * 里的几个回调，由这里填上。
 *
 * ---------------------------------------------------------------------------
 * 新增一个应用（复制 app_template.c/.h 为 ui_<name>.c/.h 并改名，下面三步手动完成；旧 tools/new_app.py 已移除）：
 *
 *   1. 在 main/apps/ 下写 ui_你的应用.c/.h —— 用 app_template.c 当骨架
 *      （它已接好 ui_app_shell 的四步，菜单/暂停/退出都不用自己写）
 *   2. 本文件：加 #include → 往 s_apps[] 里加一行
 *   3. main/CMakeLists.txt：往 SRCS 里加一行
 *
 *   ⚠️ 新应用的界面只要出现**新的中文文案**，就必须重跑 tools/gen_fonts.py，
 *      否则屏上显示方框（tofu）。app_template.c 顶部的注释也写了这条。
 * ---------------------------------------------------------------------------
 */

#include "apps_registry.h"

#include <stddef.h>

#include "sdgoods_board.h"   /* 平台层：sdgoods_apps_set_poll / sdgoods_ui_set_nav */
#include "sdgoods_hooks.h"   /* sdgoods_set_power_short_handler：电源键「一级返回」钩子 */

/* ---- 导航页（不算应用，但也要轮询） ---- */
#include "ui_home.h"         /* 根页面（首页：6 个功能按钮平铺） */
#include "ui_scan_page.h"
#include "ui_rec_page.h"
#include "ui_other_page.h"

/* ---- 应用清单里用到的应用 ---- */
#include "ui_flappy.h"       /* 首页「小鸟」按钮直接启动；poll 由 apps_poll 调用 */

/* ===========================================================================
 * ★ 应用清单 ★
 *
 * 这一份清单是「应用层 ⇄ 平台层」的接线点：每个应用在这里登记 .poll，
 * 由 apps_poll() 每帧调用；.show 目前由首页「小鸟」按钮直接调用
 * （ui_flappy_start），飞机应用已随「应用」目录一并屏蔽，不再登记。
 *
 *   .label_zh 中文按钮文字 / .label_en 英文按钮文字（按界面语言二选一）
 *   .icon     像素图标键（"bird" / …，保留作元数据，首页按钮为纯文字）
 *   .show     进入应用 / .poll 每帧推进（可为 NULL）
 * =========================================================================== */
static const sdgoods_app_t s_apps[] = {
    { .label_zh = "小鸟", .label_en = "Bird",  .icon = "bird",
      .show = ui_flappy_start, .poll = ui_flappy_poll },
};

const sdgoods_app_t *const g_sdgoods_app   = s_apps;
const int                  g_sdgoods_app_count = (int)(sizeof(s_apps) / sizeof(s_apps[0]));

/* ---------------------------------------------------------------------------
 * 应用轮询汇总
 *
 * 平台主循环每轮（约 2ms）调用一次。两条硬性要求：
 *   · 每个 *_poll() 必须「自己不是前台就立刻返回」，否则会拖慢整个 UI 刷新；
 *   · 里面**不要**做阻塞操作（vTaskDelay / 等信号量 / 阻塞读串口）。
 *     需要等待的逻辑请放到独立 FreeRTOS 任务里，poll 里只读标志位。
 * ------------------------------------------------------------------------- */
static void apps_poll(void)
{
    /* 导航页（子页）：各自 poll 自己不是前台就立刻返回，不拖慢 UI 刷新 */
    ui_scan_page_poll();
    ui_rec_page_poll();
    ui_other_page_poll();

    /* 应用清单：表里加了新应用，这里自动生效，不用再改 */
    for (int i = 0; i < g_sdgoods_app_count; i++) {
        if (s_apps[i].poll) {
            s_apps[i].poll();
        }
    }
}

/* ---------------------------------------------------------------------------
 * 屏幕导航
 * ui_home_create() 内部已经包含「创建 + 切换」，首次进入直接调它即可；
 * 后续回主页只需 lv_scr_load（ui_home_show），不必重建屏（重建会泄漏旧屏幕对象）。
 * ------------------------------------------------------------------------- */
static void home_create_show(void)
{
    ui_home_create();
}

static void home_show(void)
{
    ui_home_show();
}

static void apps_show(void)
{
    /* 应用（如小鸟）退出后落回首页，而不是已删除的「应用」启动台 */
    ui_home_show();
}

void apps_register(void)
{
    sdgoods_apps_set_poll(apps_poll);

    /* 电源键短按「一级一级返回」：子页→主页由本应用钩子消费，主页交给平台默认导航。 */
    sdgoods_set_power_short_handler(ui_app_power_short_handler);

    static const sdgoods_nav_t nav = {
        .home_create_show = home_create_show,
        .home_show        = home_show,
        .apps_show        = apps_show,
    };
    sdgoods_ui_set_nav(&nav);
}
