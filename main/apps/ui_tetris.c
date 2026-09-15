#include "ui_tetris.h"

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

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(cn_font_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(cn_font_16);

#define FIELD_W   LCD_WIDTH
#define FIELD_H   LCD_HEIGHT

/* 棋盘：10 列 × 18 行（圆屏内可见区域受限，18 行更稳） */
#define COLS      10
#define ROWS      18
#define CELL      16                       /* 每格像素边长 */
#define PF_W      (COLS * CELL)            /* 160 */
#define PF_H      (ROWS * CELL)            /* 288 */
#define PF_X      100                      /* 棋盘左上角 x（居中：360/2 - 80） */
#define PF_Y      36                       /* 棋盘左上角 y（居中：360/2 - 144） */
#define NEXT_SZ   (4 * CELL)               /* 下一个预览画布 64×64 */

/* 像素风调色板（PICO-8 风：方角 + 深色描边，与小鸟/飞机一致） */
#define PIECE_BORDER  lv_color_hex(0x101820)   /* 方块描边/棋盘底（深蓝黑） */
#define PIECE_GRID    lv_color_hex(0x0B1026)   /* 棋盘背景（星空蓝） */

/* 7 种方块，每种 4 个旋转态，每态 4 个格子（4×4 包围盒内的 dx,dy） */
static const int8_t PIECES[7][4][4][2] = {
    /* I */ { {{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}}, {{0,2},{1,2},{2,2},{3,2}}, {{1,0},{1,1},{1,2},{1,3}} },
    /* O */ { {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}} },
    /* T */ { {{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}} },
    /* S */ { {{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}, {{1,1},{2,1},{0,2},{1,2}}, {{0,0},{0,1},{1,1},{1,2}} },
    /* Z */ { {{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{1,2},{2,2}}, {{1,0},{0,1},{1,1},{0,2}} },
    /* J */ { {{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}}, {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}} },
    /* L */ { {{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}}, {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}} },
};

static lv_color_t piece_color(int id)
{
    switch (id) {
    case 1: return lv_color_hex(0x29ADFF);   /* I 青 */
    case 2: return lv_color_hex(0xFFEC27);   /* O 黄 */
    case 3: return lv_color_hex(0x9B5DE5);   /* T 紫 */
    case 4: return lv_color_hex(0x00E436);   /* S 绿 */
    case 5: return lv_color_hex(0xFF004D);   /* Z 红 */
    case 6: return lv_color_hex(0x3B82F6);   /* J 蓝 */
    case 7: return lv_color_hex(0xFFA300);   /* L 橙 */
    default: return lv_color_black();
    }
}

typedef enum {
    ST_READY,
    ST_PLAY,
    ST_OVER
} game_state_t;

static int          board[ROWS][COLS];   /* 0=空，否则 = 方块颜色 id(1..7) */
static int          cur_type, cur_rot, cur_x, cur_y;
static int          next_type;
static game_state_t s_state;
static int          s_score, s_lines, s_level;
static int          s_drop_acc;          /* 重力累计(ms) */
static int          s_drop_interval;     /* 当前下落间隔(ms) */
static int          s_saved_drop_interval;
static bool          s_soft_dropping;
static bool         board_dirty;

static lv_obj_t    *s_scr = NULL;
static lv_obj_t    *s_cv = NULL;          /* 主棋盘画布 */
static lv_obj_t    *s_next_cv = NULL;      /* 下一个预览画布 */
static uint8_t     *s_cv_buf = NULL;       /* 主棋盘画布缓冲（PSRAM） */
static uint8_t      s_next_buf[NEXT_SZ * NEXT_SZ * 3];
static lv_obj_t    *s_score_lbl = NULL;
static lv_obj_t    *s_lines_lbl = NULL;
static lv_obj_t    *s_level_lbl = NULL;
static lv_obj_t    *s_msg = NULL;
static lv_obj_t    *s_over_hint = NULL;
static lv_timer_t  *s_timer = NULL;
static bool         s_key_down;
static bool         s_paused;
static int          s_press_x, s_press_y;
static int          s_swipe_ref_x;   /* 上次已响应的 x，连续滑动按格累计 */
static int          s_swipe_ref_y;   /* 上次已响应的 y，连续下滑按格累计 */
static bool         s_swiped;        /* 本次按下是否发生过滑动 */
#define SWIPE_MOVE_PX  18            /* 每滑过该像素数，方块左右移动一格 */
#define SWIPE_DOWN_PX  14            /* 每滑过该像素数，触发一次加速下落 */

static void start_game(void);
static void close_to_app(void);
static void spawn_next(void);
static void game_over(void);

