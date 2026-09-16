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

#include "ui_flappy.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "sdgoods_i18n.h"    /* SDG_T：界面文案中英切换 */
#include "sdgoods_lcd.h"
#include "ui_app_page.h"
#include "sdgoods_audio.h"
#include "sdgoods_app_shell.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(cn_font_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(cn_font_16);

#define FIELD_W        LCD_WIDTH
#define FIELD_H        LCD_HEIGHT
#define BIRD_X         70
#define BIRD_SIZE      32
#define GRAVITY        0.45f
#define JUMP_V         -5.2f
#define MAX_VY         8.0f
#define PIPE_W         54
#define GAP_H          130      /* 第1关基准间隙 */
#define PIPE_SPEED     2.2f     /* 第1关基准速度 */
#define PIPE_SPACING   130      /* 第1关基准间距（缩小以在圆屏上看到更多水管） */
#define MAX_PIPES      6
#define PIPES_PER_LEVEL 5       /* 每通过几根水管升一关 */
#define MAX_PIPE_SPEED 5.2f     /* 速度上限 */
#define MIN_GAP_H      78       /* 间隙下限 */
#define MIN_SPACING    100      /* 间距下限 */
#define CAP_H          16       /* 水管管口帽高度 */

/* 像素风调色板（PICO-8 风，方角 + 深色描边） */
#define PIX_SKY     lv_color_hex(0x29ADFF)
#define PIX_BLACK   lv_color_hex(0x000000)
#define PIX_BIRD    lv_color_hex(0xFFEC27)
#define PIX_BELLY   lv_color_hex(0xFFF1E8)
#define PIX_BEAK    lv_color_hex(0xFFA300)
#define PIX_WING    lv_color_hex(0xF2B705)
#define PIX_CLOUD   lv_color_hex(0xFFF1E8)
#define PIX_PIPE    lv_color_hex(0x00E436)
#define PIX_PIPE_DK lv_color_hex(0x008751)
#define PIX_PIPE_LT lv_color_hex(0x5FCD6B)
#define PIX_GROUND  lv_color_hex(0x008751)
#define PIX_MTN     lv_color_hex(0x1E6FA8)   /* 远山（比天空略深的蓝） */
#define PIX_MTN2    lv_color_hex(0x2A8FC8)   /* 远山（次远，更亮一档） */

/* 飞行物（飞机/飞碟/超人/小鸟）像素调色板 */
#define PIX_PLANE   lv_color_hex(0xDEE2E6)   /* 飞机机身（银白） */
#define PIX_RED     lv_color_hex(0xFF004D)   /* 红色描边/披风/条纹 */
#define PIX_WIN     lv_color_hex(0x3B82F6)   /* 飞机舷窗（蓝） */
#define PIX_UFO     lv_color_hex(0x8371D4)   /* 飞碟（紫） */
#define PIX_DOME    lv_color_hex(0x29ADFF)   /* 飞碟玻璃罩（天蓝） */
#define PIX_YELLOW  lv_color_hex(0xFFEC27)   /* 飞碟灯（黄） */
#define PIX_HERO    lv_color_hex(0x1D7CF2)   /* 超人身体（蓝） */
#define PIX_CAPE    lv_color_hex(0xFF004D)   /* 超人披风（红） */
#define PIX_SKIN    lv_color_hex(0xFFCCAA)   /* 肤色 */
#define PIX_PIG     lv_color_hex(0xFFA8C0)   /* 小猪身体（粉） */
#define PIX_PIG_DK  lv_color_hex(0xE06A90)   /* 小猪描边/鼻孔（深粉） */

typedef enum {
    ST_READY,   /* 准备（未开始）：显示分数与音量按钮，等待点按开始 */
    ST_PLAY,
    ST_OVER
} game_state_t;

typedef struct {
    bool       active;
    lv_obj_t  *top;
    lv_obj_t  *bot;
    float      x;
    int        gap_y;
    bool       scored;
} pipe_t;

