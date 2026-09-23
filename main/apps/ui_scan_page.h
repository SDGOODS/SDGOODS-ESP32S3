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

#include <stdbool.h>

#include <stddef.h>

#define UI_SCAN_NAME_MAX  32

typedef int (*ui_scan_fn_t)(char names[][UI_SCAN_NAME_MAX + 1], size_t max_n);

void ui_scan_page_show(const char *title, const char *sub_fmt, ui_scan_fn_t scan_fn);
void ui_scan_page_poll(void);
bool ui_scan_page_is_open(void);   /* 电源键短按「一级返回」用：子页是否前台 */
void ui_scan_page_close(void);     /* 子页 → 应用主页（与左滑返回同一动作） */
