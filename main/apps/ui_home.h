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
