#include "ui_app_shell.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "st77916.h"        /* LCD_WIDTH / LCD_HEIGHT */
#include "ui_home.h"        /* UI_HOME_BTN_SIZE 等布局常量，菜单按钮复用 home 风格 */
#include "ui_app_page.h"    /* ui_app_page_show */
#include "ui_home.h"        /* ui_home_show */
#include "audio_recplay.h"  /* audio_set_volume / audio_get_volume */
#include "screenshot.h"     /* screenshot_capture_async：菜单「截屏」按钮 */
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
static int       s_vol_pct   = 0;      /* 共享音量默认 0% */
static lv_obj_t *s_vol_label = NULL;   /* 菜单内音量显示 */
static void    (*s_exit_cb)(void) = NULL;  /* 当前应用的清理函数(仅释放资源) */
static void    (*s_pause_cb)(void) = NULL; /* 菜单打开时暂停应用 */
static void    (*s_resume_cb)(void) = NULL;/* 菜单关闭时恢复应用 */
static lv_coord_t s_top_py;            /* 顶部/底部手势按下时的 y，供 RELEASED 计算滑动 */
static lv_coord_t s_bot_py;

/* ----------------------------------------------------------------------------
 * 共享音量
 * ------------------------------------------------------------------------- */
void ui_app_volume_up(void)
{
    s_vol_pct += 10;
    if (s_vol_pct > 100) s_vol_pct = 100;
    audio_set_volume(s_vol_pct);
    if (s_vol_label) {
        char b[16];
        snprintf(b, sizeof(b), "音量: %d%%", s_vol_pct);
        lv_label_set_text(s_vol_label, b);
    }
}

void ui_app_volume_down(void)
{
    s_vol_pct -= 10;
    if (s_vol_pct < 0) s_vol_pct = 0;
    audio_set_volume(s_vol_pct);
    if (s_vol_label) {
        char b[16];
        snprintf(b, sizeof(b), "音量: %d%%", s_vol_pct);
        lv_label_set_text(s_vol_label, b);
    }
}

int ui_app_volume_get(void)
{
    return s_vol_pct;
}

void ui_app_shell_init(void)
{
    audio_set_volume(s_vol_pct);
    ESP_LOGI("app_shell", "init: default volume=%d%%", s_vol_pct);
    screenshot_init();   /* 截屏能力：菜单按钮 + 串口 's' 触发（须在 LVGL 线程内初始化） */
}

bool ui_app_shell_is_app_active(void)
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
    lv_obj_set_size(btn, UI_HOME_BTN_SIZE, UI_HOME_BTN_SIZE);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, UI_HOME_BTN_SIZE / 2, 0);
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
static void on_vol_plus(lv_event_t *e)  { (void)e; ui_app_volume_up(); }
static void on_vol_minus(lv_event_t *e) { (void)e; ui_app_volume_down(); }
static void on_menu_exit(lv_event_t *e)
{
    (void)e;
    ui_app_shell_leave(false);   /* 退出应用 -> 回「应用页」启动台 */
}

/* 截屏：先关掉菜单浮层（截出的画面里不要带菜单），
 * 随后 screenshot 模块会把当前屏幕渲染成位图并经 USB 串口输出，
 * 电脑端运行 screenshot_recv.py 即可自动存成 PNG。 */
static void on_menu_shot(lv_event_t *e)
{
    (void)e;
    ui_app_shell_menu_close();
    screenshot_capture_async();
}

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
        ui_app_shell_menu_close();   /* 底部上滑 -> 关闭菜单 */
    }
}

void ui_app_shell_menu_open(void)
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
    lv_label_set_text(title, "菜单");
    lv_obj_set_style_text_font(title, &cn_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

    s_vol_label = lv_label_create(s_menu);
    {
        char b[16];
        snprintf(b, sizeof(b), "音量: %d%%", s_vol_pct);
        lv_label_set_text(s_vol_label, b);
    }
    lv_obj_set_style_text_font(s_vol_label, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_vol_label, lv_color_white(), 0);
    lv_obj_align(s_vol_label, LV_ALIGN_TOP_MID, 0, 82);

    /* 三个圆按钮，复用 home 第一行布局（y=142 居中） */
    make_round_btn(s_menu, UI_HOME_BTN1_X, 142, "音量+", on_vol_plus);
    make_round_btn(s_menu, UI_HOME_BTN2_X, 142, "音量-", on_vol_minus);
    make_round_btn(s_menu, UI_HOME_BTN3_X, 142, "退出",  on_menu_exit);

    /* 第二行：截屏（居中）。点它 = 把当前屏幕截图并通过 USB 串口传给电脑 */
    make_round_btn(s_menu, (LCD_WIDTH - UI_HOME_BTN_SIZE) / 2, 224, "截屏", on_menu_shot);

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

void ui_app_shell_menu_close(void)
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

bool ui_app_shell_menu_is_open(void)
{
    return s_menu_open;
}

void ui_app_shell_set_exit_cb(void (*cb)(void))
{
    s_exit_cb = cb;
}

void ui_app_shell_set_pause_cb(void (*cb)(void))
{
    s_pause_cb = cb;
}

void ui_app_shell_set_resume_cb(void (*cb)(void))
{
    s_resume_cb = cb;
}

void ui_app_shell_leave(bool to_home)
{
    if (s_menu_open) {
        ui_app_shell_menu_close();
    }
    s_pause_cb = NULL;
    s_resume_cb = NULL;

    /* 1) 先让 BGM 渐出停止（约 64ms），此时游戏画面仍在屏上，听不到关功放的“啪”声 */
    audio_bgm_stop();

    /* 2) 先加载目标屏，避免「删除当前活动屏」的瞬间没有活动屏导致闪黑/闪白 */
    s_app_scr = NULL;
    if (to_home) {
        ui_home_show();
    } else {
        ui_app_page_show();
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
        ui_app_shell_menu_open();   /* 顶部下滑 -> 弹出菜单 */
    }
}

void ui_app_shell_bind(lv_obj_t *scr)
{
    if (!scr) {
        return;
    }
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
}
