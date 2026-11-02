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
#include "sdgoods_tap.h"        /* SDGOODS_TAP_SLOP：滚动阈值必须与点按判据同源（见下） */

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"            /* 写盘窗口 / 慢读退避必须出声：那是验收时唯一的正面证据 */
#include "esp_sleep.h"          /* esp_sleep_get_wakeup_cause：深睡 EXT0 唤醒后吃掉「唤醒按」 */
#include "esp_timer.h"          /* esp_timer_get_time：写盘窗口与慢读计时用的单调时钟 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board_pins.h"
#include "sdgoods_lcd.h"

#define TAG                 "touch"
#define CST816_ADDR        0x15
#define CST816_REG_DATA    0x02
#define CST816_RST_LOW_MS  11
#define CST816_TRON_MS     50

/* 单次读 I2C 的超时。🔴 2026-10-31 从 50 提到 **90**：flash 写盘期间实测最坏 78.7 ms
 * （正常 ~200 µs）。这一道不是"防崩溃"（防崩溃靠下面的写盘窗口 + 慢读退避），
 * 而是撞上了也**别触发 FSM 复位** —— `i2c_master_cmd_begin()` 一旦超时会进
 * `i2c_hw_fsm_reset()` / `i2c_master_clear_bus()`，后者是不 yield 的 50 ms 忙等，
 * 连续几趟就把中断关够 300 ms ⇒ 中断看门狗 panic。 */
#define CST816_RD_TIMEOUT_MS  90

/* `sdgoods_touch_flash_window_end()` 之后再静默多久才放开触摸。
 * 40 ms 的来源：一次正常 I2C 读 ~200 µs、写盘期间实测 35~78 ms —— 取"最短的那一档"
 * 做收尾余量，既能盖住 FATFS 目录项更新，又不会让用户觉得松手后屏还在发呆。 */
#define SDGOODS_TOUCH_FLASH_GRACE_MS 40

/* ---- 慢读自适应退避 ------------------------------------------------------
 * 为什么除了"写盘窗口"还要这一道：写盘窗口得由**每个**写盘方自觉声明，漏一处就漏一处；
 * 而慢读是**已经发生的事实** —— 只要连续读慢就把轮询间隔拉开，等于"总线越忙越少敲它"，
 * 对没被声明的写盘路径同样有效。两者一正一反，缺一个都不够。
 * ⚠️ 阈值 20 ms：正常一次 6 字节读 ~200 µs，20 ms 只会在"真的被抢"时才触发。
 * ⚠️ 恢复是**立刻**的（一次正常就清零），否则退避会自己锁死在慢档。 */
#define TOUCH_SLOW_US         20000
#define TOUCH_SLOW_STEP_MS    20      /* 每多一次连续慢读，多让这么多 */
#define TOUCH_SLOW_MAX_MS     300     /* 让路上限：再慢也不能把触摸拖到不可用 */

/* 静默截止时刻：**单调毫秒**时间戳（int32，差值比较 ⇒ 天然扛 49 天回绕）。
 * 写盘窗口与慢读退避**共用这一个状态**：只用"截止时刻"表达，不设"谁持牌"的布尔 ——
 * 少一个状态就少一种漏 `_end()` 的死法；两个来源都只"往后推"，谁晚听谁的。 */
static volatile int32_t s_quiet_until_ms;

/* 静默期间要不要**先补一次 RELEASED**：写盘窗口 = 要（正按着也得让 UI 松手，
 * 否则窗口一开用户的手指就从 UI 的视角消失了）；慢读退避 = 不要（那是我们主动
 * 少读几帧，不能把"正按着"误判成"松手"，否则长按会被切断）。
 * ⚠️ 只允许置 true，绝不允许置 false —— 慢读退避不能抹掉写盘窗口的这次松手。 */
static volatile bool s_quiet_release;

