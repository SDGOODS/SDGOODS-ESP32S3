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

#include <stddef.h>
#include "esp_err.h"

#define SDGOODS_BLE_NAME_MAX  32
#define SDGOODS_BLE_LIST_N    3

esp_err_t sdgoods_ble_init(void);
int sdgoods_ble_list(char names[][SDGOODS_BLE_NAME_MAX + 1], size_t max_n);

/* 供其它模块（如飞机联机）临时接管 BLE GAP 回调后恢复用：
 * 重新注册 sdgoods_ble 自己的 GAP 回调，使扫描页在联机结束后仍可用。 */
void sdgoods_ble_restore_gap_cb(void);
