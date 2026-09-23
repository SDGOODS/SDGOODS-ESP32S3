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

/*
 * main.c —— 应用层装配点（Application entry）
 *
 * 这个文件属于**应用层**：它决定「用哪些应用、首屏是什么、怎么接线」。
 * 平台能力（屏 / 触摸 / 电源键 / 音频 / WiFi·BLE 扫描 / 应用框架 / 字体）
 * 全部来自 components/sdgoods_board，用一行 `#include "sdgoods_board.h"` 拿到。
 *
 * 想加自己的应用？（在本仓库内加演示应用）照着 main/apps/app_template.c 手写，
 * 再参考 main/apps/apps_registry.c 的「新增一个应用」三步；要独立开发并上架自己的应用，
 * 用 `python3 tools/new_app_project.py <name>` 派生一个 PLANE 形单应用直启工程（见 skill sdgoods-new-app）。
 */

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdgoods_board.h"    /* 平台层：板级支持包总入口 */
#include "sdgoods_launcher.h" /* 平台层：多应用动态插槽（开机自校验 + 孤儿清理） */
#include "apps_registry.h"    /* 应用层：应用清单与首屏接线 */
#include "app_data_store.h"   /* 应用层：持久化数据示例（appdata 优先，退 NVS） */
#include "build_version.h"    /* 自动生成：版本号 + SDGOODS 品牌信息 */
#include "lvgl.h"              /* lv_refr_now：创建首屏后立刻刷新一帧，避免背光点亮时的白屏闪烁 */

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
    ESP_LOGI(TAG, " 联系 %s", SDGOODS_CONTACT_EMAIL);
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

    /* 电池供电自锁：拉高保持上电，关机时由平台层 sdgoods_power_off.c 释放 */
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
    sdgoods_lcd_init_panel();
    sdgoods_lcd_apply_vendor_madctl();
    /* 背光刻意延迟到开机动画首帧画好之后再点亮：
       LCD 复位后的默认帧是白屏，提前点亮会看到「先白屏再跳主页」的闪烁。 */

    sdgoods_key_init();

    /* 界面语言：出厂默认英文，用户可在 DEMO 页切换（存 NVS，重启保留）。
       必须在任何界面创建之前调用 —— 首屏是在 sdgoods_ui_home_create_show() 里创建的。 */
    sdg_i18n_init();

    /* LVGL 移植 + 触摸 + 硬件信息 */
    sdgoods_lvgl_init();
    ESP_ERROR_CHECK(sdgoods_touch_init());
    ESP_ERROR_CHECK(sdgoods_hw_info_init());

    /* ⚠️ 板载服务（WiFi/BLE/Audio）刻意排在 LVGL/触摸之后 —— 内存原因，勿提前：
     *   本工程内部可分配 RAM 只有 ~156KiB（heap_init 日志），而 WiFi+BLE+I2S 的
     *   驱动缓冲会把其中的相当一部分吃掉。LVGL 的绘制缓冲虽然放在 PSRAM（全屏双块），
     *   但 PSRAM 不能直接 DMA，每个 SPI 分片仍需要一块**内部 DMA 弹跳缓冲**
     *   （见 sdgoods_lcd.h 的弹跳预算说明）。若 WiFi/BLE 先于 LVGL 初始化，
     *   LVGL 建缓冲时内部 DMA 最大连续块只剩 ~10KB，随后界面一建就掉到 ~1.4KB，
     *   整屏 flush 分片分配失败 -> 屏幕不更新（「下滑没有控制中心」「打不开 app0」）。 */
    ESP_ERROR_CHECK(sdgoods_wifi_init());
    ESP_ERROR_CHECK(sdgoods_ble_init());
    ESP_ERROR_CHECK(sdgoods_audio_init());

    /* 中文 fallback 已在编译期写入 si_yuan 图标字体的 .fallback 字段
       （见 components/sdgoods_board/fonts/si_yuan_black_icon_*.c），
       不可在运行时写 const 字体结构体，否则会触发 ESP32 flash Cache 错误。 */

    /* ★ 接线：把应用层的「轮询汇总 + 首屏 + 导航」注册给平台层。
       必须放在 sdgoods_ui_home_create_show() 之前 —— 它会回调首屏创建函数。 */
    apps_register();

    /* 直接进入首屏：本工程(app0)不再单独播开机动画 —— 设备开机动画由 Launcher 在上电时
       负责，从 Launcher 启动 app0 时若再播一遍会重复。先建好首屏并刷新一帧，再点亮背光，
       避免 LCD 复位后的白屏闪烁。 */
    sdgoods_ui_home_create_show();
    lv_refr_now(NULL);
    sdgoods_lcd_set_backlight(60);   /* 出厂默认亮度 60%（与 sdgoods_cc.c 的 s_bri_user 一致） */

    /* ⚠️ 这里**不要**调 sdgoods_launcher_boot_check()（2026-09-19 移除，别再加回来）。
     *    它是**启动器宿主**的职责：扫槽 → 以 flash 为准校正 manifest → 清理孤儿 appdata。
     *    app 固件在两种模式下都不该做，而且会真出事：
     *      · MULTI（被启动器装进 ota_N）：manifest 是启动器的簿记对象，app 改写它 = 越权，
     *        还会与 otadata 错位；
     *      · SINGLE（自己就是主机固件）：本机没有「已装槽」⇒ 孤儿清理把**本 app 自己的
     *        appdata 目录**当成孤儿删掉（每次开机删一次）。
     *    平台层已加权限闸门（`deny_unless_host`）兜底：非宿主调用会返回
     *    ESP_ERR_INVALID_STATE 并打一条 ERROR 日志，所以留着也只会刷一条错误日志。
     *    启动器自己的 main.c（launcher_main.c）里有这个调用，那才是正确位置。 */

    sdgoods_app_shell_init();   /* 应用标准框架（共享音量等），须在 sdgoods_audio_init 之后 */

    /* 持久化数据示例：appdata(FAT) 可用就写文件，否则退 NVS 键值。
       ⚠️ 刻意排在 LVGL/WiFi/BLE 之后 —— 挂载 FAT + 磨损均衡会**常驻**占一块内部 RAM，
       而内部 DMA 最大连续块是显示管线的硬预算（见 sdgoods_lcd.h 的弹跳预算）。
       放在前面会把 LVGL 建缓冲时的余量压掉，正是 2026-09-19 那次「app 打不开 /
       下滑没有控制中心」的成因。本模块开机日志会打出挂载后的余量便于核对。 */
    app_data_store_init();

    sdgoods_lvgl_loop();      /* 永不返回：sdgoods_power_key_poll + lv_timer_handler + apps_poll */
}
