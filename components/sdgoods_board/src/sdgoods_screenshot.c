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

/**
 * @file screenshot.c
 * @brief 一键截屏：LVGL 快照 -> PSRAM -> JPEG -> USB 串口 -> 电脑端 .jpg。
 *
 * 触发方式：串口收到 's'（由 BSP 串口控制台 sdgoods_console 分发，调用本文件的
 *   sdgoods_screenshot_capture()）；应用里也可以直接调用 sdgoods_screenshot_capture()。
 *
 * 本文件只负责「抓帧 + 编码 + 发送」；串口的 RX 轮询与命令分发已统一收归到
 * sdgoods_console.c（始终编译），这样即便截屏能力被关掉，'?' 能力查询仍能应答。
 *
 * 传输协议（BEGIN / END 是文本行，中间的像素是**原始二进制**，不做任何编码）：
 *   ===SHOT-BEGIN w=360 h=360 bpp=16 fmt=1 swap=0 bytes=24567===
 *   <bytes 个 JPEG 字节（fmt=1）或原始 RGB565 字节（fmt=0），不靠换行分帧>
 *   ===SHOT-END===
 *   fmt=1：JPEG（设备端用 vendored jpegenc 直接把 RGB565 编码成 JPEG，PC 端直接落 .jpg）。
 *          注意：编码前要把 LVGL 快照（LV_COLOR_16_SWAP=1，高字节在前）就地换回标准
 *          RGB565 字节序，否则编码器按小端 uint16 读会 R/B 错乱（灰底变绿）。
 *   fmt=0：原始 RGB565（编码失败时的兜底，保持 LVGL 交换字节序、头里带 swap=1，PC 端转 PNG）。
 *          旧固件无 fmt 字段，按 0 处理。
 *
 * 为什么不用 base64：base64 让数据膨胀 33%，且每字节都要编解码，是过去 33 秒
 * 耗时（约 10.5 KB/s）的主因。改成「文本头 + 定长原始二进制 + 文本尾」后，体积
 * 直接回到原始字节数，设备端也省掉编码 CPU（JPEG 编码另算）。二进制里可能含
 * 0x0A/0x0D，所以像素部分**不靠换行分帧**，而是靠 BEGIN 头里声明的 bytes 字段
 * 「精确读取那么多字节」，END 标记只用于收尾校验——彻底避免半行被当成整行。
 *
 * 速度：走设备端 JPEG 后，360x360 的 UI 画面典型 20~50KB，链路 ~10.5 KB/s 下约
 * 2~5 秒（而原始 RGB565 为 259KB、约 25 秒）。JPEG 缓冲分配失败时自动退回 RGB565。
 *
 * 传输通道：经控制台 stdout 写出原始二进制 blob（不再 base64 编码）。USB-Serial-JTAG
 *   的链路上限约 10.5 KB/s。
 *
 * 设备端反馈：抓帧在 LVGL 线程完成（保证截到的图不含遮罩），随后在顶层创建一个
 * 「截图中…/Capturing…」浮层 + 进度条；发送任务每写出一些字节就更新 s_sent_bytes，
 * LVGL 线程的 100ms 定时器把进度条推到对应百分比；传输结束（s_sending=false）后
 * 同一个定时器删掉浮层。截屏时屏幕会明显在动，不会让人以为是卡死。
 *
 * 线程模型：LVGL 是单线程。串口控制台只置 s_req_flag；真正的抓帧与所有 LVGL
 * 对象操作都在 LVGL 线程里完成，避免跨线程调用 LVGL API 造成竞争。
 */

#include "sdgoods_screenshot.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "extra/others/snapshot/lv_snapshot.h"   /* lv_snapshot_take_to_buf（LV_USE_SNAPSHOT=y） */
#include "sdgoods_i18n.h"
#include "sdgoods_jpeg.h"                          /* 设备端 JPEG 编码（vendored jpegenc） */
#include "sdgoods_caps.h"                          /* 登记 SDGOODS_CAP_SCREENSHOT 基础能力 */

LV_FONT_DECLARE(si_yuan_black_icon_16);   /* 浮层文案（中文走 cn_font 兜底） */
LV_FONT_DECLARE(cn_font_16);

static const char *TAG = "shot";

#define SHOT_POLL_MS     150   /* LVGL 线程轮询触发标志的周期 */
#define SHOT_UI_MS       100   /* 进度浮层刷新周期 */

static uint8_t      *s_buf      = NULL;   /* PSRAM 帧缓冲（RGB565） */
static uint32_t      s_buf_size = 0;
static lv_img_dsc_t  s_dsc;
static bool          s_sending  = false;  /* 正在传输（防重入） */
static volatile bool s_req_flag = false;  /* 截屏请求标志（任意线程置位，LVGL 线程消费） */

/* 进度反馈：发送任务写 s_sent_bytes，LVGL 线程的 UI 定时器读它推进度条 */
static volatile uint32_t s_sent_bytes = 0;
static uint32_t          s_total_bytes = 0;
static lv_obj_t         *s_overlay = NULL;
static lv_obj_t         *s_bar = NULL;

