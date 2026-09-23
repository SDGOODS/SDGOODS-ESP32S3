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

#pragma once

/*
 * apps_registry.h —— 应用注册表
 *
 * 把 main/apps/ 下散落的应用「接线」到平台层：
 *   · 平台层：每轮 UI 循环要推进哪些应用的 poll（平台不知道有哪些应用）
 *   · 首页（ui_home.c）直接以「小鸟」按钮启动首个应用，其余功能按钮平铺在首页
 *   · 应用菜单「退出」时回首页（见 apps_registry.c 的 apps_show）
 *
 * ★ 新增应用只需要改两个文件：
 *     1. main/apps/apps_registry.c —— 往 s_apps[] 表里加一行
 *     2. main/CMakeLists.txt       —— 往 SRCS 加一行
 *   要独立开发并上架自己的应用，用 `tools/new_app_project.py <name>` 派生工程（见 skill sdgoods-new-app）。
 */

/* ---------------------------------------------------------------------------
 * 一个「应用入口」：登记 .poll 供每帧推进，.show 由首页按钮 / 应用菜单调用
 * ------------------------------------------------------------------------- */
typedef struct {
    /* 按钮文字（中文 / 英文）。渲染时按当前界面语言二选一（sdgoods_i18n）。
       ⚠️ 含中文就等于占用字体子集 —— 新字必须先重跑 tools/gen_fonts.py，
          否则屏上是方框。 */
    const char *label_zh;
    const char *label_en;
    /* 像素图标键："bird" / …（保留作元数据；当前首页按钮为纯文字，不再由启动台渲染图标）。
       注意它是**语言无关**的固定串 —— 别拿按钮文字当键，切语言后就查不到了。 */
    const char *icon;
    /* 点按钮后进入应用（通常是 ui_xxx_show / ui_xxx_start） */
    void (*show)(void);
    /* 每帧推进；没有逐帧逻辑就填 NULL */
    void (*poll)(void);
} sdgoods_app_t;

/* 应用清单。每帧 poll 由 apps_poll() 调用；.show 由首页「小鸟」按钮直接调用。 */
extern const sdgoods_app_t *const g_sdgoods_app;
extern const int                  g_sdgoods_app_count;

/* 由 main.c 调用：把清单与轮询接到平台层。必须在 sdgoods_ui_home_create_show()（创建首屏）之前调。 */
void apps_register(void);