static lv_obj_t   *s_scr = NULL;
static lv_obj_t   *s_bird = NULL;
static lv_obj_t   *s_score_lbl = NULL;
static lv_obj_t   *s_level_lbl = NULL;
static lv_obj_t   *s_toast = NULL;
static lv_obj_t   *s_msg = NULL;
static lv_timer_t *s_timer = NULL;
static pipe_t      s_pipes[MAX_PIPES];
static float       s_bird_y;
static float       s_vy;
static int         s_score;
static int         s_level;
static float       s_pipe_speed;   /* 当前关卡速度（可动态变化） */
static int         s_gap_h;        /* 当前关卡间隙 */
static int         s_pipe_spacing; /* 当前关卡间距 */
static int         s_ticks;        /* 游戏帧计数，用于 toast 计时 */
static int         s_toast_hide_at;
static game_state_t s_state;
static float       s_last_pipe_x;
static bool        s_key_down;
static bool        s_paused;

static lv_obj_t *s_over_hint = NULL;   /* 游戏结束提示（点击重玩 / 按键返回，屏幕中间下方） */

/* 根据当前关卡计算难度：关卡越高，越快、间隙越小、间距越密 */
static void apply_difficulty(void)
{
    const int lvl = s_level - 1;
    s_pipe_speed   = PIPE_SPEED + lvl * 0.45f;
    if (s_pipe_speed > MAX_PIPE_SPEED) {
        s_pipe_speed = MAX_PIPE_SPEED;
    }
    s_gap_h        = GAP_H - lvl * 8;
    if (s_gap_h < MIN_GAP_H) {
        s_gap_h = MIN_GAP_H;
    }
    s_pipe_spacing = PIPE_SPACING - lvl * 12;
    if (s_pipe_spacing < MIN_SPACING) {
        s_pipe_spacing = MIN_SPACING;
    }
}

static void reset_game(void);
static void spawn_pipe(void);
static void on_tap(lv_event_t *e);
static void close_to_app(void);
static void on_pause(void) { s_paused = true; }
static void on_resume(void) { s_paused = false; }

/* 像素风方块：方角、可选深色描边、不可点击 */
static lv_obj_t *make_pixel_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                                 lv_color_t fill, lv_color_t border, lv_coord_t bw)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_color(o, fill, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    if (bw > 0) {
        lv_obj_set_style_border_color(o, border, 0);
        lv_obj_set_style_border_width(o, bw, 0);
        lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    }
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* 天上的白云装饰已按需求删除 */

/* 像素小鸟：用方块拼出 8-bit 造型（黑描边 + 黄身 + 白肚 + 橙喙 + 眼睛），整体放大 */
static lv_obj_t *make_bird(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 36, 32);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *o;
    o = make_pixel_rect(c, 28, 24, PIX_BLACK, PIX_BLACK, 0); lv_obj_set_pos(o, 4, 4);   /* 描边底 */
    o = make_pixel_rect(c, 22, 18, PIX_BIRD,  PIX_BLACK, 0); lv_obj_set_pos(o, 7, 7);   /* 身体 */
    o = make_pixel_rect(c, 18, 7,  PIX_BELLY, PIX_BLACK, 0); lv_obj_set_pos(o, 8, 19);  /* 肚子 */
    o = make_pixel_rect(c, 10, 6,  PIX_WING,  PIX_BLACK, 0); lv_obj_set_pos(o, 8, 17);  /* 翅膀 */
    o = make_pixel_rect(c, 7,  7,  PIX_CLOUD, PIX_BLACK, 0); lv_obj_set_pos(o, 20, 9);  /* 眼白 */
    o = make_pixel_rect(c, 3,  3,  PIX_BLACK, PIX_BLACK, 0); lv_obj_set_pos(o, 23, 12); /* 眼珠 */
    o = make_pixel_rect(c, 8,  5,  PIX_BEAK,  PIX_BLACK, 0); lv_obj_set_pos(o, 28, 14); /* 喙 */
    return c;
}

