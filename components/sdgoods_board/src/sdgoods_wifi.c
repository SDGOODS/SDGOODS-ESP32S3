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

#include "sdgoods_wifi.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "sdgoods_wifi";
static bool s_ready;

esp_err_t sdgoods_wifi_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi_init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");

    s_ready = true;
    return ESP_OK;
}

int sdgoods_wifi_list(char names[][SDGOODS_WIFI_NAME_MAX + 1], size_t max_n)
{
    if (!names || max_n == 0) {
        return -1;
    }
    if (!s_ready && sdgoods_wifi_init() != ESP_OK) {
        return -1;
    }

    wifi_scan_config_t cfg = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) {
        return -1;
    }

    uint16_t ap_num = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));
    if (ap_num == 0) {
        return 0;
    }
    if (ap_num > 64) {
        ap_num = 64;
    }

    wifi_ap_record_t *recs = calloc(ap_num, sizeof(*recs));
    if (!recs) {
        return -1;
    }
    uint16_t got = ap_num;
    if (esp_wifi_scan_get_ap_records(&got, recs) != ESP_OK) {
        free(recs);
        return -1;
    }

    size_t n = 0;
    memset(names, 0, max_n * (SDGOODS_WIFI_NAME_MAX + 1));
    for (uint16_t i = 0; i < got && n < max_n; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (!ssid[0]) {
            continue;
        }
        size_t j;
        for (j = 0; j < n; j++) {
            if (strcmp(names[j], ssid) == 0) {
                break;
            }
        }
        if (j < n) {
            continue;
        }
        strlcpy(names[n++], ssid, SDGOODS_WIFI_NAME_MAX + 1);
    }
    free(recs);
    return (int)n;
}
