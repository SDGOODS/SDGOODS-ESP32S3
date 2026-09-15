#include "ui_app_page.h"

#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "ui_flappy.h"
#include "ui_plane.h"
#include "ui_tetris.h"
#include "ui_snake.h"
#include "ui_home.h"
#include "ui_swipe_back.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);

static lv_obj_t *s_scr;
static bool s_active;   /* 当前是否为活动页（进入子页时置 false，避免 poll 误触发返回） */
static bool s_key_down;

static void on_app_btn(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    if (name && strcmp(name, "小鸟") == 0) {
        s_active = false;   /* 进入子游戏：暂停本页 poll，避免按键冲突 */
        ui_flappy_start();
    } else if (name && strcmp(name, "飞机") == 0) {
        s_active = false;
        ui_plane_start();
    } else if (name && strcmp(name, "俄罗斯方块") == 0) {
        s_active = false;
        ui_tetris_start();
    } else if (name && strcmp(name, "贪吃蛇") == 0) {
        s_active = false;
        ui_snake_start();
    }
}

/* ==================== 像素风图标按钮 ====================
 * 12x12 像素图 × ICON_SCALE = 48px 图标，用 canvas 逐像素绘制（真像素风，非色块）。
 * 调色板：'.'=透明 'K'=描边黑 'Y'=小鸟黄 'O'=喙橙 'W'=白/机身银白
 *         'B'=座舱蓝 'R'=红 'D'=小鸟翼尾(深黄)
 * ======================================================== */
#define ICON_MAP_W  12
#define ICON_SCALE  4
#define ICON_PX     (ICON_MAP_W * ICON_SCALE)      /* 48 */
#define ICON_BUF_SZ (ICON_PX * ICON_PX * 3)        /* TRUE_COLOR_ALPHA: 2B 颜色 + 1B alpha */

/* 小鸟（朝右：白眼 + 橙喙 + 深黄翼尾） */
static const char *const ICON_BIRD_MAP[ICON_MAP_W] = {
    "....KKKK....",
    "..KKYYYYKK..",
    ".KYYYYYYYYK.",
    "KYYYYYYWWYYK",
    "KYYYYYYWKYYK",
    "KYYYYYYYYOOK",
    "KYYYYYYYOOOK",
    ".KYYYYYYOOK.",
    ".KYYYYYYYK..",
    "..KDDDDDK...",
    "..KKDDDDK...",
    "...KKKKK....",
};

/* 飞机（机头朝上：三角翼喷气机，银白机身 + 蓝座舱） */
static const char *const ICON_PLANE_MAP[ICON_MAP_W] = {
    ".....KK.....",
    "....KWWK....",
    "....KWWK....",
    "...KWWWWK...",
    "...KBBBBK...",
    "..KWWBBWWK..",
    ".KWWWWWWWWK.",
    "KWWWWWWWWWWK",
    "KWWWWWWWWWWK",
    ".KWWWWWWWWK.",
    "..KWWWWWWK..",
    "...KKKKKK...",
};

/* 俄罗斯方块（2×2 四色方块：紫/青/黄/红，像素风） */
static const char *const ICON_TETRIS_MAP[ICON_MAP_W] = {
    "............",
    ".PPPP..CCCC.",
    ".PPPP..CCCC.",
    ".PPPP..CCCC.",
    ".PPPP..CCCC.",
    ".PPPP..CCCC.",
    "............",
    ".YYYY..RRRR.",
    ".YYYY..RRRR.",
    ".YYYY..RRRR.",
    ".YYYY..RRRR.",
    ".YYYY..RRRR.",
};

/* 贪吃蛇（绿色蛇身 + 红色食物） */
static const char *const ICON_SNAKE_MAP[ICON_MAP_W] = {
    "............",
    "..GGGGGG....",
    ".G........G.",
    ".G.GGGGGG.G.",
    ".G.G....G.G.",
    ".G.G.RR.G.G.",
    ".G.G.RR.G.G.",
    ".G.G....G.G.",
    ".G.GGGGGG.G.",
    ".G........G.",
    "..GGGGGG....",
    "............",
};

static lv_color_t icon_color(char c)
{
    switch (c) {
    case 'K': return lv_color_hex(0x101820);   /* 描边黑 */
    case 'Y': return lv_color_hex(0xFFEC27);   /* 小鸟黄 / O 块 */
    case 'O': return lv_color_hex(0xFF8A00);   /* 喙橙 */
    case 'W': return lv_color_hex(0xDEE2E6);   /* 机身银白 */
    case 'B': return lv_color_hex(0x3B82F6);   /* 座舱蓝 / J 块 */
    case 'R': return lv_color_hex(0xFF004D);   /* 红 / Z 块 */
    case 'D': return lv_color_hex(0xF0A500);   /* 翼尾深黄 */
    case 'C': return lv_color_hex(0x29ADFF);   /* I 块青 */
    case 'P': return lv_color_hex(0x9B5DE5);   /* T 块紫 */
    case 'G': return lv_color_hex(0x00E436);   /* 蛇身绿 */
    default:  return lv_color_white();
    }
}

