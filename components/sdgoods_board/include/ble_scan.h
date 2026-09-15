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

#define BLE_SCAN_NAME_MAX  32
#define BLE_SCAN_LIST_N    3

esp_err_t ble_scan_init(void);
int ble_scan_list(char names[][BLE_SCAN_NAME_MAX + 1], size_t max_n);

/* 供其它模块（如飞机联机）临时接管 BLE GAP 回调后恢复用：
 * 重新注册 ble_scan 自己的 GAP 回调，使扫描页在联机结束后仍可用。 */
void ble_scan_restore_gap_cb(void);
