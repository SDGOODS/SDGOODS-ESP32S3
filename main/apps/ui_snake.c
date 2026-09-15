/*
 * SDGOODS 开放平台基础工程 · 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Required Notice: Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 *
 * 个人学习、研究与业余项目免费使用；商业用途需事前书面授权，见 LICENSING.md。
 * 分发时必须完整保留本声明 —— 这是许可条款，不是建议。
 */

#include "ui_snake.h"

#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "st77916.h"
#include "ui_app_page.h"
#include "audio_recplay.h"
#include "ui_app_shell.h"
#include "touch_input.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(cn_font_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(cn_font_16);

#define FIELD_W   LCD_WIDTH
#define FIELD_H   LCD_HEIGHT

/* 棋盘：18 × 18 格（圆屏内可见区域受限，正方形更直观） */
#define GRID      18
#define CELL      16                       /* 每格像素边长 */
#define PF_W      (GRID * CELL)            /* 288 */
#define PF_H      (GRID * CELL)            /* 288 */
#define PF_X      ((LCD_WIDTH  - PF_W) / 2)/* 36 */
#define PF_Y      ((LCD_HEIGHT - PF_H) / 2)/* 36 */

#define SNAKE_MAX 250

/* 一次滑动的最小净位移(px)：位移超过此值即判定方向（取主轴） */
#define SWIPE_DIR_PX 10

/* 像素风调色板（与小鸟/飞机/俄罗斯方块一致） */
#define BG_COLOR     lv_color_hex(0x0B1026)   /* 棋盘背景（星空蓝） */
#define GRID_COLOR   lv_color_hex(0x1A2547)   /* 网格线 */
#define SNAKE_HEAD   lv_color_hex(0x00E436)   /* 蛇头（亮绿） */
#define SNAKE_BODY   lv_color_hex(0x008F11)   /* 蛇身（深绿） */
#define FOOD_COLOR   lv_color_hex(0xFF004D)   /* 食物（红） */

typedef enum {
    ST_READY,
    ST_PLAY,
    ST_OVER
} game_state_t;

typedef enum {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} dir_t;

static game_state_t s_state;
static int          s_score;
static int          s_snake_len;
static int          s_sx[SNAKE_MAX];       /* 蛇身格子坐标，[0]=头 */
static int          s_sy[SNAKE_MAX];
static dir_t        s_dir;
static dir_t        s_next_dir;
static int          s_food_x, s_food_y;
static int          s_step_acc;            /* 步进累计(ms) */
static int          s_step_interval;       /* 当前步进间隔(ms) */
static bool         s_paused;
static uint32_t     s_last_ms;             /* 上次 poll 时的时间戳(ms) */
static bool         s_touch_prev;          /* 上一拍触摸是否按下（用于检测“新按下”） */
static int          s_press_x;             /* 本次滑动起点（按下瞬间的坐标） */
static int          s_press_y;
static int          s_last_press_x;        /* 按压期间最后一个有效坐标（抬手瞬间取用） */
static int          s_last_press_y;
static bool         s_swipe_latched;       /* 本次按压内是否已换过向（一次滑动只换一次） */
static bool         s_dirty;               /* 画面是否需要重绘（避免每轮全画布重绘拖慢主循环） */

static lv_obj_t    *s_scr = NULL;
static lv_obj_t    *s_cv = NULL;
static uint8_t     *s_cv_buf = NULL;
static lv_obj_t    *s_score_lbl = NULL;
static lv_obj_t    *s_msg = NULL;
static lv_obj_t    *s_over_hint = NULL;
static lv_timer_t  *s_timer = NULL;
static bool         s_key_down;

static void start_game(void);
static void close_to_app(void);
static void game_over(void);
static void spawn_food(void);
static void step(void);

