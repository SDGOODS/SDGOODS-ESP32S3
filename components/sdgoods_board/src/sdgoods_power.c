/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（板级支持包 BSP）
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于平台层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

#include "sdgoods_power.h"

#include "board_pins.h"
#include "sdgoods_lcd.h"
#include "sdgoods_boot_skip.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_attr.h"            /* RTC_DATA_ATTR：跨深睡保留的唤醒标志 */
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"              /* 关机画面 "Power Off" 覆盖层 */
#include "sdgoods_lvgl.h"      /* sdgoods_lvgl_post：跨任务安全投递到 LVGL 线程 */

static const char *TAG = "power";

/* 轻量低功耗状态：主页短按电源键进入，再短按恢复。
 * 仅关背光 + 让 LVGL 渲染循环暂停（见 sdgoods_lvgl_loop），不切断板级供电。 */
static bool s_suspended = false;
static uint8_t s_saved_backlight = 60;   /* 仅在 suspend 前未读到真实背光时兜底，正常由 sdgoods_lcd_get_backlight() 覆盖 */

void sdgoods_power_suspend_toggle(void)
{
    if (!s_suspended) {
        s_saved_backlight = sdgoods_lcd_get_backlight();
        sdgoods_lcd_set_backlight(0);
        s_suspended = true;
        ESP_LOGI(TAG, "screen suspended (low power)");
    } else {
        sdgoods_lcd_set_backlight(s_saved_backlight);
        s_suspended = false;
        ESP_LOGI(TAG, "screen resumed");
    }
}

bool sdgoods_power_is_suspended(void)
{
    return s_suspended;
}

/* 深度睡眠唤醒后是否跳过开机动画（仅启动器宿主读取，见 sdgoods_boot.c 的 sdgoods_boot_show）。
 * 主页短按深睡（sdgoods_power_enter_deep_sleep）置位；控制中心关机（sdgoods_power_off）清除。
 * 存于 RTC_DATA_ATTR：深睡唤醒后保留，真关机断电后丢失（冷启动默认播动画）。
 * 之所以需要这个标志，而不是在 boot 里简单判 `wakeup_cause==EXT0`：
 *   CC 关机若板级未真正断电会落到 sdgoods_power_off 里的深睡兜底，其唤醒源同样是 EXT0 电源键，
 *   与「主页短按深睡唤醒」无法靠唤醒源区分；用显式标志才能正确区分两条语义。 */
RTC_DATA_ATTR static uint8_t s_wake_skip_gif = 0;

void sdgoods_power_set_wake_skip_gif(bool skip)
{
    s_wake_skip_gif = skip ? 1 : 0;
}

bool sdgoods_power_consume_wake_skip_gif(void)
{
    bool ret = (s_wake_skip_gif != 0);
    s_wake_skip_gif = 0;   /* 消费掉，避免残留影响后续启动 */
    return ret;
}

/* 主页短按电源键 → 熄屏 + 进入超低功耗浅睡眠。
 * 唤醒源 = 电源键（ext0）；按一下即「原地唤醒并亮屏」，走的是恢复（resume）而非冷启动，
 * 所以**不会重启**：唤醒后从本函数返回处继续，LVGL / 外设 / 应用状态全部原样保留。
 * 与 sdgoods_power_off() 的 fallback（真·深度睡眠，唤醒=冷重启）是两套不同的语义。
 * 进入前记背光并熄屏；唤醒后等电源键松开再亮屏，避免把「唤醒那一按」当成一次新短按。 */
void sdgoods_power_enter_light_sleep(void)
{
    ESP_LOGI(TAG, "entering light sleep (wake on power key press)");
    s_saved_backlight = sdgoods_lcd_get_backlight();
    sdgoods_lcd_set_backlight(0);   /* 先熄屏再入睡 */

    esp_sleep_enable_ext0_wakeup(BOARD_KEY_GPIO, BOARD_KEY_ACTIVE_LEVEL);
    esp_light_sleep_start();

    /* 醒来：此刻电源键正处在按下态（本就是它触发的唤醒）。等它松开，
       否则回到 sdgoods_power_key_poll 后会把这次「唤醒按」判成一次新短按而立刻再睡。 */
    int guard = 0;
    while ((gpio_get_level(BOARD_KEY_GPIO) == BOARD_KEY_ACTIVE_LEVEL) && guard < 4000) {
        vTaskDelay(pdMS_TO_TICKS(2));
        guard++;
    }
    sdgoods_lcd_set_backlight(s_saved_backlight);
    ESP_LOGI(TAG, "resumed from light sleep (screen lit)");
}

/* 主页短按电源键 → 进入深度睡眠（最低功耗）。
 * 与 sdgoods_power_enter_light_sleep 的取舍：
 *   · 浅睡：原地恢复（不重启），唤醒快，但 SoC 仍部分上电、耗电较高；
 *   · 深睡：整片断电、唤醒 = 冷启动（走 app_main 重跑），唤醒较慢，但功耗最低。
 * 用户明确选择深睡（功耗优先）。唤醒后直接回主页、不播开机动画：
 *   EBADGE/PLANE/HELLO 冷启动本就直进主页（main.c 不调开机动画）；
 *   启动器宿主（LAUNCHER）在 sdgoods_boot_show 里对「主页短按深睡唤醒」显式跳过 GIF。
 * 关键：深睡会丢失普通 GPIO 输出，必须把电池自锁闩（BOARD_BAT_CONTROL_GPIO=GPIO7）
 * 用 gpio_hold_en + gpio_deep_sleep_hold_en 锁住，否则唤醒瞬间板上已断电、
 * 电源键的 EXT0 唤醒虽触发却无电可起。GPIO7 在 ESP32-S3 的 RTC GPIO 范围内，可安全 hold。
 * 唤醒源 = 电源键（GPIO6，active-low，EXT0）。唤醒那一下按键仍按住，
 * 释放 guard 在 sdgoods_key_init 里处理（避免被当成新短按立刻再睡）。
 * 在入睡前置位「跳过开机动画」标志，供 LAUNCHER 的 sdgoods_boot_show 在 EXT0 唤醒后识别。 */
