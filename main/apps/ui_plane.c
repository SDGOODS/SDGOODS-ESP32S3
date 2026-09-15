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

#include "ui_plane.h"

#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "st77916.h"
#include "ui_app_page.h"
#include "audio_recplay.h"
#include "ui_app_shell.h"
#include "plane_net.h"
#include "touch_input.h"   /* touch_input_get_state / get_point：直接轮询触摸，绕开按钮事件不可靠问题 */
#include "esp_log.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(cn_font_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(cn_font_16);

#define FIELD_W     LCD_WIDTH
#define FIELD_H     LCD_HEIGHT

/* 像素风调色板（PICO-8 风，方角 + 深色描边） */
#define PIX_SPACE   lv_color_hex(0x0B1026)
#define PIX_BLACK   lv_color_hex(0x000000)
#define PIX_PLANE   lv_color_hex(0x2E9BFF)   /* 主飞机机身（蓝） */
#define PIX_RED     lv_color_hex(0xFF004D)   /* 红色描边/敌弹 */
#define PIX_RED_TXT lv_color_hex(0xFF2D2D)   /* 结束界面文字（正红） */
#define PIX_WIN     lv_color_hex(0x3B82F6)   /* 座舱（蓝） */
#define PIX_YELLOW  lv_color_hex(0xFFEC27)   /* 玩家子弹（黄） */
#define PIX_ENEMY   lv_color_hex(0x8371D4)   /* 敌机（紫） */
#define PIX_ENEMY2  lv_color_hex(0xFF6B6B)   /* 大型敌机（红） */
#define PIX_STAR    lv_color_hex(0xFFFFFF)   /* 星点 */
#define PIX_PEER    lv_color_hex(0xFF6FB5)   /* 从飞机机身（粉） */
#define PIX_ITEM_FIRE lv_color_hex(0xFFB300) /* 道具「火力」（琥珀） */
#define PIX_ITEM_BOMB lv_color_hex(0x45E0FF) /* 道具「炸弹」（青） */
#define PIX_ITEM_LIFE lv_color_hex(0xFFD6E2) /* 道具「生命」底色（浅粉） */
#define PIX_HEART     lv_color_hex(0xFF3D5A) /* 生命爱心（红） */
#define PIX_HEART_DIM lv_color_hex(0x4A2A36) /* 已失去的爱心（暗） */
#define PIX_LIFE_TXT  lv_color_hex(0xE0143C) /* 道具「生命」上的爱心字色（深红） */
#define PIX_POWER_OFF lv_color_hex(0x8A7A45) /* 火力已攒下但未生效时的 HUD 字色（暗琥珀） */

typedef enum {
    ST_READY,   /* 准备：等待点按开始 */
    ST_PLAY,
    ST_OVER
} game_state_t;

typedef struct {
    bool       active;
    lv_obj_t  *obj;
    int        x;       /* 中心 x */
    int        y;       /* 中心 y */
    float      vy;      /* 下落速度 */
    int        hp;
    int        pts;
    int        fire_cd; /* 敌弹发射冷却（tick） */
} enemy_t;

typedef struct {
    bool      active;
    lv_obj_t *obj;
    int       x;
    int       y;
    int       vx;   /* 横向速度（像素/tick）：当前所有子弹都是直射（0），字段保留备用 */
} bul_t;

#define MAX_ENEMY 12
/* 子弹槽位要留足：4 列火力同时在场约 14 发，取 24 保证「本机」与「对端模拟」两侧
   都不会因槽满而漏发 —— 两侧子弹集必须完全一致，否则敌机集会被打散。 */
#define MAX_PBUL  24
#define MAX_EBUL  18
#define MAX_PEER_BUL 24  /* 对端（队友）子弹：由对方 fire_seq 边沿触发本地模拟，参与碰撞以保证双方敌机一致 */

/* ============================ 道具 ============================
 * 掉落必须走共享 PRNG（与敌机同款确定性消费），炸弹清屏必须广播 bomb_seq，
 * 否则会破坏双方「敌机一致」。拾取只影响本机（道具在本机屏上消失）。 */
#define MAX_ITEM       3
#define ITEM_W         26
#define ITEM_H         26
#define ITEM_DROP_CD   340     /* 掉落间隔（tick≈16ms，约 5.5s） */
#define POWER_TICKS    375     /* 单次火力持续（tick≈16ms，约 6s；比最初的 15s 明显缩短） */
#define POWER_MAX_LEVEL 3      /* 火力等级上限：吃一个「火」+1 级（=多一列子弹） */
#define POWER_COL_MAX   4      /* 火力最多同时发 4 列子弹（1 + POWER_MAX_LEVEL） */
#define POWER_COL_STEP  12     /* 相邻两列子弹的横向间距（px） */
#define TOAST_TICKS     75     /* 拾取道具后的短暂提示显示时长（约 1.2s） */
#define ITEM_FIRE      0       /* 火力增强：多发一列子弹（可叠加到 4 列） */
#define ITEM_BOMB      1       /* 全屏清除炸弹 */
#define ITEM_LIFE      2       /* 生命爱心：+1 条命（上限 MAX_LIVES） */

/* 生命心（HUD 显示「♥」个数）：初始 1 条命，最多 MAX_LIVES */
#define MAX_LIVES      3
#define INV_TICKS      120     /* 受击后的无敌时间（tick≈16ms，约 2s；期间闪烁且不再受击） */
#define LIFE_GLYPH_SP  20      /* HUD 每颗心占宽（px），心用「♥」字形绘制 */

typedef struct {
    bool      active;
    lv_obj_t *obj;
    int       kind;   /* ITEM_FIRE / ITEM_BOMB */
    int       x;
    int       y;
} item_t;

/* 玩家（精灵边长 = 11 * SPR_SCALE） */
#define PLANE_W 33
#define PLANE_H 33
static lv_obj_t   *s_scr = NULL;
static lv_obj_t   *s_plane = NULL;
static int         s_px = FIELD_W / 2;
static int         s_py = FIELD_H - 95;
static lv_obj_t   *s_score_lbl = NULL;
static lv_obj_t   *s_level_lbl = NULL;
static lv_obj_t   *s_msg = NULL;
static lv_timer_t *s_timer = NULL;
static game_state_t s_state;
static int         s_score;
static int         s_my_score;       /* 联机：本机飞机自己的击落分（单独上报给对端，显示为「我」）；s_score 仍为总贡献 */
static int         s_level;
static int         s_ticks;
static bool        s_key_down;
static bool        s_paused;

/* ============================ 联机（多人合作） ============================ */
static int         s_mode;          /* 0=单人 1=多人 */
static uint32_t    s_prng;          /* 确定性随机状态（多人共享种子） */
static lv_obj_t   *s_peer_plane;    /* 对方飞机精灵（绿） */
static lv_obj_t   *s_peer_lbl;      /* 对方分数标签 */
static lv_obj_t   *s_btn_single;    /* READY 界面「单人」按钮 */
static lv_obj_t   *s_btn_multi;     /* READY 界面「双人」按钮 */
static plane_peer_state_t s_peer_state;  /* 对端最新状态镜像 */
static bool        s_started_by_peer;    /* 被对方握手自动开局 */
static uint8_t     s_fire_seq;           /* 本机开火序号（每发出一发玩家子弹 +1，随状态包上报） */
static uint8_t     s_peer_fire_seq;      /* 上次收到的对方 fire_seq（用于边沿检测生成本地对端子弹） */
static uint8_t     s_restart_seq;        /* 本机重玩序号（死亡方点击重玩时 +1 并广播） */
static uint8_t     s_peer_restart_seq;   /* 上次收到的对方 restart_seq（边沿检测驱动对端同步重开） */
static bul_t       s_peer_bul[MAX_PEER_BUL];  /* 对端子弹（本地模拟，参与碰撞，保证双方敌机一致） */
static void spawn_peer_bul_at(int x, int y, int vx);  /* 在绿机位置生成一发对端子弹（前向声明） */
static void update_peer_bullets(void);        /* 移动/回收/碰撞对端子弹（前向声明） */
static void over_tap_poll(void);              /* 结束界面轮询触摸触发重开（前向声明） */