/* 在画布上画一个格子（内部填充，留 2px 背景间隙形成像素方块观感） */
static void draw_cell(int gx, int gy, lv_color_t c)
{
    const int x0 = gx * CELL;
    const int y0 = gy * CELL;
    const int B = 2;
    for (int yy = B; yy < CELL - B; yy++) {
        for (int xx = B; xx < CELL - B; xx++) {
            lv_canvas_set_px_color(s_cv, x0 + xx, y0 + yy, c);
            lv_canvas_set_px_opa(s_cv, x0 + xx, y0 + yy, LV_OPA_COVER);
        }
    }
}

static void redraw(void)
{
    if (!s_cv) {
        return;
    }
    lv_canvas_fill_bg(s_cv, BG_COLOR, LV_OPA_COVER);

    /* 轻网格线 */
    for (int i = 0; i <= GRID; i++) {
        for (int k = 0; k < 2; k++) {
            if (i < GRID) {
                for (int y = 0; y < PF_H; y++) {
                    lv_canvas_set_px_color(s_cv, i * CELL + k, y, GRID_COLOR);
                    lv_canvas_set_px_opa(s_cv, i * CELL + k, y, LV_OPA_COVER);
                }
            }
            if (i < GRID) {
                for (int x = 0; x < PF_W; x++) {
                    lv_canvas_set_px_color(s_cv, x, i * CELL + k, GRID_COLOR);
                    lv_canvas_set_px_opa(s_cv, x, i * CELL + k, LV_OPA_COVER);
                }
            }
        }
    }

    /* 食物 */
    draw_cell(s_food_x, s_food_y, FOOD_COLOR);

    /* 蛇 */
    for (int i = 0; i < s_snake_len; i++) {
        draw_cell(s_sx[i], s_sy[i], (i == 0) ? SNAKE_HEAD : SNAKE_BODY);
    }
}

static int dir_dx(dir_t d)
{
    switch (d) {
    case DIR_UP:    return 0;
    case DIR_DOWN:  return 0;
    case DIR_LEFT:  return -1;
    case DIR_RIGHT: return 1;
    }
    return 0;
}

static int dir_dy(dir_t d)
{
    switch (d) {
    case DIR_UP:    return -1;
    case DIR_DOWN:  return 1;
    case DIR_LEFT:  return 0;
    case DIR_RIGHT: return 0;
    }
    return 0;
}

/* 设方向：禁止 180° 反向（防止撞自己） */
static void set_dir(dir_t d)
{
    if (d == DIR_UP    && s_dir == DIR_DOWN)  return;
    if (d == DIR_DOWN  && s_dir == DIR_UP)    return;
    if (d == DIR_LEFT  && s_dir == DIR_RIGHT) return;
    if (d == DIR_RIGHT && s_dir == DIR_LEFT)  return;
    s_next_dir = d;
}

/* 由滑动主方向(dx/dy)挑出四向之一 */
static dir_t pick_dir(int dx, int dy)
{
    if (abs(dx) > abs(dy)) {
        return (dx > 0) ? DIR_RIGHT : DIR_LEFT;
    }
    return (dy > 0) ? DIR_DOWN : DIR_UP;
}

static void spawn_food(void)
{
    for (int tries = 0; tries < 400; tries++) {
        /* 注意：lv_rand(min,max) 的返回值【包含 max 端点】(a % (max-min+1) + min)，
         * 所以上界必须是 GRID-1；写成 GRID 会生成 18（有效坐标仅 0..17），
         * 食物被画到画布外既看不见、蛇也永远吃不到。 */
        int fx = (int)lv_rand(0, GRID - 1);
        int fy = (int)lv_rand(0, GRID - 1);
        bool on_snake = false;
        for (int i = 0; i < s_snake_len; i++) {
            if (s_sx[i] == fx && s_sy[i] == fy) {
                on_snake = true;
                break;
            }
        }
        if (!on_snake) {
            s_food_x = fx;
            s_food_y = fy;
            return;
        }
    }
    /* 极端：棋盘几乎填满，随便放（不会触发，仅保险） */
    s_food_x = 0;
    s_food_y = 0;
}

