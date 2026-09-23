/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（板级支持包 BSP）· 多应用启动器
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

/* slot_cover.c —— 读「槽尾保留区」里的主页封面图。块格式见 sdgoods_slot_cover.h。 */

#include "sdgoods_slot_cover.h"

#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"

#include "sdgoods_launcher_int.h"

static const char *TAG = "sdg_cover";

/* 块头：布局必须与 sdgoods_slot_cover.h 的表格逐字节一致（下面有 static_assert）。 */
typedef struct __attribute__((packed)) {
    char     magic[8];                          /* "SDGCOVER" */
    uint8_t  version;
    uint8_t  format;
    uint16_t width;
    uint16_t height;
    uint16_t reserved;
    uint32_t data_len;
    uint32_t crc32;
    char     app_id[SDGOODS_SLOT_APPID_MAX];    /* = esp_app_desc_t.project_name */
} cover_hdr_t;

_Static_assert(sizeof(cover_hdr_t) == SDGOODS_SLOT_COVER_HDR_BYTES,
               "封面块头必须是 64 字节，与 sdgoods_slot_cover.h 的格式表一致");
_Static_assert(sizeof(cover_hdr_t) + SDGOODS_SLOT_COVER_EDGE * SDGOODS_SLOT_COVER_EDGE * 3
                   <= SDGOODS_SLOT_COVER_RESERVE_BYTES,
               "一张封面必须塞得进槽尾保留区（132×132×3 + 64 = 52336 ≤ 57344）");

static const char COVER_MAGIC[8] = { 'S', 'D', 'G', 'C', 'O', 'V', 'E', 'R' };

/* 固定边长（132）的路由：读回来的封面必须正好是这个边长。
 * 生成端输出别的尺寸时，这里判失败 ⇒ 主页回退「灰色圆盘 + 字母」，而不是画出一张
 * 溢出圆屏的图。改边长要同时改生成端（Python / 浏览器）与 ui_launcher.c 的图标直径。 */
#define COVER_EDGE ((uint32_t)SDGOODS_SLOT_COVER_EDGE)

void sdgoods_slot_cover_free(sdgoods_slot_cover_t *cover)
{
    if (!cover) {
        return;
    }
    if (cover->owner) {
        heap_caps_free(cover->owner);
    }
    memset(cover, 0, sizeof(*cover));
}

esp_err_t sdgoods_slot_cover_load(int slot_idx, sdgoods_slot_cover_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    const esp_partition_t *part = sdgoods_slot_partition(slot_idx);
    if (!part) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 槽里没有有效 app 就不用往下看了 —— 封面只有在「知道它是谁的」前提下才有意义。 */
    char proj[SDGOODS_SLOT_APPID_MAX] = { 0 };
    if (!sdgoods_slot_read_desc(slot_idx, proj, NULL, NULL) || proj[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }

    /* 保留区不可能小于块头 + 一张封面；分区表被改小过时直接放弃，别算到分区外去。 */
    if (part->size < SDGOODS_SLOT_COVER_OFFSET_FROM_END) {
        return ESP_ERR_NOT_FOUND;
    }
    const uint32_t cover_off = (uint32_t)part->size - SDGOODS_SLOT_COVER_OFFSET_FROM_END;

    cover_hdr_t hdr;
    esp_err_t r = esp_partition_read(part, cover_off, &hdr, sizeof(hdr));
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "slot %d: read cover header failed: %s", slot_idx, esp_err_to_name(r));
        return ESP_ERR_NOT_FOUND;
    }

    /* 判据顺序：magic 先判（未写过 = 全 0xFF，这一步就出去了），
     * 再看版本/格式/尺寸，最后才是 app_id 与 crc —— 越贵的检查越靠后。 */
    if (memcmp(hdr.magic, COVER_MAGIC, sizeof(COVER_MAGIC)) != 0) {
        return ESP_ERR_NOT_FOUND;   /* 该槽没有封面：最常见的正常分支，不打日志 */
    }
    if (hdr.version != SDGOODS_SLOT_COVER_VERSION || hdr.format != SDGOODS_SLOT_COVER_FMT_RGB565A8) {
        ESP_LOGW(TAG, "slot %d: unsupported cover v%u fmt%u", slot_idx,
                 (unsigned)hdr.version, (unsigned)hdr.format);
        return ESP_ERR_NOT_FOUND;
    }
    if (hdr.width != COVER_EDGE || hdr.height != COVER_EDGE) {
        ESP_LOGW(TAG, "slot %d: cover is %ux%u, expected %ux%u", slot_idx,
                 (unsigned)hdr.width, (unsigned)hdr.height, (unsigned)COVER_EDGE, (unsigned)COVER_EDGE);
        return ESP_ERR_NOT_FOUND;
    }
    const uint32_t need = COVER_EDGE * COVER_EDGE * 3u;   /* RGB565 平面 + A8 平面 */
    if (hdr.data_len != need
        || (uint32_t)SDGOODS_SLOT_COVER_HDR_BYTES + need > SDGOODS_SLOT_COVER_RESERVE_BYTES) {
        ESP_LOGW(TAG, "slot %d: cover data_len %u != %u", slot_idx, (unsigned)hdr.data_len,
                 (unsigned)need);
        return ESP_ERR_NOT_FOUND;
    }
    /* 槽被复用（今天 A、明天 B）时旧封面会留着 —— 名字对不上就当没有封面。 */
    hdr.app_id[SDGOODS_SLOT_APPID_MAX - 1] = '\0';
    if (strcmp(hdr.app_id, proj) != 0) {
        ESP_LOGI(TAG, "slot %d: cover belongs to '%s', slot holds '%s' -> ignored",
                 slot_idx, hdr.app_id, proj);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *buf = heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "slot %d: no PSRAM for %u-byte cover", slot_idx, (unsigned)need);
        return ESP_ERR_NO_MEM;
    }
    r = esp_partition_read(part, cover_off + SDGOODS_SLOT_COVER_HDR_BYTES, buf, need);
    if (r != ESP_OK) {
        heap_caps_free(buf);
        return ESP_ERR_NOT_FOUND;
    }
    /* CRC 兜住「写了一半掉电 / 只擦了没写」这两种残块。 */
    uint32_t crc = esp_rom_crc32_le(0, buf, need);
    if (crc != hdr.crc32) {
        ESP_LOGW(TAG, "slot %d: cover crc 0x%08X != 0x%08X -> ignored (partial write?)",
                 slot_idx, (unsigned)crc, (unsigned)hdr.crc32);
        heap_caps_free(buf);
        return ESP_ERR_NOT_FOUND;
    }

    out->width    = hdr.width;
    out->height   = hdr.height;
    out->data_len = need;
    out->data     = buf;
    out->owner    = buf;
    ESP_LOGI(TAG, "slot %d: cover loaded for '%s' (%ux%u)", slot_idx, proj,
             (unsigned)hdr.width, (unsigned)hdr.height);
    return ESP_OK;
}