/* ---- 道具状态 ---- */
static item_t      s_item[MAX_ITEM];     /* 场上掉落的道具（双方确定性同步生成） */
static int         s_item_cd;            /* 下一次掉落倒计时（tick） */
static int         s_power_ticks;        /* 本机火力增强剩余 tick（>0 生效） */
static int         s_power_level;        /* 本机火力等级 0..POWER_MAX_LEVEL（子弹列数 = 1 + 等级） */
static uint8_t     s_bomb_seq;           /* 本机炸弹使用序号（随状态包广播） */
static uint8_t     s_peer_bomb_seq;      /* 上次收到的对端炸弹序号（边沿检测驱动同步清屏） */
static lv_obj_t   *s_power_lbl;          /* HUD：火力等级 + 倒计时 */
static int         s_power_sec_shown;    /* HUD 已显示的剩余秒数（避免每帧重绘） */
static int         s_power_hud_mode = -1; /* HUD 火力显示模式：0=隐藏 1=生效中(琥珀+秒数) 2=已攒下(暗色，无秒数) */
static lv_obj_t   *s_toast_lbl;          /* HUD：拾取道具的短暂提示（约 1.2s 后自动隐藏） */
static int         s_toast_ticks;        /* 提示剩余 tick */
static int         s_lives;              /* 本机剩余生命（HUD 用「♥」显示；初始 1，最多 MAX_LIVES） */
static int         s_inv_ticks;          /* 受击后无敌剩余 tick（>0 时飞机闪烁且免伤） */
static lv_obj_t   *s_heart_box;          /* HUD 容器：生命心一排（顶部居中） */
static lv_obj_t   *s_heart_lbl[MAX_LIVES]; /* HUD 生命心（「♥」字形：满=红 / 已失去=暗） */
static void refresh_hearts(void);        /* 刷新生命心显示（前向声明） */
static void despawn_item(int i);
static void update_items(void);
static void apply_item(int kind);
static void clear_all_enemies(bool award);   /* 全屏清除敌机（award=是否给本机计分） */
static void refresh_score_label(void);
static bool        s_waiting_conn;       /* 多人：正在搜索/等待对方连接 */
static int         s_wait_ticks;        /* 等待连接计时（tick） */
static bool        s_ready_prev_pressed; /* READY 界面轮询触摸：上一次按压状态（用于一次按压只触发一次） */
static bool        s_over_prev_pressed;  /* 结束界面轮询触摸：上一次按压状态（用于一次按压只触发一次重开） */
static int         s_over_msg_phase;     /* 结束界面提示阶段：-1=未初始化 0=等待对方 1=双方阵亡可重开（仅联机用，防每帧重复 set_text） */
static bool        s_peer_state_valid;   /* 是否至少收到过一次对端状态包：用于区分「对端未知/还活着」与「对端已确认阵亡」，避免未收到包时误判可重开 */
static uint32_t     s_hb_ticks;          /* 心跳计时（每 ~5s 打印一次，确认固件/串口存活） */

/* 确定性 PRNG（xorshift32），保证双方敌机布局一致 */
static inline uint32_t prng_next(void)
{
    uint32_t x = s_prng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s_prng = x;
    return x;
}
/* 多人用确定性随机；单人保持原 lv_rand 行为不变 */
static int my_rand(int lo, int hi)
{
    if (s_mode == 1) {
        return (int)(prng_next() % (uint32_t)(hi - lo)) + lo;
    }
    return (int)lv_rand(lo, hi);
}

static enemy_t     s_enemies[MAX_ENEMY];
static bul_t       s_pbul[MAX_PBUL];
static bul_t       s_ebul[MAX_EBUL];
static int         s_fire_cd;
static int         s_spawn_cd;

static void reset_game(void);
static void request_restart(void);  /* 死亡方点击重玩：本机重开并广播，对端同步重开 */
static void spawn_enemy(void);
static void update_peer_plane(void);
static void ready_tap_poll(void);   /* READY 界面：直接轮询触摸判定模式选择/开始 */
static void on_net_conn(bool connected);          /* 蓝牙连接状态回调（前向声明，定义见下文） */
static void on_net_recv(const plane_peer_state_t *peer);  /* 蓝牙数据回调（前向声明） */
static void on_tap(lv_event_t *e);
static void close_to_app(void);

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

/* ============================ 像素精灵 ============================
 * 用「像素图(字符地图) → canvas 真彩带透明位图」绘制真正的像素风精灵，
 * 避免大色块 + 描边造成的"蜡笔涂鸦"观感。
 * '.'=透明  'K'=描边黑  'W'=机身银白  'B'=座舱蓝  'R'=红色  'E'=敌机红  'Y'=黄
 * ================================================================= */
#define SPR_SCALE   3                 /* 每个逻辑像素 = 3x3 屏幕像素 */
#define SPR_MAX_PX  33                /* 精灵最大边长（11 * 3） */
#define SPR_BUF_SZ  (SPR_MAX_PX * SPR_MAX_PX * 3)   /* TRUE_COLOR_ALPHA: 2B 颜色 + 1B alpha */

/* 玩家飞机：机头朝上（11x11） */
static const char *const SPR_PLAYER[11] = {
    ".....K.....",
    "....KWK....",
    "....KWK....",
    "...KWWWK...",
    "...KBWBK...",
    "..KWWBWWK..",
    ".KWWWWWWWK.",
    "KWWWWWWWWWK",
    "KRKWWWWWKRK",
    ".K.RWWWR.K.",
    "...K...K...",
};

/* 小型敌机：机头朝下（9x9，紫） */
static const char *const SPR_ENEMY0[9] = {
    "KK.....KK",
    "KWK...KWK",
    ".KWK.KWK.",
    ".KWWKWWK.",
    "KWWWRWWWK",
    ".KWWWWWK.",
    "..KWWWK..",
    "...KRK...",
    "....K....",
};

/* 大型敌机：机头朝下（11x11，红） */
static const char *const SPR_ENEMY1[11] = {
    "KKK.....KKK",
    "KEEK...KEEK",
    ".KEEK.KEEK.",
    ".KEEEEEEEK.",
    "KEEEYKYEEEK",
    "KEEEYYYEEEK",
    ".KEEEEEEEK.",
    "..KEEEEEK..",
    "...KEEEK...",
    "..K.EKE.K..",
    "....K.K....",
};

/* 对方飞机：与玩家同形，机身用绿(G)区分 */
static const char *const SPR_PEER[11] = {
    ".....K.....",
    "....KGK....",
    "....KGK....",
    "...KGGGK...",
    "...KGBGK...",
    "..KGGBGGK..",
    ".KGGGGGGGK.",
    "KGGGGGGGGK",
    "KRKGGGGGKRK",
    ".K.RGGR.K.",
    "...K...K...",
};

static lv_color_t spr_color(char c)
{
    switch (c) {
    case 'K': return lv_color_hex(0x101820);
    case 'W': return PIX_PLANE;
    case 'B': return PIX_WIN;
    case 'R': return PIX_RED;
    case 'E': return PIX_ENEMY2;
    case 'Y': return PIX_YELLOW;
    case 'G': return PIX_PEER;
    default:  return PIX_ENEMY;
    }
}

/* 把像素图渲染进 canvas（带 alpha，透明处透出星空背景） */
static lv_obj_t *make_sprite(lv_obj_t *parent, uint8_t *buf,
                            const char *const *map, int w, int h)
{
    int pw = w * SPR_SCALE;
    int ph = h * SPR_SCALE;
    memset(buf, 0, (size_t)pw * ph * 3);   /* 全透明 */

    lv_obj_t *cv = lv_canvas_create(parent);
    lv_obj_remove_style_all(cv);
    lv_canvas_set_buffer(cv, buf, pw, ph, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_obj_clear_flag(cv, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cv, LV_OBJ_FLAG_SCROLLABLE);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            char c = map[y][x];
            if (c == '.') {
                continue;
            }
            lv_color_t col = spr_color(c);
            for (int sy = 0; sy < SPR_SCALE; sy++) {
                for (int sx = 0; sx < SPR_SCALE; sx++) {
                    int px = x * SPR_SCALE + sx;
                    int py = y * SPR_SCALE + sy;
                    lv_canvas_set_px_color(cv, px, py, col);
                    lv_canvas_set_px_opa(cv, px, py, LV_OPA_COVER);
                }
            }
        }
    }
    return cv;
}

static uint8_t s_spr_player_buf[SPR_BUF_SZ];
static uint8_t s_spr_enemy_buf[MAX_ENEMY][SPR_BUF_SZ];

/* 玩家飞机（像素精灵） */
static lv_obj_t *make_player_plane(lv_obj_t *parent)
{
    return make_sprite(parent, s_spr_player_buf, SPR_PLAYER, 11, 11);
}

/* 敌机：type 0=小型(紫,1血) 1=大型(红,2血)；slot 用于取该槽专属显存 */
static lv_obj_t *make_enemy_plane(lv_obj_t *parent, int type, int slot)
{
    if (type == 0) {
        return make_sprite(parent, s_spr_enemy_buf[slot], SPR_ENEMY0, 9, 9);
    }
    return make_sprite(parent, s_spr_enemy_buf[slot], SPR_ENEMY1, 11, 11);
}

/* 对方飞机（绿色像素精灵），用独立显存 */
static uint8_t s_spr_peer_buf[SPR_BUF_SZ];
static lv_obj_t *make_peer_plane(lv_obj_t *parent)
{
    return make_sprite(parent, s_spr_peer_buf, SPR_PEER, 11, 11);
}

static void despawn_enemy(int i)
{
    if (s_enemies[i].obj) {
        lv_obj_del(s_enemies[i].obj);
        s_enemies[i].obj = NULL;
    }
    s_enemies[i].active = false;
}

static void despawn_bul(bul_t *b)
{
    if (b->obj) {
        lv_obj_del(b->obj);
        b->obj = NULL;
    }
    b->active = false;
}

/* 把玩家飞机移到触摸点（限制在可玩区域内） */
static void move_plane(int x, int y)
{
    if (!s_plane) {
        return;
    }
    if (x < 15) x = 15;
    if (x > FIELD_W - 15) x = FIELD_W - 15;
    if (y < 90) y = 90;
    if (y > FIELD_H - 20) y = FIELD_H - 20;
    s_px = x;
    s_py = y;
    lv_obj_set_pos(s_plane, s_px - PLANE_W / 2, s_py - PLANE_H / 2);
}

static bool hit(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return (abs(ax - bx) < (aw + bw) / 2) && (abs(ay - by) < (ah + bh) / 2);
}

