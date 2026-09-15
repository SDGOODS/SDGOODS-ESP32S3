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
#include "build_version.h"    /* 自动生成：版本号 + SDGOODS 品牌信息 */

static const char *TAG = "SDGOODS";

void app_main(void)
{
    /* 品牌与版本横幅 —— 固件自带的「身份证」。
       看串口日志或 dump 固件都能看出源头与授权状态。
       文案统一从 build_version.h 取，不要在别处另写一份字面量。 */
    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, " %s：%s", SDGOODS_PROGRAM, SDGOODS_PLATFORM);
    ESP_LOGI(TAG, " %s（%s）", SDGOODS_PRODUCT, SDGOODS_BRAND);
    ESP_LOGI(TAG, " %s", SDGOODS_VENDOR);
    ESP_LOGI(TAG, " 固件版本 %s", BUILD_VERSION_STR);
    ESP_LOGI(TAG, " %s", SDGOODS_LICENSE_TAG);
    ESP_LOGI(TAG, " %s", SDGOODS_HOMEPAGE);
    ESP_LOGI(TAG, "========================================================");

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

    /* 界面语言：出厂默认英文，用户可在 DEMO 页切换（存 NVS，重启保留）。
       必须在任何界面创建之前调用 —— 首屏是在 ui_boot_show() 里创建的。 */
    sdg_i18n_init();

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
