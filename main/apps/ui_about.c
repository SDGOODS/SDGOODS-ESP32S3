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

#include "ui_about.h"

#include <stdio.h>

#include "board_pins.h"
#include "build_version.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "sdgoods_i18n.h"    /* SDG_T：功能性文案中英切换 */
#include "ui_home.h"
#include "sdgoods_swipe_back.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);

static lv_obj_t *s_scr;
static bool s_key_down;
static uint32_t s_lang_seq;   /* 建屏时的语言版本号；和当前不一致就重建 */

/* 本页各行在圆屏上的纵向位置。
   注意：圆屏越靠上/下，可用宽度越窄（弦长）。离屏幕中心 dy 像素处，可用宽度约
   `2*sqrt(180^2 - dy^2)`；y=254 那行只剩约 291px。所以长句不要写死「一行放得下」，
   用 make_wrapped_line() 限宽折行 —— 单行硬塞会被圆边切掉两头。 */
#define ABT_Y_TITLE     44
#define ABT_Y_PROGRAM   72
#define ABT_Y_PLATFORM  100
#define ABT_Y_PRODUCT   130
#define ABT_Y_HOMEPAGE  152
#define ABT_Y_VENDOR    170
#define ABT_Y_VERSION   200
#define ABT_Y_LICENSE   232
#define ABT_Y_HINT      296

/* 许可标签的折行宽度。
   实测英文 "Free for personal use · Commercial needs license" 单行 327px，
   而 y=254 处可用弦宽仅约 291px —— 必须折行。
   取 200px 是因为它在自然断点处折开（"Free for personal use ·" / "Commercial needs license"），
   中文 "个人免费 · 商用需授权" 约 145px 仍是一行，两种语言观感一致。
   改文案后跑 tools/font_metrics.py 核对宽度。 */
#define ABT_LICENSE_W   200

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

/* 单行文本（自身宽度即内容宽度，不折行） */
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

/* 限宽折行的文本：宽度固定、内容居中、按空格自动折行，高度随行数长。
   长句（如英文许可标签）必须走这个，否则会顶出圆屏边缘被切掉。 */
static lv_obj_t *make_wrapped_line(lv_obj_t *parent, const char *text,
                                   const lv_font_t *font, lv_color_t color,
                                   lv_coord_t y, lv_coord_t w)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl, w);
    lv_obj_set_height(lbl, LV_SIZE_CONTENT);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
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

    sdgoods_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回上一页 */

    make_line(s_scr, SDG_T("关于", "About"),
              &si_yuan_black_icon_14, lv_color_white(), ABT_Y_TITLE);

    /* 项目标识：本固件属于「谷仓共创计划」，下面依次是开放平台、设备名与公司。
       文案全部取自 build_version.h，不要在别处另写一份字面量。
       品牌名与公司名是商标 / 法定名称，两种语言下都显示官方中文原名 —— 不翻译。 */
    make_line(s_scr, SDGOODS_PROGRAM,  &si_yuan_black_icon_14, lv_color_hex(0xFFCC33), ABT_Y_PROGRAM);
    make_line(s_scr, SDGOODS_PLATFORM, &si_yuan_black_icon_14, lv_color_white(), ABT_Y_PLATFORM);
    make_line(s_scr, SDGOODS_PRODUCT,  &si_yuan_black_icon_16, lv_color_white(), ABT_Y_PRODUCT);

    /* 官网（取自 build_version.h 的 SDGOODS_HOMEPAGE）。ASCII 在字体里由 0x20-0x7E 全量覆盖，
       显示 URL 不会出方框。改官网地址只需改 gen_build_version.cmake 一处。 */
    make_line(s_scr, SDGOODS_HOMEPAGE, &si_yuan_black_icon_14, gray, ABT_Y_HOMEPAGE);

    make_line(s_scr, SDGOODS_VENDOR, &si_yuan_black_icon_14, gray, ABT_Y_VENDOR);

    /* 关于页不展示联系方式（按产品口径，联系邮箱只保留在文档与开机串口横幅里）。
       需要时加回来：SDG_T("联系：%s", "Contact: %s"), SDGOODS_CONTACT_EMAIL */
    char buf[64];
    snprintf(buf, sizeof(buf), SDG_T("固件版本 %s", "Firmware %s"), BUILD_VERSION_STR);
    make_line(s_scr, buf, &si_yuan_black_icon_14, gray, ABT_Y_VERSION);

    /* 授权状态：这是本工程对外的承诺，放在固件里让它跟着设备走。
       英文比中文长得多，限宽折行显示。 */
    make_wrapped_line(s_scr, SDG_T(SDGOODS_LICENSE_TAG, SDGOODS_LICENSE_TAG_EN),
                      &si_yuan_black_icon_14, lv_color_white(), ABT_Y_LICENSE, ABT_LICENSE_W);

    make_line(s_scr, SDG_T("按电源键返回", "Power key to go back"),
              &si_yuan_black_icon_14, gray, ABT_Y_HINT);   /* 提示行 14 号字 */

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