static void update_score_label(void)
{
    if (s_score_lbl) {
        char b[24];
        snprintf(b, sizeof(b), "分数: %d", s_score);
        lv_label_set_text(s_score_lbl, b);
    }
}

static void step(void)
{
    int hx = s_sx[0] + dir_dx(s_next_dir);
    int hy = s_sy[0] + dir_dy(s_next_dir);
    s_dir = s_next_dir;

    /* 撞墙 */
    if (hx < 0 || hx >= GRID || hy < 0 || hy >= GRID) {
        game_over();
        return;
    }

    bool eat = (hx == s_food_x && hy == s_food_y);
    /* 撞自己：吃食物时整条保留（增长），否则尾会移走 */
    int body_end = eat ? s_snake_len : s_snake_len - 1;
    for (int i = 0; i < body_end; i++) {
        if (s_sx[i] == hx && s_sy[i] == hy) {
            game_over();
            return;
        }
    }

    if (eat) {
        /* 增长：整体后移，新头插到 [0] */
        memmove(&s_sx[1], &s_sx[0], (size_t)s_snake_len * sizeof(int));
        memmove(&s_sy[1], &s_sy[0], (size_t)s_snake_len * sizeof(int));
        s_snake_len++;
        s_sx[0] = hx;
        s_sy[0] = hy;
        s_score += 10;
        if (s_step_interval > 80) {
            s_step_interval -= 6;   /* 越长越快 */
        }
        update_score_label();
        spawn_food();
        audio_sfx_flap();
    } else {
        /* 移动：尾删，头加 */
        for (int i = s_snake_len - 1; i >= 1; i--) {
            s_sx[i] = s_sx[i - 1];
            s_sy[i] = s_sy[i - 1];
        }
        s_sx[0] = hx;
        s_sy[0] = hy;
    }
}

static void game_over(void)
{
    s_state = ST_OVER;
    char b[48];
    snprintf(b, sizeof(b), "游戏结束\n分数: %d", s_score);
    lv_label_set_text(s_msg, b);
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    if (s_over_hint) {
        lv_label_set_text(s_over_hint, "点击重玩\n按键返回");
        lv_obj_clear_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);
    }
    audio_sfx_flap();
}

static void start_game(void)
{
    s_snake_len = 3;
    /* 初始：水平居中，头朝右 */
    int cy = GRID / 2;
    s_sx[0] = GRID / 2;     s_sy[0] = cy;
    s_sx[1] = GRID / 2 - 1; s_sy[1] = cy;
    s_sx[2] = GRID / 2 - 2; s_sy[2] = cy;
    s_dir = DIR_RIGHT;
    s_next_dir = DIR_RIGHT;
    s_score = 0;
    s_step_acc = 0;
    s_step_interval = 300;      /* 初始步进放慢到 300ms，给玩家反应时间（吃食物后逐步加快） */
    s_state = ST_PLAY;
    s_last_ms = lv_tick_get();
    spawn_food();
    update_score_label();
    if (s_msg)       lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    if (s_over_hint) lv_obj_add_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);
    s_dirty = true;         /* 下一轮 poll 重绘（不在本函数里直接画，避免重复绘制） */
}

static void on_pause(void) { s_paused = true; }
static void on_resume(void) { s_paused = false; s_last_ms = lv_tick_get(); }

static void close_to_app(void)
{
    if (!s_scr) return;
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    audio_bgm_stop();
    if (s_cv_buf) {
        heap_caps_free(s_cv_buf);
        s_cv_buf = NULL;
    }
    lv_obj_t *gone = s_scr;
    s_scr = NULL;
    s_cv = NULL;
    s_score_lbl = NULL;
    s_msg = NULL;
    s_over_hint = NULL;
    s_state = ST_READY;
    lv_obj_del(gone);
}

