/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（BSP）· 应用 SDK（提供给上架 app 的极薄库）
 *
 * 实现 sdgoods_app_sdk.h：返回启动器 + appdata（高 16M 按 app_id 隔离）。
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS). SPDX-License-Identifier: Apache-2.0
 */

#include "sdgoods_app_sdk.h"
#include "sdgoods_launcher.h"
#include "sdgoods_launcher_int.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

static const char *TAG = "sdg_app_sdk";
static wl_handle_t s_wl = WL_INVALID_HANDLE;

/* ★ 编译期红线：appdata 的目录名必然超 FAT 的 8.3 短名限制。
 *
 * `sdgoods_appdata_begin()` 要建 /appdata/<app_id>/，而 app_id = esp_app_desc_t.project_name
 * （最长 32 字符，本工程即 "SDGOODS_EBADGE" 14 字符）。IDF 默认不开长文件名
 * （CONFIG_FATFS_LFN_NONE=y）时 f_mkdir 返回 FR_INVALID_NAME ⇒ VFS 报 EINVAL(22)
 * ⇒ 目录建不出来 ⇒ **app 的持久化数据静默落空**，而且只有 ≤8 字符的 app_id 看着正常，
 * 用短名字的示例去验证会「通过」，极难发现（2026-09-19 真机抓到）。
 * 修法：工程 sdkconfig **与** sdkconfig.defaults 都设 CONFIG_FATFS_LFN_HEAP=y。 */
#if defined(CONFIG_FATFS_LFN_NONE)
#warning "CONFIG_FATFS_LFN_NONE=y: appdata/<app_id> 目录建不出来（app_id 通常 > 8 字符）。请在 sdkconfig 与 sdkconfig.defaults 里设 CONFIG_FATFS_LFN_HEAP=y（见 docs/BUILD.md §2.1）。"
#endif

esp_err_t sdgoods_return_to_launcher(void)
{
    /* 直接复用启动器的「回 factory 并重启」逻辑（设计稿 §3）。
     * ⚠️ 模式闸门在 sdgoods_launcher_launch_slot(-1) 里：非「被启动器管理的 app」
     *    （单应用固件 / 启动器宿主）会拿到 ESP_ERR_INVALID_STATE，**不重启**。 */
    return sdgoods_launcher_return_to_launcher();
}

bool sdgoods_appdata_available(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_DATA_FAT,
                                    SDGOODS_APPDATA_PART_LABEL) != NULL;
}

esp_err_t sdgoods_appdata_mount(void)
{
    if (s_wl != WL_INVALID_HANDLE) {
        return ESP_OK;   /* 已挂载，幂等 */
    }
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
                                 SDGOODS_APPDATA_PART_LABEL);
    if (!part) {
        ESP_LOGE(TAG, "appdata partition '%s' (data/fat) not found",
                 SDGOODS_APPDATA_PART_LABEL);
        return ESP_ERR_NOT_FOUND;
    }
    esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = true,   /* 首次上电为空分区，格式化即可 */
        .max_files = 8,
        .allocation_unit_size = 4096,
    };
    wl_handle_t wl = WL_INVALID_HANDLE;
    esp_err_t r = esp_vfs_fat_spiflash_mount_rw_wl(SDGOODS_APPDATA_BASE_PATH,
                                                   SDGOODS_APPDATA_PART_LABEL, &cfg, &wl);
    if (r != ESP_OK) {
        return r;
    }
    s_wl = wl;
    return ESP_OK;
}

void sdgoods_appdata_unmount(void)
{
    if (s_wl == WL_INVALID_HANDLE) {
        return;
    }
    esp_vfs_fat_spiflash_unmount_rw_wl(SDGOODS_APPDATA_BASE_PATH, s_wl);
    s_wl = WL_INVALID_HANDLE;
}

esp_err_t sdgoods_appdata_path_for(const char *app_id, char *out, size_t out_len)
{
    if (!app_id || !app_id[0] || !out || out_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    int n = snprintf(out, out_len, "%s/%s", SDGOODS_APPDATA_BASE_PATH, app_id);
    if (n < 0 || (size_t)n >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t sdgoods_appdata_begin(const char *app_id, char *out, size_t out_len)
{
    esp_err_t r = sdgoods_appdata_mount();
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "appdata unavailable (%s) -> caller must fall back to NVS",
                 esp_err_to_name(r));
        return r;
    }
    r = sdgoods_appdata_path_for(app_id, out, out_len);
    if (r != ESP_OK) {
        return r;
    }
    if (mkdir(out, 0777) != 0 && errno != EEXIST) {
        int e = errno;
        ESP_LOGE(TAG, "mkdir('%s') failed: errno=%d", out, e);
        /* EINVAL(22) 几乎总是同一个原因：FAT 没开长文件名，而 app_id > 8 字符。
         * 直接把修法写进日志，省掉一轮「errno=22 是什么意思」的排查。 */
        if (e == EINVAL) {
            ESP_LOGE(TAG, "  ↳ EINVAL 多半是 FAT 未开长文件名：app_id '%s' 超过 8.3 短名限制。",
                     app_id);
            ESP_LOGE(TAG, "    请设 CONFIG_FATFS_LFN_HEAP=y（sdkconfig 与 sdkconfig.defaults 都要改）"
                          "，见 docs/BUILD.md §2.1");
        }
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "appdata ready for '%s': %s", app_id, out);
    return ESP_OK;
}
