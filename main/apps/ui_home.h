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
 * ui_home.h —— 主页（根页面）
 *
 * 布局常量（SDG_UI_*）已抽到平台层的 sdgoods_ui.h —— 主页 / 应用页 / DEMO 页 /
 * 应用菜单共用同一套圆屏栅格，这里重新导出只是为了让页面代码少写一个 include。
 */

#include "sdgoods_ui.h"

void ui_home_create(void);
void ui_home_show(void);

/* 子页返回目标：进入子页（如 DEMO 下的录音页）时由父页改写为自己的 show 函数，
 * 子页关闭时调用 ui_nav_back() 即可回到正确的父页（而不是固定跳回主页）。
 * 默认指向 ui_home_show（根页面）。 */
extern void (*ui_nav_parent_show)(void);
void ui_nav_back(void);

/* 电源键短按钩子（注册给平台层 sdgoods_set_power_short_handler）：
 * 子页/游戏在前台时回退一级到应用主页并返回 true（消费掉），已在主页时返回 false
 * 交平台默认导航（多应用：回启动器；单应用/派生：浅睡）。实现在 ui_home.c。 */
bool ui_app_power_short_handler(void);
