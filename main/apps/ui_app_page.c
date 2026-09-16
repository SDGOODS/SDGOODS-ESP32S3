/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Required Notice: Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 *
 * 个人学习、研究与业余项目免费使用；商业用途需事前书面授权，见 LICENSING.md。
 * 分发时必须完整保留本声明 —— 这是许可条款，不是建议。
 */

#include "ui_app_page.h"

#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "apps_registry.h"   /* g_sdgoods_app：应用清单（增删应用改那里，不用动本文件） */
#include "sdgoods_i18n.h"    /* SDG_T：按钮与标题文案中英切换 */
#include "ui_home.h"
#include "sdgoods_swipe_back.h"

LV_FONT_DECLARE(si_yuan_black_icon_14);
LV_FONT_DECLARE(si_yuan_black_icon_16);

static lv_obj_t *s_scr;
static bool s_active;   /* 当前是否为活动页（进入子页时置 false，避免 poll 误触发返回） */
static bool s_key_down;
static uint32_t s_lang_seq;   /* 建屏时的语言版本号；和当前不一致就重建 */

/* 点按钮 → 进入应用。
 * 应用是谁、怎么进入，全部来自 apps_registry.c 的清单，本文件不需要知道任何应用。 */
static void on_app_btn(lv_event_t *e)
{
    const sdgoods_app_t *app = (const sdgoods_app_t *)lv_event_get_user_data(e);
    if (!app || !app->show) {
        return;
    }
    s_active = false;   /* 进入子应用：暂停本页 poll，避免按键冲突 */
    app->show();
}

/* ==================== 像素风图标 ====================
 * 12x12 像素图 × ICON_SCALE = 48px 图标，用 canvas 逐像素绘制（真像素风，非色块）。
 * 调色板：'.'=透明 'K'=描边黑 'Y'=小鸟黄 'O'=喙橙 'W'=白/机身银白
 *         'B'=座舱蓝 'D'=小鸟翼尾(深黄) 'R'/'C'/'P'/'G' 暂无图标使用，留给新应用
 *
 * 想给自己的应用加图标：在下面加一个 12 行 x 12 列的字符数组，
 * 再在 icon_map_for() 里按**图标键**返回它即可（键写在 apps_registry.c 的
 * s_apps[] 表里，与界面语言无关）。
 * 不加图标也没关系 —— 会退化成显示文字标签。
 * ======================================================== */
#define ICON_MAP_W  12
#define ICON_SCALE  4
#define ICON_PX     (ICON_MAP_W * ICON_SCALE)      /* 48 */
#define ICON_BUF_SZ (ICON_PX * ICON_PX * 3)        /* TRUE_COLOR_ALPHA: 2B 颜色 + 1B alpha */

/* 启动台最多显示几个应用（图标缓冲按这个数量静态分配）。
   超过这个数的应用仍会被轮询，只是没有按钮入口。 */
#define SDG_UI_LAUNCHER_MAX 6

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

static lv_color_t icon_color(char c)
{
    switch (c) {
    case 'K': return lv_color_hex(0x101820);   /* 描边黑 */
    case 'Y': return lv_color_hex(0xFFEC27);   /* 小鸟黄 */
    case 'O': return lv_color_hex(0xFF8A00);   /* 喙橙 */
    case 'W': return lv_color_hex(0xDEE2E6);   /* 机身银白 */
    case 'B': return lv_color_hex(0x3B82F6);   /* 座舱蓝 */
    case 'D': return lv_color_hex(0xF0A500);   /* 小鸟翼尾深黄 */
    case 'R': return lv_color_hex(0xFF004D);   /* 红   ┐ */
    case 'C': return lv_color_hex(0x29ADFF);   /* 青   │ 暂无图标使用， */
    case 'P': return lv_color_hex(0x9B5DE5);   /* 紫   │ 留给新应用 */
    case 'G': return lv_color_hex(0x00E436);   /* 绿   ┘ */
    default:  return lv_color_white();
    }
}

/* 按图标键查像素图。没有对应图标的返回 NULL（调用方会退化成文字标签）。
   键是语言无关的固定串（apps_registry.h 的 icon 字段）——**不要**用按钮文字当键：
   切到英文后按钮文字变了，就再也查不到图标。 */
static const char *const *icon_map_for(const char *key)
{
    if (!key) {
        return NULL;
    }
    if (strcmp(key, "bird") == 0)  return ICON_BIRD_MAP;
    if (strcmp(key, "plane") == 0) return ICON_PLANE_MAP;
    return NULL;
}