/* 像素远山：用阶梯状方块叠出三角形山形，cx 为山顶中心，base_w 底边宽，h 高度 */
static void make_mountain(lv_obj_t *parent, lv_coord_t cx, lv_coord_t base_w,
                          lv_coord_t h, lv_color_t col)
{
    const int steps = 9;
    lv_coord_t step_h = h / steps;
    lv_coord_t ground_top = FIELD_H - 14;
    for (int i = 0; i < steps; i++) {
        lv_coord_t w = base_w * (steps - i) / steps;
        if (w < 4) {
            w = 4;
        }
        lv_coord_t x = cx - w / 2;
        lv_coord_t y = ground_top - (i + 1) * step_h;
        lv_obj_t *m = make_pixel_rect(parent, w, step_h + 1, col, PIX_BLACK, 0);
        lv_obj_set_pos(m, x, y);
    }
}

/* ---------------------------------------------------------------------------
 * 天空飞行物（统一顺序出现）：飞机 / 飞碟 / 超人 / 小鸟 / 小猪
 * 全部从右→左飞，且“一次只出现一只”——飞出左界后隐藏，等待随机间隔
 * 再从右侧重生，避免多只同时出现
 * ------------------------------------------------------------------------- */
#define FLYER_W      48
#define FLYER_H      28
#define PIG_W        44
#define PIG_H        30

typedef struct {
    lv_obj_t *obj;
    int       type;     /* 0=飞机 1=飞碟 2=超人 3=小鸟 4=小猪 */
    float     x;
    float     y;
    float     vx;       /* 负值 = 向右→左 */
    float     bob;      /* 上下浮动相位 */
    int       cooldown; /* >0 时隐藏等待，倒计时结束后从右侧重生 */
} sky_t;

static sky_t s_sky;

/* 像素方块绘制 + 定位；mirror 时水平镜像（飞行物均右→左飞，机头/朝向朝左） */
static lv_obj_t *flyer_rect(lv_obj_t *cont, int w, int h, lv_color_t fill,
                            lv_color_t border, int r, int x, int y, int mirror)
{
    lv_obj_t *o = make_pixel_rect(cont, w, h, fill, border, r);
    if (mirror) x = FLYER_W - x - w;   /* 以 FLYER_W 为轴做水平翻转 */
    lv_obj_set_pos(o, x, y);
    return o;
}

/* 根据类型用像素方块拼出飞行物造型（清掉旧子对象后重建）；统一朝左飞 → 全部镜像 */
static void make_flyer_content(lv_obj_t *cont, int type)
{
    lv_obj_clean(cont);
    const int mirror = 1;   /* 全部右→左飞，机头/朝向朝左（小鸟 V 形对称，翻转无影响） */
    switch (type) {
    case 0:   /* 飞机：银白机身 + 红条纹 + 蓝舷窗 + 上下机翼 */
        flyer_rect(cont, 30, 9, PIX_PLANE, PIX_BLACK, 2, 9, 11, mirror);
        flyer_rect(cont, 8, 7, PIX_PLANE, PIX_BLACK, 2, 4, 9, mirror);
        flyer_rect(cont, 16, 4, PIX_PLANE, PIX_BLACK, 2, 16, 6, mirror);
        flyer_rect(cont, 8, 6, PIX_PLANE, PIX_BLACK, 2, 16, 17, mirror);
        flyer_rect(cont, 6, 5, PIX_WIN, PIX_BLACK, 2, 30, 12, mirror);
        flyer_rect(cont, 30, 3, PIX_RED, PIX_BLACK, 0, 9, 17, mirror);
        break;
    case 1:   /* 飞碟：梯形碟身 + 玻璃罩 + 三盏黄灯 */
        flyer_rect(cont, 18, 5, PIX_UFO, PIX_BLACK, 2, 15, 8, mirror);
        flyer_rect(cont, 30, 5, PIX_UFO, PIX_BLACK, 2, 9, 12, mirror);
        flyer_rect(cont, 40, 6, PIX_UFO, PIX_BLACK, 2, 4, 16, mirror);
        flyer_rect(cont, 14, 8, PIX_DOME, PIX_BLACK, 2, 17, 2, mirror);
        flyer_rect(cont, 5, 4, PIX_YELLOW, PIX_BLACK, 0, 10, 17, mirror);
        flyer_rect(cont, 5, 4, PIX_YELLOW, PIX_BLACK, 0, 21, 17, mirror);
        flyer_rect(cont, 5, 4, PIX_YELLOW, PIX_BLACK, 0, 32, 17, mirror);
        break;
    case 2:   /* 超人：红披风 + 蓝身体 + 肤色头 + 黄胸标 */
        flyer_rect(cont, 14, 20, PIX_CAPE, PIX_BLACK, 2, 4, 5, mirror);
        flyer_rect(cont, 14, 18, PIX_HERO, PIX_BLACK, 2, 20, 6, mirror);
        flyer_rect(cont, 12, 8, PIX_SKIN, PIX_BLACK, 2, 23, 0, mirror);
        flyer_rect(cont, 8, 4, PIX_YELLOW, PIX_BLACK, 0, 28, 11, mirror);
        break;
    default:  /* 小鸟：简笔 V 形飞鸟（也右→左飞） */
        flyer_rect(cont, 20, 4, PIX_BLACK, PIX_BLACK, 0, 6, 8, mirror);
        flyer_rect(cont, 8, 4, PIX_BLACK, PIX_BLACK, 0, 2, 5, mirror);
        flyer_rect(cont, 8, 4, PIX_BLACK, PIX_BLACK, 0, 22, 5, mirror);
        flyer_rect(cont, 8, 4, PIX_BLACK, PIX_BLACK, 0, 12, 11, mirror);
        break;
    }
}

