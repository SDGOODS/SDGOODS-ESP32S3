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

#include "sdgoods_i18n.h"    /* SDG_T：中英文案；sdg_i18n_seq：语言切换后重建 */
#include "ui_demo_page.h"
#include "ui_app_page.h"
#include "sdgoods_power.h"

LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(si_yuan_black_icon_14);

/* 子页返回目标：进入子页（如 DEMO 下的录音页）时由父页改写为自己的 show 函数，
 * 子页关闭时调用 ui_nav_back() 即可回到正确的父页。默认回主页。 */
void (*ui_nav_parent_show)(void) = ui_home_show;

void ui_nav_back(void)
{
    if (ui_nav_parent_show) {
        ui_nav_parent_show();
    } else {
        ui_home_show();
    }
}

static void power_off_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, SDG_T("关机中...", "Powering off..."));
    lv_obj_set_style_text_font(t, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);

    lv_scr_load(scr);
    lv_refr_now(NULL);
    sdgoods_power_off();
}

static lv_obj_t *s_home_scr;
static uint32_t  s_lang_seq;   /* 建屏时的语言版本号；和当前不一致就重建 */

/* 按钮回调：user_data 传的是**语言无关的键**（"demo" / "apps" / "power"），
   不是按钮文字 —— 否则切到英文后按钮文字变了，strcmp 就再也对不上。
   新增按钮请照这个规矩加键。 */
static void on_btn(lv_event_t *e)
{
    const char *key = (const char *)lv_event_get_user_data(e);
    if (!key) {
        return;
    }
    if (strcmp(key, "power") == 0) {
        power_off_screen();
    } else if (strcmp(key, "apps") == 0) {
        ui_app_page_show();
    } else if (strcmp(key, "demo") == 0) {
        ui_demo_page_show();
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
        lv_obj_set_style_text_font(lbl, &si_yuan_black_icon_14, 0);
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

    /* 顶部标题：主页 / HOME（y 用栅格常量 SDG_UI_TITLE_Y，和 DEMO / 应用页同高）。
       不要写死数字，否则二级页调了这里又会对不齐。 */
    lv_obj_t *title = lv_label_create(s_home_scr);
    lv_label_set_text(title, SDG_T("主页", "HOME"));
    lv_obj_set_style_text_font(title, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SDG_UI_TITLE_Y);

    /* 主页 3 个按钮：DEMO / 应用 / 关机，单行垂直居中 */
    make_round_btn(s_home_scr, SDG_UI_BTN1_X, 142, "DEMO",                    "demo");
    make_round_btn(s_home_scr, SDG_UI_BTN2_X, 142, SDG_T("应用", "Apps"),     "apps");
    make_round_btn(s_home_scr, SDG_UI_BTN3_X, 142, SDG_T("关机", "Power off"), "power");

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

    /* 品牌名不翻译：任何语言下都显示官方中文名（与关于页、源文件头一致） */
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
