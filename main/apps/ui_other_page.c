/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于应用层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

#include "ui_other_page.h"

#include <stdio.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "sdgoods_hw_info.h"
#include "lvgl.h"
#include "sdgoods_i18n.h"    /* SDG_T：界面文案中英切换 */
#include "sdgoods_lcd.h"
#include "ui_home.h"
#include "sdgoods_swipe_back.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);

#define ROW_X      108
#define GYRO_NUM_W 24

static lv_obj_t *s_scr;
static lv_obj_t *s_bat;
static lv_obj_t *s_gx;
static lv_obj_t *s_gy;
static lv_obj_t *s_gz;
static bool s_key_down;
static int64_t s_bat_next_us;

static lv_obj_t *make_row(lv_obj_t *parent, lv_coord_t y, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, ROW_X, y);
    return lbl;
}

static lv_obj_t *make_txt(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    return lbl;
}

static lv_obj_t *make_gyro_num(lv_obj_t *parent)
{
    lv_obj_t *lbl = make_txt(parent, "0");
    lv_obj_set_width(lbl, GYRO_NUM_W);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    return lbl;
}

static void set_num(lv_obj_t *lbl, int v)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", v);
    lv_label_set_text(lbl, buf);
}

static void set_bat(void)
{
    char buf[40];
    snprintf(buf, sizeof(buf), SDG_T("电池电压: %.2fV", "Battery: %.2fV"),
             (double)sdgoods_hw_bat_v());
    lv_label_set_text(s_bat, buf);
}

static void set_gyro(void)
{
    int x = 0, y = 0, z = 0;
    (void)sdgoods_hw_gyro(&x, &y, &z);
    set_num(s_gx, x);
    set_num(s_gy, y);
    set_num(s_gz, z);
}

static void close_page(void)
{
    if (!s_scr) {
        return;
    }
    lv_obj_t *gone = s_scr;
    s_scr = NULL;
    s_bat = NULL;
    s_gx = NULL;
    s_gy = NULL;
    s_gz = NULL;
    ui_nav_back();
    lv_obj_del(gone);
}

void ui_other_page_show(void)
{
    if (s_scr) {
        return;
    }

    const lv_color_t gray = lv_color_hex(0x808080);
    s_key_down = false;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    sdgoods_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回主页 */

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, SDG_T("其他", "More"));
    lv_obj_set_style_text_font(title, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *sub = lv_label_create(s_scr);
    lv_label_set_text(sub, SDG_T("读取设备其他硬件信息", "Device hardware info"));
    lv_obj_set_style_text_font(sub, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(sub, gray, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 88);

    s_bat = make_row(s_scr, 140, "");

    lv_obj_t *gyro = lv_obj_create(s_scr);
    lv_obj_remove_style_all(gyro);
    lv_obj_set_size(gyro, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(gyro, 2, 0);
    lv_obj_set_flex_flow(gyro, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gyro, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(gyro, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(gyro, LV_ALIGN_TOP_LEFT, ROW_X, 178);
    make_txt(gyro, SDG_T("陀螺仪: x", "Gyro: x"));
    s_gx = make_gyro_num(gyro);
    make_txt(gyro, "y");
    s_gy = make_gyro_num(gyro);
    make_txt(gyro, "z");
    s_gz = make_gyro_num(gyro);

    char buf[40];
    snprintf(buf, sizeof(buf), SDG_T("可用空间: %.1f MB", "Free space: %.1f MB"),
             sdgoods_hw_space_mb());
    make_row(s_scr, 216, buf);
    snprintf(buf, sizeof(buf), SDG_T("屏幕亮度: %u%%", "Brightness: %u%%"),
             (unsigned)sdgoods_lcd_get_backlight());
    make_row(s_scr, 254, buf);

    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, SDG_T("按电源键返回", "Power key to go back"));
    lv_obj_set_style_text_font(hint, &si_yuan_black_icon_14, 0);   /* 提示行用小一号的字（14） */
    lv_obj_set_style_text_color(hint, gray, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 296);

    set_bat();
    set_gyro();
    s_bat_next_us = esp_timer_get_time() + 1000000;
    lv_scr_load(s_scr);
}

void ui_other_page_poll(void)
{
    if (!s_scr) {
        return;
    }
    set_gyro();
    const int64_t now = esp_timer_get_time();
    if (now >= s_bat_next_us) {
        set_bat();
        s_bat_next_us = now + 1000000;
    }
    const int down = (gpio_get_level(BOARD_KEY_GPIO) == BOARD_KEY_ACTIVE_LEVEL);
    if (down && !s_key_down) {
        close_page();
    }
    s_key_down = down;
}