/* 在画布上画一个方块（内部填充，留 2px 深色描边作为分隔，形成像素方块观感） */
static void draw_cell(lv_obj_t *cv, int col, int row, lv_color_t c)
{
    const int x0 = col * CELL;
    const int y0 = row * CELL;
    const int B = 2;
    for (int yy = B; yy < CELL - B; yy++) {
        for (int xx = B; xx < CELL - B; xx++) {
            lv_canvas_set_px_color(cv, x0 + xx, y0 + yy, c);
            lv_canvas_set_px_opa(cv, x0 + xx, y0 + yy, LV_OPA_COVER);
        }
    }
}

static void redraw(void)
{
    if (!s_cv) {
        return;
    }
    lv_canvas_fill_bg(s_cv, PIECE_BORDER, LV_OPA_COVER);
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (board[r][c]) {
                draw_cell(s_cv, c, r, piece_color(board[r][c]));
            }
        }
    }
    if (s_state == ST_PLAY) {
        for (int i = 0; i < 4; i++) {
            int bx = cur_x + PIECES[cur_type][cur_rot][i][0];
            int by = cur_y + PIECES[cur_type][cur_rot][i][1];
            if (by >= 0) {
                draw_cell(s_cv, bx, by, piece_color(cur_type + 1));
            }
        }
    }
    if (s_next_cv) {
        lv_canvas_fill_bg(s_next_cv, PIECE_BORDER, LV_OPA_COVER);
        for (int i = 0; i < 4; i++) {
            int bx = PIECES[next_type][0][i][0];
            int by = PIECES[next_type][0][i][1];
            draw_cell(s_next_cv, bx, by, piece_color(next_type + 1));
        }
    }
}

static bool collides(int type, int rot, int x, int y)
{
    for (int i = 0; i < 4; i++) {
        int bx = x + PIECES[type][rot][i][0];
        int by = y + PIECES[type][rot][i][1];
        if (bx < 0 || bx >= COLS || by >= ROWS) {
            return true;
        }
        if (by >= 0 && board[by][bx]) {
            return true;
        }
    }
    return false;
}

static void move_piece(int dx)
{
    if (!collides(cur_type, cur_rot, cur_x + dx, cur_y)) {
        cur_x += dx;
        board_dirty = true;
    }
}

static void rotate_piece(void)
{
    int nr = (cur_rot + 1) % 4;
    if (!collides(cur_type, nr, cur_x, cur_y)) {
        cur_rot = nr; board_dirty = true; return;
    }
    if (!collides(cur_type, nr, cur_x - 1, cur_y)) {
        cur_x--; cur_rot = nr; board_dirty = true; return;
    }
    if (!collides(cur_type, nr, cur_x + 1, cur_y)) {
        cur_x++; cur_rot = nr; board_dirty = true; return;
    }
    if (!collides(cur_type, nr, cur_x, cur_y - 1)) {
        cur_y--; cur_rot = nr; board_dirty = true; return;
    }
}

static void clear_lines(void)
{
    int cleared = 0;
    for (int r = ROWS - 1; r >= 0; r--) {
        bool full = true;
        for (int c = 0; c < COLS; c++) {
            if (!board[r][c]) { full = false; break; }
        }
        if (full) {
            for (int rr = r; rr > 0; rr--) {
                for (int c = 0; c < COLS; c++) {
                    board[rr][c] = board[rr - 1][c];
                }
            }
            for (int c = 0; c < COLS; c++) {
                board[0][c] = 0;
            }
            cleared++;
            r++;   /* 同一行索引在整体上移后重新检查 */
        }
    }
    if (cleared > 0) {
        static const int pts[5] = {0, 100, 300, 500, 800};
        s_score += pts[cleared];
        s_lines += cleared;
        s_level = 1 + s_lines / 10;
        s_drop_interval = 480 - (s_level - 1) * 35;
        if (s_drop_interval < 120) s_drop_interval = 120;
        if (s_score_lbl) { char b[32]; snprintf(b, sizeof(b), "分数: %d", s_score); lv_label_set_text(s_score_lbl, b); }
        if (s_lines_lbl) { char b[32]; snprintf(b, sizeof(b), "行数: %d", s_lines); lv_label_set_text(s_lines_lbl, b); }
        if (s_level_lbl) { char b[16]; snprintf(b, sizeof(b), "第%d关", s_level); lv_label_set_text(s_level_lbl, b); }
        audio_sfx_flap();
    }
}

static void step(void)
{
    if (!collides(cur_type, cur_rot, cur_x, cur_y + 1)) {
        cur_y++;
    } else {
        for (int i = 0; i < 4; i++) {
            int bx = cur_x + PIECES[cur_type][cur_rot][i][0];
            int by = cur_y + PIECES[cur_type][cur_rot][i][1];
            if (by >= 0 && by < ROWS && bx >= 0 && bx < COLS) {
                board[by][bx] = cur_type + 1;
            }
        }
        audio_sfx_flap();
        clear_lines();
        spawn_next();
    }
    board_dirty = true;
}

