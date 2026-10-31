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
    /* 🔴 必须在 `esp_wifi_start()` **之后**（之前调会返回 ESP_ERR_WIFI_NOT_STARTED，
     *    且启动那一刻用的是 PHY init data 里的上限 20 dBm）。
     *    失败只告警、不阻断 init：功率没压下去顶多覆盖差一点，
     *    而 wifi 起不来是"整机没网"，两件事的代价不在一个量级。 */
    (void)sdgoods_wifi_apply_tx_power();
    return ESP_OK;
}

/*
 * 最大发射功率：固定压到 8.5 dBm（2026-10-22 产品口径，与 launcher 那套固件同值 ——
 * 同一个硬件上跑两套固件，射频行为不能有两种）。
 *
 * 为什么走运行时 API 而不是 sdkconfig：`CONFIG_ESP_PHY_MAX_WIFI_TX_POWER` 只收**整数 dBm**
 *   （2..20），填 8 或 9 都不是 8.5。要 0.25 dBm 的分辨率只有这条路。
 *
 * 单位/量化（IDF esp_wifi.h 的 attention 3/5）：入参单位 0.25 dBm，
 *   设 34 会被量化成 34（{[34,43] → 34}）⇒ 实际 8.5 dBm，正好落在我们要的那一档；
 *   真正生效的下限是 8（2 dBm），上限是 80（20 dBm）。
 *
 * 为什么压低（真机踩过的，不是理论推导）：
 *   贴脸连手机热点（rssi ≈ −21 dBm）时满功率发射会把 AP 的接收前端顶到饱和，
 *   认证帧丢失 ⇒ 设备报 `reason=2 AUTH_EXPIRE`，屏上却显示"密码错误"（误导用户去改密码）。
 *   压到 8.5 dBm（比 20 dBm 低 11.5 dB ⇒ 辐射功率约 1/14）后不再复现；
 *   本产品的使用场景是"拿着设备在室内 / 贴脸热点"，这个功率的链路余量够用。
 *
 * ⚠️ 这是 `max` 上限，不是"每个速率/信道上的实际值"：驱动仍会按速率、信道、法规回退，
 *    实际只会 ≤ 8.5 dBm。
 * ⚠️ 这个值挂在 station 的 country 配置上（IDF esp_wifi.h attention 2），而
 *    `esp_wifi_stop()` 会 **frees station control block** ⇒ IDF 不承诺它跨 stop/start 保留。
 *    所以重启射频的路径必须补调本函数（见 sdgoods_audio.c::rf_quiet_for_mic —— 幂等，重复设同值无害）。
 */
esp_err_t sdgoods_wifi_apply_tx_power(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;   /* 还没 init/start：调用时机不对，不是射频问题 */
    }
    const esp_err_t err = esp_wifi_set_max_tx_power(SDGOODS_WIFI_TX_POWER_X4);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tx power: set %d (8.5 dBm) failed: %s",
                 SDGOODS_WIFI_TX_POWER_X4, esp_err_to_name(err));
        return err;
    }

    /* 回读自证：只打"设成 34"证明不了驱动真的接受了 —— 设进去和读回来两件事都要看得见。
     * ⚠️ `esp_wifi_get_max_tx_power()` 的签名是 `int8_t *`（IDF v5.5），用 `int *` 编译不过。
     * 不用 `%f`：newlib 精简版 printf 未必带浮点，改成整数定点打印（34 → 8.50）。 */
    int8_t got = -1;
    const esp_err_t eget = esp_wifi_get_max_tx_power(&got);
    ESP_LOGI(TAG, "tx power: max=%d (=%d.%02d dBm) readback=%d (%s) phy_cap=%d dBm",
             SDGOODS_WIFI_TX_POWER_X4,
             (SDGOODS_WIFI_TX_POWER_X4 * 25) / 100,
             (SDGOODS_WIFI_TX_POWER_X4 * 25) % 100,
             (int)got, esp_err_to_name(eget),
             (int)CONFIG_ESP_PHY_MAX_WIFI_TX_POWER);
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