/* 生成一发玩家子弹（返回是否成功占位） */
static bool spawn_pbul_at(int x, int y, int vx)
{
    for (int i = 0; i < MAX_PBUL; i++) {
        if (s_pbul[i].active) {
            continue;
        }
        /* 像素风子弹：纯色小块，不加描边（描边会显得像蜡笔涂鸦） */
        lv_obj_t *o = make_pixel_rect(s_scr, 4, 12, PIX_YELLOW, PIX_BLACK, 0);
        lv_obj_set_pos(o, x - 2, y);
        s_pbul[i].obj = o;
        s_pbul[i].x = x;
        s_pbul[i].y = y;
        s_pbul[i].vx = vx;
        s_pbul[i].active = true;
        return true;
    }
    return false;
}

/* 火力等级 → 子弹列数（1..POWER_COL_MAX）。本机与对端必须用同一套公式，
 * 否则两侧子弹数不同 → 命中不同 → 敌机集分叉。 */
static inline int power_cols(int level)
{
    int c = 1 + level;
    if (c < 1) c = 1;
    if (c > POWER_COL_MAX) c = POWER_COL_MAX;
    return c;
}

/* 第 c 列（0-based，共 cols 列）相对机身的横向偏移：以机身为中心左右对称分布。
 * 例：1 列 → [0]；2 列 → [-6,+6]；3 列 → [-12,0,+12]；4 列 → [-18,-6,+6,+18] */
static inline int power_col_off(int c, int cols)
{
    return (2 * c - (cols - 1)) * POWER_COL_STEP / 2;
}

/* 当前「生效」的火力等级：倒计时结束即视为 0（基础 1 列）。
 * s_power_level 只负责「攒档」（吃一个「火」+1，永不掉档），是否生效由 s_power_ticks 决定。
 * 广播给对端的 st.power 必须也走这个函数，本机开火与对端复制才会用同一组列数。 */
static inline int power_eff_level(void)
{
    return (s_power_ticks > 0) ? s_power_level : 0;
}

/* 本机自动开火：生效火力等级决定子弹列数（1~4 列，全部直射）。
 * 一次「开火事件」只让 fire_seq +1，对端按同一个 power 等级复制出同样的列数与偏移，
 * 双方模拟同一批子弹 → 命中一致 → 场上敌机集一致（不破坏联机同步）。 */
static void spawn_pbul(void)
{
    int y = s_py - PLANE_H / 2 - 6;
    int cols = power_cols(power_eff_level());
    bool any = false;
    for (int c = 0; c < cols; c++) {
        any |= spawn_pbul_at(s_px + power_col_off(c, cols), y, 0);
    }
    if (any) {
        s_fire_seq++;   /* 上报开火事件，供对端渲染并参与碰撞（保证敌机一致） */
    }
}

static void spawn_ebul(enemy_t *e)
{
    for (int i = 0; i < MAX_EBUL; i++) {
        if (s_ebul[i].active) {
            continue;
        }
        lv_obj_t *o = make_pixel_rect(s_scr, 4, 10, PIX_RED, PIX_BLACK, 0);
        int x = e->x;
        int y = e->y + (e->hp > 1 ? 16 : 13);
        lv_obj_set_pos(o, x - 2, y);
        s_ebul[i].obj = o;
        s_ebul[i].x = x;
        s_ebul[i].y = y;
        s_ebul[i].vx = 0;   /* 敌弹恒直射（槽位复用，显式清零） */
        s_ebul[i].active = true;
        return;
    }
}

static void spawn_enemy(void)
{
    /* 多人：先把敌机参数算好（固定消费 3 次共享 PRNG），再找空槽放置。
     * 空槽满时丢弃参数，但 PRNG 已推进 —— 这样双方「生成周期数」一致即序列一致，
     * 不依赖各自击落造成的空槽差异（否则敌机会越走越岔）。 */
    int type = (my_rand(0, 9) < 3) ? 1 : 0;   /* 约 30% 大型 */
    int w = (type == 0) ? 27 : 33;   /* 精灵边长 9*3 / 11*3 */
    int h = (type == 0) ? 27 : 33;
    int x = (int)my_rand(w / 2 + 4, FIELD_W - w / 2 - 4);
    int fire_cd = (int)my_rand(120, 260);

    for (int i = 0; i < MAX_ENEMY; i++) {
        if (s_enemies[i].active) {
            continue;
        }
        lv_obj_t *o = make_enemy_plane(s_scr, type, i);
        int y = -h;
        lv_obj_set_pos(o, x - w / 2, y - h / 2);
        s_enemies[i].obj = o;
        s_enemies[i].x = x;
        s_enemies[i].y = y;
        s_enemies[i].vy = 1.2f + s_level * 0.18f;
        if (s_enemies[i].vy > 4.2f) s_enemies[i].vy = 4.2f;
        s_enemies[i].hp = (type == 0) ? 1 : 2;
        s_enemies[i].pts = (type == 0) ? 1 : 3;
        s_enemies[i].fire_cd = fire_cd;
        s_enemies[i].active = true;
        return;
    }
}

static void apply_difficulty(void)
{
    /* 多人：关卡随真实时间推进（基于 tick），保证双方进度一致、敌机序列不分裂；
       单人：保持原「按分数」难度曲线不变。 */
    if (s_mode == 1) {
        s_level = 1 + s_ticks / 600;   /* 约每 10s 一关（tick=16ms） */
    } else {
        s_level = 1 + s_score / 15;
    }
    if (s_level_lbl) {
        char buf[32];
        snprintf(buf, sizeof(buf), "第%d关", s_level);
        lv_label_set_text(s_level_lbl, buf);
    }
}

/* ============================ 道具逻辑 ============================ */
static void despawn_item(int i)
{
    if (s_item[i].obj) {
        lv_obj_del(s_item[i].obj);
        s_item[i].obj = NULL;
    }
    s_item[i].active = false;
}

/* 道具外观：带底色的徽章（「火」=火力 / 「弹」=炸弹 / 「♥」=生命） */
static lv_obj_t *make_item_badge(lv_obj_t *parent, int kind)
{
    lv_color_t bg = PIX_ITEM_BOMB;        /* 炸弹：青 */
    lv_color_t fg = lv_color_black();
    const char *glyph = "弹";
    if (kind == ITEM_FIRE) {
        bg = PIX_ITEM_FIRE; glyph = "火";                 /* 火力：琥珀 */
    } else if (kind == ITEM_LIFE) {
        bg = PIX_ITEM_LIFE; fg = PIX_LIFE_TXT; glyph = "♥"; /* 生命：浅粉底 + 深红爱心 */
    }
    lv_obj_t *o = lv_label_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, ITEM_W, ITEM_H);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_border_color(o, PIX_BLACK, 0);
    lv_obj_set_style_border_width(o, 3, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 7, 0);
    lv_obj_set_style_text_font(o, &cn_font_16, 0);
    lv_obj_set_style_text_color(o, fg, 0);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_label_set_text(o, glyph);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* 掉落一个道具。多人：先固定消费共享 PRNG 算好参数，再找空槽；
 * 空槽满则丢弃参数但 PRNG 已推进 —— 与敌机生成同理，保证双方掉落序列/位置一致。 */
static void spawn_item(void)
{
    /* 仍只消费 1 次共享 PRNG（一次 my_rand(0,10)），双方跑同一段代码 →
       掉落种类/位置完全一致。概率：火力 40% / 生命 30% / 炸弹 30%
       （火力不再一家独大，生命爱心能稳定出现）。 */
    int r = (int)my_rand(0, 10);
    int kind = (r < 4) ? ITEM_FIRE : ((r < 7) ? ITEM_LIFE : ITEM_BOMB);
    int x    = (int)my_rand(ITEM_W / 2 + 10, FIELD_W - ITEM_W / 2 - 10);
    for (int i = 0; i < MAX_ITEM; i++) {
        if (s_item[i].active) {
            continue;
        }
        int y = -ITEM_H;
        lv_obj_t *o = make_item_badge(s_scr, kind);
        lv_obj_set_pos(o, x - ITEM_W / 2, y - ITEM_H / 2);
        s_item[i].obj = o;
        s_item[i].kind = kind;
        s_item[i].x = x;
        s_item[i].y = y;
        s_item[i].active = true;
        ESP_LOGI("plane", "item spawn kind=%d x=%d", kind, x);
        return;
    }
}

static void refresh_score_label(void)
{
    if (!s_score_lbl) return;
    char buf[48];
    if (s_mode == 1) {
        snprintf(buf, sizeof(buf), "我 %d  队友 %d", s_my_score, s_peer_state.score);
    } else {
        snprintf(buf, sizeof(buf), "分数: %d", s_score);
    }
    lv_label_set_text(s_score_lbl, buf);
}

/* 全屏清除敌机（含敌弹）。award=true 时把击落分记入本机（本机自己吃的炸弹）；
 * 对端吃炸弹时由 bomb_seq 边沿驱动本函数，award=false —— 双方敌机集保持一致，且分数不重复计算。 */
static void clear_all_enemies(bool award)
{
    int gained = 0;
    for (int i = 0; i < MAX_ENEMY; i++) {
        if (!s_enemies[i].active) continue;
        gained += s_enemies[i].pts;
        despawn_enemy(i);
    }
    for (int i = 0; i < MAX_EBUL; i++) despawn_bul(&s_ebul[i]);
    if (award && gained > 0) {
        s_my_score += gained;
        s_score += gained;
        apply_difficulty();
    }
    refresh_score_label();
}

