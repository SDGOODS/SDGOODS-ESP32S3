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

#include "ui_scan_page.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board_pins.h"
#include "sdgoods_i18n.h"    /* SDG_T：界面文案中英切换 */
#include "ui_home.h"
#include "sdgoods_swipe_back.h"

LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(si_yuan_black_icon_14);

#define LIST_N  3

static lv_obj_t *s_scr;
static lv_obj_t *s_sub;
static lv_obj_t *s_row[LIST_N];
static TaskHandle_t s_task;
static volatile int s_n = -1;
static char s_names[LIST_N][UI_SCAN_NAME_MAX + 1];
static const char *s_sub_fmt;
static ui_scan_fn_t s_scan_fn;
static bool s_key_down;

static void apply_results(void)
{
    char buf[48];
    int n = s_n < 0 ? 0 : s_n;
    snprintf(buf, sizeof(buf), s_sub_fmt, n);
    lv_label_set_text(s_sub, buf);
    for (int i = 0; i < LIST_N; i++) {
        lv_label_set_text(s_row[i], (i < n) ? s_names[i] : "");
    }
    s_n = -2;
}

static void scan_task(void *arg)
{
    (void)arg;
    int n = s_scan_fn(s_names, LIST_N);
    s_n = (n < 0) ? 0 : n;
    s_task = NULL;
    vTaskDelete(NULL);
}

static void close_page(void)
{
    if (!s_scr) {
        return;
    }
    lv_obj_t *gone = s_scr;
    s_scr = NULL;
    s_sub = NULL;
    memset(s_row, 0, sizeof(s_row));
    s_n = -1;
    ui_nav_back();
    lv_obj_del(gone);
}

void ui_scan_page_show(const char *title, const char *sub_fmt, ui_scan_fn_t scan_fn)
{
    if (s_scr || !title || !sub_fmt || !scan_fn) {
        return;
    }

    const lv_color_t gray = lv_color_hex(0x808080);
    static const lv_coord_t row_y[LIST_N] = {140, 178, 216};

    s_sub_fmt = sub_fmt;
    s_scan_fn = scan_fn;
    s_n = -1;
    s_key_down = false;
    memset(s_names, 0, sizeof(s_names));

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    sdgoods_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回主页 */

    lv_obj_t *t = lv_label_create(s_scr);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 48);

    s_sub = lv_label_create(s_scr);
    lv_label_set_text(s_sub, SDG_T("扫描中...", "Scanning..."));
    lv_obj_set_style_text_font(s_sub, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(s_sub, gray, 0);
    lv_obj_align(s_sub, LV_ALIGN_TOP_MID, 0, 88);

    for (int i = 0; i < LIST_N; i++) {
        s_row[i] = lv_label_create(s_scr);
        lv_label_set_text(s_row[i], "");
        lv_obj_set_style_text_font(s_row[i], &si_yuan_black_icon_14, 0);
        lv_obj_set_style_text_color(s_row[i], lv_color_white(), 0);
        lv_obj_align(s_row[i], LV_ALIGN_TOP_MID, 0, row_y[i]);
    }

    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, SDG_T("按电源键返回", "Power key to go back"));
    lv_obj_set_style_text_font(hint, &si_yuan_black_icon_14, 0);   /* 提示行用小一号的字（14） */
    lv_obj_set_style_text_color(hint, gray, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 296);

    lv_scr_load(s_scr);
    if (!s_task) {
        xTaskCreate(scan_task, "scan", 4096, NULL, 5, &s_task);
    }
}

void ui_scan_page_poll(void)
{
    if (!s_scr) {
        return;
    }
    if (s_n >= 0) {
        apply_results();
    }
    const int down = (gpio_get_level(BOARD_KEY_GPIO) == BOARD_KEY_ACTIVE_LEVEL);
    if (down && !s_key_down) {
        close_page();
    }
    s_key_down = down;
}
