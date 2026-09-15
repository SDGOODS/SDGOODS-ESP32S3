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
 * @brief 一键截屏：LVGL 快照 -> PSRAM -> base64 -> USB 串口 -> 电脑端 PNG。
 *
 * 两种触发方式：
 *   1) 设备端：顶部下滑打开「菜单」-> 点「截屏」按钮；
 *   2) 电脑端：向串口写入一个字符 's'（例如 `python3 screenshot_recv.py -t`）。
 *
 * 传输协议（纯文本行，便于走 console）：
 *   ===SHOT-BEGIN w=360 h=360 bpp=16 swap=1 bytes=259200 lines=4548===
 *   0000:<76 个 base64 字符>
 *   0001:<76 个 base64 字符>
 *   ...
 *   ===SHOT-END===
 *
 * 每行前缀是该行序号（4 位十六进制）。电脑端按序号拼装、校验行数是否齐全，
 * 这样即使有日志行混进来（会被过滤掉）也不会静默拼出错位的图片。
 * 传输期间还会临时把所有日志静音，进一步避免交错。
 *
 * 线程模型：LVGL 是单线程（app_main 里的 lvgl_port_loop 调用 lv_timer_handler）。
 *   因此任何线程（含串口接收任务）都只置一个标志位 s_req_flag，
 *   真正的抓帧动作由 LVGL 线程里的 150ms 轮询定时器消费标志后执行 —— 避免跨线程
 *   调用 LVGL API 造成链表/内存竞争。
 */

#include "screenshot.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
/* 直接轮询 USB-Serial-JTAG 的 RX FIFO 实现「串口 's' 触发」。
 * 不能装 usb_serial_jtag driver（本工程该外设被次級 console 占用，driver_install 会被拒），
 * 但 console 只用它做输出（ROM putc），RX FIFO 由我们自己读是安全的。 */
#include "hal/usb_serial_jtag_ll.h"
#include "lvgl.h"
#include "extra/others/snapshot/lv_snapshot.h"   /* lv_snapshot_take_to_buf（LV_USE_SNAPSHOT=y） */

static const char *TAG = "shot";

#define SHOT_POLL_MS        150   /* LVGL 线程轮询触发标志的周期 */
#define SHOT_B64_PER_LINE   76    /* 每行 base64 字符数（4 的倍数，便于逐行解码） */

static uint8_t      *s_buf      = NULL;   /* PSRAM 帧缓冲（RGB565） */
static uint32_t      s_buf_size = 0;
static lv_img_dsc_t  s_dsc;
static bool          s_sending  = false;  /* 正在传输（防重入） */
static volatile bool s_req_flag = false;  /* 截屏请求标志（任意线程置位，LVGL 线程消费） */

/* base64 编码：src(n 字节) -> dst，返回写入的字符数（不写结束符） */
static int b64_encode(const uint8_t *src, int n, char *dst)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < n) v |= (uint32_t)src[i + 1] << 8;
        if (i + 2 < n) v |= (uint32_t)src[i + 2];
        dst[o++] = T[(v >> 18) & 0x3F];
        dst[o++] = T[(v >> 12) & 0x3F];
        dst[o++] = (i + 1 < n) ? T[(v >> 6) & 0x3F] : '=';
        dst[o++] = (i + 2 < n) ? T[v & 0x3F] : '=';
    }
    return o;
}

/* 串口发送任务：把 s_buf 里的位图分块 base64 输出 */
static void shot_send_task(void *arg)
{
    (void)arg;
    const uint32_t bytes    = (uint32_t)s_dsc.header.w * (uint32_t)s_dsc.header.h * 2;  /* RGB565: 2 B/px */
    const uint32_t per_line = SHOT_B64_PER_LINE / 4 * 3;                               /* 每行原始字节数 = 57 */
    const uint32_t lines    = (bytes + per_line - 1) / per_line;

    /* 传输期间静音日志：ESP_LOG 和数据行走同一个串口，交错会破坏 base64 行 */
    esp_log_level_set("*", ESP_LOG_NONE);

    printf("===SHOT-BEGIN w=%d h=%d bpp=16 swap=%d bytes=%u lines=%u===\n",
           (int)s_dsc.header.w, (int)s_dsc.header.h, (int)LV_COLOR_16_SWAP,
           (unsigned)bytes, (unsigned)lines);
    fflush(stdout);

    char line[8 + SHOT_B64_PER_LINE + 4];
    uint32_t seq = 0;
    for (uint32_t off = 0; off < bytes; off += per_line, seq++) {
        int n = (int)((bytes - off >= per_line) ? per_line : (bytes - off));
        int p = snprintf(line, sizeof(line), "%04X:", (unsigned)seq);
        p += b64_encode(s_buf + off, n, line + p);
        line[p++] = '\n';
        fwrite(line, 1, (size_t)p, stdout);
        if ((seq & 15) == 0) {
            fflush(stdout);   /* 每 16 行 flush 一次 */
        }
    }
    fflush(stdout);
    printf("===SHOT-END===\n");
    fflush(stdout);

    esp_log_level_set("*", ESP_LOG_INFO);   /* 恢复日志 */
    ESP_LOGI(TAG, "sent %u bytes (%u lines)", (unsigned)bytes, (unsigned)lines);

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
    ESP_LOGI(TAG, "captured %dx%d, sending over USB serial...",
             (int)s_dsc.header.w, (int)s_dsc.header.h);

    s_sending = true;
    if (xTaskCreatePinnedToCore(shot_send_task, "shot_tx", 4096, NULL, 5, NULL, tskNO_AFFINITY) != pdPASS) {
        s_sending = false;
        ESP_LOGE(TAG, "send task create failed");
    }
}

/* 串口接收任务：轮询 USB-Serial-JTAG 的 RX FIFO，收到 's'/'S' 即请求一次截屏。
 * 注意：只置标志，不直接调 LVGL API（LVGL 非线程安全）。 */
static void shot_rx_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "serial trigger ready (send 's' over USB serial to take a screenshot)");

    for (;;) {
        if (usb_serial_jtag_ll_rxfifo_data_available()) {
            uint8_t buf[32];
            uint32_t n = usb_serial_jtag_ll_read_rxfifo(buf, sizeof(buf));
            for (uint32_t i = 0; i < n; i++) {
                if (buf[i] == 's' || buf[i] == 'S') {
                    ESP_LOGI(TAG, "serial trigger: capture");
                    s_req_flag = true;
                } else if (buf[i] >= 32 && buf[i] < 127) {
                    ESP_LOGI(TAG, "serial rx '%c' (send 's' to capture)", buf[i]);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void screenshot_init(void)
{
    static bool inited = false;
    if (inited) {
        return;
    }
    inited = true;

    lv_timer_create(shot_poll_cb, SHOT_POLL_MS, NULL);
    if (xTaskCreate(shot_rx_task, "shot_rx", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "rx task create failed -> serial trigger unavailable (menu button still works)");
    }
    ESP_LOGI(TAG, "init: poll %dms + serial trigger 's'", SHOT_POLL_MS);
}

/* 任意线程可调用：只置标志，真正的抓帧在 LVGL 线程完成 */
void screenshot_capture_async(void)
{
    s_req_flag = true;
}
