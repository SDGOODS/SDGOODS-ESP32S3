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

#include "sdgoods_app_shell.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "sdgoods_i18n.h"    /* SDG_T：界面文案中英切换 */
#include "sdgoods_lcd.h"        /* LCD_WIDTH / LCD_HEIGHT */
#include "sdgoods_ui.h"     /* SDG_UI_BTN_SIZE 等布局常量，菜单按钮复用主页风格 */
#include "sdgoods_hooks.h"  /* 回主页 / 回应用页：交给应用层注册的实现 */
#include "sdgoods_audio.h"  /* sdgoods_audio_set_volume / sdgoods_audio_get_volume */
#include "sdgoods_screenshot.h"     /* sdgoods_screenshot_init：串口 's' 触发截屏（菜单按钮已移除） */
#include "sdgoods_console.h"        /* sdgoods_console_init：BSP 串口控制台（'?' 能力查询，始终存在） */
#include "sdgoods_tap.h"            /* 设备级手势策略：随外壳自动安装，app 无需自己接 */
#include "sdgoods_nvs.h"           /* sdgoods_nvs_ensure：平台自己保证 NVS 可用 */
#include "sdgoods_hw_info.h"       /* sdgoods_hw_info_init：电池电压 ADC（控制中心电量页要用） */
#include "esp_log.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(cn_font_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(cn_font_16);

/* 顶部下滑手势区高度 / 底部上滑手势区高度 / 滑动判定阈值（像素） */
#define TOP_ZONE      50
#define BOTTOM_ZONE   50
#define SWIPE_DY      50

static lv_obj_t *s_app_scr   = NULL;   /* 当前应用屏（菜单浮层挂在其上） */
static lv_obj_t *s_menu      = NULL;   /* 菜单浮层对象 */
static bool      s_menu_open = false;
static int       s_vol_pct   = 0;      /* 菜单内音量显示用的镜像；真值以 sdgoods_audio 为准，
                                        * 由 sdgoods_app_shell_init() 在启动时从 NVS 对齐 */
static lv_obj_t *s_vol_label = NULL;   /* 菜单内音量显示 */
static void    (*s_exit_cb)(void) = NULL;  /* 当前应用的清理函数(仅释放资源) */
static void    (*s_pause_cb)(void) = NULL; /* 菜单打开时暂停应用 */
static void    (*s_resume_cb)(void) = NULL;/* 菜单关闭时恢复应用 */
static lv_coord_t s_top_py;            /* 顶部/底部手势按下时的 y，供 RELEASED 计算滑动 */
static lv_coord_t s_bot_py;
static lv_obj_t  *s_bound_scr = NULL;  /* 已经绑过顶部手势的屏（防重复叠加捕获层） */
static lv_timer_t *s_autobind_timer = NULL;  /* 自动给「后来才出现的屏」套用外壳手势 */

/* ----------------------------------------------------------------------------
 * 共享音量
 *
 * ⚠️ 这里的音量**必须与真实音量（sdgoods_audio）和控制中心（NVS）三方一致**，
 *    否则用户会看到「在 A 里调的音量，进 B 又变回去了」。三条约束：
 *      ① 增减以 `sdgoods_audio_get_volume()`（真实值）为基准，**不用** s_vol_pct 镜像
 *         —— 用户在控制中心拖过滑块之后，那个镜像就过期了；
 *      ② 改完立刻落盘 NVS（sdgoods_cc_flush），否则菜单里调的音量跨不了重启；
 *      ③ 启动时由 sdgoods_app_shell_init() 从 NVS 恢复（见那里的说明）。
 * ------------------------------------------------------------------------- */
static void vol_apply(int pct)
{
    if (pct > 100) pct = 100;
    if (pct < 0)   pct = 0;
    s_vol_pct = pct;
    sdgoods_audio_set_volume(s_vol_pct);
    if (s_vol_label) {
        char b[16];
        snprintf(b, sizeof(b), SDG_T("音量: %d%%", "Vol: %d%%"), s_vol_pct);
        lv_label_set_text(s_vol_label, b);
    }
    /* 立即落盘：菜单按钮是离散点按（不是滑块拖动），不需要防抖；
     * 用户下一次很可能就是「返回启动器 → 进另一个 app」，不写就丢了。
     * 弱符号：控制中心组件未链接时跳过（那种固件本来也没有跨 app 的场景）。 */
    extern void sdgoods_cc_flush(void) __attribute__((weak));
    if (sdgoods_cc_flush) {
        sdgoods_cc_flush();
    }
}

