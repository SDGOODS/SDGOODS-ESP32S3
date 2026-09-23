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

#include "sdgoods_boot_skip.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdgoods_nvs.h"     /* sdgoods_nvs_ensure：平台层统一的 NVS 保障（不要自己 nvs_flash_init） */

static const char *TAG = "sdg_boot_skip";

/* 独立命名空间，和槽清单（SDGOODS_SLOTS_NVS_NS）互不干扰 */
#define SKIP_NVS_NS  "sdgoods_boot"
#define SKIP_NVS_KEY "skip"

/* NVS 保障已抽到平台层公共工具 sdgoods_nvs.c（那里也解释了「为什么不能假设 app 做过
 * nvs_flash_init」）。此处直接复用，不再自带一份实现。 */

void sdgoods_boot_set_skip_next(bool skip)
{
    sdgoods_nvs_ensure();
    nvs_handle_t h;
    esp_err_t r = nvs_open(SKIP_NVS_NS, NVS_READWRITE, &h);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(%s) failed: %s, skip flag not persisted",
                 SKIP_NVS_NS, esp_err_to_name(r));
        return;
    }
    nvs_set_u8(h, SKIP_NVS_KEY, skip ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

bool sdgoods_boot_should_skip(void)
{
    sdgoods_nvs_ensure();
    nvs_handle_t h;
    esp_err_t r = nvs_open(SKIP_NVS_NS, NVS_READONLY, &h);
    if (r != ESP_OK) {
        return false;
    }
    uint8_t v = 0;
    r = nvs_get_u8(h, SKIP_NVS_KEY, &v);
    nvs_close(h);
    if (r != ESP_OK) {
        return false;   /* 未置位 / 首次上电 / 命名空间不存在 */
    }
    return (v == 1);
}
