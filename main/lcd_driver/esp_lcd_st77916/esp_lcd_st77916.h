#pragma once

#include <stdint.h>
#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} st77916_lcd_init_cmd_t;

typedef struct {
    const st77916_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct {
        unsigned int use_qspi_interface : 1;
        unsigned int init_madctl_valid : 1;
    } flags;
    uint8_t init_madctl;
} st77916_vendor_config_t;

esp_err_t esp_lcd_new_panel_st77916(const esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel);

esp_err_t esp_lcd_panel_st77916_set_madctl(esp_lcd_panel_handle_t panel, uint8_t madctl);

#ifdef __cplusplus
}
#endif
