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

#include <stddef.h>

#define UI_SCAN_NAME_MAX  32

typedef int (*ui_scan_fn_t)(char names[][UI_SCAN_NAME_MAX + 1], size_t max_n);

void ui_scan_page_show(const char *title, const char *sub_fmt, ui_scan_fn_t scan_fn);
void ui_scan_page_poll(void);
