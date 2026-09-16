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
 * @file sdgoods_console.c
 * @brief BSP 串口控制台：常驻命令分发。
 *
 * 直接轮询 USB-Serial-JTAG 的 RX FIFO 实现命令触发（不装 usb_serial_jtag driver，
 * 因为该外设被次级 console 占用，driver_install 会被拒；但 RX/TX FIFO 由我们自己
 * 读写是安全的）。本任务只做命令分发，真正的截屏抓帧在 LVGL 线程完成。
 */

#include "sdgoods_console.h"
#include "sdgoods_caps.h"
#include "sdgoods_screenshot.h"   /* sdgoods_screenshot_capture（仅在能力启用时调用） */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
/* 关闭控制台 VFS 对 TX 的 CRLF 转换：截屏二进制里的 0x0A 不能被膨胀成 0x0D 0x0A，
 * 否则破坏帧边界、挤掉 JPEG EOI。改为 LF 即「不修改」，二进制才能逐字节对齐。 */
#include "esp_vfs_common.h"                       /* esp_line_endings_t / ESP_LINE_ENDINGS_LF */
void usb_serial_jtag_vfs_set_tx_line_endings(esp_line_endings_t mode);
#include "hal/usb_serial_jtag_ll.h"

static const char *TAG = "bsp-console";

/* 串口命令分发：'?' 查能力；'s'/'S' 触发截屏（若能力已登记）。 */
static void console_rx_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "ready (send '?' for capabilities, 's' to capture if enabled)");

    for (;;) {
        if (usb_serial_jtag_ll_rxfifo_data_available()) {
            uint8_t buf[32];
            uint32_t n = usb_serial_jtag_ll_read_rxfifo(buf, sizeof(buf));
            for (uint32_t i = 0; i < n; i++) {
                if (buf[i] == '?') {
                    /* 能力查询：静音日志避免与回传行交错，回传 SDGOODS-CAPS: 一行 */
                    esp_log_level_set("*", ESP_LOG_NONE);
                    printf("SDGOODS-CAPS:%s\n", sdgoods_caps_names());
                    fflush(stdout);
                    esp_log_level_set("*", ESP_LOG_INFO);
                } else if (buf[i] == 's' || buf[i] == 'S') {
#ifdef CONFIG_SDGOODS_SCREENSHOT
                    if (sdgoods_caps_has(SDGOODS_CAP_SCREENSHOT)) {
                        sdgoods_screenshot_capture();
                    } else {
                        ESP_LOGW(TAG, "screenshot capability not enabled");
                    }
#else
                    ESP_LOGW(TAG, "screenshot not built into this firmware");
#endif
                } else if (buf[i] >= 32 && buf[i] < 127) {
                    ESP_LOGI(TAG, "serial rx '%c'", buf[i]);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void sdgoods_console_init(void)
{
    static bool inited = false;
    if (inited) {
        return;
    }
    inited = true;

    /* 关闭 TX 的 CRLF 转换（详见文件头说明）。此设置全局，对普通日志无影响。 */
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);

    if (xTaskCreate(console_rx_task, "bsp_console", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "rx task create failed");
    }
}
