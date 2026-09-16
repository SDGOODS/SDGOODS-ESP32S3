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

#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "board_pins.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"

#define LCD_WIDTH       360
#define LCD_HEIGHT      360
#define LCD_COLOR_BITS  16

#define LCD_SPI_HOST               SPI2_HOST
#define LCD_SPI_MODE               0
/* 事务队列深度 = 6（满速档）。
 * 前提：LVGL 绘制缓冲已迁回内部 SRAM（见 sdgoods_lvgl.c：单缓冲 20 行 = 14400 字节，
 * 小于实测内部 DMA 最大连续空闲块 15360，故分配在内部 SRAM，ext=0）。
 * 内部 SRAM 可被 DMA 直接读取，无需弹跳缓冲 -> 并发弹跳预算红线不再存在，
 * 队列深度可放心拉到 6，SPI 形成深流水、分片间隙被完全掩盖，整屏刷新满速。
 * 注意：若未来 LVGL 缓冲又回退到 PSRAM（ext=1），此值必须回到 <=2 否则立刻 NO_MEM 黑带。 */
#define LCD_SPI_TRANS_QUEUE_SZ     6
#define LCD_SPI_CMD_BITS           32
#define LCD_SPI_PARAM_BITS         8
/* 单次 SPI 事务最大字节数 = 8192（满速档）。
 * 前提同队列深度：LVGL 缓冲在内部 SRAM -> 无弹跳缓冲 -> 此值可尽量大以减少分片数、
 * 降低事务/ISR/回收开销。8192 下 40 行带仅 4 片、单屏约 36 片，配合队列=6 深流水极快。
 * 注意：若 LVGL 缓冲回退 PSRAM，此值必须 <=2048 否则每片弹跳缓冲过大立刻 NO_MEM 黑带。 */
#define LCD_SPI_MAX_TRANSFER_SIZE  8192

#define LEDC_HS_TIMER        LEDC_TIMER_0
#define LEDC_LS_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_HS_CH0_CHANNEL  LEDC_CHANNEL_0
#define LEDC_ResolutionRatio LEDC_TIMER_13_BIT
#define LEDC_MAX_Duty        ((1 << LEDC_ResolutionRatio) - 1)

extern esp_lcd_panel_handle_t panel_handle;

void sdgoods_lcd_init_panel(void);
void sdgoods_lcd_apply_vendor_madctl(void);
esp_err_t sdgoods_lcd_draw_bitmap_safe(int x1, int y1, int x2, int y2, const void *color_data);
/* 仅持指针，前向声明避免 sdgoods_lcd.h 强依赖 lvgl.h（实现见 sdgoods_lcd.c，已包含 lvgl.h） */
typedef struct _lv_disp_drv_t lv_disp_drv_t;
void sdgoods_lcd_set_lvgl_drv(lv_disp_drv_t *drv);
void sdgoods_lcd_set_backlight(uint8_t light);
uint8_t sdgoods_lcd_get_backlight(void);
