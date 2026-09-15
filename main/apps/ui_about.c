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

#include "ui_about.h"

#include <stdio.h>

#include "board_pins.h"
#include "build_version.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "ui_home.h"
#include "ui_swipe_back.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);

static lv_obj_t *s_scr;
static bool s_key_down;

static void close_page(void)
{
    if (!s_scr) {
        return;
    }
    lv_obj_t *gone = s_scr;
    s_scr = NULL;
    ui_nav_back();
    lv_obj_del(gone);
}

static lv_obj_t *make_line(lv_obj_t *parent, const char *text,
                           const lv_font_t *font, lv_color_t color, lv_coord_t y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
    return lbl;
}

void ui_about_page_show(void)
{
    if (s_scr) {
        return;
    }

    const lv_color_t gray = lv_color_hex(0x9A9A9A);
    s_key_down = false;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回主页 */

    make_line(s_scr, "关于", &si_yuan_black_icon_14, lv_color_white(), 48);

    /* 品牌区：文案全部取自 build_version.h，不要在别处另写一份字面量 */
    make_line(s_scr, SDGOODS_BRAND, &si_yuan_black_icon_16, lv_color_white(), 104);
    make_line(s_scr, SDGOODS_PRODUCT, &si_yuan_black_icon_14, lv_color_white(), 138);

    make_line(s_scr, SDGOODS_VENDOR, &si_yuan_black_icon_14, gray, 180);

    char buf[64];
    snprintf(buf, sizeof(buf), "固件版本 %s", BUILD_VERSION_STR);
    make_line(s_scr, buf, &si_yuan_black_icon_14, gray, 214);

    /* 授权状态：这是本工程对外的承诺，放在固件里让它跟着设备走 */
    make_line(s_scr, SDGOODS_LICENSE_TAG, &si_yuan_black_icon_14, lv_color_white(), 250);

    make_line(s_scr, "按电源键返回", &si_yuan_black_icon_16, gray, 296);

    lv_scr_load(s_scr);
}

void ui_about_page_poll(void)
{
    if (!s_scr) {
        return;
    }
    const int down = (gpio_get_level(BOARD_KEY_GPIO) == BOARD_KEY_ACTIVE_LEVEL);
    if (down && !s_key_down) {
        close_page();
    }
    s_key_down = down;
}