void sdgoods_power_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "entering deep sleep (wake on power key press, lowest power)");
    s_saved_backlight = sdgoods_lcd_get_backlight();
    sdgoods_lcd_set_backlight(0);   /* 先熄屏再入睡 */

    /* 锁住电池自锁闩，使其在高功耗深睡期间仍保持「上电」电平。 */
    gpio_hold_en(BOARD_BAT_CONTROL_GPIO);
    gpio_deep_sleep_hold_en();

    sdgoods_power_set_wake_skip_gif(true);   /* 标记：本次深睡唤醒应跳过开机动画 */

    esp_sleep_enable_ext0_wakeup(BOARD_KEY_GPIO, BOARD_KEY_ACTIVE_LEVEL);
    esp_deep_sleep_start();
    /* 不会返回；唤醒后从复位向量冷启动 */
}

/* DEBUG ONLY：在 LVGL 线程画出与 sdgoods_power_off 同款的「Power Off」覆盖层，
 * 但不切电，便于串口截屏核验关机画面。必须用 sdgoods_lvgl_post 投到 LVGL 线程，
 * 否则会乱摸 LVGL 对象树（见 sdgoods_lvgl.h 的红线说明）。 */
static void power_off_preview_draw(void *arg)
{
    (void)arg;
    lv_obj_t *scr = lv_scr_act();
    if (!scr) {
        return;
    }
    lv_obj_t *ov = lv_obj_create(scr);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(ov);

    lv_obj_t *lbl = lv_label_create(ov);
    lv_label_set_text(lbl, "Power Off");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);

    sdgoods_lcd_set_backlight(100);   /* 确保可见（可能处于熄屏态） */
    lv_refr_now(NULL);                /* 立即刷新到屏，不等主循环 */
}

void sdgoods_power_off_preview(void)
{
    sdgoods_lvgl_post(power_off_preview_draw, NULL);
}

void sdgoods_power_off(void)
{
    /* 关机画面：先显示 "Power Off"，让用户明确知道正在关机（再短按恢复无效）。
       可能处于熄屏态（背光 0），故先把背光拉亮再强制刷新一帧。 */
    lv_obj_t *scr = lv_scr_act();
    if (scr) {
        lv_obj_t *ov = lv_obj_create(scr);
        lv_obj_remove_style_all(ov);
        lv_obj_set_size(ov, LCD_WIDTH, LCD_HEIGHT);
        lv_obj_set_pos(ov, 0, 0);
        lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
        lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(ov);

        lv_obj_t *lbl = lv_label_create(ov);
        lv_label_set_text(lbl, "Power Off");
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);

        sdgoods_lcd_set_backlight(100);   /* 确保可见（可能处于熄屏态） */
        lv_refr_now(NULL);                /* 立即刷新到屏，不等主循环 */
        vTaskDelay(pdMS_TO_TICKS(800));   /* 让用户看清再断电 */
    }

    /* 若在 app 内关机：下次开机直接回启动器（factory 分区）。
       关机是完整的「断电-再上电」周期，视为一次冷启动，开机动画照常播放，
       故不写「跳过动画」标志。factory 指向启动器。 */
    const esp_partition_t *factory =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                 ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory) {
        esp_ota_set_boot_partition(factory);
    }

    /* Turn the display off first so we don't leave a lit panel behind. */
    sdgoods_lcd_set_backlight(0);
    vTaskDelay(pdMS_TO_TICKS(200));

    /* 明确清除「跳过开机动画」标志：本次是真实关机意图，下次上电（无论真断电冷启
       还是板级未断电落到下方深睡兜底）都应播开机动画，而非像主页短按深睡那样跳过。 */
    sdgoods_power_set_wake_skip_gif(false);

    /* Release the self-holding battery latch -> hard power cut.
       BOARD_BAT_CONTROL_LATCH_LEVEL is the level that keeps power on, so the
       opposite level releases it. */
    gpio_set_level(BOARD_BAT_CONTROL_GPIO,
                   BOARD_BAT_CONTROL_LATCH_LEVEL ? 0 : 1);

    /* Fallback: if power was NOT actually cut (e.g. the physical power button
       is still held and overrides the latch), drop into deep sleep and wake on
       the physical key so the badge at least stops consuming power.
       注意：上面的 set_wake_skip_gif(false) 已抢先清掉标志，所以这条兜底深睡唤醒
       后也会播开机动画（与真实关机语义一致），不会误判成「主页短按深睡」而跳过 GIF。 */
    esp_sleep_enable_ext0_wakeup(BOARD_KEY_GPIO, BOARD_KEY_ACTIVE_LEVEL);
    esp_deep_sleep_start();
}
