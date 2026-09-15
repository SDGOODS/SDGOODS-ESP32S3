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

#include <stddef.h>
#include "esp_err.h"

#define WIFI_SCAN_NAME_MAX  32
#define WIFI_SCAN_LIST_N    3

esp_err_t wifi_scan_init(void);
int wifi_scan_list(char names[][WIFI_SCAN_NAME_MAX + 1], size_t max_n);
