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

#include "sdgoods_input.h"

#include "sdgoods_power.h"
#include "sdgoods_app_shell.h"
#include "sdgoods_hooks.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_sleep.h"          /* esp_sleep_get_wakeup_cause：深睡 EXT0 唤醒后吃掉「唤醒按」 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board_pins.h"
#include "sdgoods_lcd.h"

#define CST816_ADDR        0x15
#define CST816_REG_DATA    0x02
#define CST816_RST_LOW_MS  11
#define CST816_TRON_MS     50

static lv_indev_drv_t s_indev_drv;
static lv_indev_t     *s_indev = NULL;   /* 注册后保存指针 indev 句柄（备用） */

/* 直接缓存最近一次触摸读取的状态/坐标，供游戏在主循环里轮询，
 * 完全绕开 LVGL 事件派发，且本工程 LVGL 8.3.11 未导出 lv_indev_get_state。 */
static lv_indev_state_t s_touch_state = LV_INDEV_STATE_RELEASED;
static lv_point_t       s_touch_point = {0, 0};

static esp_err_t cst816_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device((i2c_port_t)BOARD_I2C_PORT, CST816_ADDR,
                                        &reg, 1, data, len, pdMS_TO_TICKS(50));
}

static void map_raw_to_lvgl(uint16_t raw_x, uint16_t raw_y, uint16_t *out_x, uint16_t *out_y)
{
    uint16_t x = (uint16_t)(LCD_WIDTH - 1 - raw_x);
    uint16_t y = raw_y;
    *out_x = y;
    *out_y = x;
    if (*out_x >= LCD_WIDTH) {
        *out_x = LCD_WIDTH - 1;
    }
    if (*out_y >= LCD_HEIGHT) {
        *out_y = LCD_HEIGHT - 1;
    }
}

static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    uint8_t buf[5] = {0};
    if (cst816_read(CST816_REG_DATA, buf, sizeof(buf)) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        s_touch_state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if ((buf[0] & 0x0F) == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        s_touch_state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t raw_x = ((uint16_t)(buf[1] & 0x0F) << 8) | buf[2];
    uint16_t raw_y = ((uint16_t)(buf[3] & 0x0F) << 8) | buf[4];
    uint16_t x = 0, y = 0;
    map_raw_to_lvgl(raw_x, raw_y, &x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;

    /* 缓存最新触摸状态/坐标，供游戏直接轮询（与 LVGL 事件派发解耦） */
    s_touch_state = LV_INDEV_STATE_PRESSED;
    s_touch_point.x = (lv_coord_t)x;
    s_touch_point.y = (lv_coord_t)y;
}

esp_err_t sdgoods_touch_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config((i2c_port_t)BOARD_I2C_PORT, &conf));
    esp_err_t err = i2c_driver_install((i2c_port_t)BOARD_I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (BOARD_TOUCH_GPIO_RST >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << (unsigned)BOARD_TOUCH_GPIO_RST,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level(BOARD_TOUCH_GPIO_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(CST816_RST_LOW_MS));
        gpio_set_level(BOARD_TOUCH_GPIO_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(CST816_TRON_MS));
    }

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touchpad_read;
    s_indev = lv_indev_drv_register(&s_indev_drv);

    return ESP_OK;
}

/* 返回已注册的指针 indev 句柄（备用） */
lv_indev_t *sdgoods_touch_get_indev(void)
{
    return s_indev;
}

/* 最近一次触摸读取的状态（RELEASED / PRESSED）。由 touchpad_read 实时更新，
 * 主循环在 lv_timer_handler() 之后即可读到最新值。 */
lv_indev_state_t sdgoods_touch_get_state(void)
{
    return s_touch_state;
}

/* 最近一次触摸读取的坐标（LVGL 坐标空间）。 */
void sdgoods_touch_get_point(lv_point_t *p)
{
    if (p) {
        *p = s_touch_point;
    }
}

/* 合成触摸同步写轮询缓存：让轮询型 app（不经 LVGL indev、直接读缓存的）也能
 * 「看见」串口调试键注入的点按/滑动。语义与真实触摸完全一致 —— 合成序列结束后
 * 由还原出来的真实触摸驱动 touchpad_read 把缓存刷回真实状态，无需额外清理。 */
void sdgoods_touch_synth_report(lv_indev_state_t st, lv_coord_t x, lv_coord_t y)
{
    s_touch_state = st;
    s_touch_point.x = x;
    s_touch_point.y = y;
}

void sdgoods_key_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << (unsigned)BOARD_KEY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* 若本次启动是「深度睡眠 EXT0 唤醒」：唤醒那一刻电源键仍被按住
     * （正是它触发的唤醒）。若直接开始轮询，sdgoods_power_key_poll 会把这次
     * 「唤醒按」当成一次新短按，在松手时立刻再进深睡。在此自旋等待按键松开，
     * 把这次唤醒按「吃掉」。深睡 = 冷启动，按键松开后轮询从干净状态
     * （s_key_was_down=false）重新开始。电池闩已在 app_main 里重新拉高，
     * 故等待期间不会掉电。 */
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        int guard = 0;
        while ((gpio_get_level(BOARD_KEY_GPIO) == BOARD_KEY_ACTIVE_LEVEL) && guard < 6000) {
            vTaskDelay(pdMS_TO_TICKS(5));
            guard++;
        }
    }
}