/* 把像素图渲染成 canvas（带 alpha），加到按钮上 */
static void draw_pixel_icon(lv_obj_t *btn, uint8_t *buf, const char *const *map)
{
    memset(buf, 0, ICON_BUF_SZ);   /* 先全透明 */

    lv_obj_t *cv = lv_canvas_create(btn);
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
}

/* 每个图标一份常驻缓冲（启动台只建一次屏，静态即可） */
static uint8_t s_icon_buf[SDG_UI_LAUNCHER_MAX][ICON_BUF_SZ];

/* 一行 n 个按钮时，第 1 个按钮的左上角 x（整行在圆屏内水平居中）。
   n <= 0 时返回什么都不会被用到。 */
static lv_coord_t row_start_x(int n)
{
    const lv_coord_t total = n * SDG_UI_BTN_SIZE
                           + (n - 1) * (SDG_UI_BTN_PITCH - SDG_UI_BTN_SIZE);
    return SDG_UI_CENTER_X - total / 2;
}

/* app == NULL 时画一个不可点的空位按钮（保留此分支：新应用图标缓冲未就绪时用得上） */
static lv_obj_t *make_round_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                const sdgoods_app_t *app, uint8_t *icon_buf)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, SDG_UI_BTN_SIZE, SDG_UI_BTN_SIZE);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, SDG_UI_BTN_SIZE / 2, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    if (!app || !app->show) {
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);   /* 空位：不响应点击 */
        return btn;
    }

    const char *label = SDG_T(app->label_zh, app->label_en);
    const char *const *map = icon_map_for(app->icon);
    if (map) {
        draw_pixel_icon(btn, icon_buf, map);
    } else if (label && label[0]) {
        /* 兜底：没有像素图标就显示文字 —— 注意这会用到字体子集，
           中文文案改动后必须先重跑 tools/gen_fonts.py */
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, &si_yuan_black_icon_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_center(lbl);
    }

    lv_obj_add_event_cb(btn, on_app_btn, LV_EVENT_CLICKED, (void *)app);
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
    /* 语言切换过 -> 旧屏文案作废，删掉重建 */
    if (s_scr && s_lang_seq != sdg_i18n_seq()) {
        lv_obj_del(s_scr);
        s_scr = NULL;
    }

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
    s_lang_seq = sdg_i18n_seq();

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    sdgoods_swipe_back_bind(s_scr, close_page);   /* 空白处从左滑到右 = 返回主页 */

    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, SDG_T("应用", "Apps"));
    lv_obj_set_style_text_font(title, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SDG_UI_TITLE_Y);

    /* 按钮按应用数量自适应排布（apps_registry.c 的 s_apps[] 顺序 = 按钮顺序）：
         1~3 个 → 单行，垂直居中（与主页按钮行同高）
         4~6 个 → 两行，每行各自水平居中
       位置全部由 row_start_x() + sdgoods_ui.h 的常量推导，增减应用不用手改坐标。 */
    int shown = g_sdgoods_app_count;
    if (shown > SDG_UI_LAUNCHER_MAX) {
        shown = SDG_UI_LAUNCHER_MAX;   /* 放不下的应用仍会被轮询，只是没有入口 */
    }
    const bool two_rows = (shown > 3);
    const int  row0_n   = two_rows ? 3 : shown;
    const int  row1_n   = two_rows ? (shown - 3) : 0;

    lv_coord_t x = row_start_x(row0_n);
    for (int i = 0; i < row0_n; i++) {
        make_round_btn(s_scr, x + i * SDG_UI_BTN_PITCH,
                       two_rows ? SDG_UI_ROW1_Y : SDG_UI_ROW_MID_Y,
                       &g_sdgoods_app[i], s_icon_buf[i]);
    }
    x = row_start_x(row1_n);
    for (int i = 0; i < row1_n; i++) {
        make_round_btn(s_scr, x + i * SDG_UI_BTN_PITCH, SDG_UI_ROW2_Y,
                       &g_sdgoods_app[3 + i], s_icon_buf[3 + i]);
    }

    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, SDG_T("按电源键返回", "Power key to go back"));
    lv_obj_set_style_text_font(hint, &si_yuan_black_icon_14, 0);   /* 提示行用小一号的字（14） */
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
