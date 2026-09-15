/*
 * SDGOODS 开放平台基础工程 · 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Required Notice: Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 *
 * 个人学习、研究与业余项目免费使用；商业用途需事前书面授权，见 LICENSING.md。
 * 分发时必须完整保留本声明 —— 这是许可条款，不是建议。
 */

/*
 * apps_registry.c —— 应用注册表（应用层）
 *
 * 这是「应用层 ⇄ 平台层」的唯一接线点，同时定义了启动台上显示哪些应用。
 * 平台层（components/sdgoods_board）不知道有哪些应用，它只认 sdgoods_hooks.h
 * 里的几个回调，由这里填上。
 *
 * ---------------------------------------------------------------------------
 * 新增一个应用（tools/new_app.py 会自动完成下面三步）：
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

/* ---- 导航页（不算应用，但也要轮询） ---- */
#include "ui_home.h"         /* 根页面 */
#include "ui_app_page.h"     /* 「应用」启动台 */
#include "ui_demo_page.h"
#include "ui_scan_page.h"
#include "ui_rec_page.h"
#include "ui_other_page.h"

/* ---- 应用清单里用到的应用 ---- */
#include "app_template.h"    /* 「示例」：新应用骨架，点进去可看到完整参考实现 */
#include "ui_flappy.h"
#include "ui_plane.h"
#include "ui_tetris.h"
#include "ui_snake.h"
#include "ui_about.h"        /* 「关于」：品牌 / 公司 / 固件版本 / 授权提示 */
/* >>> new_app.py: 新应用 include 插到这里 >>> */

/* ===========================================================================
 * ★ 应用清单 ★
 *
 * 顺序 = 启动台上的按钮顺序（第 1 行 3 个，第 2 行 3 个，共 6 个位置）。
 * 超过 6 个的应用不会显示按钮（但 poll 仍会被调用），要更多入口请调整
 * ui_app_page.c 的栅格。
 *
 *   {  按钮文字,    进入函数,        每帧推进（可为 NULL） },
 * =========================================================================== */
static const sdgoods_app_t s_apps[] = {
    { "小鸟",       ui_flappy_start, ui_flappy_poll },
    { "飞机",       ui_plane_start,  ui_plane_poll  },
    { "俄罗斯方块", ui_tetris_start, ui_tetris_poll },
    { "贪吃蛇",     ui_snake_start,  ui_snake_poll  },
    { "示例",       ui_app_template_show, ui_app_template_poll },
    { "关于",       ui_about_page_show, ui_about_page_poll },
    /* >>> new_app.py: 新应用插到这里（保持缩进即可） >>> */
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
    /* 导航页 */
    ui_scan_page_poll();
    ui_rec_page_poll();
    ui_other_page_poll();
    ui_demo_page_poll();
    ui_app_page_poll();

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
    ui_app_page_show();
}

void apps_register(void)
{
    sdgoods_apps_set_poll(apps_poll);

    static const sdgoods_nav_t nav = {
        .home_create_show = home_create_show,
        .home_show        = home_show,
        .apps_show        = apps_show,
    };
    sdgoods_ui_set_nav(&nav);
}