/* ---------------------------------------------------------------------------
 * 电源键统一处理
 * 短按（分级导航，任何页面通用）：
 *   ① 上层钩子优先：启动器控制中心浮层打开 → 关闭浮层；应用子页打开 → 由应用钩子
 *      返回 true 表示已回退一级（子页 → 应用主页）。钩子未消费才走下面默认逻辑。
 *   ② 默认导航（已处在「应用主页」或「启动器主页」时）：
 *        · 应用菜单打开          → 关闭菜单；
 *        · 在应用内（钩子未消费）→ 多应用模式返回启动器（应用主页 → 启动器），
 *                                  派生/单应用模式熄屏 + 进入深度睡眠；
 *        · 已在应用主页          → 多应用模式（被启动器管理的 app）重启回启动器；
 *                                  其余熄屏 + 进入深度睡眠；
 *        · 已在启动器/设备主页   → 短按熄屏 + 进入深度睡眠（最低功耗，
 *                                  按一下电源键唤醒 = 冷启动直回主页、不重启）。
 *      即形成层级：子页 → 应用主页 →（多应用）启动器 → 深睡兜底；
 *      （派生/单应用）应用主页 / 启动器主页 → 深度睡眠。
 * 长按：按住 >= 1.2 s 后松手立即关机（任何页面通用，关机画面显示 "Power Off"），
 *       此时按键已不再压住自锁闩，闩释放即真正断电。
 * 注意：不能在按键仍按住时直接调用 sdgoods_power_off()，否则按键会把闩的释放
 * 信号覆盖掉，导致设备进入深度睡眠而不是关机，表现为“死机/黑屏动不了”。
 * ------------------------------------------------------------------------- */
static bool s_key_was_down = false;
static TickType_t s_key_press_tick = 0;
static bool s_long_hold = false;

/* 电源键「短按」的统一动作：真实短按（松手沿）与串口调试键 'P' 共用同一条路径，
 * 保证「串口模拟 == 真手指按键」。
 * 层级：上层钩子（关浮层/子页回退一级）→ 关应用菜单 → 应用内（多应用：回启动器；
 * 单应用：深度睡眠）→ 应用主页（被管理 app：重启回启动器；其余：深度睡眠）→ 深度睡眠。 */
void sdgoods_power_key_short_action(void)
{
    /* 短按：上层钩子优先（启动器控制中心浮层打开 → 关闭浮层；
       应用子页打开 → 由应用钩子回退一级并返回 true）。
       返回 true 表示已消费，不再走默认导航。 */
    if (sdgoods_ui_power_short()) {
        return;
    }
    if (sdgoods_app_shell_menu_is_open()) {
        sdgoods_app_shell_menu_close();
    } else if (sdgoods_app_shell_is_app_active()) {
        /* 处在某应用内、且子页已收起（钩子未消费 = 已在应用主页）：
         * · 多应用模式：返回到启动器（写 otadata 指回 factory + 重启；
         *   sdgoods_multi_app_exit_to_launcher 内部再判「是否被启动器管理」，
         *   非被管理 app 返回 false 时退回「熄屏 + 浅睡眠」兜底）；
         * · 派生/单应用模式：已在应用主页，熄屏并进入深度睡眠
         *   （最低功耗；再按电源键唤醒 = 冷启动直回主页、不重启，
         *   见 sdgoods_power_enter_deep_sleep）。 */
        if (sdgoods_device_is_multi_app_mode()) {
            if (!sdgoods_multi_app_exit_to_launcher()) {
                sdgoods_power_enter_light_sleep();
            }
        } else {
            sdgoods_power_enter_deep_sleep();
        }
    } else {
        /* 已在「应用主页 / 启动器主页」：
         * · 被启动器管理的 app（多应用）→ 退出回启动器
         *   （写 otadata 指回 factory + 重启，见
         *   sdgoods_multi_app_exit_to_launcher 强符号）；
         * · 其余（派生 / 单应用 / 启动器宿主主页）→ 短按熄屏 +
         *   进入深度睡眠（最低功耗；再按电源键唤醒 = 冷启动直回主页、
         *   启动器宿主跳过开机 GIF，见 sdgoods_power_enter_deep_sleep）。 */
        if (!sdgoods_multi_app_exit_to_launcher()) {
            sdgoods_power_enter_deep_sleep();
        }
    }
}

void sdgoods_power_key_poll(void)
{
    bool down = (gpio_get_level(BOARD_KEY_GPIO) == BOARD_KEY_ACTIVE_LEVEL);

    if (down) {
        if (!s_key_was_down) {
            s_key_press_tick = xTaskGetTickCount();
            s_long_hold = false;
        } else if (!s_long_hold &&
                   (xTaskGetTickCount() - s_key_press_tick) >= pdMS_TO_TICKS(1200)) {
            s_long_hold = true;   /* 长按阈值已到，但此时按键仍按住，暂不关机 */
        }
    } else {
        if (s_key_was_down) {
            if (s_long_hold) {
                s_long_hold = false;
                sdgoods_power_off();   /* 松手瞬间按键已释放，干净关机 */
            } else {
                sdgoods_power_key_short_action();   /* 短按：统一动作（与调试键 'P' 同路径） */
            }
        }
    }
    s_key_was_down = down;
}

bool sdgoods_power_key_long_hold(void)
{
    return s_long_hold;
}