/* 像素小猪（朝左飞：猪鼻在左、翅膀在右），用方块拼出造型 */
static void make_pig_content(lv_obj_t *cont)
{
    lv_obj_clean(cont);
    lv_obj_t *o;
    o = make_pixel_rect(cont, 26, 18, PIX_PIG,    PIX_PIG_DK, 2); lv_obj_set_pos(o, 11, 7);   /* 身体 */
    o = make_pixel_rect(cont, 9,  9,  PIX_PIG,    PIX_PIG_DK, 2); lv_obj_set_pos(o, 4, 11);   /* 猪鼻（凸向左） */
    o = make_pixel_rect(cont, 3,  3,  PIX_PIG_DK, PIX_PIG_DK, 0); lv_obj_set_pos(o, 6, 12);   /* 鼻孔 */
    o = make_pixel_rect(cont, 3,  3,  PIX_PIG_DK, PIX_PIG_DK, 0); lv_obj_set_pos(o, 6, 16);   /* 鼻孔 */
    o = make_pixel_rect(cont, 7,  6,  PIX_PIG,    PIX_PIG_DK, 2); lv_obj_set_pos(o, 15, 3);   /* 耳朵 */
    o = make_pixel_rect(cont, 7,  6,  PIX_PIG,    PIX_PIG_DK, 2); lv_obj_set_pos(o, 24, 3);   /* 耳朵 */
    o = make_pixel_rect(cont, 4,  4,  PIX_BLACK,  PIX_BLACK,  0); lv_obj_set_pos(o, 19, 12);  /* 眼睛 */
    o = make_pixel_rect(cont, 12, 5,  PIX_CLOUD,  PIX_BLACK,  2); lv_obj_set_pos(o, 30, 9);   /* 翅膀 */
    o = make_pixel_rect(cont, 9,  4,  PIX_CLOUD,  PIX_BLACK,  2); lv_obj_set_pos(o, 33, 4);   /* 翅膀（上） */
}

/* 飞行物宽度（猪比其余略宽） */
static int sky_w(int type) { return (type == 4) ? PIG_W : FLYER_W; }

/* 根据类型重绘造型（猪用猪的绘制，其余用飞行物绘制） */
static void sky_draw(lv_obj_t *obj, int type)
{
    if (type == 4) {
        make_pig_content(obj);
    } else {
        make_flyer_content(obj, type);
    }
}

/* 从右侧随机生成一个天空飞行物（含小猪），一次一只 */
static void sky_respawn(void)
{
    sky_t *f = &s_sky;
    f->type = (int)lv_rand(0, 5);                 /* 0~4：飞机/飞碟/超人/小鸟/小猪 */
    const int w = sky_w(f->type);
    f->x  = (float)FIELD_W + (float)w;            /* 从右侧进入（右→左飞） */
    f->y  = (float)lv_rand(40, 210);
    f->vx = -(0.5f + (float)lv_rand(0, 130) / 100.0f);  /* 右→左：-0.5 ~ -1.8 */
    f->bob = (float)lv_rand(0, 628) / 100.0f;
    f->cooldown = 0;
    lv_obj_set_size(f->obj, w, (f->type == 4) ? PIG_H : FLYER_H);
    sky_draw(f->obj, f->type);
    lv_obj_clear_flag(f->obj, LV_OBJ_FLAG_HIDDEN);
}

