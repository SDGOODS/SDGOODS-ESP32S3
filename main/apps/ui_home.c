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

#include "ui_home.h"

#include <string.h>

#include "lvgl.h"

#include "sdgoods_i18n.h"    /* SDG_T：中英文案；sdg_i18n_seq / sdg_i18n_toggle：语言切换后重建 */
#include "ui_rec_page.h"
#include "ui_scan_page.h"
#include "ui_other_page.h"
#include "ui_flappy.h"        /* 首页「小鸟」按钮直接启动小鸟游戏 */
#include "sdgoods_wifi.h"     /* sdgoods_wifi_list：WIFI 扫描页数据源 */
#include "sdgoods_ble.h"      /* sdgoods_ble_list：蓝牙扫描页数据源 */

LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(cn_font_14);   /* 首页按钮的中文文案（录音 / 蓝牙 / 其他 / 语言） */

/* 子页返回目标：进入子页（如录音页）前由本页把 ui_nav_parent_show 指回这里，
 * 子页关闭时调用 ui_nav_back() 即可回到首页。默认回主页。 */
void (*ui_nav_parent_show)(void) = ui_home_show;

void ui_nav_back(void)
{
    if (ui_nav_parent_show) {
        ui_nav_parent_show();
    } else {
        ui_home_show();
    }
}

static lv_obj_t *s_home_scr;
static uint32_t  s_lang_seq;   /* 建屏时的语言版本号；和当前不一致就重建 */

/* 按钮回调：user_data 传的是**语言无关的键**（"rec" / "wifi" / "ble" / "other" / "bird" / "lang"），
   不是按钮文字 —— 否则切到英文后按钮文字变了，strcmp 就再也对不上。
   新增按钮请照这个规矩加键。 */
static void on_btn(lv_event_t *e)
{
    const char *key = (const char *)lv_event_get_user_data(e);
    if (!key) {
        return;
    }

    /* 语言按钮：切换语言后原地重建主页（ui_home_show 检测到语言版本变化会自动重建） */
    if (strcmp(key, "lang") == 0) {
        sdg_i18n_toggle();
        ui_home_show();
        return;
    }

    /* 进入子页前把返回目标指回主页：子页用 ui_nav_back() 回到这里 */
    ui_nav_parent_show = ui_home_show;

    if (strcmp(key, "rec") == 0) {
        ui_rec_page_show();
    } else if (strcmp(key, "wifi") == 0) {
        ui_scan_page_show("WIFI", SDG_T("扫描附近%d个SSID", "%d SSIDs found"), sdgoods_wifi_list);
    } else if (strcmp(key, "ble") == 0) {
        ui_scan_page_show(SDG_T("蓝牙", "BLE"), SDG_T("扫描附近%d个蓝牙设备", "%d devices found"), sdgoods_ble_list);
    } else if (strcmp(key, "other") == 0) {
        ui_other_page_show();
    } else if (strcmp(key, "bird") == 0) {
        ui_flappy_start();
    }
}

/* key == NULL 表示这颗按钮没有功能（占位用） */
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
        lv_obj_add_event_cb(btn, on_btn, LV_EVENT_CLICKED, (void *)key);
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

/* 主页是全工程唯一常驻的屏（不会被删），所以语言切换后必须显式重建，
   不能像别的页那样「下次 show 时自查」。 */