void sdgoods_app_volume_up(void)
{
    vol_apply(sdgoods_audio_get_volume() + 10);
}

void sdgoods_app_volume_down(void)
{
    vol_apply(sdgoods_audio_get_volume() - 10);
}

int sdgoods_app_volume_get(void)
{
    /* 返回**真实**音量而不是 s_vol_pct 镜像：日志/菜单显示都该以真实值为准，
     * 否则控制中心改过之后这里会报一个过期数字。 */
    return sdgoods_audio_get_volume();
}

/* 自动给当前屏套用外壳手势（顶部下滑 → 控制中心）。
 * 目的：让「每个 app 都有控制中心」成为**默认行为**，而不是依赖 app 记得调
 * sdgoods_app_shell_bind()。app 若自己调过 bind，s_bound_scr 已置位，这里会跳过 ⇒ 不重复。
 * 覆盖「后来才创建 / 加载的其他屏」：只比一次指针，换屏时才真正绑一次。 */
static void shell_autobind_scan(lv_timer_t *t)
{
    (void)t;
    lv_obj_t *cur = lv_scr_act();
    if (!cur || cur == s_bound_scr) {
        return;
    }
    sdgoods_app_shell_bind(cur);
}

void sdgoods_app_shell_init(void)
{
    /* ★ 先保障 NVS 可用：平台层有多个模块要写 NVS（控制中心音量/亮度、跳过开机动画
     *   标志、语言…），而最小 app 可能从不 nvs_flash_init ⇒ 写入静默失败。
     *   做成平台自己的事，app 完全不必知道 NVS 的存在。幂等，开销一次函数调用。 */
    sdgoods_nvs_ensure();

    /* ★★ 音量 / 亮度的**跨 app 同步**（2026-09-19 修）★★
     *
     * 为什么必须在这里做：本设备每次「进 app / 回启动器」都是**重启**
     * （`esp_ota_set_boot_partition()` + `esp_restart()`，见 slot_manifest.c），
     * RAM 里的任何状态都不会被带到下一个固件 —— 能跨固件传递状态的**只有 NVS**。
     * 所以音量/亮度必须**每次启动都从 NVS 恢复**，否则用户会看到
     * 「在 A 里调好 → 进 B 又变回去了」。写侧在控制中心（改一下防抖 800ms 落盘，
     * 见 sdgoods_cc.c 的 save_timer；关闭 CC / 进 app / 关机前另有 flush）。
     *
     * ⚠️ 旧实现的 bug（两个叠加，用户可见症状 = 「音量亮度在 app 之间不同步」）：
     *   ① 这里直接 `sdgoods_audio_set_volume(s_vol_pct)`，而 `s_vol_pct` 硬编初值 0
     *      ⇒ **每个 app 一启动就被压成静音**，日志 `init: default volume=0%`；
     *   ② 恢复函数 sdgoods_cc_settings_restore() 只在**启动器**里被调过
     *      （main/ui_launcher.c，创建主页时），app 侧从来没人调
     *      ⇒ 亮度在 app 里根本没有恢复点，一直是面板复位后的默认值。
     *
     * 弱符号引用（同 sdgoods_cc_open 的做法）：控制中心组件未链接时符号为 NULL，
     * 跳过即可，不引入 board → launcher 的硬依赖。 */
    extern void sdgoods_cc_settings_restore(void) __attribute__((weak));
    if (sdgoods_cc_settings_restore) {
        sdgoods_cc_settings_restore();      /* NVS 有记录则同时恢复音量与亮度 */
    }

    /* 把外壳的音量镜像与**真实**音量对齐（无论上面恢复成功与否）：
     * 恢复成功时取 NVS 里的值；无记录时取音频模块自己的默认值（sdgoods_audio.c 里是 70）。
     * 这样菜单里的「音量 +/-」是从当前值继续加减，不会像旧版那样从 0 往上跳。 */
    s_vol_pct = sdgoods_audio_get_volume();
    sdgoods_audio_set_volume(s_vol_pct);
    ESP_LOGI("app_shell", "init: volume=%d%% (NVS-restored; cross-app synced)", s_vol_pct);

    /* ★ 电池读数（电压 / 百分比）同样是平台自己的事：
     *   控制中心的「电量页」是**平台层共享代码**，任何 app 都能顶部下滑打开它，
     *   而 `sdgoods_hw_bat_v()` 在 ADC 未初始化时**直接返回 0.f** ⇒ 屏上
     *   `Voltage --V` / `Level --%`。
     *   旧版只有部分固件自己调 sdgoods_hw_info_init()（启动器、app0 调了；HELLO 没调）
     *   ⇒ 同一个电量页在不同 app 里时好时坏 —— 典型症状「app 里看不到电量和电压」。
     *   sdgoods_hw_info_init() 已做成幂等 + 不致命（见 sdgoods_hw_info.c），
     *   重复调用安全，失败也只是电量页显示 `--` 而不会 abort。 */
    sdgoods_hw_info_init();

    sdgoods_console_init();      /* BSP 串口控制台（'?' 能力查询等，始终存在） */
#ifdef CONFIG_SDGOODS_SCREENSHOT
    sdgoods_screenshot_init();   /* 截屏能力：串口 's' 触发（须在 LVGL 线程内初始化） */
#endif
    /* ★ 设备级手势策略随应用外壳一起安装：app 只要按 SDK 文档接了应用外壳，
     *   就自动获得与启动器一致的手势行为（PRESS_LOCK 所有权 + 点按位移守卫 +
     *   装饰物穿透），无需自己知道这些细节。详见 sdgoods_tap.h。 */
    sdgoods_tap_install();

    /* ★ 控制中心也做成默认：立刻给当前屏套上「顶部下滑唤出」，并起看门狗覆盖
     *   之后才加载的屏。这样 app 即使漏调 sdgoods_app_shell_bind() 也有控制中心。 */
    if (!s_autobind_timer) {
        s_autobind_timer = lv_timer_create(shell_autobind_scan, 100, NULL);
    }
    sdgoods_app_shell_bind(lv_scr_act());
    ESP_LOGI("app_shell", "control center armed on screen %p", (void *)lv_scr_act());
}