void ui_snake_start(void)
{
    if (s_scr) {
        return;
    }
    s_key_down = false;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 棋盘画布（PSRAM 缓冲，CPU 读取安全，不触发面板 DMA 从 PSRAM 取数） */
    s_cv_buf = heap_caps_aligned_alloc(32, (size_t)PF_W * PF_H * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!s_cv_buf) {
        ESP_LOGE("snake", "canvas buf alloc failed");
        lv_obj_del(s_scr);
        s_scr = NULL;
        return;
    }
    s_cv = lv_canvas_create(s_scr);
    lv_obj_remove_style_all(s_cv);
    lv_canvas_set_buffer(s_cv, s_cv_buf, PF_W, PF_H, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_obj_set_pos(s_cv, PF_X, PF_Y);
    lv_obj_clear_flag(s_cv, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_cv, LV_OBJ_FLAG_SCROLLABLE);

    /* 分数标签（右上角，圆屏可见区） */
    s_score_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_score_lbl, "分数: 0");
    lv_obj_set_style_text_font(s_score_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_score_lbl, lv_color_white(), 0);
    lv_obj_clear_flag(s_score_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_score_lbl, 268, 120);

    /* 开始 / 结算文案（居中） */
    s_msg = lv_label_create(s_scr);
    lv_label_set_text(s_msg, "");
    lv_obj_set_width(s_msg, FIELD_W - 40);
    lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_msg, &cn_font_16, 0);
    lv_obj_set_style_text_color(s_msg, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_msg, LV_ALIGN_CENTER, 0, 0);

    /* 游戏结束提示（居中下方） */
    s_over_hint = lv_label_create(s_scr);
    lv_label_set_text(s_over_hint, "");
    lv_obj_set_width(s_over_hint, FIELD_W - 40);
    lv_label_set_long_mode(s_over_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_over_hint, &cn_font_16, 0);
    lv_obj_set_style_text_color(s_over_hint, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_over_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(s_over_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_over_hint, LV_ALIGN_CENTER, 0, 30);
    lv_obj_add_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);

    /* 全屏透明按钮：仅作视觉占位/兼容，方向控制改由 ui_snake_poll 直接读 indev，
     * 不再依赖 LVGL 事件派发（避免事件没派到按钮导致失控）。 */
    lv_obj_t *tap = lv_btn_create(s_scr);
    lv_obj_set_size(tap, FIELD_W, FIELD_H);
    lv_obj_set_pos(tap, 0, 0);
    lv_obj_set_style_radius(tap, 0, 0);
    lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap, 0, 0);
    lv_obj_set_style_shadow_width(tap, 0, 0);
    lv_obj_clear_flag(tap, LV_OBJ_FLAG_CLICKABLE);

    /* 进入“准备”态：显示操作说明 */
    s_state = ST_READY;
    s_snake_len = 0;
    s_score = 0;
    s_paused = false;
    s_step_acc = 0;
    s_touch_prev = false;
    s_swipe_latched = false;
    s_dirty = false;
    s_last_ms = lv_tick_get();
    lv_canvas_fill_bg(s_cv, BG_COLOR, LV_OPA_COVER);
    lv_label_set_text(s_msg, "点击开始\n滑动控制方向");
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);

    /* 步进改由 ui_snake_poll() 在主循环里驱动（不依赖 LVGL 定时器，更稳） */
    s_timer = NULL;
    lv_scr_load(s_scr);

    ui_app_shell_set_exit_cb(close_to_app);
    ui_app_shell_set_pause_cb(on_pause);
    ui_app_shell_set_resume_cb(on_resume);
    ui_app_shell_bind(s_scr);

    audio_bgm_start(AUDIO_BGM_THEME_TETRIS);
}

/* 在主循环里直接轮询指针 indev：点按开始/重玩；一次「按下->滑动->抬手」判定一次方向。
 * 完全不依赖 LVGL 事件派发，绕开「事件没派到全屏按钮」导致失控的问题。
 * 方向在抬手瞬间按整段滑动的总位移判定：左滑->向左，右滑->向右，上滑->向上，下滑->向下。 */
