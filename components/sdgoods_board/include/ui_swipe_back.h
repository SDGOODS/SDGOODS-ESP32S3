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
#include "lvgl.h"

/* 在页面屏幕上挂一个全屏透明“返回捕获层”：空白处从左滑到右 → 调用 on_back 返回。
 * 捕获层置于控件之下，因此页面上的按钮仍可正常点击；直接从按钮上起手的滑动不触发返回（符合预期）。 */
void ui_swipe_back_bind(lv_obj_t *scr, void (*on_back)(void));