static void game_over(void)
{
    s_state = ST_OVER;
    char b[48];
    snprintf(b, sizeof(b), "游戏结束\n分数: %d 行: %d", s_score, s_lines);
    lv_label_set_text(s_msg, b);
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    if (s_over_hint) {
        lv_label_set_text(s_over_hint, "点击重玩\n按键返回");
        lv_obj_clear_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);
    }
    audio_sfx_flap();
}

static void spawn_next(void)
{
    cur_type = next_type;
    next_type = (int)lv_rand(0, 7);
    cur_rot = 0;
    cur_x = 3;
    cur_y = 0;
    if (collides(cur_type, cur_rot, cur_x, cur_y)) {
        game_over();
    }
    board_dirty = true;
}

static void start_game(void)
{
    memset(board, 0, sizeof(board));
    s_score = 0;
    s_lines = 0;
    s_level = 1;
    s_drop_acc = 0;
    s_drop_interval = 480;
    next_type = (int)lv_rand(0, 7);
    spawn_next();
    s_state = ST_PLAY;
    if (s_score_lbl) lv_label_set_text(s_score_lbl, "分数: 0");
    if (s_lines_lbl) lv_label_set_text(s_lines_lbl, "行数: 0");
    if (s_level_lbl) lv_label_set_text(s_level_lbl, "第1关");
    if (s_msg) lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    if (s_over_hint) lv_obj_add_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);
    board_dirty = true;
}

static void tetris_tick(lv_timer_t *t)
{
    (void)t;
    if (!s_scr || s_state != ST_PLAY || s_paused) {
        return;
    }
    s_drop_acc += 16;
    if (s_drop_acc >= s_drop_interval) {
        s_drop_acc = 0;
        step();
    }
    if (board_dirty) {
        board_dirty = false;
        redraw();
    }
}

static void on_tap(lv_event_t *e)
{
    if (s_paused) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        if (!s_scr) return;
        if (s_state == ST_READY) {
            start_game();
            return;
        }
        if (s_state == ST_OVER) {
            return;   /* 松手时重玩（见 RELEASED） */
        }
        if (s_state == ST_PLAY) {
            lv_indev_t *indev = lv_indev_get_act();
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            s_press_x = p.x;
            s_press_y = p.y;
            s_swipe_ref_x = p.x;
            s_swipe_ref_y = p.y;
            s_swiped = false;
            s_soft_dropping = false;
            s_saved_drop_interval = s_drop_interval;
        }
    }
    else if (code == LV_EVENT_PRESSING) {
        if (!s_scr || s_state != ST_PLAY) return;
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        /* 左右滑动 -> 方块左右移动（每滑过 SWIPE_MOVE_PX 移一格，可连续多格） */
        int dx = (int)p.x - s_swipe_ref_x;
        if (dx >= SWIPE_MOVE_PX) {
            move_piece(1);
            s_swipe_ref_x += SWIPE_MOVE_PX;
            s_swiped = true;
        } else if (dx <= -SWIPE_MOVE_PX) {
            move_piece(-1);
            s_swipe_ref_x -= SWIPE_MOVE_PX;
            s_swiped = true;
        }

        /* 下滑 -> 持续加速下落（软降），只改变下落速度不改变左右方向 */
        int dy = (int)p.y - s_swipe_ref_y;
        if (dy >= SWIPE_DOWN_PX) {
            if (!s_soft_dropping) {
                s_soft_dropping = true;
                s_saved_drop_interval = s_drop_interval;
                s_drop_interval = 90;   /* 按住时快速下落但不直接落底 */
            }
            /* 每滑过 SWIPE_DOWN_PX 触发一次下落，可连续软降 */
            step();
            s_swipe_ref_y += SWIPE_DOWN_PX;
            s_swiped = true;
            board_dirty = true;
        }
    }
    else if (code == LV_EVENT_RELEASED) {
        if (!s_scr) return;
        if (s_soft_dropping) {
            s_drop_interval = s_saved_drop_interval;
            s_soft_dropping = false;
        }
        if (s_state == ST_OVER) {
            start_game();
        } else if (s_state == ST_PLAY && !s_swiped) {
            rotate_piece();   /* 纯点击（未滑动）-> 旋转 */
        }
    }
}

static void on_pause(void) { s_paused = true; }
static void on_resume(void) { s_paused = false; }

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
    s_next_cv = NULL;
    s_score_lbl = NULL;
    s_lines_lbl = NULL;
    s_level_lbl = NULL;
    s_msg = NULL;
    s_over_hint = NULL;
    s_state = ST_READY;
    lv_obj_del(gone);
}