static void init_sky(void)
{
    lv_obj_t *c = lv_obj_create(s_scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, FLYER_W, FLYER_H);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    s_sky.obj = c;
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    s_sky.cooldown = (int)lv_rand(20, 120);       /* 进场后稍等再首只出现 */
}

/* 每帧推进：当前飞行物右→左移动；飞出左界则隐藏并随机冷却，倒计时再重生 */
static void animate_sky(void)
{
    sky_t *f = &s_sky;
    if (f->cooldown > 0) {
        f->cooldown--;
        if (f->cooldown == 0) {
            sky_respawn();
        }
        return;
    }
    f->x += f->vx;
    f->bob += 0.05f;
    const float yy = f->y + sinf(f->bob) * 3.0f;
    const int w = sky_w(f->type);
    if (f->x < -(float)w) {
        lv_obj_add_flag(f->obj, LV_OBJ_FLAG_HIDDEN);
        f->cooldown = 60 + (int)lv_rand(0, 180);  /* 飞走后等 1~4 秒再下一只 */
        return;
    }
    lv_obj_set_pos(f->obj, (lv_coord_t)f->x, (lv_coord_t)yy);
}

/* 半截水管（像素风）：主体 + 浅绿高光 + 暗绿阴影 + 管口帽，cap 贴向间隙端 */
static lv_obj_t *build_pipe_half(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, bool cap_at_bottom)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, w, h);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *body = make_pixel_rect(cont, w, h, PIX_PIPE, PIX_PIPE_DK, 2);
    lv_obj_set_pos(body, 0, 0);
    lv_obj_t *hl = make_pixel_rect(cont, 5, h - 4, PIX_PIPE_LT, PIX_PIPE_DK, 0);
    lv_obj_set_pos(hl, 4, 2);
    lv_obj_t *sh = make_pixel_rect(cont, 5, h - 4, PIX_PIPE_DK, PIX_PIPE_DK, 0);
    lv_obj_set_pos(sh, w - 9, 2);
    lv_obj_t *cap = make_pixel_rect(cont, w + 10, CAP_H, PIX_PIPE, PIX_PIPE_DK, 2);
    lv_obj_set_pos(cap, -5, cap_at_bottom ? (h - CAP_H) : 0);
    return cont;
}

static void reset_game(void)
{
    s_bird_y = FIELD_H / 2.0f;
    s_vy = 0;
    s_score = 0;
    s_level = 1;
    s_ticks = 0;
    s_toast_hide_at = 0;
    s_state = ST_PLAY;
    apply_difficulty();
    s_last_pipe_x = FIELD_W - s_pipe_spacing; /* 让第一根水管立即在右侧生成 */

    for (int i = 0; i < MAX_PIPES; i++) {
        if (s_pipes[i].active) {
            lv_obj_del(s_pipes[i].top);
            lv_obj_del(s_pipes[i].bot);
            s_pipes[i].active = false;
        }
    }
    spawn_pipe();

    if (s_score_lbl) {
        lv_label_set_text(s_score_lbl, SDG_T("分数: 0", "Score: 0"));
    }
    if (s_level_lbl) {
        lv_label_set_text(s_level_lbl, SDG_T("第1关", "Lv1"));
    }
    if (s_msg) {
        lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_toast) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_over_hint) {
        lv_obj_add_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);  /* 开始游戏后隐藏结束提示 */
    }
    lv_obj_set_pos(s_bird, BIRD_X - 1, (lv_coord_t)s_bird_y);
}

