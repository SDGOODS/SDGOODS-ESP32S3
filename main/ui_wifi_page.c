#include "ui_wifi_page.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board_pins.h"
#include "ui_home.h"
#include "ui_swipe_back.h"
#include "wifi_scan.h"

LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(si_yuan_black_icon_14);

#define UI_WIFI_TITLE_Y   48
#define UI_WIFI_SUB_Y     88
#define UI_WIFI_SSID0_Y   140
#define UI_WIFI_SSID1_Y   178
#define UI_WIFI_SSID2_Y   216
#define UI_WIFI_HINT_Y    296

static const char *TAG = "ui_wifi";

static lv_obj_t *s_scr;
static lv_obj_t *s_sub;
static lv_obj_t *s_ssid[WIFI_SCAN_LIST_N];
static TaskHandle_t s_scan_task;
static volatile int s_scan_n = -1;
static wifi_scan_ap_t s_scan_buf[WIFI_SCAN_LIST_N];
static bool s_key_was_down;

static void apply_scan_results(void)
{
    if (!s_scr || s_scan_n < 0) {
        return;
    }

    char sub[48];
    if (s_scan_n <= 0) {
        snprintf(sub, sizeof(sub), "扫描附近0个SSID");
        lv_label_set_text(s_sub, sub);
        for (int i = 0; i < WIFI_SCAN_LIST_N; i++) {
            lv_label_set_text(s_ssid[i], "");
        }
    } else {
        snprintf(sub, sizeof(sub), "扫描附近%d个SSID", s_scan_n);
        lv_label_set_text(s_sub, sub);
        for (int i = 0; i < WIFI_SCAN_LIST_N; i++) {
            if (i < s_scan_n) {
                lv_label_set_text(s_ssid[i], s_scan_buf[i].ssid);
            } else {
                lv_label_set_text(s_ssid[i], "");
            }
        }
    }
    s_scan_n = -2;
}

static void scan_task(void *arg)
{
    (void)arg;
    int n = wifi_scan_list(s_scan_buf, WIFI_SCAN_LIST_N);
    s_scan_n = (n < 0) ? 0 : n;
    s_scan_task = NULL;
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
    memset(s_ssid, 0, sizeof(s_ssid));
    s_scan_n = -1;
    ui_home_show();
    lv_obj_del(gone);
}

void ui_wifi_page_show(void)
{
    if (s_scr) {
        return;
    }

    const lv_color_t gray = lv_color_hex(0x808080);

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回主页 */

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "WIFI");
    lv_obj_set_style_text_font(title, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_WIFI_TITLE_Y);

    s_sub = lv_label_create(s_scr);
    lv_label_set_text(s_sub, "扫描中...");
    lv_obj_set_style_text_font(s_sub, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(s_sub, gray, 0);
    lv_obj_align(s_sub, LV_ALIGN_TOP_MID, 0, UI_WIFI_SUB_Y);

    static const lv_coord_t ssid_y[WIFI_SCAN_LIST_N] = {
        UI_WIFI_SSID0_Y, UI_WIFI_SSID1_Y, UI_WIFI_SSID2_Y,
    };
    for (int i = 0; i < WIFI_SCAN_LIST_N; i++) {
        s_ssid[i] = lv_label_create(s_scr);
        lv_label_set_text(s_ssid[i], "");
        lv_obj_set_style_text_font(s_ssid[i], &si_yuan_black_icon_14, 0);
        lv_obj_set_style_text_color(s_ssid[i], lv_color_white(), 0);
        lv_obj_align(s_ssid[i], LV_ALIGN_TOP_MID, 0, ssid_y[i]);
    }

    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, "按电源键返回");
    lv_obj_set_style_text_font(hint, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(hint, gray, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, UI_WIFI_HINT_Y);

    lv_scr_load(s_scr);
    s_key_was_down = false;
    s_scan_n = -1;

    if (s_scan_task == NULL) {
        xTaskCreate(scan_task, "wifi_scan", 4096, NULL, 5, &s_scan_task);
    }
    ESP_LOGI(TAG, "page open, scanning");
}

bool ui_wifi_page_is_open(void)
{
    return s_scr != NULL;
}

void ui_wifi_page_poll(void)
{
    if (!s_scr) {
        return;
    }

    if (s_scan_n >= 0) {
        apply_scan_results();
    }

    const int down = (gpio_get_level(BOARD_KEY_GPIO) == BOARD_KEY_ACTIVE_LEVEL);
    if (down && !s_key_was_down) {
        close_page();
    }
    s_key_was_down = down;
}