/* 拾取提示：顶部短暂显示一行字（约 1.2s），让玩家明确知道吃到了什么 */
static void show_toast(const char *txt)
{
    if (!s_toast_lbl) return;
    lv_label_set_text(s_toast_lbl, txt);
    lv_obj_clear_flag(s_toast_lbl, LV_OBJ_FLAG_HIDDEN);
    s_toast_ticks = TOAST_TICKS;
}

/* 拾取道具生效。
 * 关键点：火力与生命是两套互不干扰的状态 —— 火力走 s_power_level/s_power_ticks，
 * 生命走 s_lives；所以「吃了火道具后照样能吃生命道具」，两者可叠加、不冲突。
 * 另外这三类道具都只影响本机（不改变别的玩家该看到什么），不需要扩协议。 */
static void apply_item(int kind)
{
    if (kind == ITEM_FIRE) {
        /* 每吃一个「火」：攒档 +1（列数 2→3→4，永不掉档），并把生效时间重置为满。
           之所以不掉档：按当前掉落节奏（约 13s 一个「火」）如果一到期就清零，
           玩家几乎不可能攒到 3~4 列。 */
        if (s_power_level < POWER_MAX_LEVEL) {
            s_power_level++;
        }
        s_power_ticks = POWER_TICKS;
        s_power_sec_shown = -1;    /* 触发 HUD 立即刷新 */
        s_power_hud_mode  = -1;    /* 让 HUD 重新按「生效中」上色 */
        char t[24];
        snprintf(t, sizeof(t), "火力 %d列", power_cols(s_power_level));
        show_toast(t);
        ESP_LOGI("plane", "item: FIRE level=%d cols=%d (%d ticks)",
                 s_power_level, power_cols(s_power_level), POWER_TICKS);
    } else if (kind == ITEM_LIFE) {
        if (s_lives < MAX_LIVES) {
            s_lives++;
            refresh_hearts();
            show_toast("生命 +1");
            ESP_LOGI("plane", "item: LIFE +1 (lives=%d/%d)", s_lives, MAX_LIVES);
        } else {
            /* 命已满：折算分数，道具不浪费 */
            s_my_score += 5;
            s_score += 5;
            apply_difficulty();
            show_toast("生命已满 +5");
            ESP_LOGI("plane", "item: LIFE full -> +5 score");
        }
    } else {
        clear_all_enemies(true);   /* 本机吃炸弹：计分 */
        s_bomb_seq++;              /* 广播炸弹事件，对端同步清屏（不计分） */
        show_toast("清屏!");
        ESP_LOGI("plane", "item: BOMB seq=%d", s_bomb_seq);
    }
    audio_sfx_flap();
    refresh_score_label();
}

/* 刷新生命心 HUD：前 s_lives 颗为红色 ♥，其余为暗色（表示已失去 / 未拥有） */
static void refresh_hearts(void)
{
    for (int i = 0; i < MAX_LIVES; i++) {
        if (!s_heart_lbl[i]) continue;
        lv_obj_set_style_text_color(s_heart_lbl[i],
                                   (i < s_lives) ? PIX_HEART : PIX_HEART_DIM, 0);
    }
}

/* 道具：按 tick 确定性掉落 + 下落 + 本机拾取（阵亡后不拾取，只随世界继续下落） */
static void update_items(void)
{
    s_item_cd--;
    if (s_item_cd <= 0) {
        s_item_cd = ITEM_DROP_CD;
        spawn_item();
    }
    for (int i = 0; i < MAX_ITEM; i++) {
        item_t *it = &s_item[i];
        if (!it->active) continue;
        it->y += 2;
        lv_obj_set_pos(it->obj, it->x - ITEM_W / 2, it->y - ITEM_H / 2);
        if (it->y > FIELD_H + ITEM_H) {
            despawn_item(i);
            continue;
        }
        if (s_state == ST_PLAY &&
            hit(it->x, it->y, ITEM_W, ITEM_H, s_px, s_py, PLANE_W, PLANE_H)) {
            int k = it->kind;
            despawn_item(i);
            apply_item(k);
        }
    }
}

static void reset_game(void)
{
    for (int i = 0; i < MAX_ENEMY; i++) despawn_enemy(i);
    for (int i = 0; i < MAX_PBUL; i++) despawn_bul(&s_pbul[i]);
    for (int i = 0; i < MAX_EBUL; i++) despawn_bul(&s_ebul[i]);
    for (int i = 0; i < MAX_PEER_BUL; i++) despawn_bul(&s_peer_bul[i]);
    for (int i = 0; i < MAX_ITEM; i++) despawn_item(i);   /* 清掉上一局残留道具 */

    s_px = FIELD_W / 2;
    s_py = FIELD_H - 60;
    s_score = 0;
    s_my_score = 0;
    s_over_prev_pressed = false;   /* 新一局，结束界面按压状态清零，避免误触发重开 */
    s_over_msg_phase = -1;         /* 新一局，结束界面提示阶段复位 */
    s_level = 1;
    s_ticks = 0;
    s_fire_cd = 0;
    s_spawn_cd = 0;
    s_item_cd = ITEM_DROP_CD / 2;   /* 开局约 2.7s 后掉第一个道具（按 tick 双方一致） */
    s_power_ticks = 0;              /* 新一局火力生效时间清零 */
    s_power_level = 0;              /* 新一局火力攒档清零（回到基础 1 列） */
    s_power_sec_shown = -1;
    s_power_hud_mode = -1;          /* 让 HUD 重新按当前状态上色 */
    if (s_power_lbl) lv_obj_add_flag(s_power_lbl, LV_OBJ_FLAG_HIDDEN);
    s_toast_ticks = 0;              /* 清掉上一局残留的拾取提示 */
    if (s_toast_lbl) lv_obj_add_flag(s_toast_lbl, LV_OBJ_FLAG_HIDDEN);
    s_lives = 1;                    /* 新一局：1 条命，靠「生命爱心」道具增加（上限 MAX_LIVES） */
    s_inv_ticks = 0;                /* 受击无敌清零 */
    refresh_hearts();
    /* 注意：s_bomb_seq / s_peer_bomb_seq 故意不重置 —— 它们是单调事件计数，
       跨局保持同步可避免重开瞬间被误判为「对端用了炸弹」而白清一次屏。 */

    if (s_mode == 1) {
        /* 用共享种子初始化确定性 PRNG，使双方敌机布局一致 */
        uint32_t seed = plane_net_seed();
        s_prng = seed ? seed : 0x12345678u;
        memset(&s_peer_state, 0, sizeof(s_peer_state));
        s_peer_state.x = -1; s_peer_state.y = -1; s_peer_state.level = 1;
        s_peer_state_valid = false;   /* 新一局：尚未收到对端状态，视为「对端未知」，不允许重开 */
        s_started_by_peer = false;
        s_waiting_conn = false;
        s_wait_ticks = 0;
        s_fire_seq = 0;
        s_peer_fire_seq = 0;
        if (s_peer_lbl) lv_obj_add_flag(s_peer_lbl, LV_OBJ_FLAG_HIDDEN);
        if (s_peer_plane) lv_obj_add_flag(s_peer_plane, LV_OBJ_FLAG_HIDDEN);
    }

    s_state = ST_PLAY;

    if (s_plane) {
        lv_obj_clear_flag(s_plane, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_plane, s_px - PLANE_W / 2, s_py - PLANE_H / 2);
    }
    if (s_score_lbl) {
        if (s_mode == 1) {
            lv_label_set_text(s_score_lbl, "我 0  队友 0");
        } else {
            lv_label_set_text(s_score_lbl, "分数: 0");
        }
    }
    if (s_level_lbl) lv_label_set_text(s_level_lbl, "第1关");
    if (s_msg) {
        lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_msg, lv_color_white(), 0);   /* 复位提示颜色（结束界面为红色，重开后恢复白色） */
    }
    if (s_btn_single) lv_obj_add_flag(s_btn_single, LV_OBJ_FLAG_HIDDEN);
    if (s_btn_multi)  lv_obj_add_flag(s_btn_multi,  LV_OBJ_FLAG_HIDDEN);
}

/* 死亡方点击「重玩」：本机立即重开；多人且已连接则递增 restart_seq 并广播，
 * 对端检测到 restart_seq 边沿即同步 reset_game()，双方回到同一局（PRNG 重新用共享种子，
 * 敌机布局再次一致），不会再出现单方重开造成双方状态分叉。 */
static void request_restart(void)
{
    if (s_state != ST_OVER) return;   /* 仅游戏结束态可重开；首次调用后状态变 ST_PLAY，后续重复调用被忽略，避免事件+轮询双触发 */
    if (s_mode == 1 && !plane_net_connected()) {
        /* 已断线：直接本机重开（等同单人） */
        reset_game();
        return;
    }
    if (s_mode == 1) {
        s_restart_seq++;
    }
    reset_game();

    if (s_mode == 1) {
        /* 立即广播一次，让对端尽快收到新 restart_seq（后续每帧正常广播也会带上） */
        plane_peer_state_t st;
        memset(&st, 0, sizeof(st));
        st.x = (int16_t)s_px;
        st.y = (int16_t)s_py;
        st.score = (int16_t)s_score;
        st.alive = 1;
        st.level = (uint8_t)s_level;
        st.start_seq = 1;
        st.fire_seq = s_fire_seq;
        st.restart_seq = s_restart_seq;
        st.power = 0;                 /* 重开后火力增强已清零 */
        st.bomb_seq = s_bomb_seq;     /* 炸弹序号单调，随包同步 */
        plane_net_send(&st);
    }
}

