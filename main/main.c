/*
 * main.c —— 应用层装配点（Application entry）
 *
 * 这个文件属于**应用层**：它决定「用哪些应用、首屏是什么、怎么接线」。
 * 平台能力（屏 / 触摸 / 电源键 / 音频 / WiFi·BLE 扫描 / 应用框架 / 字体）
 * 全部来自 components/sdgoods_board，用一行 `#include "sdgoods_board.h"` 拿到。
 *
 * 想加自己的应用？
 *   跑 `python3 tools/new_app.py my_app "我的应用"`，它会生成骨架并自动接线；
 *   也可以照着 main/apps/app_template.c 手写，再参考 apps/apps_registry.c 的注释。
 */

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdgoods_board.h"    /* 平台层：板级支持包总入口 */
#include "apps_registry.h"    /* 应用层：应用清单与首屏接线 */

void app_main(void)
{
    /* 静音噪音大的子系统日志（只留 warn 以上），让串口日志聚焦在自己的代码上。
       调试某个子系统时，把它改成 ESP_LOG_INFO 或 DEBUG。 */
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

    /* 电池供电自锁：拉高保持上电，关机时由平台层 power_off.c 释放 */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_BAT_CONTROL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(BOARD_BAT_CONTROL_GPIO, BOARD_BAT_CONTROL_LATCH_LEVEL);

    /* 屏控制器上电初始化（此时背光仍关着，见下） */
    LCD_Init_Panel();
    lcd_panel_apply_vendor_madctl();
    /* 背光刻意延迟到开机动画首帧画好之后再点亮：
       LCD 复位后的默认帧是白屏，提前点亮会看到「先白屏再跳主页」的闪烁。 */

    /* 板载服务。它们只做初始化，耗时操作在各自任务里。 */
    ESP_ERROR_CHECK(wifi_scan_init());
    ESP_ERROR_CHECK(ble_scan_init());
    ESP_ERROR_CHECK(audio_recplay_init());
    key_input_init();

    /* LVGL 移植 + 触摸 + 硬件信息 */
    lvgl_port_init();
    ESP_ERROR_CHECK(touch_input_init());
    ESP_ERROR_CHECK(hw_info_init());

    /* 中文 fallback 已在编译期写入 si_yuan 图标字体的 .fallback 字段
       （见 components/sdgoods_board/fonts/si_yuan_black_icon_*.c），
       不可在运行时写 const 字体结构体，否则会触发 ESP32 flash Cache 错误。 */

    /* ★ 接线：把应用层的「轮询汇总 + 首屏 + 导航」注册给平台层。
       必须放在 ui_boot_show() 之前 —— 开机动画结束时会回调首屏创建函数。 */
    apps_register();

    /* 开机动画：点亮背光、播放 logo 动画，结束后自动进入 apps_registry 提供的首屏 */
    ui_boot_show();

    ui_app_shell_init();   /* 应用标准框架（共享音量等），须在 audio_recplay_init 之后 */

    lvgl_port_loop();      /* 永不返回：power_key_poll + lv_timer_handler + apps_poll */
}