/* 设备端 JPEG：把 s_buf(RGB565) 编码进 s_jbuf，再发送 s_jbuf（fmt=1）。
 * 编码失败则退回发送原始 RGB565（fmt=0）。发送任务只读 s_payload/s_payload_len。 */
static uint8_t      *s_jbuf = NULL;                       /* PSRAM：JPEG 输出缓冲 */
static const uint32_t SHOT_JPEG_CAP = 360u * 360u * 4u;  /* JPEG 输出上限（含余量；UI 画面远小于此） */
static const uint8_t *s_payload = NULL;                   /* 本次要发送的数据（JPEG 或原始 RGB565） */
static uint32_t       s_payload_len = 0;
static bool           s_use_jpeg = false;

/* 串口发送任务：把 s_buf 里的位图作为**原始二进制 blob**，经控制台 stdout 写出。
 *
 * 二进制里可能含 0x0A/0x0D，所以**不靠换行分帧**：接收端按 BEGIN 头里的 bytes 字段
 * 精确收齐这么多字节，再用 END 标记收尾（见 tools/screenshot_recv.py）。 */
static void shot_send_task(void *arg)
{
    (void)arg;
    const uint8_t *payload = s_payload;
    const uint32_t bytes = s_payload_len;
    const bool use_jpeg = s_use_jpeg;

    /* 传输期间静音日志：ESP_LOG 和数据走同一个串口，交错会破坏帧边界 */
    esp_log_level_set("*", ESP_LOG_NONE);

    /* fmt=1: JPEG（payload 即 JPEG 字节，PC 端直接落 .jpg）；fmt=0: 原始 RGB565。
     * 二进制里可能含 0x0A/0x0D，所以像素部分不靠换行分帧，接收端按 bytes 精确收齐。 */
    printf("===SHOT-BEGIN w=%d h=%d bpp=16 fmt=%d swap=%d bytes=%u===\n",
           (int)s_dsc.header.w, (int)s_dsc.header.h,
           use_jpeg ? 1 : 0, use_jpeg ? 0 : (int)LV_COLOR_16_SWAP, (unsigned)bytes);
    fflush(stdout);

    uint32_t off = 0;
    const uint32_t CHUNK = 512;
    while (off < bytes) {
        uint32_t n = (bytes - off > CHUNK) ? CHUNK : (bytes - off);
        fwrite(payload + off, 1, n, stdout);
        off += n;
        s_sent_bytes = off;
        if ((off & 0x3FF) == 0 || off == bytes) {   /* 每 1KB flush 一次 */
            fflush(stdout);
        }
        if ((off & 0xFFF) == 0) {                    /* 每 4KB 让 USB 排空 FIFO */
            vTaskDelay(1);
        }
    }
    fflush(stdout);

    printf("===SHOT-END===\n");
    fflush(stdout);

    esp_log_level_set("*", ESP_LOG_INFO);   /* 恢复日志 */
    ESP_LOGI(TAG, "sent %u bytes (%s)", (unsigned)bytes, use_jpeg ? "jpeg" : "raw-rgb565");

    s_sending = false;
    vTaskDelete(NULL);
}

/* 在 LVGL 线程内把整个屏幕渲染到 PSRAM 缓冲 */
static bool shot_render(void)
{
    lv_obj_t *scr = lv_scr_act();
    if (!scr) {
        ESP_LOGE(TAG, "no active screen");
        return false;
    }

    uint32_t need = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
    if (need == 0) {
        ESP_LOGE(TAG, "snapshot buf size = 0");
        return false;
    }
    if (need > s_buf_size) {
        if (s_buf) {
            heap_caps_free(s_buf);
            s_buf = NULL;
            s_buf_size = 0;
        }
        s_buf = (uint8_t *)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_buf) {
            ESP_LOGE(TAG, "PSRAM alloc %u bytes failed", (unsigned)need);
            return false;
        }
        s_buf_size = need;
    }

    if (lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &s_dsc, s_buf, s_buf_size) != LV_RES_OK) {
        ESP_LOGE(TAG, "lv_snapshot_take_to_buf failed (need=%u, buf=%u)",
                 (unsigned)need, (unsigned)s_buf_size);
        return false;
    }
    return true;
}

/* 在 LVGL 线程创建「截图中」浮层（在抓帧之后，所以截到的图不含遮罩） */
static void shot_overlay_create(uint32_t total)
{
    lv_obj_t *top = lv_layer_top();
    s_overlay = lv_obj_create(top);
    lv_obj_set_size(s_overlay, 156, 92);
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_70, 0);
    lv_obj_set_style_radius(s_overlay, 14, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);

    lv_obj_t *lbl = lv_label_create(s_overlay);
    lv_label_set_text(lbl, SDG_T("截图中…", "Capturing..."));
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &si_yuan_black_icon_16, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 18);

    s_bar = lv_bar_create(s_overlay);
    lv_obj_set_size(s_bar, 120, 10);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_white(), LV_PART_INDICATOR);

    s_total_bytes = total;
    s_sent_bytes = 0;
}

