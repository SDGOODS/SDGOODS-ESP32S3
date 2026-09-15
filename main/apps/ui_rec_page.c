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

#include "ui_rec_page.h"

#include <string.h>

#include "audio_recplay.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui_home.h"
#include "ui_swipe_back.h"

LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(si_yuan_black_icon_14);

typedef enum {
    REC_IDLE = 0,
    REC_RECORDING,
    REC_PLAYING,
} rec_state_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_btn;
static lv_obj_t *s_btn_lbl;
static TaskHandle_t s_task;
static volatile rec_state_t s_state;
static volatile rec_state_t s_ui_state;
static bool s_key_down;

static void set_btn(rec_state_t st)
{
    if (!s_btn_lbl) {
        return;
    }
    if (st == REC_IDLE) {
        lv_label_set_text(s_btn_lbl, "开始录音");
        lv_obj_set_style_text_color(s_btn_lbl, lv_color_white(), 0);
        lv_obj_add_flag(s_btn, LV_OBJ_FLAG_CLICKABLE);
    } else if (st == REC_RECORDING) {
        lv_label_set_text(s_btn_lbl, "录音中");
        lv_obj_set_style_text_color(s_btn_lbl, lv_color_hex(0x00E676), 0);
        lv_obj_clear_flag(s_btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_label_set_text(s_btn_lbl, "播放中");
        lv_obj_set_style_text_color(s_btn_lbl, lv_color_hex(0x00E676), 0);
        lv_obj_clear_flag(s_btn, LV_OBJ_FLAG_CLICKABLE);
    }
    s_ui_state = st;
}

static void worker(void *arg)
{
    (void)arg;
    s_state = REC_RECORDING;
    if (audio_recplay_record() == ESP_OK) {
        s_state = REC_PLAYING;
        (void)audio_recplay_play();
    }
    s_state = REC_IDLE;
    s_task = NULL;
    vTaskDelete(NULL);
}

static void on_start(lv_event_t *e)
{
    (void)e;
    if (s_state != REC_IDLE || s_task) {
        return;
    }
    set_btn(REC_RECORDING);
    xTaskCreatePinnedToCore(worker, "recplay", 8192, NULL, 20, &s_task, 1);
}

static void close_page(void)
{
    if (!s_scr) {
        return;
    }
    audio_recplay_abort();
    for (int i = 0; i < 60 && s_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    lv_obj_t *gone = s_scr;
    s_scr = NULL;
    s_btn = NULL;
    s_btn_lbl = NULL;
    s_state = REC_IDLE;
    s_ui_state = REC_IDLE;
    ui_nav_back();
    lv_obj_del(gone);
}

void ui_rec_page_show(void)
{
    if (s_scr) {
        return;
    }

    const lv_color_t gray = lv_color_hex(0x808080);
    s_state = REC_IDLE;
    s_ui_state = REC_IDLE;
    s_key_down = false;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回主页 */

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "录音");
    lv_obj_set_style_text_font(title, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *sub = lv_label_create(s_scr);
    lv_label_set_text(sub, "录5s声音后自动播放");
    lv_obj_set_style_text_font(sub, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(sub, gray, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 88);

    s_btn = lv_btn_create(s_scr);
    lv_obj_set_size(s_btn, 120, 120);
    lv_obj_center(s_btn);
    lv_obj_set_style_radius(s_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(s_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_btn, 0, 0);
    lv_obj_add_event_cb(s_btn, on_start, LV_EVENT_CLICKED, NULL);

    s_btn_lbl = lv_label_create(s_btn);
    lv_obj_set_style_text_font(s_btn_lbl, &si_yuan_black_icon_14, 0);
    lv_obj_center(s_btn_lbl);
    set_btn(REC_IDLE);

    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, "按电源键返回");
    lv_obj_set_style_text_font(hint, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(hint, gray, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 296);

    lv_scr_load(s_scr);
}

void ui_rec_page_poll(void)
{
    if (!s_scr) {
        return;
    }
    if (s_state != s_ui_state) {
        set_btn(s_state);
    }
    const int down = (gpio_get_level(BOARD_KEY_GPIO) == BOARD_KEY_ACTIVE_LEVEL);
    if (down && !s_key_down) {
        close_page();
    }
    s_key_down = down;
}
