#include "ble_scan.h"

#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static bool s_ready;
static SemaphoreHandle_t s_done;
static char (*s_out)[BLE_SCAN_NAME_MAX + 1];
static size_t s_max_n;
static size_t s_n;
static uint8_t s_addrs[BLE_SCAN_LIST_N][6];

static void add_dev(const uint8_t *bda, const uint8_t *name, uint8_t len)
{
    if (!s_out || s_n >= s_max_n || !name || !len) {
        return;
    }
    for (size_t i = 0; i < s_n; i++) {
        if (memcmp(s_addrs[i], bda, 6) == 0) {
            return;
        }
    }
    if (len > BLE_SCAN_NAME_MAX) {
        len = BLE_SCAN_NAME_MAX;
    }
    memcpy(s_addrs[s_n], bda, 6);
    memcpy(s_out[s_n], name, len);
    s_out[s_n][len] = '\0';
    s_n++;
    if (s_n >= s_max_n) {
        (void)esp_ble_gap_stop_scanning();
    }
}

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_SCAN_RESULT_EVT) {
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            uint8_t len = 0;
            uint8_t *adv = (uint8_t *)param->scan_rst.ble_adv;
            uint16_t adv_len = param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len;
            uint8_t *name = esp_ble_resolve_adv_data_by_type(adv, adv_len, ESP_BLE_AD_TYPE_NAME_CMPL, &len);
            if (!name || !len) {
                name = esp_ble_resolve_adv_data_by_type(adv, adv_len, ESP_BLE_AD_TYPE_NAME_SHORT, &len);
            }
            add_dev(param->scan_rst.bda, name, len);
        } else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            xSemaphoreGive(s_done);
        }
    } else if (event == ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT) {
        xSemaphoreGive(s_done);
    }
}

esp_err_t ble_scan_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    s_done = xSemaphoreCreateBinary();
    if (!s_done) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_cb));

    s_ready = true;
    return ESP_OK;
}

void ble_scan_restore_gap_cb(void)
{
    esp_ble_gap_register_callback(gap_cb);
}

int ble_scan_list(char names[][BLE_SCAN_NAME_MAX + 1], size_t max_n)
{
    if (!names || max_n == 0) {
        return -1;
    }
    if (!s_ready && ble_scan_init() != ESP_OK) {
        return -1;
    }
    if (max_n > BLE_SCAN_LIST_N) {
        max_n = BLE_SCAN_LIST_N;
    }

    memset(names, 0, max_n * (BLE_SCAN_NAME_MAX + 1));
    memset(s_addrs, 0, sizeof(s_addrs));
    s_out = names;
    s_max_n = max_n;
    s_n = 0;
    while (xSemaphoreTake(s_done, 0) == pdTRUE) {
    }

    esp_ble_scan_params_t params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
    };
    if (esp_ble_gap_set_scan_params(&params) != ESP_OK ||
        esp_ble_gap_start_scanning(3) != ESP_OK) {
        s_out = NULL;
        return -1;
    }

    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
        (void)esp_ble_gap_stop_scanning();
        (void)xSemaphoreTake(s_done, pdMS_TO_TICKS(1000));
    }

    int n = (int)s_n;
    s_out = NULL;
    return n;
}
