/*
 * SDGOODS 开放平台基础工程 · 平台层（BSP）
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于平台层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

#pragma once

/*
 * sdgoods_ui.h —— 圆屏通用 UI 栅格（平台层）
 *
 * 这块 360x360 圆屏的可用区域比同尺寸矩形屏小得多：四角会被圆边切掉。
 * 下面这套「3 列 x 2 行 + 标题 + 页脚」的栅格是主页 / 应用页 / DEMO 页共用的，
 * 坐标为各控件的**左上角**：
 *
 *            BTN1    BTN2    BTN3          <- 第 1 行 y = 93
 *            BTN4    BTN5    BTN6          <- 第 2 行 y = 191
 *
 *  标题在 y = 35 居中；页脚在 y = 296 居中。
 *
 * ⚠️ 圆边裁切：第 1 / 3 列（x=48 / 236）与第 2 行（y=191）已经比较靠近圆边。
 *    把 BTN_SIZE 调大或把坐标继续外移，按钮会被圆边切掉一块。
 *    改完请用 tools/screenshot_recv.py 截屏确认。
 */

#define SDG_UI_BTN_SIZE   76

#define SDG_UI_BTN1_X     48
#define SDG_UI_BTN1_Y     93
#define SDG_UI_BTN2_X     142
#define SDG_UI_BTN2_Y     93
#define SDG_UI_BTN3_X     236
#define SDG_UI_BTN3_Y     93
#define SDG_UI_BTN4_X     48
#define SDG_UI_BTN4_Y     191
#define SDG_UI_BTN5_X     142
#define SDG_UI_BTN5_Y     191
#define SDG_UI_BTN6_X     236
#define SDG_UI_BTN6_Y     191

#define SDG_UI_TITLE_Y    35
#define SDG_UI_FOOTER_Y   296
