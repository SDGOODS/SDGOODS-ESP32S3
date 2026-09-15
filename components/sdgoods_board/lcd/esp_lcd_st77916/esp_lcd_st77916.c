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

/*
 * Minimal ST77916 QSPI panel driver.
 * Init cmds, MADCTL, draw_bitmap, disp_on_off.
 */

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_lcd_st77916.h"

#define LCD_OPCODE_WRITE_CMD   (0x02ULL)
#define LCD_OPCODE_WRITE_COLOR (0x32ULL)

static const char *TAG = "st77916";

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    uint8_t madctl_val;
    uint8_t colmod_val;
    uint8_t fb_bits_per_pixel;
    const st77916_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct {
        unsigned int use_qspi_interface : 1;
        unsigned int reset_level : 1;
    } flags;
} st77916_panel_t;

static esp_err_t tx_param(st77916_panel_t *p, int lcd_cmd, const void *param, size_t param_size)
{
    if (p->flags.use_qspi_interface) {
        lcd_cmd = ((lcd_cmd & 0xff) << 8) | (int)(LCD_OPCODE_WRITE_CMD << 24);
    }
    return esp_lcd_panel_io_tx_param(p->io, lcd_cmd, param, param_size);
}

static esp_err_t tx_color(st77916_panel_t *p, int lcd_cmd, const void *param, size_t param_size)
{
    if (p->flags.use_qspi_interface) {
        lcd_cmd = ((lcd_cmd & 0xff) << 8) | (int)(LCD_OPCODE_WRITE_COLOR << 24);
    }
    return esp_lcd_panel_io_tx_color(p->io, lcd_cmd, param, param_size);
}

static esp_err_t panel_del(esp_lcd_panel_t *panel)
{
    st77916_panel_t *p = __containerof(panel, st77916_panel_t, base);
    if (p->reset_gpio_num >= 0) {
        gpio_reset_pin(p->reset_gpio_num);
    }
    free(p);
    return ESP_OK;
}

static esp_err_t panel_reset(esp_lcd_panel_t *panel)
{
    st77916_panel_t *p = __containerof(panel, st77916_panel_t, base);
    if (p->reset_gpio_num >= 0) {
        gpio_set_level(p->reset_gpio_num, p->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(p->reset_gpio_num, !p->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        ESP_RETURN_ON_ERROR(tx_param(p, LCD_CMD_SWRESET, NULL, 0), TAG, "SWRESET");
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return ESP_OK;
}

static esp_err_t panel_init(esp_lcd_panel_t *panel)
{
    st77916_panel_t *p = __containerof(panel, st77916_panel_t, base);
    ESP_RETURN_ON_FALSE(p->init_cmds && p->init_cmds_size, ESP_ERR_INVALID_STATE, TAG, "init_cmds required");

    ESP_RETURN_ON_ERROR(tx_param(p, LCD_CMD_MADCTL, (uint8_t[]){ p->madctl_val }, 1), TAG, "MADCTL");
    ESP_RETURN_ON_ERROR(tx_param(p, LCD_CMD_COLMOD, (uint8_t[]){ p->colmod_val }, 1), TAG, "COLMOD");

    for (uint16_t i = 0; i < p->init_cmds_size; i++) {
        const st77916_lcd_init_cmd_t *cmd = &p->init_cmds[i];
        ESP_RETURN_ON_ERROR(tx_param(p, cmd->cmd, cmd->data, cmd->data_bytes), TAG, "init cmd");
        if (cmd->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
        }
    }
    return ESP_OK;
}

static esp_err_t panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                   const void *color_data)
{
    st77916_panel_t *p = __containerof(panel, st77916_panel_t, base);
    ESP_RETURN_ON_FALSE(x_start < x_end && y_start < y_end, ESP_ERR_INVALID_ARG, TAG, "bad area");

    ESP_RETURN_ON_ERROR(tx_param(p, LCD_CMD_CASET, (uint8_t[]){
        (x_start >> 8) & 0xFF, x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF, (x_end - 1) & 0xFF,
    }, 4), TAG, "CASET");
    ESP_RETURN_ON_ERROR(tx_param(p, LCD_CMD_RASET, (uint8_t[]){
        (y_start >> 8) & 0xFF, y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF, (y_end - 1) & 0xFF,
    }, 4), TAG, "RASET");

    size_t len = (size_t)(x_end - x_start) * (size_t)(y_end - y_start) * p->fb_bits_per_pixel / 8;
    return tx_color(p, LCD_CMD_RAMWR, color_data, len);
}

static esp_err_t panel_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    st77916_panel_t *p = __containerof(panel, st77916_panel_t, base);
    return tx_param(p, on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
}

static esp_err_t panel_stub_bool(esp_lcd_panel_t *panel, bool v)
{
    (void)panel;
    (void)v;
    return ESP_OK;
}

static esp_err_t panel_stub_mirror(esp_lcd_panel_t *panel, bool x, bool y)
{
    (void)panel;
    (void)x;
    (void)y;
    return ESP_OK;
}

static esp_err_t panel_stub_gap(esp_lcd_panel_t *panel, int x, int y)
{
    (void)panel;
    (void)x;
    (void)y;
    return ESP_OK;
}

esp_err_t esp_lcd_new_panel_st77916(const esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *cfg,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && cfg && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid arg");

    st77916_panel_t *p = calloc(1, sizeof(*p));
    ESP_RETURN_ON_FALSE(p, ESP_ERR_NO_MEM, TAG, "oom");

    if (cfg->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << cfg->reset_gpio_num,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            free(p);
            return err;
        }
    }

    p->madctl_val = (cfg->rgb_ele_order == LCD_RGB_ELEMENT_ORDER_BGR) ? LCD_CMD_BGR_BIT : 0;
    if (cfg->bits_per_pixel == 16) {
        p->colmod_val = 0x55;
        p->fb_bits_per_pixel = 16;
    } else {
        free(p);
        return ESP_ERR_NOT_SUPPORTED;
    }

    st77916_vendor_config_t *vc = (st77916_vendor_config_t *)cfg->vendor_config;
    if (vc) {
        if (vc->flags.init_madctl_valid) {
            p->madctl_val = vc->init_madctl;
        }
        p->init_cmds = vc->init_cmds;
        p->init_cmds_size = vc->init_cmds_size;
        p->flags.use_qspi_interface = vc->flags.use_qspi_interface;
    }

    p->io = io;
    p->reset_gpio_num = cfg->reset_gpio_num;
    p->flags.reset_level = cfg->flags.reset_active_high;
    p->base.del = panel_del;
    p->base.reset = panel_reset;
    p->base.init = panel_init;
    p->base.draw_bitmap = panel_draw_bitmap;
    p->base.disp_on_off = panel_disp_on_off;
    p->base.invert_color = panel_stub_bool;
    p->base.mirror = panel_stub_mirror;
    p->base.swap_xy = panel_stub_bool;
    p->base.set_gap = panel_stub_gap;

    *ret_panel = &p->base;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_st77916_set_madctl(esp_lcd_panel_handle_t panel, uint8_t madctl)
{
    st77916_panel_t *p = __containerof(panel, st77916_panel_t, base);
    p->madctl_val = madctl;
    return tx_param(p, LCD_CMD_MADCTL, (uint8_t[]){ madctl }, 1);
}