static uint32_t s_win_pauses;      /* 写盘窗口真的让路了：几个 LVGL 周期（验收正面证据） */
static uint32_t s_backoff_pauses;  /* 慢读退避跳过：几个 LVGL 周期 */
static uint32_t s_slow_run;        /* 连续慢读次数（只在 touchpad_read 单线程里读写） */

static inline int32_t touch_now_ms(void)
{
    return (int32_t)(esp_timer_get_time() / 1000);
}

/* 把静默截止时刻往后推到 `until`（**只延不缩**：后到的、更早的截止不许把前面改早）。 */
static void touch_quiet_push(int32_t until, bool release)
{
    if (release) {
        s_quiet_release = true;
    }
    if ((until - s_quiet_until_ms) > 0) {
        s_quiet_until_ms = until;
    }
}

/* 截止时刻是否还没到（`until - now > 0`；int32 差值比较 ⇒ 回绕安全）。 */
static bool touch_quiet_busy(void)
{
    return (s_quiet_until_ms - touch_now_ms()) > 0;
}

void sdgoods_touch_flash_window(int ms)
{
    if (ms <= 0) {
        ms = 1;
    }
    if (ms > SDGOODS_TOUCH_FLASH_WIN_MAX_MS) {
        ms = SDGOODS_TOUCH_FLASH_WIN_MAX_MS;
    }
    touch_quiet_push(touch_now_ms() + ms, true);
}

void sdgoods_touch_flash_window_end(void)
{
    /* 不立刻清零：fwrite/rename 返回之后，flash 关 cache 的"回程"（含 FATFS 的
     * 目录项更新）还有几十毫秒。留 GRACE 再放开，比精确卡着更稳。 */
    touch_quiet_push(touch_now_ms() + SDGOODS_TOUCH_FLASH_GRACE_MS, false);
}

static lv_indev_drv_t s_indev_drv;
static lv_indev_t     *s_indev = NULL;   /* 注册后保存指针 indev 句柄（备用） */

/* 直接缓存最近一次触摸读取的状态/坐标，供游戏在主循环里轮询，
 * 完全绕开 LVGL 事件派发，且本工程 LVGL 8.3.11 未导出 lv_indev_get_state。 */
static lv_indev_state_t s_touch_state = LV_INDEV_STATE_RELEASED;
static lv_point_t       s_touch_point = {0, 0};