/* LVGL 线程：刷新进度浮层；传输结束后删掉浮层 */
static void shot_ui_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_overlay) {
        return;
    }
    if (s_total_bytes) {
        int pct = (int)((uint64_t)s_sent_bytes * 100u / s_total_bytes);
        if (pct > 100) pct = 100;
        lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
    }
    if (!s_sending) {   /* 发送任务已置位结束 */
        lv_obj_del(s_overlay);
        s_overlay = NULL;
        s_bar = NULL;
        s_total_bytes = 0;
        s_sent_bytes = 0;
    }
}

/* LV_COLOR_16_SWAP=1 时 LVGL 快照缓冲里每像素两字节是交换过的（高字节在前，
 * 与屏幕 QSPI 字节序一致）。而 jpegenc 的 JPEGE_PIXEL_RGB565 在小端 ESP32 上按
 * uint16 读取、要求标准 RGB565（R 在高位），直接喂会 R/B 通道错乱（灰底变绿）。
 * 编码前就地换回标准字节序；编码失败走原始 RGB565 兜底时再换回去。 */
static void shot_swap16_inplace(uint16_t *px, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint16_t v = px[i];
        px[i] = (uint16_t)((v >> 8) | (v << 8));
    }
}

/* LVGL 线程：轮询截屏请求标志（150ms 一次），命中则抓帧并交给发送任务 */
static void shot_poll_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_req_flag) {
        return;
    }
    s_req_flag = false;

    if (s_sending) {
        ESP_LOGW(TAG, "previous transfer still running, skipped");
        return;
    }
    if (!shot_render()) {
        return;
    }

    uint32_t w = s_dsc.header.w, h = s_dsc.header.h;
    uint32_t raw_bytes = w * h * 2;
    const uint8_t *payload = s_buf;
    uint32_t plen = raw_bytes;
    bool use_jpeg = false;

    if (s_jbuf) {
        shot_swap16_inplace((uint16_t *)s_buf, w * h);   /* 换回标准 RGB565 给编码器 */
        int jlen = sdgoods_jpeg_encode_rgb565(s_buf, (int)w, (int)h, s_jbuf, SHOT_JPEG_CAP);
        if (jlen > 0) {
            payload = s_jbuf;
            plen = (uint32_t)jlen;
            use_jpeg = true;
        } else {
            shot_swap16_inplace((uint16_t *)s_buf, w * h);   /* 换回去，供 raw 兜底（swap=1 语义） */
            ESP_LOGW(TAG, "jpeg encode failed (cap=%u) -> fall back to raw RGB565",
                     (unsigned)SHOT_JPEG_CAP);
        }
    }
    s_payload = payload;
    s_payload_len = plen;
    s_use_jpeg = use_jpeg;

    shot_overlay_create(plen);   /* 先上浮层，截到的图已不含遮罩 */

    ESP_LOGI(TAG, "captured %dx%d, %s %u bytes over USB serial...",
             (int)w, (int)h, use_jpeg ? "jpeg" : "raw", (unsigned)plen);

    s_sending = true;
    if (xTaskCreatePinnedToCore(shot_send_task, "shot_tx", 4096, NULL, 5, NULL, tskNO_AFFINITY) != pdPASS) {
        s_sending = false;
        if (s_overlay) {
            lv_obj_del(s_overlay);
            s_overlay = NULL;
            s_bar = NULL;
        }
        ESP_LOGE(TAG, "send task create failed");
    }
}

void sdgoods_screenshot_init(void)
{
    static bool inited = false;
    if (inited) {
        return;
    }
    inited = true;

    /* JPEG 输出缓冲（PSRAM）。分配失败时截图自动退回原始 RGB565，不影响功能。 */
    s_jbuf = (uint8_t *)heap_caps_malloc(SHOT_JPEG_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_jbuf) {
        ESP_LOGW(TAG, "jpeg buffer alloc failed (%u B) -> screenshots will send raw RGB565",
                 (unsigned)SHOT_JPEG_CAP);
    }

    lv_timer_create(shot_poll_cb, SHOT_POLL_MS, NULL);
    lv_timer_create(shot_ui_cb, SHOT_UI_MS, NULL);

    /* 登记 BSP 基础能力：网页端 '?' 查询会包含 SHOT。
     * 串口 RX 与命令分发在 sdgoods_console.c（始终编译），本文件不再自建 RX 任务。 */
    sdgoods_caps_add(SDGOODS_CAP_SCREENSHOT);

    ESP_LOGI(TAG, "init: poll %dms + ui %dms (serial trigger handled by bsp-console)",
             SHOT_POLL_MS, SHOT_UI_MS);
}

/* 任意线程可调用：只置标志，真正的抓帧在 LVGL 线程完成 */
void sdgoods_screenshot_capture(void)
{
    s_req_flag = true;
}