static void spawn_pipe(void)
{
    pipe_t *p = NULL;
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!s_pipes[i].active) {
            p = &s_pipes[i];
            break;
        }
    }
    if (!p) {
        return;
    }
    p->active = true;
    p->x = FIELD_W;
    p->gap_y = (int)lv_rand((int32_t)(s_gap_h / 2 + 25),
                            (int32_t)(FIELD_H - s_gap_h / 2 - 25));
    p->scored = false;

    const int top_h = p->gap_y - s_gap_h / 2;
    const int bot_y = p->gap_y + s_gap_h / 2;
    const int bot_h = FIELD_H - bot_y;

    p->top = build_pipe_half(s_scr, PIPE_W, top_h, true);
    lv_obj_set_pos(p->top, (lv_coord_t)p->x, 0);
    p->bot = build_pipe_half(s_scr, PIPE_W, bot_h, false);
    lv_obj_set_pos(p->bot, (lv_coord_t)p->x, bot_y);

    s_last_pipe_x = p->x;
}

static void flappy_tick(lv_timer_t *t)
{
    if (s_paused) {
        return;
    }
    (void)t;
    if (!s_scr) {
        return;
    }
    animate_sky();                    /* 天空飞行物/小猪：一次一只，右→左飞 */
    if (s_state != ST_PLAY) {
        return;
    }
    s_ticks++;

    /* 小鸟重力物理 */
    s_vy += GRAVITY;
    if (s_vy > MAX_VY) {
        s_vy = MAX_VY;
    }
    s_bird_y += s_vy;
    if (s_bird_y < 0) {
        s_bird_y = 0;
    }
    if (s_bird_y > FIELD_H - BIRD_SIZE) {
        s_bird_y = FIELD_H - BIRD_SIZE;
    }
    lv_obj_set_pos(s_bird, BIRD_X - 1, (lv_coord_t)s_bird_y);

    /* 水管左移 + 计分 + 出界回收；同时记录最右侧（最大 x）活动水管 */
    float rightmost = -1e9f;
    for (int i = 0; i < MAX_PIPES; i++) {
        pipe_t *p = &s_pipes[i];
        if (!p->active) {
            continue;
        }
        p->x -= s_pipe_speed;
        if (p->x + PIPE_W < 0) {
            lv_obj_del(p->top);
            lv_obj_del(p->bot);
            p->active = false;
            continue;
        }
        lv_obj_set_pos(p->top, (lv_coord_t)p->x, 0);
        lv_obj_set_pos(p->bot, (lv_coord_t)p->x, p->gap_y + s_gap_h / 2);

        if (p->x > rightmost) {
            rightmost = p->x;
        }

        if (!p->scored && (p->x + PIPE_W) < BIRD_X) {
            p->scored = true;
            s_score++;
            char buf[32];
            snprintf(buf, sizeof(buf), SDG_T("分数: %d", "Score: %d"), s_score);
            lv_label_set_text(s_score_lbl, buf);

            /* 每通过 PIPES_PER_LEVEL 根水管升一关，提升难度并提示 */
            const int new_level = s_score / PIPES_PER_LEVEL + 1;
            if (new_level > s_level) {
                s_level = new_level;
                apply_difficulty();
                if (s_level_lbl) {
                    char lbuf[32];
                    snprintf(lbuf, sizeof(lbuf), SDG_T("第%d关", "Lv%d"), s_level);
                    lv_label_set_text(s_level_lbl, lbuf);
                }
                if (s_toast) {
                    char tbuf[16];
                    snprintf(tbuf, sizeof(tbuf), SDG_T("第%d关", "Lv%d"), s_level);
                    lv_label_set_text(s_toast, tbuf);
                    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
                    s_toast_hide_at = s_ticks + 75; /* 约1.2秒后隐藏 */
                }
            }
        }
    }

    /* 间隔生成新水管：当最右侧水管已左移到足够远（屏宽-间距以内）就在右侧补一根，
       若当前没有活动水管则立即补一根，避免断流 */
    s_last_pipe_x = (rightmost > 0) ? rightmost : FIELD_W;
    if (rightmost < 0 || s_last_pipe_x <= (FIELD_W - s_pipe_spacing)) {
        spawn_pipe();
    }

    /* 升级提示自动隐藏 */
    if (s_toast_hide_at && s_ticks >= s_toast_hide_at) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        s_toast_hide_at = 0;
    }

    /* 碰撞检测 */
    bool dead = (s_bird_y <= 0 || s_bird_y >= FIELD_H - BIRD_SIZE);
    for (int i = 0; i < MAX_PIPES && !dead; i++) {
        pipe_t *p = &s_pipes[i];
        if (!p->active) {
            continue;
        }
        int px = (int)p->x;
        if (BIRD_X < px + PIPE_W && BIRD_X + BIRD_SIZE > px) {
            if (s_bird_y < p->gap_y - s_gap_h / 2) {
                dead = true;
            } else if (s_bird_y + BIRD_SIZE > p->gap_y + s_gap_h / 2) {
                dead = true;
            }
        }
    }

    if (dead) {
        s_state = ST_OVER;
        char buf[64];
        snprintf(buf, sizeof(buf), SDG_T("游戏结束\n分数: %d 第%d关",
                                              "Game Over\nScore: %d  Lv%d"),
                 s_score, s_level);
        lv_label_set_text(s_msg, buf);
        lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_msg);
        if (s_over_hint) {
            lv_label_set_text(s_over_hint, SDG_T("点击重玩\n按键返回", "Tap to retry\nKey to go back"));
            lv_obj_clear_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_over_hint);
        }
    }
}

