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

#include "ui_home.h"

#include <string.h>

#include "lvgl.h"

#include "ui_demo_page.h"
#include "ui_app_page.h"
#include "power_off.h"
#include "build_version.h"

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
    lv_label_set_text(t, "关机中...");
    lv_obj_set_style_text_font(t, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);

    lv_scr_load(scr);
    lv_refr_now(NULL);
    system_power_off();
}

static lv_obj_t *s_home_scr;

static void on_btn(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name) {
        return;
    }
    if (strcmp(name, "关机") == 0) {
        power_off_screen();
    } else if (strcmp(name, "应用") == 0) {
        ui_app_page_show();
    } else if (strcmp(name, "DEMO") == 0) {
        ui_demo_page_show();
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

    if (text && (strcmp(text, "DEMO") == 0 || strcmp(text, "应用") == 0
                 || strcmp(text, "关机") == 0)) {
        lv_obj_add_event_cb(btn, on_btn, LV_EVENT_CLICKED, (void *)text);
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

void ui_home_create(void)
{
    s_home_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_home_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_home_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_home_scr, LV_OBJ_FLAG_SCROLLABLE);

    const lv_color_t footer_gray = lv_color_hex(0x808080);

    lv_obj_t *title = lv_label_create(s_home_scr);
    lv_label_set_text(title, "谷仓电子徽章");
    lv_obj_set_style_text_font(title, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SDG_UI_TITLE_Y);

    /* 编译版本号 (自动生成, MMDDHHMM)，位于标题正下方，每次编译刷新 */
    lv_obj_t *ver = lv_label_create(s_home_scr);
    lv_label_set_text(ver, BUILD_VERSION_STR);
    lv_obj_set_style_text_font(ver, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(ver, footer_gray, 0);
    lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 56);

    /* 主页 3 个按钮：DEMO / 应用 / 关机，单行垂直居中 */
    make_round_btn(s_home_scr, SDG_UI_BTN1_X, 142, "DEMO");
    make_round_btn(s_home_scr, SDG_UI_BTN2_X, 142, "应用");
    make_round_btn(s_home_scr, SDG_UI_BTN3_X, 142, "关机");

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

    lv_obj_t *txt = lv_label_create(footer);
    lv_label_set_text(txt, "谷仓SDGOODS");
    lv_obj_set_style_text_font(txt, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(txt, footer_gray, 0);

    ui_home_show();
}

void ui_home_show(void)
{
    if (s_home_scr) {
        lv_scr_load(s_home_scr);
    }
}