void ui_home_create(void)
{
    if (s_home_scr) {
        lv_obj_del(s_home_scr);
        s_home_scr = NULL;
    }
    s_lang_seq = sdg_i18n_seq();

    s_home_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_home_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_home_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_home_scr, LV_OBJ_FLAG_SCROLLABLE);

    const lv_color_t footer_gray = lv_color_hex(0x808080);

    /* 顶部标题：主页 / HOME（y 用栅格常量 SDG_UI_TITLE_Y，和二级页同高）。
       不要写死数字，否则二级页调了这里又会对不齐。 */
    lv_obj_t *title = lv_label_create(s_home_scr);
    lv_label_set_text(title, SDG_T("主页", "HOME"));
    lv_obj_set_style_text_font(title, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SDG_UI_TITLE_Y);

    /* 主页 6 个功能按钮，平铺成 2 行 × 3 列（沿用 sdgoods_ui.h 的栅格常量，
       不写死坐标，避免圆边裁切 / 与二级页错位）：
           录音   WIFI   蓝牙
           其他   小鸟   语言
       「语言」按钮显示当前语言（中文界面="中文"，英文界面="English"），点一下切换并重建主页。 */
    make_round_btn(s_home_scr, SDG_UI_BTN1_X, SDG_UI_BTN1_Y, SDG_T("录音", "Rec"),     "rec");
    make_round_btn(s_home_scr, SDG_UI_BTN2_X, SDG_UI_BTN2_Y, "WIFI",                   "wifi");
    make_round_btn(s_home_scr, SDG_UI_BTN3_X, SDG_UI_BTN3_Y, SDG_T("蓝牙", "BLE"),     "ble");
    make_round_btn(s_home_scr, SDG_UI_BTN4_X, SDG_UI_BTN4_Y, SDG_T("其他", "More"),    "other");
    make_round_btn(s_home_scr, SDG_UI_BTN5_X, SDG_UI_BTN5_Y, SDG_T("小鸟", "Bird"),    "bird");
    make_round_btn(s_home_scr, SDG_UI_BTN6_X, SDG_UI_BTN6_Y, SDG_T("中文", "English"), "lang");

    lv_obj_t *footer = lv_obj_create(s_home_scr);
    lv_obj_set_size(footer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_style_pad_column(footer, 4, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(footer, LV_ALIGN_TOP_MID, 0, SDG_UI_FOOTER_Y);

    lv_obj_t *mark = lv_obj_create(footer);
    lv_obj_remove_style_all(mark);
    lv_obj_set_size(mark, 12, 12);
    lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mark, 1, 0);
    lv_obj_set_style_border_color(mark, footer_gray, 0);
    lv_obj_set_style_border_opa(mark, LV_OPA_COVER, 0);
    lv_obj_clear_flag(mark, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = lv_label_create(mark);
    lv_label_set_text(c, "C");
    lv_obj_set_style_text_font(c, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(c, footer_gray, 0);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);

    /* 品牌名不翻译：任何语言下都显示官方中文名（与源文件头一致） */
    lv_obj_t *txt = lv_label_create(footer);
    lv_label_set_text(txt, "谷仓SDGOODS");
    lv_obj_set_style_text_font(txt, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(txt, footer_gray, 0);

    lv_scr_load(s_home_scr);
}

void ui_home_show(void)
{
    if (!s_home_scr || s_lang_seq != sdg_i18n_seq()) {
        ui_home_create();   /* 首次建屏 / 语言切换过 -> 建（重建），内部会加载 */
        return;
    }
    lv_scr_load(s_home_scr);
}

/* 电源键短按钩子（注册给平台层）：实现「一级一级返回」而非子页直接跳主页/启动器。
 *   · 子页（录音/扫描/其他）或游戏（小鸟）前台 → 回退一级到应用主页，返回 true 消费掉；
 *   · 已在应用主页 → 返回 false，交给平台默认导航
 *     （多应用：返回启动器；单应用/派生：熄屏 + 浅睡眠，按一下电源键原地唤醒亮屏不重启）。 */
bool ui_app_power_short_handler(void)
{
    if (ui_flappy_is_open())     { ui_flappy_close();     return true; }
    if (ui_rec_page_is_open())   { ui_rec_page_close();   return true; }
    if (ui_scan_page_is_open())  { ui_scan_page_close();  return true; }
    if (ui_other_page_is_open()) { ui_other_page_close(); return true; }
    return false;
}