/* 拍翅：给小鸟一个向上速度，并触发“拍翅”音效 */
static void flap(void)
{
    s_vy = JUMP_V;
    sdgoods_audio_sfx_flap();
}

static void on_tap(lv_event_t *e)
{
    if (s_paused) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        if (!s_scr) {
            return;
        }
        if (s_state == ST_PLAY) {
            flap();                         /* 游戏中：按下即拍翅 */
        } else if (s_state == ST_READY) {
            reset_game();                   /* 准备态：点按开始游戏 */
        }
        /* ST_OVER：松手时轻点即重玩（见 RELEASED） */
    }
    else if (code == LV_EVENT_RELEASED) {
        if (!s_scr) {
            return;
        }
        if (s_state == ST_OVER) {
            reset_game();                   /* 游戏结束：轻点屏幕重玩 */
        }
    }
}

static void close_to_app(void)
{
    if (!s_scr) {
        return;
    }
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    sdgoods_audio_bgm_stop();   /* 退出游戏时停止背景音乐 */
    lv_obj_t *gone = s_scr;
    s_scr = NULL;
    s_bird = NULL;
    s_score_lbl = NULL;
    s_level_lbl = NULL;
    s_toast = NULL;
    s_msg = NULL;
    s_over_hint = NULL;
    for (int i = 0; i < MAX_PIPES; i++) {
        s_pipes[i].active = false;
        s_pipes[i].top = NULL;
        s_pipes[i].bot = NULL;
    }
    lv_obj_del(gone);
}