static esp_err_t cst816_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device((i2c_port_t)BOARD_I2C_PORT, CST816_ADDR,
                                        &reg, 1, data, len, pdMS_TO_TICKS(CST816_RD_TIMEOUT_MS));
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

    /* 🔴🔴 静默判断必须在**第一次 I2C 事务之前** —— 这是整个防护的落点。
     * 放后面（读完了再判）= 事务已经发出去了，写盘期间照样会撞、照样 panic。 */
    if (touch_quiet_busy()) {
        if (s_quiet_release) {
            /* 写盘窗口的第一次：正按着也补一次松手，否则 UI 卡在按下态。 */
            s_quiet_release = false;
            data->state = LV_INDEV_STATE_RELEASED;
            s_touch_state = LV_INDEV_STATE_RELEASED;
            s_win_pauses++;
            /* 必须出声：这是真机验收时唯一的正面证据（`--e2e` 日志里搜这句）。
             * 只在第 1 次和每 20 次打，避免写盘期间刷屏把串口拖慢。 */
            if (s_win_pauses == 1 || (s_win_pauses % 20) == 0) {
                ESP_LOGI(TAG, "flash write window -> touch paused (x%u)",
                         (unsigned)s_win_pauses);
            }
            return;
        }
        /* 慢读退避：**沿用上次状态**，一个事务都不发。不能在这里补 RELEASED ——
         * 那会把"正按着"误判成"松手"，长按 / 游戏按住会被切断。 */
        data->state = s_touch_state;
        data->point = s_touch_point;
        s_backoff_pauses++;
        return;
    }

    const int64_t t0 = esp_timer_get_time();
    uint8_t buf[5] = {0};
    const esp_err_t rd = cst816_read(CST816_REG_DATA, buf, sizeof(buf));
    const int32_t cost_us = (int32_t)(esp_timer_get_time() - t0);

    /* 慢读自适应退避：连续慢就自己把后面的几帧让开（对没声明写盘窗口的路径兜底）。 */
    if (cost_us >= TOUCH_SLOW_US) {
        if (s_slow_run < 1000) {
            s_slow_run++;
        }
        int32_t backoff = (int32_t)s_slow_run * TOUCH_SLOW_STEP_MS;
        if (backoff > TOUCH_SLOW_MAX_MS) {
            backoff = TOUCH_SLOW_MAX_MS;
        }
        touch_quiet_push(touch_now_ms() + backoff, false);
        ESP_LOGW(TAG, "slow read: %d us (run=%u, backoff=%d ms%s)", (int)cost_us,
                 (unsigned)s_slow_run, (int)backoff,
                 rd == ESP_OK ? "" : " -> ESP_ERR");
    } else {
        s_slow_run = 0;   /* 一次正常立刻清零，否则退避会自己锁死在慢档 */
    }

    if (rd != ESP_OK) {
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
    /* 🔴🔴 2026-10-05：把**滚动阈值**抬到和点按判据同一个数（LVGL 默认只有 10）。
     *
     * 病根（用户报「点击列表 / 底部那一排的按钮时，经常点了没反应」）：LVGL 在按下期间按
     *   `scroll_sum += vect` 累计指针位移，一旦 `|scroll_sum| >= driver->scroll_limit`
     *   就选出一个 `scroll_obj`（lv_indev_scroll.c:278 / :340-352）。而松手时
     *   `indev_proc_release()` 只在 `scroll_obj == NULL` 的分支里发 **LV_EVENT_CLICKED**
     *   （lv_indev.c:974-982）⇒ 只要这一摸被判成"滚动"，**CLICKED 整个不派发**。
     *
     * 谁会被它吃掉：所有**裸挂 LV_EVENT_CLICKED** 的控件，尤其是「放在可滚动容器里」的那些
     *   （横向/纵向列表、底部整排按钮所在的可滚行）。手指点按时只要有一点抖动（含触摸起手
     *   那一两帧的坐标跳变），累计 ≥10px 就把 scroll_obj 抢过去 ⇒ 动作永远不触发，
     *   而且**没有任何日志** —— 正是"有时候点了没反应"。
     *
     * 为什么取 SDGOODS_TAP_SLOP（24）而不是别的数：
     *   平台层判「这一次到底是点按还是滑动」用的就是这 24px（主页应用格、CC 按钮、
     *   列表行的位移守卫全是它）。两者不等时会出现**自相矛盾的区间**：位移 10..24px
     *   既"够滚动"（CLICKED 被吞）又"不够滑动"（守卫认为该当点按处理）⇒ 这一段位移里的
     *   所有点按都凭空消失。**让两者相等，这个矛盾区间就不存在了。**
     *   ⚠️ 改这里必须连着 sdgoods_tap.h 的 SDGOODS_TAP_SLOP 一起看，别只改一个。
     *   ⚠️ 副作用只有一条：横滑翻页要滑满 24px 才开始跟手（原来 10px）。这是**想要的**
     *      ——10px 太过敏，正常点按的抖动就足以把翻页抢走；24px 仍在"手指一动就翻"的
     *     手感范围内，而且滑动本身有吸附，最终落位一样。
     *   ⚠️ LVGL 8.3 **没有** `lv_indev_set_scroll_limit()`，只能像这样在注册前写进
     *      `s_indev_drv`：`lv_indev_drv_register()` 只存指针、不覆盖这个字段，
     *      而 `find_scroll_obj()` 每帧运行时读 `indev_act->driver->scroll_limit`。 */
    s_indev_drv.scroll_limit = SDGOODS_TAP_SLOP;
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
