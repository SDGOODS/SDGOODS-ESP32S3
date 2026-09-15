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

/*
 * sdgoods_i18n.c —— 界面语言实现（详见 include/sdgoods_i18n.h）
 *
 * 状态就两个：当前语言 + 版本号。持久化用 NVS 的一个 u8 键。
 * 单线程访问（都在 LVGL/主循环线程），不加锁。
 */

#include "sdgoods_i18n.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "i18n";

#define I18N_NVS_NS   "sdgoods"   /* NVS 命名空间 */
#define I18N_NVS_KEY  "lang"      /* 0 = EN（默认），1 = ZH */

static sdg_lang_t s_lang = SDG_LANG_EN;   /* 出厂默认：英文 */
static uint32_t   s_seq  = 1;             /* 语言版本号，切换时 +1 */

void sdg_i18n_init(void)
{
    /* NVS 可能已被别的模块（wifi_scan）初始化过；重复调用是安全的。 */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要重建（%s），已擦除", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        /* 读不到就当默认英文，功能不受影响，只是不记忆选择 */
        ESP_LOGW(TAG, "NVS 不可用（%s）：语言可用但不能持久化", esp_err_to_name(err));
        return;
    }

    nvs_handle_t h;
    if (nvs_open(I18N_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = (uint8_t)SDG_LANG_EN;
        if (nvs_get_u8(h, I18N_NVS_KEY, &v) == ESP_OK && v <= (uint8_t)SDG_LANG_ZH) {
            s_lang = (sdg_lang_t)v;
        }
        nvs_close(h);
    }

    ESP_LOGI(TAG, "语言 = %s（默认英文；DEMO 页「语言」按钮可切换，设置已存 NVS）",
             sdg_i18n_code());
}

sdg_lang_t sdg_i18n_get(void)
{
    return s_lang;
}

bool sdg_i18n_is_zh(void)
{
    return (s_lang == SDG_LANG_ZH);
}

uint32_t sdg_i18n_seq(void)
{
    return s_seq;
}

const char *sdg_i18n_code(void)
{
    return (s_lang == SDG_LANG_ZH) ? "zh" : "en";
}

const char *sdg_i18n_pick(const char *zh, const char *en)
{
    return (s_lang == SDG_LANG_ZH) ? zh : en;
}

void sdg_i18n_set(sdg_lang_t lang)
{
    if (lang > SDG_LANG_ZH || lang == s_lang) {
        return;
    }
    s_lang = lang;
    s_seq++;   /* 页面靠这个值发现「文案作废了」，必须 +1 */

    nvs_handle_t h;
    if (nvs_open(I18N_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        esp_err_t e1 = nvs_set_u8(h, I18N_NVS_KEY, (uint8_t)lang);
        esp_err_t e2 = nvs_commit(h);
        nvs_close(h);
        if (e1 != ESP_OK || e2 != ESP_OK) {
            ESP_LOGW(TAG, "语言设置写 NVS 失败（本次运行仍生效）");
        }
    }
    ESP_LOGI(TAG, "语言切换 -> %s", sdg_i18n_code());
}

void sdg_i18n_toggle(void)
{
    sdg_i18n_set(sdg_i18n_is_zh() ? SDG_LANG_EN : SDG_LANG_ZH);
}