void ui_flappy_start(void)
{
    if (s_scr) {
        return;
    }
    s_key_down = false;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, PIX_SKY, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 背景像素装饰：远山 + 底部草地（云朵已按需求删除） */
    make_mountain(s_scr, 250, 120, 70, PIX_MTN2);   /* 次远山（更亮） */
    make_mountain(s_scr, 120, 160, 100, PIX_MTN);    /* 主远山（更暗） */
    lv_obj_t *ground = make_pixel_rect(s_scr, FIELD_W, 14, PIX_GROUND, PIX_BLACK, 2);
    lv_obj_set_pos(ground, 0, FIELD_H - 14);

    /* 天空飞行物（飞机/飞碟/超人/小鸟/小猪），统一顺序出现、右→左飞，位于云朵之后、水管之前 */
    init_sky();

    /* 全屏透明按钮，用于接收点按（小鸟/水管不可点击，点按会透传到这里） */
    lv_obj_t *tap = lv_btn_create(s_scr);
    lv_obj_set_size(tap, FIELD_W, FIELD_H);
    lv_obj_set_pos(tap, 0, 0);
    lv_obj_set_style_radius(tap, 0, 0);
    lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap, 0, 0);
    lv_obj_set_style_shadow_width(tap, 0, 0);
    lv_obj_add_event_cb(tap, on_tap, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(tap, on_tap, LV_EVENT_RELEASED, NULL);   /* 松手判断：左滑返回 / 轻点重玩 */

    /* 小鸟（像素方块造型） */
    s_bird = make_bird(s_scr);

    for (int i = 0; i < MAX_PIPES; i++) {
        s_pipes[i].active = false;
        s_pipes[i].top = NULL;
        s_pipes[i].bot = NULL;
    }

    s_score_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_score_lbl, SDG_T("分数: 0", "Score: 0"));
    lv_obj_set_style_text_font(s_score_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_score_lbl, lv_color_white(), 0);
    lv_obj_clear_flag(s_score_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_MID, 0, 12);

    /* 关卡指示（左上角） */
    s_level_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_level_lbl, SDG_T("第1关", "Lv1"));
    lv_obj_set_style_text_font(s_level_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_level_lbl, lv_color_hex(0xFFEC27), 0);
    lv_obj_clear_flag(s_level_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_level_lbl, LV_ALIGN_TOP_LEFT, 12, 12);

    /* 升级提示（居中大字，短暂显示） */
    s_toast = lv_label_create(s_scr);
    lv_label_set_text(s_toast, "");
    lv_obj_set_style_text_font(s_toast, &cn_font_16, 0);
    lv_obj_set_style_text_color(s_toast, lv_color_hex(0xFFEC27), 0);
    lv_obj_set_style_text_align(s_toast, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_toast, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    s_msg = lv_label_create(s_scr);
    lv_label_set_text(s_msg, "");
    lv_obj_set_width(s_msg, FIELD_W - 60);
    lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_msg, &cn_font_16, 0);
    lv_obj_set_style_text_color(s_msg, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_msg, LV_ALIGN_CENTER, 0, -34);
    lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);

    /* 接入「应用标准框架」：顶部下滑弹菜单 + 电源键离开(回 home)。
     * 音量调节移到菜单内(共享音量)，故此处不再创建游戏内音量按钮。 */
    sdgoods_app_shell_set_exit_cb(close_to_app);
    sdgoods_app_shell_set_pause_cb(on_pause);
    sdgoods_app_shell_set_resume_cb(on_resume);
    sdgoods_app_shell_bind(s_scr);

    /* 游戏结束提示（屏幕中间下方）：「点击重玩 / 按键返回」，与居中的结算文案分离 */
    s_over_hint = lv_label_create(s_scr);
    lv_label_set_text(s_over_hint, "");
    lv_obj_set_width(s_over_hint, FIELD_W - 60);
    lv_label_set_long_mode(s_over_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_over_hint, &cn_font_16, 0);
    lv_obj_set_style_text_color(s_over_hint, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_over_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(s_over_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_over_hint, LV_ALIGN_CENTER, 0, 30);   /* 结算文案下方，整块居中显示 */
    lv_obj_add_flag(s_over_hint, LV_OBJ_FLAG_HIDDEN);

    /* 进入“准备”状态：显示分数与音量按钮，等待玩家点按开始；点音量按钮不会触发开始 */
    s_state = ST_READY;
    s_score = 0;
    s_level = 1;
    s_ticks = 0;
    s_bird_y = FIELD_H / 2.0f;
    lv_obj_set_pos(s_bird, BIRD_X - 1, (lv_coord_t)s_bird_y);
    if (s_score_lbl) lv_label_set_text(s_score_lbl, SDG_T("分数: 0", "Score: 0"));
    if (s_level_lbl) lv_label_set_text(s_level_lbl, SDG_T("第1关", "Lv1"));
    if (s_msg) {
        lv_label_set_text(s_msg, SDG_T("点击屏幕开始", "Tap to start"));
        lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    }

    s_timer = lv_timer_create(flappy_tick, 16, NULL);
    lv_scr_load(s_scr);

    sdgoods_audio_bgm_start(AUDIO_BGM_THEME_FLAPPY);   /* 进入游戏即播放背景音乐（失败也不影响游戏运行） */
}

void ui_flappy_poll(void)
{
    if (!s_scr) {
        return;
    }
    /* 电源键短按/长按统一由 sdgoods_input.c 的 sdgoods_power_key_poll 处理 */
}
