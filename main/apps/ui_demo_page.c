/*
 * SDGOODS 开放平台基础工程 · 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Required Notice: Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 *
 * 个人学习、研究与业余项目免费使用；商业用途需事前书面授权，见 LICENSING.md。
 * 分发时必须完整保留本声明 —— 这是许可条款，不是建议。
 */

#include "ui_demo_page.h"

#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "lvgl.h"

#include "ui_scan_page.h"
#include "ui_rec_page.h"
#include "ui_other_page.h"
#include "ui_home.h"
#include "ui_swipe_back.h"
#include "wifi_scan.h"
#include "ble_scan.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(cn_font_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(cn_font_16);

static lv_obj_t *s_scr;
static bool s_active;   /* 当前是否为活动页（进入子页时置 false，避免 poll 误触发返回） */
static bool s_key_down;

static void on_demo_btn(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name) {
        return;
    }
    /* 进入子页前把返回目标指回 DEMO 页，子页关闭即可回到本页 */
    ui_nav_parent_show = ui_demo_page_show;
    s_active = false;
    if (strcmp(name, "录音") == 0) {
        ui_rec_page_show();
    } else if (strcmp(name, "WIFI") == 0) {
        ui_scan_page_show("WIFI", "扫描附近%d个SSID", wifi_scan_list);
    } else if (strcmp(name, "蓝牙") == 0) {
        ui_scan_page_show("蓝牙", "扫描附近%d个蓝牙设备", ble_scan_list);
    } else if (strcmp(name, "其他") == 0) {
        ui_other_page_show();
    }
}

static lv_obj_t *make_round_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *text)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, SDG_UI_BTN_SIZE, SDG_UI_BTN_SIZE);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, SDG_UI_BTN_SIZE / 2, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    if (text && text[0]) {
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &cn_font_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, on_demo_btn, LV_EVENT_CLICKED, (void *)text);
    } else {
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
    return btn;
}

static void close_page(void)
{
    if (!s_scr) {
        return;
    }
    lv_obj_t *gone = s_scr;
    s_scr = NULL;
    s_active = false;
    ui_nav_parent_show = ui_home_show;   /* 离开 DEMO 后，返回目标恢复为主页 */
    ui_home_show();
    lv_obj_del(gone);
}

void ui_demo_page_show(void)
{
    if (s_scr) {
        /* 已创建（从子页返回）：直接重载并恢复活动态 */
        s_active = true;
        s_key_down = false;
        lv_scr_load(s_scr);
        return;
    }
    s_active = true;
    s_key_down = false;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回主页 */

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "DEMO");
    lv_obj_set_style_text_font(title, &cn_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SDG_UI_TITLE_Y);

    /* 4 个圆形按钮：录音 / WIFI / 蓝牙 / 其他（沿用首页原有布局） */
    make_round_btn(s_scr, SDG_UI_BTN1_X, SDG_UI_BTN1_Y, "录音");
    make_round_btn(s_scr, SDG_UI_BTN2_X, SDG_UI_BTN2_Y, "WIFI");
    make_round_btn(s_scr, SDG_UI_BTN3_X, SDG_UI_BTN3_Y, "蓝牙");
    make_round_btn(s_scr, SDG_UI_BTN4_X, SDG_UI_BTN4_Y, "其他");
    make_round_btn(s_scr, SDG_UI_BTN5_X, SDG_UI_BTN5_Y, "");
    make_round_btn(s_scr, SDG_UI_BTN6_X, SDG_UI_BTN6_Y, "");

    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, "按电源键返回");
    lv_obj_set_style_text_font(hint, &cn_font_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x808080), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 296);

    lv_scr_load(s_scr);
}

void ui_demo_page_poll(void)
{
    if (!s_scr || !s_active) {
        return;
    }
    const int down = (gpio_get_level(BOARD_KEY_GPIO) == BOARD_KEY_ACTIVE_LEVEL);
    if (down && !s_key_down) {
        close_page();
    }
    s_key_down = down;
}
