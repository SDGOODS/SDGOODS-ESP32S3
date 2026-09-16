/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Required Notice: Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 *
 * 个人学习、研究与业余项目免费使用；商业用途需事前书面授权，见 LICENSING.md。
 * 分发时必须完整保留本声明 —— 这是许可条款，不是建议。
 */

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
    /* 按钮文字（中文 / 英文）。渲染时按当前界面语言二选一（sdgoods_i18n）。
       ⚠️ 含中文就等于占用字体子集 —— 新字必须先重跑 tools/gen_fonts.py，
          否则屏上是方框。 */
    const char *label_zh;
    const char *label_en;
    /* 像素图标键（见 ui_app_page.c 的 icon_map_for）："bird" / "plane" …
       没有图标填 NULL，届时退化成显示文字标签。
       注意它是**语言无关**的固定串 —— 别拿按钮文字当键，切语言后就查不到了。 */
    const char *icon;
    /* 点按钮后进入应用（通常是 ui_xxx_show / ui_xxx_start） */
    void (*show)(void);
    /* 每帧推进；没有逐帧逻辑就填 NULL */
    void (*poll)(void);
} sdgoods_app_t;

/* 应用清单。改动它即可增删启动台上的入口（顺序 = 按钮顺序 1..6）。 */
extern const sdgoods_app_t *const g_sdgoods_app;
extern const int                  g_sdgoods_app_count;

/* 由 main.c 调用：把清单与轮询接到平台层。必须在 sdgoods_boot_show() 之前调。 */
void apps_register(void);
