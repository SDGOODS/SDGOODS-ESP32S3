/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
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
#include "sdgoods_i18n.h"    /* SDG_T：功能性文案中英切换 */
#include "ui_home.h"
#include "ui_swipe_back.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);

static lv_obj_t *s_scr;
static bool s_key_down;
static uint32_t s_lang_seq;   /* 建屏时的语言版本号；和当前不一致就重建 */

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
    /* 语言切换过 -> 旧屏文案作废，删掉重建 */
    if (s_scr && s_lang_seq != sdg_i18n_seq()) {
        lv_obj_del(s_scr);
        s_scr = NULL;
    }
    if (s_scr) {
        return;
    }

    const lv_color_t gray = lv_color_hex(0x9A9A9A);
    s_key_down = false;
    s_lang_seq = sdg_i18n_seq();

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回上一页 */

    make_line(s_scr, SDG_T("关于", "About"), &si_yuan_black_icon_14, lv_color_white(), 48);

    /* 项目标识：本固件属于「谷仓共创计划」，下面依次是开放平台与设备名。
       文案全部取自 build_version.h，不要在别处另写一份字面量。
       品牌名与公司名是商标 / 法定名称，两种语言下都显示官方中文原名 —— 不翻译。 */
    make_line(s_scr, SDGOODS_PROGRAM,  &si_yuan_black_icon_14, lv_color_hex(0xFFCC33), 74);
    make_line(s_scr, SDGOODS_PLATFORM, &si_yuan_black_icon_14, lv_color_white(), 104);
    make_line(s_scr, SDGOODS_PRODUCT,  &si_yuan_black_icon_16, lv_color_white(), 138);

    make_line(s_scr, SDGOODS_VENDOR, &si_yuan_black_icon_14, gray, 180);

    char buf[64];
    snprintf(buf, sizeof(buf), SDG_T("固件版本 %s", "Firmware %s"), BUILD_VERSION_STR);
    make_line(s_scr, buf, &si_yuan_black_icon_14, gray, 214);

    /* 授权状态：这是本工程对外的承诺，放在固件里让它跟着设备走 */
    make_line(s_scr, SDG_T(SDGOODS_LICENSE_TAG, SDGOODS_LICENSE_TAG_EN),
              &si_yuan_black_icon_14, lv_color_white(), 250);

    make_line(s_scr, SDG_T("按电源键返回", "Power key to go back"),
              &si_yuan_black_icon_16, gray, 296);

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
