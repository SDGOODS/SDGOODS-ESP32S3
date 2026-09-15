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

#include <stdint.h>
#include "driver/gpio.h"

#define BOARD_LCD_SPI_IO_TE              18
#define BOARD_LCD_SPI_IO_SCK             40
#define BOARD_LCD_SPI_IO_DATA0           46
#define BOARD_LCD_SPI_IO_DATA1           45
#define BOARD_LCD_SPI_IO_DATA2           42
#define BOARD_LCD_SPI_IO_DATA3           41
#define BOARD_LCD_SPI_IO_CS              21
#define BOARD_LCD_GPIO_RST               9
#define BOARD_LCD_POW_EN_GPIO            12
#define BOARD_LCD_GPIO_BACKLIGHT         13
#define BOARD_LCD_BACKLIGHT_LEDC_INVERT  0
#define BOARD_LCD_RGB565_TX_BYTE_SWAP    0
#define BOARD_LCD_SPI_PCLK_HZ            (80 * 1000 * 1000)
#define BOARD_LCD_RGB_ORDER_BGR          0
#define BOARD_ST77916_VENDOR_MADCTL      ((uint8_t)(0x60u | (BOARD_LCD_RGB_ORDER_BGR ? 0x08u : 0u)))

#define BOARD_BAT_CONTROL_GPIO         GPIO_NUM_7
#define BOARD_BAT_CONTROL_LATCH_LEVEL  1

#define BOARD_I2C_SCL                  10
#define BOARD_I2C_SDA                  11
#define BOARD_I2C_PORT                 1
#define BOARD_I2C_FREQ_HZ              400000

#define BOARD_TOUCH_GPIO_INT           GPIO_NUM_4
#define BOARD_TOUCH_GPIO_RST           GPIO_NUM_5

#define BOARD_KEY_GPIO                 GPIO_NUM_6
#define BOARD_KEY_ACTIVE_LEVEL         0

#define BOARD_MIC_I2S_GPIO_BCLK        GPIO_NUM_16
#define BOARD_MIC_I2S_GPIO_WS          GPIO_NUM_2
#define BOARD_MIC_I2S_GPIO_DIN         GPIO_NUM_17

#define BOARD_SPK_I2S_GPIO_BCLK        GPIO_NUM_48
#define BOARD_SPK_I2S_GPIO_WS          GPIO_NUM_38
#define BOARD_SPK_I2S_GPIO_DOUT        GPIO_NUM_47
#define BOARD_SPK_I2S_GPIO_MCLK        GPIO_NUM_15
#define BOARD_AUDIO_PA_EN_GPIO         GPIO_NUM_3
#define BOARD_AUDIO_PA_EN_ACTIVE_LEVEL 1
