#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st77916.h"
#include "lvgl_port.h"
#include "ui_boot.h"
#include "ui_home.h"
#include "ui_app_shell.h"
#include "board_pins.h"
#include "touch_input.h"
#include "audio_recplay.h"
#include "ble_scan.h"
#include "wifi_scan.h"
#include "hw_info.h"
#include "lvgl.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);

void app_main(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("gpio", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);
    esp_log_level_set("phy", ESP_LOG_WARN);
    esp_log_level_set("coexist", ESP_LOG_WARN);
    esp_log_level_set("pp", ESP_LOG_WARN);
    esp_log_level_set("net80211", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
    esp_log_level_set("BLE_INIT", ESP_LOG_WARN);
    esp_log_level_set("BT_BTC", ESP_LOG_WARN);
    esp_log_level_set("BT_BTM", ESP_LOG_WARN);
    esp_log_level_set("BT_APPL", ESP_LOG_WARN);
    esp_log_level_set("i2c", ESP_LOG_ERROR);

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_BAT_CONTROL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(BOARD_BAT_CONTROL_GPIO, BOARD_BAT_CONTROL_LATCH_LEVEL);

    LCD_Init_Panel();
    lcd_panel_apply_vendor_madctl();
    /* 背光亮起延迟到首屏(home)画好之后，避免开机白屏/闪屏：
       原 Set_Backlight(80) 在 LVGL 就绪前点亮，此时屏上是 LCD 复位后的默认帧(常白屏)，
       导致开机先白屏再跳 home。 */

    ESP_ERROR_CHECK(wifi_scan_init());
    ESP_ERROR_CHECK(ble_scan_init());
    ESP_ERROR_CHECK(audio_recplay_init());
    key_input_init();


    lvgl_port_init();
    ESP_ERROR_CHECK(touch_input_init());
    ESP_ERROR_CHECK(hw_info_init());

    /* 中文 fallback 已在编译期写入 si_yuan 图标字体的 .fallback 字段（见 si_yuan_black_icon_*.c），
       不可在运行时写 const 字体结构体，否则会触发 ESP32 flash Cache 错误。 */

    /* 开机动画：点亮背光、播放 SDGOODS logo 缩放淡入动画，结束后自动创建并加载 home。
       原有 "home 先构建再点亮背光" 的逻辑被 boot 替代，避免 LCD 复位后的白屏闪。 */
    ui_boot_show();

    ui_app_shell_init();   /* 初始化应用标准框架共享音量(须在 audio_recplay_init 之后) */

    lvgl_port_loop();
}
