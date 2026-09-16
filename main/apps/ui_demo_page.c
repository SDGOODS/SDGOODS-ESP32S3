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

#include "ui_demo_page.h"

#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "lvgl.h"

#include "sdgoods_i18n.h"    /* SDG_T / sdg_i18n_toggle：语言切换按钮 */
#include "ui_scan_page.h"
#include "ui_rec_page.h"
#include "ui_other_page.h"
#include "ui_about.h"        /* 「关于」：品牌 / 公司 / 版本 / 授权 */
#include "ui_home.h"
#include "sdgoods_swipe_back.h"
#include "sdgoods_wifi.h"
#include "sdgoods_ble.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(cn_font_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(cn_font_16);

static lv_obj_t *s_scr;
static bool s_active;   /* 当前是否为活动页（进入子页时置 false，避免 poll 误触发返回） */
static bool s_key_down;
static uint32_t s_lang_seq;   /* 建屏时的语言版本号；和当前不一致就重建 */

/* 按钮回调：user_data 是**语言无关的键**，不是按钮文字。
   切语言后按钮文字会变，拿文字当键就再也对不上（这是 i18n 最常见的坑）。 */
static void on_demo_btn(lv_event_t *e)
{
    const char *key = (const char *)lv_event_get_user_data(e);
    if (!key) {
        return;
    }

    /* 「语言」按钮：切完语言**留在本页**并重建，好让用户立刻看到效果 */
    if (strcmp(key, "lang") == 0) {
        sdg_i18n_toggle();
        lv_obj_t *gone = s_scr;
        s_scr = NULL;                 /* 置空 -> show() 走创建分支重建 */
        ui_demo_page_show();
        if (gone) {
            lv_obj_del(gone);         /* 新屏已加载，旧屏可以安全删除 */
        }
        return;
    }

    /* 进入子页前把返回目标指回 DEMO 页，子页关闭即可回到本页 */
    ui_nav_parent_show = ui_demo_page_show;
    s_active = false;
    if (strcmp(key, "rec") == 0) {
        ui_rec_page_show();
    } else if (strcmp(key, "wifi") == 0) {
        ui_scan_page_show("WIFI", SDG_T("扫描附近%d个SSID", "%d SSIDs found"), sdgoods_wifi_list);
    } else if (strcmp(key, "ble") == 0) {
        ui_scan_page_show(SDG_T("蓝牙", "BLE"), SDG_T("扫描附近%d个蓝牙设备", "%d devices found"),
                          sdgoods_ble_list);
    } else if (strcmp(key, "other") == 0) {
        ui_other_page_show();
    } else if (strcmp(key, "about") == 0) {
        ui_about_page_show();
    }
}

/* key == NULL 表示占位按钮（不可点） */
static lv_obj_t *make_round_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                const char *text, const char *key)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, SDG_UI_BTN_SIZE, SDG_UI_BTN_SIZE);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, SDG_UI_BTN_SIZE / 2, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    if (key) {
        lv_obj_add_event_cb(btn, on_demo_btn, LV_EVENT_CLICKED, (void *)key);
    } else {
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }

    if (text && text[0]) {
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &cn_font_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_center(lbl);
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
    /* 语言切换过 -> 旧屏文案作废，删掉重建 */
    if (s_scr && s_lang_seq != sdg_i18n_seq()) {
        lv_obj_del(s_scr);
        s_scr = NULL;
    }

    if (s_scr) {
        /* 已创建（从子页返回）：直接重载并恢复活动态 */
        s_active = true;
        s_key_down = false;
        lv_scr_load(s_scr);
        return;
    }
    s_lang_seq = sdg_i18n_seq();
    s_active = true;
    s_key_down = false;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    sdgoods_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回主页 */

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "DEMO");
    lv_obj_set_style_text_font(title, &cn_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SDG_UI_TITLE_Y);

    /* 6 个圆形按钮：录音 / WIFI / 蓝牙 / 其他 / 关于 / 语言。
       最后一个「语言」显示**当前语言**（中文界面显示「中文」，英文界面显示
       "English"），点一下切到另一种 —— 用户一眼能看出现在是哪种语言。 */
    make_round_btn(s_scr, SDG_UI_BTN1_X, SDG_UI_BTN1_Y, SDG_T("录音", "Rec"),      "rec");
    make_round_btn(s_scr, SDG_UI_BTN2_X, SDG_UI_BTN2_Y, "WIFI",                    "wifi");
    make_round_btn(s_scr, SDG_UI_BTN3_X, SDG_UI_BTN3_Y, SDG_T("蓝牙", "BLE"),      "ble");
    make_round_btn(s_scr, SDG_UI_BTN4_X, SDG_UI_BTN4_Y, SDG_T("其他", "More"),     "other");
    make_round_btn(s_scr, SDG_UI_BTN5_X, SDG_UI_BTN5_Y, SDG_T("关于", "About"),    "about");
    make_round_btn(s_scr, SDG_UI_BTN6_X, SDG_UI_BTN6_Y, SDG_T("中文", "English"),  "lang");

    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, SDG_T("按电源键返回", "Power key to go back"));
    lv_obj_set_style_text_font(hint, &cn_font_14, 0);   /* 提示行用小一号的字（14） */
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
