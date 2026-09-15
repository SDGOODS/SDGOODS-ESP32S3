#pragma once

/*
 * apps_registry.h —— 应用注册表
 *
 * 把 main/apps/ 下散落的应用「接线」到平台层与启动台：
 *   · 平台层：每轮 UI 循环要推进哪些应用的 poll（平台不知道有哪些应用）
 *   · 启动台（ui_app_page.c）：显示哪些圆按钮
 *   · 开机动画结束后首屏是什么、应用菜单「退出」时回哪个页面
 *
 * ★ 新增应用只需要改两个文件：
 *     1. main/apps/apps_registry.c —— 往 s_apps[] 表里加一行
 *     2. main/CMakeLists.txt       —— 往 SRCS 加一行（自动生成脚本会代劳）
 *   用 tools/new_app.py 生成骨架时这两步会自动完成。
 */

/* ---------------------------------------------------------------------------
 * 一个「应用入口」= 启动台上的一个圆按钮
 * ------------------------------------------------------------------------- */
typedef struct {
    /* 按钮上显示的文字。也是「有没有像素图标」的查找键（见 ui_app_page.c）。
       ⚠️ 含中文就等于占用字体子集 —— 新字必须先重跑 tools/gen_fonts.py，
          否则屏上是方框。 */
    const char *label;
    /* 点按钮后进入应用（通常是 ui_xxx_show / ui_xxx_start） */
    void (*show)(void);
    /* 每帧推进；没有逐帧逻辑就填 NULL */
    void (*poll)(void);
} sdgoods_app_t;

/* 应用清单。改动它即可增删启动台上的入口（顺序 = 按钮顺序 1..6）。 */
extern const sdgoods_app_t *const g_sdgoods_app;
extern const int                  g_sdgoods_app_count;

/* 由 main.c 调用：把清单与轮询接到平台层。必须在 ui_boot_show() 之前调。 */
void apps_register(void);