static void snake_input_poll(void)
{
    lv_indev_state_t st = touch_input_get_state();
    lv_point_t p;
    touch_input_get_point(&p);
    bool pressed = (st == LV_INDEV_STATE_PRESSED);

    if (pressed) {
        /* 按压中：持续记录最后一个有效坐标，供抬手时计算本次滑动的总位移 */
        s_last_press_x = p.x;
        s_last_press_y = p.y;
        if (!s_touch_prev) {
            /* 新按下：准备/结束态 -> 开始或重玩；并记下本次滑动的起点 */
            if (s_state == ST_READY || s_state == ST_OVER) {
                ESP_LOGI("snake", "press -> start");
                start_game();
            }
            s_press_x = p.x;
            s_press_y = p.y;
            s_swipe_latched = false;
        } else if (!s_swipe_latched && s_state == ST_PLAY) {
            /* 滑动过程中：位移一过阈值就立即换向（不必等抬手），
             * 并锁定到本次按压结束——一次滑动只换一次向。 */
            int dx = (int)p.x - s_press_x;
            int dy = (int)p.y - s_press_y;
            if (abs(dx) >= SWIPE_DIR_PX || abs(dy) >= SWIPE_DIR_PX) {
                dir_t d = pick_dir(dx, dy);
                set_dir(d);
                s_swipe_latched = true;
                ESP_LOGI("snake", "swipe-dir=%d dx=%d dy=%d", d, dx, dy);
            }
        }
    } else if (s_touch_prev) {
        /* 抬手：若按压期间没来得及换向（滑动太快采样少），用总位移补判一次 */
        if (!s_swipe_latched && s_state == ST_PLAY) {
            int dx = s_last_press_x - s_press_x;
            int dy = s_last_press_y - s_press_y;
            if (abs(dx) >= SWIPE_DIR_PX || abs(dy) >= SWIPE_DIR_PX) {
                dir_t d = pick_dir(dx, dy);
                set_dir(d);
                ESP_LOGI("snake", "swipe-rel=%d dx=%d dy=%d", d, dx, dy);
            }
        }
    }
    s_touch_prev = pressed;

    {
        static uint32_t s_diag = 0;
        uint32_t t = lv_tick_get();
        if (t - s_diag > 500) {
            s_diag = t;
            ESP_LOGI("snake", "touch x=%d y=%d pressed=%d", p.x, p.y, pressed);
        }
    }
}

/* 由主循环 lvgl_port_loop 调用：用真实时间差推进蛇的步进 + 轮询触摸输入。
 * 只要主循环在跑（其它游戏都正常），蛇就一定动、且能响应触摸。 */
void ui_snake_poll(void)
{
    if (!s_scr) {
        return;
    }
    snake_input_poll();

    {
        static uint32_t s_log_ms = 0;
        uint32_t t = lv_tick_get();
        if (t - s_log_ms > 1000) {
            s_log_ms = t;
            ESP_LOGI("snake", "poll st=%d acc=%d len=%d paused=%d",
                     s_state, s_step_acc, s_snake_len, s_paused);
        }
    }

    uint32_t now = lv_tick_get();
    uint32_t dt = now - s_last_ms;
    s_last_ms = now;
    if (s_state != ST_PLAY || s_paused) {
        return;
    }
    if (dt > 200) {
        dt = 200;   /* 暂停/长帧后防止一次大跳 */
    }
    s_step_acc += (int)dt;
    while (s_step_acc >= s_step_interval) {
        s_step_acc -= s_step_interval;
        step();
        s_dirty = true;
        if (s_state != ST_PLAY) {
            break;  /* 本步撞墙/撞自己 -> game_over，停止继续步进 */
        }
    }
    /* 只在画面真的变了才重绘：288x288 全画布重绘约 10 万次 PSRAM 写，
     * 若每轮都画会把主循环拖到几十毫秒一轮，触摸采样变慢、滑动就不灵敏。 */
    if (s_state == ST_PLAY && s_dirty) {
        redraw();
        s_dirty = false;
    }
}
