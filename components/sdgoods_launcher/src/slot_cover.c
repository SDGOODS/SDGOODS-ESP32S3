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

#include <stdio.h>
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

/* 一张封面的像素数据长度（RGB565 平面 + A8 平面）。 */
#define COVER_PIX_BYTES (COVER_EDGE * COVER_EDGE * 3u)

/* ---- 头校验：load() 与 check() **共用同一份实现** ----------------------------------
 *
 * 抽出来的理由不是"少写几行"，而是**两条路的容错度必须一致**：
 * 槽尾封面（load）与推送列表封面（check，平台下发的同一个格式）如果各写一份校验，
 * 迟早会漂移 —— 症状是「装进槽能显示、推送列表里显示不出来」或反过来，极难倒推。
 *
 * `what` 只进日志（如 "slot 3" / "push/abc.cov"），不参与判断。
 * 判据顺序：magic 先判（没写过 = 全 0xFF，这一步就出去了），再看版本/格式/尺寸，
 * 最后才是长度 —— 越贵的检查越靠后。 */
static esp_err_t cover_hdr_check(const cover_hdr_t *hdr, const char *what)
{
    if (memcmp(hdr->magic, COVER_MAGIC, sizeof(COVER_MAGIC)) != 0) {
        return ESP_ERR_NOT_FOUND;   /* 没有封面：最常见的正常分支，不打日志 */
    }
    if (hdr->version != SDGOODS_SLOT_COVER_VERSION || hdr->format != SDGOODS_SLOT_COVER_FMT_RGB565A8) {
        ESP_LOGW(TAG, "%s: unsupported cover v%u fmt%u", what,
                 (unsigned)hdr->version, (unsigned)hdr->format);
        return ESP_ERR_NOT_FOUND;
    }
    if (hdr->width != COVER_EDGE || hdr->height != COVER_EDGE) {
        ESP_LOGW(TAG, "%s: cover is %ux%u, expected %ux%u", what,
                 (unsigned)hdr->width, (unsigned)hdr->height, (unsigned)COVER_EDGE, (unsigned)COVER_EDGE);
        return ESP_ERR_NOT_FOUND;
    }
    if (hdr->data_len != COVER_PIX_BYTES
        || (uint32_t)SDGOODS_SLOT_COVER_HDR_BYTES + COVER_PIX_BYTES > SDGOODS_SLOT_COVER_RESERVE_BYTES) {
        ESP_LOGW(TAG, "%s: cover data_len %u != %u", what, (unsigned)hdr->data_len,
                 (unsigned)COVER_PIX_BYTES);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

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

esp_err_t sdgoods_slot_cover_check(const void *blob, size_t len)
{
    if (!blob || len < sizeof(cover_hdr_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    const cover_hdr_t *hdr = (const cover_hdr_t *)blob;

    esp_err_t r = cover_hdr_check(hdr, "blob");
    if (r != ESP_OK) {
        return r;
    }
    /* 头说像素有 data_len 字节 ⇒ 实长必须够（HTTP 下到一半 / 文件被截断）。 */
    if (len < sizeof(cover_hdr_t) + COVER_PIX_BYTES) {
        ESP_LOGW(TAG, "blob: truncated cover: %u < %u", (unsigned)len,
                 (unsigned)(sizeof(cover_hdr_t) + COVER_PIX_BYTES));
        return ESP_ERR_INVALID_ARG;
    }
    /* CRC 兜住「写了一半掉电 / 只擦了没写 / 传输被截断」这几种残块。 */
    const uint32_t crc = esp_rom_crc32_le(0, (const uint8_t *)blob + SDGOODS_SLOT_COVER_HDR_BYTES,
                                          COVER_PIX_BYTES);
    if (crc != hdr->crc32) {
        ESP_LOGW(TAG, "blob: cover crc 0x%08X != 0x%08X -> rejected", (unsigned)crc, (unsigned)hdr->crc32);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t sdgoods_slot_cover_adopt(void *blob, size_t len, sdgoods_slot_cover_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!blob) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t r = sdgoods_slot_cover_check(blob, len);
    if (r != ESP_OK) {
        /* ⚠️ 失败**不接管**：blob 仍归调用方，这里绝不能 free —— 见头文件的契约。 */
        return r;
    }

    const cover_hdr_t *hdr = (const cover_hdr_t *)blob;
    out->width    = hdr->width;
    out->height   = hdr->height;
    out->data_len = hdr->data_len;
    out->data     = (const uint8_t *)blob + SDGOODS_SLOT_COVER_HDR_BYTES;
    out->owner    = blob;      /* 接管：sdgoods_slot_cover_free() 会一并 free */
    return ESP_OK;
}

void sdgoods_slot_cover_grayscale(sdgoods_slot_cover_t *cover)
{
    if (!cover || !cover->data || cover->data_len < COVER_PIX_BYTES) {
        return;
    }
    /* 缓冲是 _adopt / _load 拿到的堆内存，内容归我们所有 ⇒ 就地改是安全的。
     * 结构体里的 data 是 const 只是为了不让**消费者**改它。 */
    uint8_t *plane = (uint8_t *)cover->data;

    /* 只遍历 RGB565 平面（2 字节/像素），A8 平面（1 字节/像素，紧随其后）原样保留 ——
     * 圆形遮罩的 alpha 与灰度无关。 */
    const uint32_t px = (uint32_t)cover->width * (uint32_t)cover->height;
    for (uint32_t i = 0; i < px; i++) {
        const uint8_t *p = plane + i * 2u;
        /* 🔴 先把落盘时对调过的两个字节还原成 RGB565，再拆通道。
         *    生成端一律 `byte0 = v>>8, byte1 = v&0xff`（见生成端 buildCoverBlock），
         *    所以还原就是 `v = (byte0 << 8) | byte1`。
         *    顺序写反 ⇒ R/B 通道互换 ⇒ 灰值偏色（灰度图上看不出来，但真机一测就露）。 */
        const uint16_t v  = (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
        const uint8_t  r5 = (uint8_t)((v >> 11) & 0x1Fu);
        const uint8_t  g6 = (uint8_t)((v >> 5)  & 0x3Fu);
        const uint8_t  b5 = (uint8_t)(v & 0x1Fu);
        /* 5/6 位 → 8 位：低位用高位补齐（`(x<<3)|(x>>2)`），比单纯左移更接近线性。 */
        const uint8_t  r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
        const uint8_t  g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
        const uint8_t  b8 = (uint8_t)((b5 << 3) | (b5 >> 2));
        /* BT.601 整数亮度（权值和 = 256 ⇒ 右移 8 位）。 */
        const uint8_t  y  = (uint8_t)((77u * r8 + 150u * g8 + 29u * b8) >> 8);
        /* 灰值再压回 565（R/B 取高 5 位、G 取高 6 位），并**按原约定对调写回**。 */
        const uint8_t  gy5 = (uint8_t)(y >> 3);
        const uint8_t  gy6 = (uint8_t)(y >> 2);
        const uint16_t gv  = (uint16_t)(((uint16_t)gy5 << 11) | ((uint16_t)gy6 << 5) | gy5);
        plane[i * 2u]      = (uint8_t)((gv >> 8) & 0xFFu);
        plane[i * 2u + 1u] = (uint8_t)(gv & 0xFFu);
    }
}

esp_err_t sdgoods_slot_cover_save(int slot_idx, const void *blob, size_t len)
{
    /* ① 整块校验（头 + 长度 + CRC）。坏块绝不落盘 —— 否则每次建主页都要白读一次 52 KB，
     *    而且屏上永远是"没有封面"，现场根本分不清是没写进去还是写了块坏的。 */
    esp_err_t r = sdgoods_slot_cover_check(blob, len);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "slot %d: refuse to save an invalid cover block (%s)",
                 slot_idx, esp_err_to_name(r));
        return r;
    }
    /* 写 flash = 改设备状态，闸门与 uninstall / install_* 同口径：只有启动器宿主的
     * 安装流程才有资格。app 工程 vendor 了同一份组件，不加这道闸门的话一个手滑的
     * 调用就能把别的槽的 app 尾部擦掉。 */
    if (!sdgoods_device_is_launcher_host()) {
        ESP_LOGE(TAG, "slot %d: cover save refused (not the launcher host)", slot_idx);
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t *part = sdgoods_slot_partition(slot_idx);
    if (!part) {
        return ESP_ERR_INVALID_ARG;
    }
    if (part->size < SDGOODS_SLOT_COVER_OFFSET_FROM_END) {
        ESP_LOGE(TAG, "slot %d: partition %u B < cover reserve %u B",
                 slot_idx, (unsigned)part->size, (unsigned)SDGOODS_SLOT_COVER_OFFSET_FROM_END);
        return ESP_ERR_NOT_FOUND;
    }

    /* ② 封面署谁的名：**以 flash 上的 app 镜像为准**（manifest 缓存在平台安装路径上
     *    存的是平台 Firmware.id，与 project_name 不是同一个标识 —— 用错了就会被
     *    自己的 _load() 拒掉）。 */
    char proj[SDGOODS_SLOT_APPID_MAX] = { 0 };
    if (!sdgoods_slot_read_desc(slot_idx, proj, NULL, NULL) || proj[0] == '\0') {
        ESP_LOGW(TAG, "slot %d: no readable app desc -> cover not saved", slot_idx);
        return ESP_ERR_NOT_FOUND;
    }

    /* ③ 头在**本地副本**里改 app_id：不动调用方的缓冲。 */
    cover_hdr_t hdr;
    memcpy(&hdr, blob, sizeof(hdr));
    memset(hdr.app_id, 0, sizeof(hdr.app_id));
    memcpy(hdr.app_id, proj, strnlen(proj, sizeof(hdr.app_id) - 1));

    const uint32_t cover_off = (uint32_t)part->size - SDGOODS_SLOT_COVER_OFFSET_FROM_END;

    /* ④ 擦 → 写头 → 写像素。擦的是**整块保留区**（不是只擦用到的 52336 字节）：
     *    保留区按扇区对齐、大小也是扇区的整数倍，整块擦省得算尾部零头。 */
    r = esp_partition_erase_range(part, cover_off, SDGOODS_SLOT_COVER_RESERVE_BYTES);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "slot %d: erase cover region failed (%s)", slot_idx, esp_err_to_name(r));
        return r;
    }
    r = esp_partition_write(part, cover_off, &hdr, sizeof(hdr));
    if (r == ESP_OK) {
        r = esp_partition_write(part, cover_off + SDGOODS_SLOT_COVER_HDR_BYTES,
                                (const uint8_t *)blob + SDGOODS_SLOT_COVER_HDR_BYTES,
                                COVER_PIX_BYTES);
    }
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "slot %d: write cover failed (%s)", slot_idx, esp_err_to_name(r));
        return r;
    }

    /* ⑤ 回读 magic 确认：擦除会把整块写成 0xFF，写失败时 magic 就不在。
     *    这一步专治「erase/write 都返回 OK 却什么也没落地」——那种失败在这块板上
     *    是**不报错**的，只会表现为"封面写了但图标还是字母"。 */
    char back[8];
    if (esp_partition_read(part, cover_off, back, sizeof(back)) != ESP_OK
        || memcmp(back, COVER_MAGIC, sizeof(back)) != 0) {
        ESP_LOGE(TAG, "slot %d: cover written but magic not readable back", slot_idx);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "slot %d: cover saved for app '%s' (crc 0x%08X)",
             slot_idx, proj, (unsigned)hdr.crc32);
    return ESP_OK;
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

    char what[24];
    snprintf(what, sizeof(what), "slot %d", slot_idx);

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
        ESP_LOGW(TAG, "%s: read cover header failed: %s", what, esp_err_to_name(r));
        return ESP_ERR_NOT_FOUND;
    }

    r = cover_hdr_check(&hdr, what);
    if (r != ESP_OK) {
        return r;
    }
    /* 槽被复用（今天 A、明天 B）时旧封面会留着 —— 名字对不上就当没有封面。
     * ⚠️ 这一条是**槽尾封面独有**的判据（推送列表那份没有归属的槽，见 .h 的说明）。 */
    hdr.app_id[SDGOODS_SLOT_APPID_MAX - 1] = '\0';
    if (strcmp(hdr.app_id, proj) != 0) {
        ESP_LOGI(TAG, "slot %d: cover belongs to '%s', slot holds '%s' -> ignored",
                 slot_idx, hdr.app_id, proj);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *buf = heap_caps_malloc(COVER_PIX_BYTES, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "slot %d: no PSRAM for %u-byte cover", slot_idx, (unsigned)COVER_PIX_BYTES);
        return ESP_ERR_NO_MEM;
    }
    r = esp_partition_read(part, cover_off + SDGOODS_SLOT_COVER_HDR_BYTES, buf, COVER_PIX_BYTES);
    if (r != ESP_OK) {
        heap_caps_free(buf);
        return ESP_ERR_NOT_FOUND;
    }
    uint32_t crc = esp_rom_crc32_le(0, buf, COVER_PIX_BYTES);
    if (crc != hdr.crc32) {
        ESP_LOGW(TAG, "slot %d: cover crc 0x%08X != 0x%08X -> ignored (partial write?)",
                 slot_idx, (unsigned)crc, (unsigned)hdr.crc32);
        heap_caps_free(buf);
        return ESP_ERR_NOT_FOUND;
    }

    out->width    = hdr.width;
    out->height   = hdr.height;
    out->data_len = COVER_PIX_BYTES;
    out->data     = buf;
    out->owner    = buf;
    ESP_LOGI(TAG, "slot %d: cover loaded for '%s' (%ux%u)", slot_idx, proj,
             (unsigned)hdr.width, (unsigned)hdr.height);
    return ESP_OK;
}
