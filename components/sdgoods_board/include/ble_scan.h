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