void ui_tetris_start(void)
{
    if (s_scr) {
        return;
    }
    s_key_down = false;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, PIECE_GRID, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 棋盘外框（深色描边矩形，置于画布之后=底层） */
    lv_obj_t *frame = lv_obj_create(s_scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, PF_W + 4, PF_H + 4);
    lv_obj_set_pos(frame, PF_X - 2, PF_Y - 2);
    lv_obj_set_style_radius(frame, 0, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x3A4A7A), 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    /* 棋盘画布（PSRAM 缓冲，CPU 读取安全，不触发面板 DMA 从 PSRAM 取数） */
    s_cv_buf = heap_caps_aligned_alloc(32, (size_t)PF_W * PF_H * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!s_cv_buf) {
        ESP_LOGE("tetris", "canvas buf alloc failed");
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

    /* “下一个”预览画布（左上角内，圆屏可见区） */
    s_next_cv = lv_canvas_create(s_scr);
    lv_obj_remove_style_all(s_next_cv);
    lv_canvas_set_buffer(s_next_cv, s_next_buf, NEXT_SZ, NEXT_SZ, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_obj_set_pos(s_next_cv, 268, 132);
    lv_obj_clear_flag(s_next_cv, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_next_cv, LV_OBJ_FLAG_SCROLLABLE);

    /* 左侧文字：分数 / 行数 / 关卡 */
    s_score_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_score_lbl, "分数: 0");
    lv_obj_set_style_text_font(s_score_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_score_lbl, lv_color_white(), 0);
    lv_obj_clear_flag(s_score_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_score_lbl, 12, 120);

    s_lines_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_lines_lbl, "行数: 0");
    lv_obj_set_style_text_font(s_lines_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_lines_lbl, lv_color_hex(0xFFEC27), 0);
    lv_obj_clear_flag(s_lines_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_lines_lbl, 12, 150);

    s_level_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_level_lbl, "第1关");
    lv_obj_set_style_text_font(s_level_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_level_lbl, lv_color_hex(0x29ADFF), 0);
    lv_obj_clear_flag(s_level_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_level_lbl, 12, 180);

    /* “下一个”标签 */
    lv_obj_t *next_lbl = lv_label_create(s_scr);
    lv_label_set_text(next_lbl, "下一个");
    lv_obj_set_style_text_font(next_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(next_lbl, lv_color_white(), 0);
    lv_obj_clear_flag(next_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(next_lbl, 268, 104);

    /* 结算文案（居中） */
    s_msg = lv_label_create(s_scr);
    lv_label_set_text(s_msg, "");
    lv_obj_set_width(s_msg, FIELD_W - 60);
    lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_msg, &cn_font_16, 0);
    lv_obj_set_style_text_color(s_msg, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_msg, LV_ALIGN_CENTER, 0, 0);

    /* 游戏结束提示（居中下方） */
    s_over_hint = lv_label_create(s_scr);
    lv_label_set_text(s_over_hint, "");
    lv_obj_set_width(s_over_hint, FIELD_W - 60);
    lv_label_set_long_mode(s_over_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_over_hint, &cn_font_16, 0);
    lv_obj_set_style_text_color(s_over_hint, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_over_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(s_over_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_over_hint, LV_ALIGN_CENTER, 0, 62);
    lv_obj_add_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);

    /* 全屏透明按钮：接收所有触摸 */
    lv_obj_t *tap = lv_btn_create(s_scr);
    lv_obj_set_size(tap, FIELD_W, FIELD_H);
    lv_obj_set_pos(tap, 0, 0);
    lv_obj_set_style_radius(tap, 0, 0);
    lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap, 0, 0);
    lv_obj_set_style_shadow_width(tap, 0, 0);
    lv_obj_add_event_cb(tap, on_tap, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(tap, on_tap, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(tap, on_tap, LV_EVENT_RELEASED, NULL);

    /* 进入“准备”态：空棋盘 + 操作说明 */
    s_state = ST_READY;
    memset(board, 0, sizeof(board));
    next_type = (int)lv_rand(0, 7);
    redraw();
    lv_label_set_text(s_msg, "点击开始游戏\n左右移动 中间旋转\n下滑加速");
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);

    s_timer = lv_timer_create(tetris_tick, 16, NULL);
    lv_scr_load(s_scr);

    ui_app_shell_set_exit_cb(close_to_app);
    ui_app_shell_set_pause_cb(on_pause);
    ui_app_shell_set_resume_cb(on_resume);
    ui_app_shell_bind(s_scr);

    audio_bgm_start(AUDIO_BGM_THEME_TETRIS);
}

void ui_tetris_poll(void)
{
    if (!s_scr) {
        return;
    }
    /* 电源键短按/长按统一由 touch_input.c 的 power_key_poll 处理 */
}
