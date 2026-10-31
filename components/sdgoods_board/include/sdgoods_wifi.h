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

#define SDGOODS_WIFI_NAME_MAX  32
#define SDGOODS_WIFI_LIST_N    3

/* 最大发射功率，单位 **0.25 dBm** ⇒ 34 = **8.5 dBm**（驱动会量化到台阶，34 这一档正好是 8.5）。
 * 口径来自产品：这块屏贴脸连热点时满功率（20 dBm）会认证超时（`reason=2 AUTH_EXPIRE`，
 * 用户侧显示成"密码错误"），压到 8.5 dBm（约 1/14 功率）后不再复现，连接距离仍够用。
 * ⚠️ sdkconfig 的 `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER` 只有整数 dBm（填 8 或 9 都不是 8.5），
 *    所以只能走运行时 API，见 sdgoods_wifi_apply_tx_power()。 */
#define SDGOODS_WIFI_TX_POWER_X4  34

esp_err_t sdgoods_wifi_init(void);
int sdgoods_wifi_list(char names[][SDGOODS_WIFI_NAME_MAX + 1], size_t max_n);

/* 把最大发射功率重新压到 8.5 dBm，并回读一次驱动里的实际值打日志（自证）。
 * `sdgoods_wifi_init()` 里已自动调用过一次，正常情况下调用方**不用管**。
 * 只在「自己停过射频再开回来」之后需要补一次：`esp_wifi_stop()` → `esp_wifi_start()`
 * 之后驱动会退回 PHY 上限（本板 20 dBm）。本工程的 MIC 静噪路径（sdgoods_audio.c 的
 * rf_quiet_for_mic）就是这么做的；app 里若自己 stop/start 了 wifi，也照这样补一次。
 * 返回 ESP_ERR_INVALID_STATE 表示 wifi 还没 init/start（调用时机不对，不是射频问题）。 */
esp_err_t sdgoods_wifi_apply_tx_power(void);