/* 把像素图渲染成 canvas（带 alpha），返回图标对象 */
static lv_obj_t *make_pixel_icon(lv_obj_t *parent, uint8_t *buf, const char *const *map)
{
    memset(buf, 0, ICON_BUF_SZ);   /* 先全透明 */

    lv_obj_t *cv = lv_canvas_create(parent);
    lv_obj_remove_style_all(cv);
    lv_canvas_set_buffer(cv, buf, ICON_PX, ICON_PX, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_obj_clear_flag(cv, LV_OBJ_FLAG_CLICKABLE);   /* 点击透传给按钮 */
    lv_obj_clear_flag(cv, LV_OBJ_FLAG_SCROLLABLE);

    for (int y = 0; y < ICON_MAP_W; y++) {
        for (int x = 0; x < ICON_MAP_W; x++) {
            char c = map[y][x];
            if (c == '.') {
                continue;
            }
            lv_color_t col = icon_color(c);
            for (int sy = 0; sy < ICON_SCALE; sy++) {
                for (int sx = 0; sx < ICON_SCALE; sx++) {
                    lv_canvas_set_px_color(cv, x * ICON_SCALE + sx, y * ICON_SCALE + sy, col);
                    lv_canvas_set_px_opa(cv, x * ICON_SCALE + sx, y * ICON_SCALE + sy, LV_OPA_COVER);
                }
            }
        }
    }
    lv_obj_center(cv);
    return cv;
}

static uint8_t s_icon_bird_buf[ICON_BUF_SZ];
static uint8_t s_icon_plane_buf[ICON_BUF_SZ];
static uint8_t s_icon_tetris_buf[ICON_BUF_SZ];
static uint8_t s_icon_snake_buf[ICON_BUF_SZ];

static lv_obj_t *make_round_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *text)
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
        /* 像素风图标：按钮内居中显示像素精灵，取代文字标签 */
        uint8_t *buf = NULL;
        const char *const *map = NULL;
        if (strcmp(text, "小鸟") == 0) {
            buf = s_icon_bird_buf;
            map = ICON_BIRD_MAP;
        } else if (strcmp(text, "飞机") == 0) {
            buf = s_icon_plane_buf;
            map = ICON_PLANE_MAP;
        } else if (strcmp(text, "俄罗斯方块") == 0) {
            buf = s_icon_tetris_buf;
            map = ICON_TETRIS_MAP;
        } else if (strcmp(text, "贪吃蛇") == 0) {
            buf = s_icon_snake_buf;
            map = ICON_SNAKE_MAP;
        }
        if (buf && map) {
            make_pixel_icon(btn, buf, map);
        } else {
            /* 兜底：未定义图标的入口仍显示文字 */
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, text);
            lv_obj_set_style_text_font(lbl, &si_yuan_black_icon_16, 0);
            lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
            lv_obj_center(lbl);
        }

        /* 带文字的按钮 = 游戏入口，统一绑定回调（空按钮无回调）；
           on_app_btn 内部按 name 分派，未知 name 不做任何事，后续加新游戏只需改 text + 分支 */
        lv_obj_add_event_cb(btn, on_app_btn, LV_EVENT_CLICKED, (void *)text);
    }
    return btn;
}

static void close_page(void)
{
    if (!s_scr) {
        return;
    }
    lv_obj_t *gone = s_scr;
    s_scr = NULL;
    s_active = false;
    ui_home_show();
    lv_obj_del(gone);
}

void ui_app_page_show(void)
{
    if (s_scr) {
        /* 屏已存在（如从子游戏退出返回）：仅切回活动屏、恢复 poll 状态，不重建。
           若不 lv_scr_load，活动屏仍是子游戏屏，随后被 close_to_app 删除，
           导致活动屏变 NULL、触摸事件无人接收、应用图标点不进（需重启）。 */
        s_active = true;
        s_key_down = false;
        lv_scr_load(s_scr);
        return;
    }
    s_active = true;
    s_key_down = false;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回主页 */

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "应用");
    lv_obj_set_style_text_font(title, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_HOME_TITLE_Y);

    /* 6 个圆形按钮，沿用首页布局；第一个为「小鸟」，第二个「飞机」，第三个「俄罗斯方块」 */
    make_round_btn(s_scr, UI_HOME_BTN1_X, UI_HOME_BTN1_Y, "小鸟");
    make_round_btn(s_scr, UI_HOME_BTN2_X, UI_HOME_BTN2_Y, "飞机");
    make_round_btn(s_scr, UI_HOME_BTN3_X, UI_HOME_BTN3_Y, "俄罗斯方块");
    make_round_btn(s_scr, UI_HOME_BTN4_X, UI_HOME_BTN4_Y, "贪吃蛇");
    make_round_btn(s_scr, UI_HOME_BTN5_X, UI_HOME_BTN5_Y, "");
    make_round_btn(s_scr, UI_HOME_BTN6_X, UI_HOME_BTN6_Y, "");

    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, "按电源键返回");
    lv_obj_set_style_text_font(hint, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x808080), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 296);

    lv_scr_load(s_scr);
}

void ui_app_page_resume(void)
{
    if (s_scr) {
        s_active = true;
        s_key_down = false;
        lv_scr_load(s_scr);
    }
}

void ui_app_page_poll(void)
{
    if (!s_scr || !s_active) {
        return;
    }
    const int down = (gpio_get_level(BOARD_KEY_GPIO) == BOARD_KEY_ACTIVE_LEVEL);
    if (down && !s_key_down) {
        close_page();
    }
    s_key_down = down;
}