bool sdgoods_app_shell_is_app_active(void)
{
    return (s_app_scr != NULL);
}

/* ----------------------------------------------------------------------------
 * 圆角按钮（样式同 home：深灰圆底 + 白字）
 * ------------------------------------------------------------------------- */
static lv_obj_t *make_round_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, SDG_UI_BTN_SIZE, SDG_UI_BTN_SIZE);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, SDG_UI_BTN_SIZE / 2, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    if (text && text[0]) {
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &cn_font_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_center(lbl);
    }
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    return btn;
}

/* ----------------------------------------------------------------------------
 * 菜单
 * ------------------------------------------------------------------------- */
static void on_vol_plus(lv_event_t *e)  { (void)e; sdgoods_app_volume_up(); }
static void on_vol_minus(lv_event_t *e) { (void)e; sdgoods_app_volume_down(); }
static void on_menu_exit(lv_event_t *e)
{
    (void)e;
    sdgoods_app_shell_leave(false);   /* 退出应用 -> 回「应用页」启动台 */
}

/* 截屏按钮已从菜单中移除（保留串口 's' 触发，见 screenshot.c）。
 * 需要时在电脑端运行 tools/screenshot_recv.py -t 即可抓图。 */

static void on_bot_pressed(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_bot_py = p.y;
}

static void on_bot_released(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dy = (int)p.y - (int)s_bot_py;
    if (s_bot_py >= (LCD_HEIGHT - BOTTOM_ZONE) && dy <= -SWIPE_DY) {
        sdgoods_app_shell_menu_close();   /* 底部上滑 -> 关闭菜单 */
    }
}