static void plane_tick(lv_timer_t *t)
{
    if (s_paused) {
        return;
    }
    (void)t;
    /* 心跳：每 ~5s 打印一次，确认固件/串口链路存活（空闲时无其他日志） */
    s_hb_ticks++;
    if (s_hb_ticks >= 312) {
        s_hb_ticks = 0;
        ESP_LOGI("plane", "heartbeat state=%d mode=%d waiting=%d conn=%d",
                 s_state, s_mode, s_waiting_conn, plane_net_connected());
    }
    /* 多人：收到对方开局握手(start_seq)且本机仍在 READY，则自动开始，对齐战场 */
    if (s_mode == 1 && s_state == ST_READY && s_peer_state.start_seq == 1 && !s_started_by_peer) {
        s_started_by_peer = true;
        reset_game();
    }
    /* 多人：等待连接期间持续刷新提示（防止被任意路径隐藏），并做超时提醒 */
    if (s_mode == 1 && s_state == ST_READY && !plane_net_connected()) {
        s_waiting_conn = true;
        s_wait_ticks++;
        if (s_wait_ticks % 20 == 0) {
            if (s_wait_ticks > 1200) {   /* 约 20s 仍未找到 */
                lv_label_set_text(s_msg, "正在搜索对手蓝牙…\n（约 20s 未找到\n请确认对方也进入双人模式）");
            } else {
                lv_label_set_text(s_msg, "正在搜索对手蓝牙…\n等待对方连接");
            }
            lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* READY 界面：轮询触摸判定模式选择/开始（绕开不可靠的按钮事件） */
    if (s_state == ST_READY) {
        ready_tap_poll();
    }

    /* B 方案：一方死亡后本机世界继续推进（tick/生成/敌机/对端子弹），
       但本机飞机冻结（不开火、不参与碰撞、不再判死），保持双方 tick 同步 → 敌机继续一致 */
    bool world_runs = (s_state == ST_PLAY || s_state == ST_OVER);
    if (!s_scr || !world_runs) {
        return;
    }
    s_ticks++;

    /* 结束界面：轮询触摸 + 按对端存活状态刷新提示；依赖轮询而非 LVGL RELEASED 事件 */
    if (s_state == ST_OVER) {
        over_tap_poll();
        /* 联机：先死的一方进入「等待模式」（只看对方玩、点屏无效）；
           待对端也阵亡、双方都进入等待模式后，提示变为「点击重玩」且任一方点屏即同步重开 */
        if (s_mode == 1 && s_msg) {
            int phase = (s_peer_state_valid && s_peer_state.alive == 0) ? 1 : 0;
            if (phase != s_over_msg_phase) {
                s_over_msg_phase = phase;
                char buf[96];   /* 含内联着色标记 "#FF2D2D ...#"，需比纯文本留更多余量 */
                if (phase == 1) {
                    snprintf(buf, sizeof(buf),
                             "#FF2D2D 游戏结束#\n双方阵亡\n分数: %d 第%d关\n点击重玩", s_score, s_level);
                } else {
                    snprintf(buf, sizeof(buf),
                             "#FF2D2D 你已阵亡#\n等待对方…\n分数: %d 第%d关", s_score, s_level);
                }
                lv_label_set_text(s_msg, buf);
                lv_obj_set_style_text_color(s_msg, lv_color_white(), 0);   /* 正文白色，仅标题内联标红 */
            }
        }
    }

    if (s_state == ST_OVER) {
        /* 死亡方：清掉本机残留玩家子弹，敌机只由「对端子弹（同步）」驱动，
           避免本机残留子弹打掉本地敌机造成双方敌机不一致 */
        for (int i = 0; i < MAX_PBUL; i++) despawn_bul(&s_pbul[i]);
    }

    /* 道具：掉落 / 下落 / 拾取。属于「世界推进」，ST_OVER（先死方观战）也继续跑，
       双方按同一 tick 序列消费共享 PRNG → 掉落种类与位置一致。
       ⚠ 必须排在「自动开火」之前：拾取「火」会当场改变子弹列数，本 tick 广播出去的
       power 必须与刚才那一批子弹实际发射的列数一致，否则对端会复制出不同数量的子弹。 */
    update_items();

    /* 火力状态：倒计时 + HUD（阵亡后冻结）。
       必须放在「自动开火」之前 —— 本 tick 若火力到期，先结算再开火，
       这样本 tick 广播出去的 power 与刚才实际发射的列数必然一致
       （否则对端会按新档位多复制几列 → 子弹集分叉）。
       到期只让「生效」状态结束，已攒下的列数 s_power_level 保留：
       下次再吃「火」从该档继续 +1，这样才可能攒满 4 列。 */
    if (s_state == ST_PLAY) {
        if (s_power_ticks > 0) {
            s_power_ticks--;
        }
        int mode = (s_power_ticks > 0) ? 1 : ((s_power_level > 0) ? 2 : 0);
        if (mode != s_power_hud_mode) {
            s_power_hud_mode  = mode;
            s_power_sec_shown = -1;   /* 换态后必刷新一次文字 */
            if (s_power_lbl) {
                if (mode == 0) {
                    lv_obj_add_flag(s_power_lbl, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_set_style_text_color(s_power_lbl,
                        (mode == 1) ? PIX_ITEM_FIRE : PIX_POWER_OFF, 0);
                    lv_obj_clear_flag(s_power_lbl, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
        if (s_power_lbl) {
            char pb[32];
            if (mode == 1) {
                int sec = (s_power_ticks + 31) / 62;   /* tick≈16ms → 约 62 tick/s，四舍五入 */
                if (sec < 1) sec = 1;
                if (sec != s_power_sec_shown) {
                    s_power_sec_shown = sec;
                    snprintf(pb, sizeof(pb), "火力 %d列 %ds", power_cols(s_power_level), sec);
                    lv_label_set_text(s_power_lbl, pb);
                }
            } else if (mode == 2 && s_power_sec_shown != 0) {
                s_power_sec_shown = 0;   /* 用 0 表示「暗色档位提示」已写好 */
                snprintf(pb, sizeof(pb), "火力 %d列", power_cols(s_power_level));
                lv_label_set_text(s_power_lbl, pb);
            }
        }
    }

    /* 玩家自动开火（死亡后冻结，不再开火） */
    if (s_state == ST_PLAY) {
        s_fire_cd--;
        if (s_fire_cd <= 0) {
            spawn_pbul();
            s_fire_cd = 16;   /* ~256ms 一发 */
        }
    }

    /* 玩家子弹上移（全部直射；多列火力靠「发射点横向偏移」实现，不用侧向速度） */
    for (int i = 0; i < MAX_PBUL; i++) {
        bul_t *b = &s_pbul[i];
        if (!b->active) continue;
        b->y -= 6;
        b->x += b->vx;
        lv_obj_set_pos(b->obj, b->x - 2, b->y);
        if (b->y < -14) despawn_bul(b);
    }

    /* 敌机生成（间隔随关卡缩短） */
    s_spawn_cd--;
    if (s_spawn_cd <= 0) {
        spawn_enemy();
        int interval = 50 - s_level * 4;
        if (interval < 18) interval = 18;
        s_spawn_cd = interval;
    }

    /* 拾取提示（toast）：显示 TOAST_TICKS（约 1.2s）后自动隐藏 */
    if (s_toast_ticks > 0) {
        s_toast_ticks--;
        if (s_toast_ticks == 0 && s_toast_lbl) {
            lv_obj_add_flag(s_toast_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 敌机下移 + 开火 */
    for (int i = 0; i < MAX_ENEMY; i++) {
        enemy_t *e = &s_enemies[i];
        if (!e->active) continue;
        e->y += (int)e->vy;
        int hh = (e->hp > 1) ? 30 : 22;
        lv_obj_set_pos(e->obj, e->x - hh / 2, e->y - hh / 2);
        e->fire_cd--;
        if (e->fire_cd <= 0) {
            spawn_ebul(e);
            e->fire_cd = (int)lv_rand(140, 300) - s_level * 6;
            if (e->fire_cd < 70) e->fire_cd = 70;
        }
        if (e->y > FIELD_H + 24) despawn_enemy(i);
    }

    /* 敌弹下移 */
    for (int i = 0; i < MAX_EBUL; i++) {
        bul_t *b = &s_ebul[i];
        if (!b->active) continue;
        b->y += 3;
        lv_obj_set_pos(b->obj, b->x - 2, b->y);
        if (b->y > FIELD_H + 12) despawn_bul(b);
    }

    /* 碰撞：玩家子弹 vs 敌机 */
    for (int i = 0; i < MAX_PBUL; i++) {
        bul_t *b = &s_pbul[i];
        if (!b->active) continue;
        for (int j = 0; j < MAX_ENEMY; j++) {
            enemy_t *e = &s_enemies[j];
            if (!e->active) continue;
            int eh = (e->hp > 1) ? 30 : 22;
            if (hit(b->x, b->y, 4, 12, e->x, e->y, eh, eh)) {
                despawn_bul(b);
                e->hp--;
                if (e->hp <= 0) {
                    despawn_enemy(j);
                    s_my_score += e->pts;   /* 本机飞机自己的击落分（联机时单独上报） */
                    s_score += e->pts;
                    if (s_mode == 1 && s_score_lbl) {
                        char buf[48];
                        snprintf(buf, sizeof(buf), "我 %d  队友 %d", s_my_score, s_peer_state.score);
                        lv_label_set_text(s_score_lbl, buf);
                    } else if (s_score_lbl) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "分数: %d", s_score);
                        lv_label_set_text(s_score_lbl, buf);
                    }
                    apply_difficulty();
                    audio_sfx_flap();   /* 击落音效 */
                }
                break;
            }
        }
    }

    /* 碰撞：敌机 / 敌弹 vs 玩家。
       有生命时先扣 1 条命并进入短暂无敌（飞机闪烁、期间免伤），最后一命被击中才阵亡。
       注意：受击**不删除**撞到的敌机/敌弹 —— 删除会改变场上敌机集，而对端仍在模拟
       同一架敌机 → 双方敌机会分叉。靠无敌时间让玩家自行飞离即可。 */
    if (s_state == ST_PLAY) {
        if (s_inv_ticks > 0) {
            /* 无敌中：闪烁 + 免伤 */
            s_inv_ticks--;
            if (s_plane) {
                if ((s_inv_ticks / 4) & 1) {
                    lv_obj_add_flag(s_plane, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_clear_flag(s_plane, LV_OBJ_FLAG_HIDDEN);
                }
                if (s_inv_ticks == 0) {
                    lv_obj_clear_flag(s_plane, LV_OBJ_FLAG_HIDDEN);   /* 无敌结束恢复可见 */
                }
            }
        } else {
            bool hit_now = false;
            for (int j = 0; j < MAX_ENEMY && !hit_now; j++) {
                enemy_t *e = &s_enemies[j];
                if (e->active && hit(e->x, e->y, (e->hp > 1) ? 30 : 22, (e->hp > 1) ? 30 : 22,
                                      s_px, s_py, PLANE_W, PLANE_H)) {
                    hit_now = true;
                }
            }
            if (!hit_now) {
                for (int i = 0; i < MAX_EBUL; i++) {
                    bul_t *b = &s_ebul[i];
                    if (b->active && hit(b->x, b->y, 4, 10, s_px, s_py, PLANE_W, PLANE_H)) {
                        hit_now = true;
                        break;
                    }
                }
            }
            if (hit_now) {
                if (s_lives > 1) {
                    /* 还有命：扣命 + 进入无敌闪烁，游戏继续（不改变敌机集，双方仍一致） */
                    s_lives--;
                    s_inv_ticks = INV_TICKS;
                    refresh_hearts();
                    audio_sfx_flap();
                    ESP_LOGI("plane", "player hit -> life lost (lives=%d, inv=%d ticks)", s_lives, INV_TICKS);
                } else {
                    goto over;
                }
            }
        }
    }

    /* 多人：检测对端重启请求（死亡方点击重玩），双方同步重开 */
    if (s_mode == 1 && s_peer_state.restart_seq != s_peer_restart_seq) {
        s_peer_restart_seq = s_peer_state.restart_seq;
        s_restart_seq = s_peer_restart_seq;   /* 本机序号与对端对齐，避免下次误触发 */
        reset_game();   /* 本机也重开，回到与对端同一局 */
        return;         /* 本帧不广播，下一帧以新状态正常同步 */
    }

    /* 多人：对端使用「全屏清除炸弹」→ 本机同步清屏（不计分），
       保证双方场上敌机集一致（本机自己吃的炸弹已在 apply_item 里计分清过） */
    if (s_mode == 1 && s_peer_state.bomb_seq != s_peer_bomb_seq) {
        s_peer_bomb_seq = s_peer_state.bomb_seq;
        clear_all_enemies(false);
        ESP_LOGI("plane", "peer bomb -> sync clear (no score)");
    }

    /* 多人：广播本机状态 + 刷新对方飞机 */
    if (s_mode == 1) {
        plane_peer_state_t st;
        st.x = (int16_t)s_px;
        st.y = (int16_t)s_py;
        st.score = (int16_t)s_my_score;   /* 上报本机飞机自己的击落分（对端显示为「队友」），不再上报合作总分 */
        st.alive = (s_state == ST_PLAY) ? 1 : 0;
        st.level = (uint8_t)s_level;
        st.start_seq = 1;   /* 本方已开始，带动对方 */
        st.fire_seq = s_fire_seq;
        st.restart_seq = s_restart_seq;   /* 随状态包带上重玩序号，供对端边沿检测 */
        st.power = (uint8_t)power_eff_level();   /* 生效火力等级 0..3：对端据此把一次开火复制成同样多列 */
        st.bomb_seq = s_bomb_seq;                 /* 炸弹序号：对端边沿检测同步清屏 */
        plane_net_send(&st);
        update_peer_plane();
        update_peer_bullets();
    }
    return;

over:
    s_state = ST_OVER;
    s_over_msg_phase = -1;
    /* 进入结束态时把「上一次按压」视为已按下：若阵亡瞬间手指还按在屏上（如正在拖动），
       必须先抬手、再重新按下，才算一次有效点击重开（避免后阵亡方一死就自动重开） */
    s_over_prev_pressed = true;
    char buf[96];   /* 含内联着色标记 "#FF2D2D ...#"，需比纯文本留更多余量 */
    if (s_mode == 1) {
        /* 联机：先死时实际为「等待模式」，提示由 plane_tick 按对端存活状态刷新；这里给个初值 */
        snprintf(buf, sizeof(buf), "#FF2D2D 你已阵亡#\n等待对方…\n分数: %d 第%d关", s_score, s_level);
    } else {
        snprintf(buf, sizeof(buf), "#FF2D2D 游戏结束#\n分数: %d 第%d关\n点击重玩\n按键返回", s_score, s_level);
    }
    lv_label_set_text(s_msg, buf);
    lv_obj_set_style_text_color(s_msg, lv_color_white(), 0);   /* 正文白色，仅标题内联标红 */
    lv_obj_align(s_msg, LV_ALIGN_CENTER, 0, 0);   /* 结束信息整块居中显示 */
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    audio_sfx_flap();
}

static void on_tap(lv_event_t *e)
{
    if (s_paused) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        if (!s_scr) return;
        if (s_state == ST_PLAY) {
            lv_indev_t *indev = lv_indev_get_act();
            if (indev) {
                lv_point_t p;
                lv_indev_get_point(indev, &p);
                move_plane(p.x, p.y);
            }
        }
        /* READY 态的模式选择/开始改由 ready_tap_poll() 直接轮询触摸处理，
         * 不再依赖此处 LVGL 事件（按钮 CLICKED 在本框架下不可靠）。 */
    }
    else if (code == LV_EVENT_PRESSING) {
        if (!s_scr) return;
        if (s_state == ST_PLAY) {
            lv_indev_t *indev = lv_indev_get_act();
            if (indev) {
                lv_point_t p;
                lv_indev_get_point(indev, &p);
                move_plane(p.x, p.y);
            }
        }
    }
    /* ST_OVER 态的「点屏重开」已改由 plane_tick 中的 over_tap_poll() 轮询处理，
     * 更可靠且带门控（联机需双方都阵亡才允许重开）。此处不再响应 RELEASED，
     * 避免绕过等待模式导致先死方点屏即可重开。 */
}

/* READY 界面：直接轮询触摸芯片坐标，按落点判定点中「单人」还是「双人」。
 * 完全不依赖 LVGL 按钮 CLICKED 事件（本框架下该事件不可靠，会落到全屏 tap
 * 导致直接进单人）。一次「按下」只触发一次选择。 */
static void ready_tap_poll(void)
{
    bool pressed = (touch_input_get_state() == LV_INDEV_STATE_PRESSED);
    if (!pressed) {
        s_ready_prev_pressed = false;
        return;
    }
    if (s_ready_prev_pressed) {
        return;   /* 同一次按压只处理一次 */
    }
    s_ready_prev_pressed = true;

    lv_point_t p;
    touch_input_get_point(&p);
    ESP_LOGI("plane", "ready press x=%d y=%d mode=%d", p.x, p.y, s_mode);

    if (s_mode == 0) {
        /* 尚未选模式：按落点 y 判定点中哪个按钮（按钮宽 120 居中，单人中心 y≈188，双人 y≈244） */
        if (p.y >= 160 && p.y <= 212) {
            /* 单人：直接开局（不再依赖按钮 CLICKED 事件，那里不可靠） */
            ESP_LOGI("plane", "on_btn_single: enter SINGLE mode");
            s_mode = 0;
            if (s_btn_single) lv_obj_add_flag(s_btn_single, LV_OBJ_FLAG_HIDDEN);
            if (s_btn_multi)  lv_obj_add_flag(s_btn_multi,  LV_OBJ_FLAG_HIDDEN);
            reset_game();
        } else if (p.y >= 220 && p.y <= 280) {
            /* 双人：开始搜索对手蓝牙 */
            ESP_LOGI("plane", "on_btn_multi: enter MULTI mode");
            s_mode = 1;
            s_waiting_conn = true;
            s_wait_ticks = 0;
            if (s_btn_single) lv_obj_add_flag(s_btn_single, LV_OBJ_FLAG_HIDDEN);
            if (s_btn_multi)  lv_obj_add_flag(s_btn_multi,  LV_OBJ_FLAG_HIDDEN);
            plane_net_begin(on_net_recv, on_net_conn);
            lv_label_set_text(s_msg, "正在搜索对手蓝牙…\n等待对方连接");
            lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        }
        /* 落在按钮区外（顶部提示等）忽略，避免误开局 */
    } else {
        /* 已选多人：双方蓝牙连上后，点屏开始（由本方带动对方） */
        if (plane_net_connected()) {
            reset_game();
        }
    }
}

/* 结束界面：直接轮询触摸芯片坐标，检测一次「按下」即触发重开（死亡方点击重玩）。
 * 不依赖 LVGL RELEASED 事件（本框架下该事件在 ST_OVER 态不可靠），用与 READY 相同的轮询法。
 * 通过 s_over_prev_pressed 去抖：同一次按压只触发一次；松手后才能再次触发。 */
static void over_tap_poll(void)
{
    bool pressed = (touch_input_get_state() == LV_INDEV_STATE_PRESSED);
    if (!pressed) {
        s_over_prev_pressed = false;
        return;
    }
    if (s_over_prev_pressed) {
        return;   /* 同一次按压只处理一次 */
    }
    s_over_prev_pressed = true;
    /* 联机：必须「确认对端也已阵亡」才允许点屏重开。
       注意两点：① 先死的一方只能看对方玩，点屏无效；
                ② 若从未收到过对端状态（s_peer_state_valid=false，含首帧未到/链路异常），
                   视为「对端未知」，同样不允许重开，避免默认 alive=0 被误判成对端已死。 */
    if (s_mode == 1 && plane_net_connected()) {
        if (!s_peer_state_valid || s_peer_state.alive != 0) {
            ESP_LOGI("plane", "over press ignored (waiting mode): peer_valid=%d peer_alive=%d",
                     s_peer_state_valid, s_peer_state.alive);
            return;
        }
    }
    ESP_LOGI("plane", "over press -> request_restart (state=%d mode=%d conn=%d valid=%d peer_alive=%d)",
             s_state, s_mode, plane_net_connected(), s_peer_state_valid, s_peer_state.alive);
    request_restart();
}

static void on_pause(void) { s_paused = true; }
static void on_resume(void) { s_paused = false; }

/* ======================== 联机（多人）回调与辅助 ======================== */
static void net_conn_changed_async(void *p)
{
    bool c = (bool)(uintptr_t)p;
    ESP_LOGI("plane", "net_conn_changed_async connected=%d state=%d", c, s_state);
    if (!s_scr) return;
    if (c) {
        s_waiting_conn = false;
        s_wait_ticks = 0;
        if (s_state != ST_PLAY) {
            lv_label_set_text(s_msg, "已连接! 点击开始游戏");
            lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (s_peer_plane) lv_obj_add_flag(s_peer_plane, LV_OBJ_FLAG_HIDDEN);
        if (s_state != ST_PLAY) {
            s_waiting_conn = true;
            s_wait_ticks = 0;
            lv_label_set_text(s_msg, "对方断开 重新搜索中…");
            lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void on_net_conn(bool connected)
{
    /* BLE 栈任务上下文 -> 转 LVGL 线程安全更新 UI */
    lv_async_call(net_conn_changed_async, (void *)(uintptr_t)(connected ? 1 : 0));
}

static void on_net_recv(const plane_peer_state_t *peer)
{
    /* 仅镜像数据，plane_tick 在 LVGL 线程读取并刷新对象 */
    memcpy(&s_peer_state, peer, sizeof(s_peer_state));
    s_peer_state_valid = true;   /* 已收到对端状态：此后 s_peer_state.alive 才是可信的 */
}

static void update_peer_plane(void)
{
    if (s_peer_state.x < 0 || s_peer_state.y < 0 || s_peer_state.alive == 0) {
        if (s_peer_plane) lv_obj_add_flag(s_peer_plane, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!s_peer_plane) {
        s_peer_plane = make_peer_plane(s_scr);
        lv_obj_add_flag(s_peer_plane, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_peer_plane, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_peer_plane, s_peer_state.x - PLANE_W / 2, s_peer_state.y - PLANE_H / 2);

    /* 双分数：顶部一行并排显示「我 X 队友 Y」。
     * 注意：「我」必须用 s_my_score（本机飞机自己击落分），不能用 s_score（合作总分），
     * 否则本地收到对端包后会把自己的分数显示成双方总分。 */
    if (s_score_lbl) {
        char buf[48];
        snprintf(buf, sizeof(buf), "我 %d  队友 %d", s_my_score, s_peer_state.score);
        lv_label_set_text(s_score_lbl, buf);
    }

    /* 对端开火边沿检测：fire_seq 变化 → 在绿机位置生成本地模拟绿色子弹。
       若对端处于「火力增强」，必须用与本机 spawn_pbul() 完全相同的公式
       （power_cols + power_col_off，全部直射）复制出同样的列数与偏移，
       双方模拟同一批子弹 → 命中一致 → 敌机集一致。
       丢包时 fire_seq 会一次跳多格：这里按 uint16 差值逐批补齐（≤4 批），
       比「只补一批」更接近本机真实的子弹集；差值异常（回绕/重开）则直接对齐。 */
    if (s_peer_state.fire_seq != s_peer_fire_seq) {
        uint16_t fr   = s_peer_state.fire_seq;
        uint16_t diff = (uint16_t)(fr - s_peer_fire_seq);   /* uint16 运算天然回绕安全 */
        if (diff > 4) {
            s_peer_fire_seq = fr;          /* 序号异常（重开/长时间停止）：直接对齐，不补发 */
        } else {
            int px = s_peer_state.x;
            int py = s_peer_state.y - PLANE_H / 2 - 6;
            int cols = power_cols((int)s_peer_state.power);
            while (s_peer_fire_seq != fr) {
                s_peer_fire_seq++;
                for (int c = 0; c < cols; c++) {
                    spawn_peer_bul_at(px + power_col_off(c, cols), py, 0);
                }
            }
        }
    }
}

static void spawn_peer_bul_at(int x, int y, int vx)
{
    for (int i = 0; i < MAX_PEER_BUL; i++) {
        if (s_peer_bul[i].active) continue;
        lv_obj_t *o = make_pixel_rect(s_scr, 4, 12, PIX_PEER, PIX_BLACK, 0);
        lv_obj_set_pos(o, x - 2, y);
        s_peer_bul[i].obj = o;
        s_peer_bul[i].x = x;
        s_peer_bul[i].y = y;
        s_peer_bul[i].vx = vx;
        s_peer_bul[i].active = true;
        return;
    }
}

/* 对端子弹：本地模拟移动，并参与敌机碰撞 —— 初始位置=同步的绿机坐标、开火时机=同步的
 * fire_seq 边沿，双方轨迹确定性一致，故命中一致，场上敌机集也一致。 */
static void update_peer_bullets(void)
{
    for (int i = 0; i < MAX_PEER_BUL; i++) {
        bul_t *b = &s_peer_bul[i];
        if (!b->active) continue;
        b->y -= 6;
        b->x += b->vx;   /* vx 恒 0（保留字段），多列火力靠发射点偏移而非侧向速度 */
        lv_obj_set_pos(b->obj, b->x - 2, b->y);
        for (int j = 0; j < MAX_ENEMY; j++) {
            enemy_t *e = &s_enemies[j];
            if (e->active && hit(b->x, b->y, 4, 12, e->x, e->y,
                                  (e->hp > 1) ? 30 : 22, (e->hp > 1) ? 30 : 22)) {
                despawn_bul(b);
                e->hp--;
                if (e->hp <= 0) {
                    despawn_enemy(j);
                    s_score += e->pts;   /* 对端子弹击落只计入总贡献，不计入本机「我」的分 */
                    if (s_score_lbl) {
                        char buf[48];
                        snprintf(buf, sizeof(buf), "我 %d  队友 %d", s_my_score, s_peer_state.score);
                        lv_label_set_text(s_score_lbl, buf);
                    }
                    apply_difficulty();
                    audio_sfx_flap();
                }
                break;
            }
        }
        if (b->active && b->y < -14) despawn_bul(b);
    }
}

static void close_to_app(void)
{
    if (!s_scr) return;
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    audio_bgm_stop();
    plane_net_stop();   /* 退出联机，恢复 BLE GAP 回调（供扫描页） */
    for (int i = 0; i < MAX_ENEMY; i++) despawn_enemy(i);
    for (int i = 0; i < MAX_PBUL; i++) despawn_bul(&s_pbul[i]);
    for (int i = 0; i < MAX_EBUL; i++) despawn_bul(&s_ebul[i]);
    for (int i = 0; i < MAX_PEER_BUL; i++) despawn_bul(&s_peer_bul[i]);
    for (int i = 0; i < MAX_ITEM; i++) despawn_item(i);
    lv_obj_t *gone = s_scr;
    s_scr = NULL;
    s_plane = NULL;
    s_score_lbl = NULL;
    s_level_lbl = NULL;
    s_power_lbl = NULL;
    s_toast_lbl = NULL;
    s_power_ticks = 0;          /* 退出时清掉火力状态，避免下次进入带着残留档位 */
    s_power_level = 0;
    s_power_sec_shown = -1;
    s_power_hud_mode = -1;
    s_toast_ticks = 0;
    s_heart_box = NULL;
    for (int i = 0; i < MAX_LIVES; i++) s_heart_lbl[i] = NULL;
    s_lives = 1;
    s_inv_ticks = 0;
    s_msg = NULL;
    s_peer_plane = NULL;
    s_peer_lbl = NULL;
    s_btn_single = NULL;
    s_btn_multi = NULL;
    s_mode = 0;
    s_started_by_peer = false;
    s_waiting_conn = false;
    s_wait_ticks = 0;
    s_restart_seq = 0; s_peer_restart_seq = 0;   /* 退出会话，重玩序号清零 */
    memset(&s_peer_state, 0, sizeof(s_peer_state));
    s_peer_state.x = -1; s_peer_state.y = -1; s_peer_state.level = 1;
    s_state = ST_READY;
    s_px = FIELD_W / 2;
    s_py = FIELD_H - 60;
    lv_obj_del(gone);
}

void ui_plane_start(void)
{
    ESP_LOGI("plane", "ui_plane_start BUILD=build_fix44 (power: 1~4 cols stackable & 6s, item drop fire40/life30/bomb30, toast HUD)");
    if (s_scr) {
        return;
    }
    s_key_down = false;
    s_mode = 0;
    s_peer_plane = NULL;
    s_peer_lbl = NULL;
    s_btn_single = NULL;
    s_btn_multi = NULL;
    s_started_by_peer = false;
    memset(&s_peer_state, 0, sizeof(s_peer_state));
    s_peer_state.x = -1; s_peer_state.y = -1; s_peer_state.level = 1;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, PIX_SPACE, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 静态星空背景 */
    for (int i = 0; i < 26; i++) {
        int x = (int)lv_rand(2, FIELD_W - 2);
        int y = (int)lv_rand(2, FIELD_H - 2);
        lv_obj_t *s = make_pixel_rect(s_scr, 2, 2, PIX_STAR, PIX_BLACK, 0);
        lv_obj_set_pos(s, x, y);
    }

    s_plane = make_player_plane(s_scr);
    lv_obj_add_flag(s_plane, LV_OBJ_FLAG_HIDDEN);   /* READY 界面隐藏飞机，避免遮挡按钮 */

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

    s_score_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_score_lbl, "分数: 0");
    lv_obj_set_style_text_font(s_score_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_score_lbl, lv_color_white(), 0);
    lv_obj_clear_flag(s_score_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_MID, 0, 12);

    s_level_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_level_lbl, "第1关");
    lv_obj_set_style_text_font(s_level_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_level_lbl, lv_color_hex(0xFFEC27), 0);
    lv_obj_clear_flag(s_level_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_level_lbl, LV_ALIGN_TOP_LEFT, 12, 12);

    /* HUD：火力等级 + 剩余时间（吃到「火」道具后显示，默认隐藏） */
    s_power_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_power_lbl, "火力 1列 0s");
    lv_obj_set_style_text_font(s_power_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_power_lbl, PIX_ITEM_FIRE, 0);
    lv_obj_clear_flag(s_power_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_power_lbl, LV_ALIGN_TOP_LEFT, 12, 34);
    lv_obj_add_flag(s_power_lbl, LV_OBJ_FLAG_HIDDEN);

    /* HUD：拾取道具的短暂提示（如「火力 3列」「生命 +1」「清屏!」），约 1.2s 后自动隐藏 */
    s_toast_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_toast_lbl, "");
    lv_obj_set_style_text_font(s_toast_lbl, &cn_font_16, 0);
    lv_obj_set_style_text_color(s_toast_lbl, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_color(s_toast_lbl, lv_color_hex(0xFFEC27), 0);
    lv_obj_set_style_bg_opa(s_toast_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast_lbl, 6, 0);
    lv_obj_set_style_pad_hor(s_toast_lbl, 8, 0);
    lv_obj_set_style_pad_ver(s_toast_lbl, 2, 0);
    lv_obj_clear_flag(s_toast_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_toast_lbl, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_add_flag(s_toast_lbl, LV_OBJ_FLAG_HIDDEN);

    /* HUD：生命心（「♥」一排，顶部居中，位于分数下方）。
       满 = 红色 ♥，已失去 = 暗色 ♥；初始 1 条命，吃「生命爱心」道具增加（上限 MAX_LIVES）。 */
    s_heart_box = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_heart_box);
    lv_obj_set_size(s_heart_box, MAX_LIVES * LIFE_GLYPH_SP, 20);
    lv_obj_align(s_heart_box, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_clear_flag(s_heart_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_heart_box, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < MAX_LIVES; i++) {
        s_heart_lbl[i] = lv_label_create(s_heart_box);
        lv_label_set_text(s_heart_lbl[i], "♥");
        lv_obj_set_style_text_font(s_heart_lbl[i], &cn_font_16, 0);
        lv_obj_set_style_text_color(s_heart_lbl[i], PIX_HEART, 0);
        lv_obj_set_pos(s_heart_lbl[i], i * LIFE_GLYPH_SP + 3, 0);
        lv_obj_clear_flag(s_heart_lbl[i], LV_OBJ_FLAG_CLICKABLE);
    }
    s_lives = 1;
    s_inv_ticks = 0;
    refresh_hearts();

    s_msg = lv_label_create(s_scr);
    lv_obj_set_width(s_msg, FIELD_W - 60);
    lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_msg, &cn_font_16, 0);
    lv_obj_set_style_text_color(s_msg, lv_color_white(), 0);
    lv_label_set_recolor(s_msg, true);   /* 允许内联变色 "#RRGGBB 文字#"，供结束标题单独标红 */
    lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_msg, LV_ALIGN_CENTER, 0, -72);
    lv_label_set_text(s_msg, "拖动飞机躲避敌机\n自动开火 击落得分\n选择下方模式开始");
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);

    /* 对方分数标签（多人时显示，默认隐藏） */
    s_peer_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_peer_lbl, "队友: 0");
    lv_obj_set_style_text_font(s_peer_lbl, &cn_font_14, 0);
    lv_obj_set_style_text_color(s_peer_lbl, PIX_PEER, 0);
    lv_obj_clear_flag(s_peer_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_peer_lbl, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_add_flag(s_peer_lbl, LV_OBJ_FLAG_HIDDEN);

    /* 模式按钮：单人 / 双人（READY 界面显示，垂直两排置于提示文字下方） */
    s_btn_single = lv_btn_create(s_scr);
    lv_obj_set_size(s_btn_single, 120, 44);
    lv_obj_align(s_btn_single, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_radius(s_btn_single, 8, 0);
    lv_obj_set_style_bg_color(s_btn_single, PIX_PEER, 0);
    lv_obj_set_style_bg_opa(s_btn_single, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_btn_single, 0, 0);
    lv_obj_t *lb1 = lv_label_create(s_btn_single);
    lv_label_set_text(lb1, "单人");
    lv_obj_set_style_text_font(lb1, &cn_font_16, 0);
    lv_obj_set_style_text_color(lb1, lv_color_black(), 0);
    lv_obj_center(lb1);
    /* 不再挂 LV_EVENT_CLICKED：模式选择改由 ready_tap_poll() 直接轮询触摸落点判定，
     * 按钮仅作视觉提示（本框架下按钮 CLICKED 事件不可靠，会落到全屏 tap 导致直接进单人）。 */

    s_btn_multi = lv_btn_create(s_scr);
    lv_obj_set_size(s_btn_multi, 120, 44);
    lv_obj_align(s_btn_multi, LV_ALIGN_CENTER, 0, 64);
    lv_obj_set_style_radius(s_btn_multi, 8, 0);
    lv_obj_set_style_bg_color(s_btn_multi, PIX_WIN, 0);
    lv_obj_set_style_bg_opa(s_btn_multi, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_btn_multi, 0, 0);
    lv_obj_t *lb2 = lv_label_create(s_btn_multi);
    lv_label_set_text(lb2, "双人");
    lv_obj_set_style_text_font(lb2, &cn_font_16, 0);
    lv_obj_set_style_text_color(lb2, lv_color_white(), 0);
    lv_obj_center(lb2);
    /* 同上：双人按钮仅作视觉提示，选择由 ready_tap_poll() 处理 */

    /* 确保模式按钮不被飞机/提示文字遮挡 */
    lv_obj_move_foreground(s_btn_single);
    lv_obj_move_foreground(s_btn_multi);

    /* 接入「应用标准框架」：顶部下滑弹菜单 + 电源键离开(回 home)。
     * 音量调节与退出已移至应用菜单(框架)，故此处不再创建游戏内按钮。 */
    ui_app_shell_set_exit_cb(close_to_app);
    ui_app_shell_set_pause_cb(on_pause);
    ui_app_shell_set_resume_cb(on_resume);
    ui_app_shell_bind(s_scr);

    /* 退出按钮已移至应用菜单(框架)，此处不再创建 */

    s_state = ST_READY;
    s_ready_prev_pressed = false;   /* 重置 READY 界面触摸轮询的按压边沿 */
    s_score = 0;
    s_level = 1;
    s_ticks = 0;
    s_px = FIELD_W / 2;
    s_py = FIELD_H - 95;
    if (s_plane) lv_obj_set_pos(s_plane, s_px - PLANE_W / 2, s_py - PLANE_H / 2);

    s_timer = lv_timer_create(plane_tick, 16, NULL);
    lv_scr_load(s_scr);

    audio_bgm_start(AUDIO_BGM_THEME_PLANE);
}

void ui_plane_poll(void)
{
    if (!s_scr) {
        return;
    }
    /* 电源键短按/长按统一由 touch_input.c 的 power_key_poll 处理 */
}
