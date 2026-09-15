/*
 * SDGOODS 开放平台基础工程 · 平台层（BSP）
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于平台层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

#include "lvgl_port.h"

#include "st77916.h"
#include "touch_input.h"
#include "lvgl.h"
#include "sdgoods_hooks.h"   /* sdgoods_apps_poll：应用层注册过来的轮询汇总 */

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 双缓冲放在内部 SRAM：每块 LVGL_BUF_ROWS 行，两块轮流使用。
 * 关键：flush_cb 改为「异步」——仅把数据交给 DMA 就返回，CPU 立刻去渲染另一块，
 * DMA 在后台传完由完成回调通知 LVGL（见 st77916.c 的 on_color_trans_done）。
 * 这样 CPU 渲染与 DMA 传输重叠，彻底消除此前「单缓冲同步等 DMA」的串行卡顿。
 * 每块 8 行 = 5760 字节，双块共 11520 字节 < 实测内部 DMA 最大连续空闲块(~15KB)，必能放下。
 * 若极端情况下内部 SRAM 放不下双块，回退单块 20 行同步 flush（稳但不快，避免 PSRAM 弹跳黑条）。 */
#define LVGL_BUF_ROWS   8
#define LVGL_BUF_PIXELS  (LCD_WIDTH * LVGL_BUF_ROWS)
#define LVGL_TICK_MS     2

static const char *TAG = "lvgl_port";
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static bool s_flush_async = true;   /* 双缓冲内部 SRAM 时为 true；回退单块时 false */

static void lvgl_flush_cb_async(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    /* 异步模式：仅把这块数据交给 DMA，立即返回。
     * DMA 在后台传输，完成后由 st77916.c 的 on_color_trans_done 回调调 lv_disp_flush_ready(drv)。
     * 此刻 CPU 已去渲染另一块缓冲，渲染与传输重叠 -> 流畅无卡顿。 */
    (void)drv;
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    if (err != ESP_OK) {
        /* 罕见（队列满/临时失败）：降级为同步重试，绝不静默丢块 */
        lcd_panel_draw_bitmap_safe(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    }
}

static void lvgl_flush_cb_sync(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    /* 回退模式（内部 SRAM 双块分配失败）：单块同步 flush，安全但较慢 */
    lcd_panel_draw_bitmap_safe(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    if (s_flush_async) {
        lvgl_flush_cb_async(drv, area, color_map);
    } else {
        lvgl_flush_cb_sync(drv, area, color_map);
    }
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_MS);
}

void lvgl_port_init(void)
{
    lv_init();

    /* 诊断：内部 DMA 堆最大连续空闲块，用于判断双缓冲能否放下 */
    size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "internal DMA largest free block: %d bytes (single buffer need %d, double need %d)",
             (int)dma_largest, (int)(LCD_WIDTH * LVGL_BUF_ROWS * (int)sizeof(lv_color_t)),
             (int)(LCD_WIDTH * LVGL_BUF_ROWS * (int)sizeof(lv_color_t) * 2));

    uint32_t block_rows = LVGL_BUF_ROWS;
    size_t buf_bytes = (size_t)LCD_WIDTH * block_rows * sizeof(lv_color_t);
    /* 优先：双缓冲（每块 LVGL_BUF_ROWS 行）放内部 SRAM，配异步 flush 实现满速 */
    lv_color_t *buf1 = heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *buf2 = heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf1 && buf2) {
        s_flush_async = true;
        ESP_LOGI(TAG, "draw buffer: DOUBLE %d-row/blk INTERNAL SRAM (async flush), ext1=%d ext2=%d bytes=%d",
                 (int)block_rows, (int)esp_ptr_external_ram(buf1), (int)esp_ptr_external_ram(buf2), (int)buf_bytes);
    } else {
        /* 内部 SRAM 放不下双块：回退单块 20 行同步 flush（稳但不快，绝不回退 PSRAM 双块以免弹跳黑条） */
        if (buf1) heap_caps_free(buf1);
        if (buf2) heap_caps_free(buf2);
        block_rows = 20;
        buf_bytes = (size_t)LCD_WIDTH * block_rows * sizeof(lv_color_t);
        buf1 = heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        buf2 = NULL;
        s_flush_async = false;
        if (buf1) {
            ESP_LOGW(TAG, "double internal buffer failed -> SINGLE %d-row internal (sync flush), bytes=%d",
                     (int)block_rows, (int)buf_bytes);
        } else {
            buf1 = heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            ESP_LOGW(TAG, "internal SRAM also failed -> PSRAM single %d-row (bounce, may be slow)", (int)block_rows);
        }
    }
    if (!buf1) {
        ESP_LOGE(TAG, "draw buffer alloc failed");
        abort();
    }
    lv_disp_draw_buf_init(&s_draw_buf, buf1, buf2, (uint32_t)((size_t)LCD_WIDTH * block_rows));

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_WIDTH;
    s_disp_drv.ver_res = LCD_HEIGHT;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.user_data = panel_handle;
    lv_disp_drv_register(&s_disp_drv);
    /* 让 SPI DMA 完成回调能在刷完后通知 LVGL（异步 flush 模式下必需） */
    lcd_set_lvgl_drv(&s_disp_drv);

    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));
}

void lvgl_port_loop(void)
{
    while (1) {
        power_key_poll();
        lv_timer_handler();
        /* 各应用的定时器推进（主页刷新 / 游戏物理 / 蓝牙收包 / 扫描结果…）
           由应用层汇总成一个函数注册进来，新增应用只需改 apps_registry.c，
           这个平台主循环永远不用动。 */
        sdgoods_apps_poll();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