void sdgoods_app_shell_menu_open(void)
{
    if (s_menu_open || !s_app_scr) {
        return;
    }
    s_menu_open = true;
    if (s_pause_cb) {
        s_pause_cb();
    }

    s_menu = lv_obj_create(s_app_scr);
    lv_obj_remove_style_all(s_menu);
    lv_obj_set_size(s_menu, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_pos(s_menu, 0, 0);
    lv_obj_set_style_bg_color(s_menu, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_90, 0);   /* 透明度减小：更不透明压暗，露出应用更少 */
    lv_obj_clear_flag(s_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_menu);

    lv_obj_t *title = lv_label_create(s_menu);
    lv_label_set_text(title, SDG_T("菜单", "Menu"));
    lv_obj_set_style_text_font(title, &cn_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

    s_vol_label = lv_label_create(s_menu);
    {
        /* 开菜单时把镜像与**真实**音量对齐再显示：app 运行期间用户可能用控制中心
         * 调过音量（滑过滑块），此时 s_vol_pct 是过期的 —— 直接显示它会给出错的数字。 */
        s_vol_pct = sdgoods_audio_get_volume();
        char b[16];
        snprintf(b, sizeof(b), SDG_T("音量: %d%%", "Vol: %d%%"), s_vol_pct);
        lv_label_set_text(s_vol_label, b);
    }
    lv_obj_set_style_text_font(s_vol_label, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_vol_label, lv_color_white(), 0);
    lv_obj_align(s_vol_label, LV_ALIGN_TOP_MID, 0, 82);

    /* 三个圆按钮，复用 home 第一行布局（y=142 居中）：
       音量+ / 音量- / 退出。第二行留空（原来的「截屏」按钮已移除）。 */
    make_round_btn(s_menu, SDG_UI_BTN1_X, 142, SDG_T("音量+", "Vol+"), on_vol_plus);
    make_round_btn(s_menu, SDG_UI_BTN2_X, 142, SDG_T("音量-", "Vol-"), on_vol_minus);
    make_round_btn(s_menu, SDG_UI_BTN3_X, 142, SDG_T("退出", "Exit"), on_menu_exit);

    /* 底部上滑手势捕获层（菜单内最上层） */
    lv_obj_t *bot = lv_obj_create(s_menu);
    lv_obj_remove_style_all(bot);
    lv_obj_set_size(bot, LCD_WIDTH, BOTTOM_ZONE);
    lv_obj_set_pos(bot, 0, LCD_HEIGHT - BOTTOM_ZONE);
    lv_obj_set_style_bg_opa(bot, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(bot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(bot);
    lv_obj_add_event_cb(bot, on_bot_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(bot, on_bot_released, LV_EVENT_RELEASED, NULL);
}

void sdgoods_app_shell_menu_close(void)
{
    if (!s_menu_open) {
        return;
    }
    s_menu_open = false;
    if (s_resume_cb) {
        s_resume_cb();
    }
    if (s_menu) {
        lv_obj_del(s_menu);
        s_menu = NULL;
    }
    s_vol_label = NULL;
}

bool sdgoods_app_shell_menu_is_open(void)
{
    return s_menu_open;
}

void sdgoods_app_shell_set_exit_cb(void (*cb)(void))
{
    s_exit_cb = cb;
}

void sdgoods_app_shell_set_pause_cb(void (*cb)(void))
{
    s_pause_cb = cb;
}

void sdgoods_app_shell_set_resume_cb(void (*cb)(void))
{
    s_resume_cb = cb;
}

void sdgoods_app_shell_leave(bool to_home)
{
    if (s_menu_open) {
        sdgoods_app_shell_menu_close();
    }
    s_pause_cb = NULL;
    s_resume_cb = NULL;

    /* 1) 先让 BGM 渐出停止（约 64ms），此时游戏画面仍在屏上，听不到关功放的“啪”声 */
    sdgoods_audio_bgm_stop();

    /* 2) 先加载目标屏，避免「删除当前活动屏」的瞬间没有活动屏导致闪黑/闪白。
          目标屏由应用层注册（sdgoods_hooks），平台层不需要知道主页/应用页的存在。 */
    s_app_scr = NULL;
    if (to_home) {
        sdgoods_ui_home_show();
    } else {
        sdgoods_ui_apps_show();
    }

    /* 3) 最后再让应用释放资源（停定时器/删旧屏/释放缓冲） */
    if (s_exit_cb) {
        void (*cb)(void) = s_exit_cb;
        s_exit_cb = NULL;
        cb();
    }
}

/* ----------------------------------------------------------------------------
 * 顶部下滑手势捕获层
 * ------------------------------------------------------------------------- */
static void on_top_pressed(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_top_py = p.y;
}

static void on_top_released(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dy = (int)p.y - (int)s_top_py;
    if (s_top_py <= TOP_ZONE && dy >= SWIPE_DY) {
        /* 顶部下滑 → 打开**设备级控制中心**（音量 / 亮度 / 数据 / 电量 / Power|Exit）。
         * 原来的简易菜单已被控制中心取代（功能是其超集）；sdgoods_app_shell_menu_*
         * 系列 API 仍保留可用，只是默认不再由手势触发。
         * 控制中心实现在平台层 sdgoods_launcher，BSP 通过弱符号调用（见本文件末尾）。 */
        sdgoods_cc_open();
    }
}

void sdgoods_app_shell_bind(lv_obj_t *scr)
{
    if (!scr) {
        return;
    }
    /* 幂等：同一屏重复调用不再叠加捕获层（下面的自动套用看门狗会反复尝试绑定）。 */
    if (scr == s_bound_scr) {
        return;
    }
    s_bound_scr = scr;
    s_app_scr = scr;

    lv_obj_t *cat = lv_obj_create(scr);
    lv_obj_remove_style_all(cat);
    lv_obj_set_size(cat, LCD_WIDTH, TOP_ZONE);
    lv_obj_set_pos(cat, 0, 0);
    lv_obj_set_style_bg_opa(cat, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(cat, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cat, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(cat);   /* 置于游戏全屏 tap 之上，捕获顶部下滑 */
    lv_obj_add_event_cb(cat, on_top_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(cat, on_top_released, LV_EVENT_RELEASED, NULL);

    /* ★ 设备级手势策略：app 只要接了外壳就自动具备，无需自己知道细节。
     *   ① 屏是 lv_obj_create(NULL) 建的 ⇒ **无父对象 ⇒ LVGL 不会给它 PRESS_LOCK**
     *      （lv_obj.c:438 只对「有父对象」的加），这正是「按下落在屏上、滑动掠过
     *      按钮/图标后被接管成点按」的根源，必须先补上。
     *   ② 再对整棵树落实一次：装饰物（canvas）穿透 + 交互对象锁定所有权。
     *   两者共同保证「按在哪就归谁、滑走不算点按」。详见 sdgoods_tap.h。 */
    sdgoods_tap_lock(scr);
    sdgoods_tap_normalize(scr);
}

/* ---- 设备级控制中心：BSP 侧的弱默认实现 ------------------------------------
 * 真正的控制中心在平台层 components/sdgoods_launcher/src/sdgoods_cc.c。
 * 这里给一个**弱符号**空实现，让「不含 sdgoods_launcher 的精简工程」也能编过；
 * 只要工程带了该组件（app 都要带，返回启动器 / appdata 就在里面），链接器会选它的强符号。
 * 做法与本工程 `sdgoods_console_ext_cmd`（BSP 弱默认、由工程覆盖）一致。 */
__attribute__((weak)) void sdgoods_cc_open(void)
{
    ESP_LOGW("app_shell", "sdgoods_cc_open(): control center not linked in this firmware");
}

/* 控制中心打开 / 关闭时回调这里：把「系统浮层开着」映射成 app 的暂停 / 恢复，
 * 使游戏类 app 在控制中心盖住画面时停止推进（与旧菜单的 pause/resume 语义一致）。 */
void sdgoods_app_shell_notify_overlay(bool open)
{
    if (open) {
        if (s_pause_cb) {
            s_pause_cb();
        }
    } else {
        if (s_resume_cb) {
            s_resume_cb();
        }
    }
}
