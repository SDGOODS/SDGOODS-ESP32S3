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
#include "lvgl.h"

/* 在页面屏幕底部挂一个透明“上滑返回捕获层”（底部 SWIPE_UP_ZONE 高）：
 * 从底部往上滑（dy < -阈值且 |dy| > |dx|）→ 调用 on_up 返回上一级 / 主页。
 * 捕获层置于其它控件之下，因此页面上的按钮 / 滑块仍可正常点按、拖动；
 * 只有「从底部空白区起手」的上滑才触发（拖动滑块等不会误触）。 */
void sdgoods_swipe_up_bind(lv_obj_t *scr, void (*on_up)(void));
