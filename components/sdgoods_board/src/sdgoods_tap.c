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

#include "sdgoods_tap.h"
#include "sdgoods_lvgl.h"   /* sdgoods_lvgl_post：跨任务投递到 LVGL 线程的唯一入口 */
#include "sdgoods_input.h"  /* sdgoods_touch_synth_report：合成触摸同步轮询缓存（见 tap_syn_read） */

#include "esp_log.h"     /* 合成触摸 / 无触摸设备时的日志（调试用） */
#include "esp_timer.h"   /* esp_timer_get_time：合成长按按**真实时间**计时（理由见 sdgoods_tap.h） */

static void tap_on_pressed(lv_event_t *e)
{
    sdgoods_tap_ctx_t *c = (sdgoods_tap_ctx_t *)lv_event_get_user_data(e);
    c->valid    = false;
    c->max_disp = 0;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        /* 拿不到输入设备：保持 invalid。松开时按「放行」处理 ——
         * 宁可放过一次滑动，也不能吞掉一次真实点按（历史上吞点按的教训）。 */
        return;
    }
    lv_indev_get_point(indev, &c->start);
    c->valid = true;
}

static void tap_on_pressing(lv_event_t *e)
{
    sdgoods_tap_ctx_t *c = (sdgoods_tap_ctx_t *)lv_event_get_user_data(e);
    if (!c->valid) {
        return;
    }
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int32_t dx = (int32_t)p.x - (int32_t)c->start.x;
    int32_t dy = (int32_t)p.y - (int32_t)c->start.y;
    int32_t d  = (LV_ABS(dx) > LV_ABS(dy)) ? LV_ABS(dx) : LV_ABS(dy);
    if (d > c->max_disp) {
        c->max_disp = d;
    }
}

static void tap_on_clicked(lv_event_t *e)
{
    sdgoods_tap_ctx_t *c = (sdgoods_tap_ctx_t *)lv_event_get_user_data(e);
    if (c->valid && c->max_disp > SDGOODS_TAP_SLOP) {
        /* 判为滑动：不触发点按动作（这正是「按在按钮上滑走再松手」的场景） */
        return;
    }
    if (c->cb) {
        c->cb(c->ud);
    }
}

void sdgoods_tap_lock(lv_obj_t *obj)
{
    if (obj) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
    }
}

void sdgoods_tap_transparent(lv_obj_t *obj)
{
    if (obj) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
}

void sdgoods_tap_bind(lv_obj_t *obj, sdgoods_tap_ctx_t *ctx,
                      sdgoods_tap_cb_t on_tap, void *user_data)
{
    if (!obj || !ctx) {
        return;
    }
    ctx->start.x  = 0;
    ctx->start.y  = 0;
    ctx->max_disp = 0;
    ctx->valid    = false;
    ctx->cb       = on_tap;
    ctx->ud       = user_data;

    sdgoods_tap_lock(obj);
    lv_obj_add_event_cb(obj, tap_on_pressed,  LV_EVENT_PRESSED,  ctx);
    lv_obj_add_event_cb(obj, tap_on_pressing, LV_EVENT_PRESSING, ctx);
    lv_obj_add_event_cb(obj, tap_on_clicked,  LV_EVENT_CLICKED,  ctx);
}

/* 「有意交互」标记：用 LVGL 留给用户的 USER_1。这样 normalize 不会把
 * 一个特意做成可交互的 canvas（画板）误判成装饰物去穿透。 */
void sdgoods_tap_keep_interactive(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_USER_1);   /* 标记：跳过「装饰物穿透」 */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
}

/* 递归落实统一策略 */
static void normalize_rec(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    /* 明确声明过「有意交互」的对象：只补 PRESS_LOCK，绝不穿透。 */
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_1)) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
    } else if (lv_obj_check_type(obj, &lv_label_class) || lv_obj_check_type(obj, &lv_canvas_class)) {
        /* 纯装饰：不可点击，让触摸穿透到下面的真正交互对象。
         * lv_label 构造时本就已清 CLICKABLE（lv_label.c:716），这里是兜底；
         * **canvas 默认可点击**（构造时不改 flags），压在按钮上会吃掉点击 —— 真正的坑在它。 */
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    } else if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE)) {
        /* 可交互：锁定所有权，杜绝手势中途被接管。
         * 普通控件 LVGL 已默认带 PRESS_LOCK（lv_obj.c:438 对「有父对象」的都加），
         * 这一分支真正补上的其实是**无父对象的屏**（lv_obj_create(NULL)）。 */
        lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
    }

    uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        normalize_rec(lv_obj_get_child(obj, (int32_t)i));
    }
}

void sdgoods_tap_normalize(lv_obj_t *root)
{
    normalize_rec(root);
}

/* ---- 看门狗：覆盖「后来才创建 / 加载的屏」 ------------------------------------
 * 只有 100ms 一次的指针比较；换屏时才做一次全树规范化。
 * 为什么必须有它：app 往往在启动后才建屏（甚至建多块屏来回切），
 * 而 sdgoods_app_shell_bind() 只能规范化「当下这一块」。 */
static lv_obj_t *s_watch_scr = NULL;
static lv_timer_t *s_watch_timer = NULL;

static void tap_watch_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_t *cur = lv_scr_act();
    if (cur == s_watch_scr) {
        return;                     /* 没换屏：直接返回 */
    }
    s_watch_scr = cur;
    sdgoods_tap_normalize(cur);
}

void sdgoods_tap_install(void)
{
    if (!s_watch_timer) {
        /* 周期取 100ms：一次指针比较的代价，换屏后最迟 100ms 补上策略。 */
        s_watch_timer = lv_timer_create(tap_watch_cb, 100, NULL);
    }
    s_watch_scr = lv_scr_act();
    sdgoods_tap_normalize(s_watch_scr);
}

/* ============================ 无人手验证：合成触摸 ============================
 * 契约与按键映射见 sdgoods_tap.h。这里只讲两条实现红线：
 *   ① read_cb 只在**序列结束那一帧**还原（并立刻透传给原实现）—— 保证真实触摸
 *      不会长时间处于「被替换」状态；即使中途有人手动触摸，最多也只是一帧被合成值覆盖。
 *   ② 必须在 LVGL 线程里改驱动状态（read_cb 会被 LVGL 任务调用），
 *      所以对外只提供 async 入口，真正干活的是 *_now 版本。 */

typedef void (*tap_syn_read_cb_t)(struct _lv_indev_drv_t *, lv_indev_data_t *);

/* ⚠️ 日志 tag **不能**取 "tap"：那样输出行是 `tap: synth: ...`，其中含子串 "tap: "，
 *    而「按钮被点」的既有日志形如 `cc: tap: Volume` ⇒ 用 "tap: " 做禁词的手势回归
 *    脚本会把合成触摸本身误判成「按钮被误触」（已踩，见 /tmp/gesture_matrix.py 的
 *    假阳性排查记录）。取一个不含 "tap:" 的 tag 最省事。 */
#define TAG_SYN "gesture"

static struct _lv_indev_drv_t *s_syn_drv  = NULL;
static tap_syn_read_cb_t       s_syn_orig = NULL;   /* 原触摸 read_cb，只记一次 */
static int                     s_syn_frame = 0;
static int                     s_syn_frames = 0;
static int                     s_syn_dx = 0;
static int                     s_syn_dy = 0;
static lv_point_t              s_syn_start;
static lv_point_t              s_syn_pend_start;    /* async 入口到执行之间的参数暂存 */
static int                     s_syn_pend_dx, s_syn_pend_dy, s_syn_pend_frames;

/* 合成长按的会话状态（与「逐帧移动」模式互斥：同一时刻只跑一种序列）。
 * s_syn_hold_ms > 0 ⇒ 长按进行中；到点后置 0 并让**下一帧**负责还原驱动。 */
static int         s_syn_hold_ms   = 0;
static int64_t     s_syn_hold_t0   = 0;      /* 长按起始时刻（µs） */
static bool        s_syn_hold_rel  = false;  /* 已发出 RELEASED，待下一帧还原 */
static lv_point_t  s_syn_hold_pend;          /* async 入口暂存 */
static int         s_syn_hold_pend_ms = 0;

/* 合成器的 read_cb（定义在下面；ensure_drv 要用它做「序列是否还在跑」的判据）。 */
static void tap_syn_read(struct _lv_indev_drv_t *drv, lv_indev_data_t *data);

/* 序列收尾：还原真实触摸驱动，并把这一帧的数据**透传**给它
 * （所以真实触摸最多只有「序列最后一帧」被合成值占掉）。 */
static void tap_syn_restore(struct _lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    drv->read_cb = s_syn_orig;
    if (s_syn_orig) {
        s_syn_orig(drv, data);
    }
}

/* 取（或重新取）触摸 indev 的驱动指针。返回 false = 本工程没有触摸设备。
 * ⚠️ 序列正在跑时**绝不能**覆盖 s_syn_orig，否则会把原实现记成合成器自己，
 *    序列结束后永远还原不回真实触摸。 */
static bool tap_syn_ensure_drv(void)
{
    if (!s_syn_drv) {
        /* 本工程只注册了一个触摸设备（见 sdgoods_input.c）。 */
        lv_indev_t *indev = lv_indev_get_next(NULL);
        if (!indev || !indev->driver) {
            ESP_LOGW(TAG_SYN, "no touch indev");
            return false;
        }
        s_syn_drv  = indev->driver;
        s_syn_orig = (tap_syn_read_cb_t)s_syn_drv->read_cb;
    } else if (s_syn_drv->read_cb != tap_syn_read) {
        s_syn_orig = (tap_syn_read_cb_t)s_syn_drv->read_cb;
    }
    return true;
}

static void tap_syn_read(struct _lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    /* ---- 长按模式：同一位置按住 hold_ms 毫秒，再松开一帧 ---- */
    if (s_syn_hold_ms > 0) {
        data->point.x = s_syn_start.x;
        data->point.y = s_syn_start.y;
        int elapsed_ms = (int)((esp_timer_get_time() - s_syn_hold_t0) / 1000);
        if (elapsed_ms < s_syn_hold_ms) {
            data->state = LV_INDEV_STATE_PRESSED;
            sdgoods_touch_synth_report(data->state, data->point.x, data->point.y);
            return;
        }
        s_syn_hold_ms  = 0;          /* 时间到：本帧发 RELEASED */
        s_syn_hold_rel = true;
        data->state = LV_INDEV_STATE_RELEASED;
        sdgoods_touch_synth_report(data->state, data->point.x, data->point.y);
        return;
    }
    if (s_syn_hold_rel) {
        s_syn_hold_rel = false;
        tap_syn_restore(drv, data); /* 松手后一帧还原真实触摸并透传 */
        return;
    }

    if (s_syn_frame > s_syn_frames) {
        /* 序列结束：立刻还原真实触摸驱动，并把本次数据透传出去。 */
        tap_syn_restore(drv, data);
        return;
    }
    data->point.x = (lv_coord_t)(s_syn_start.x + s_syn_dx * s_syn_frame);
    data->point.y = (lv_coord_t)(s_syn_start.y + s_syn_dy * s_syn_frame);
    /* 最后一帧给 RELEASED（LVGL 收到它才会走 RELEASED + CLICKED 分支） */
    data->state = (s_syn_frame == s_syn_frames) ? LV_INDEV_STATE_RELEASED
                                                : LV_INDEV_STATE_PRESSED;
    /* 同步轮询缓存：轮询型 app（ready_tap_poll 等）读的是 sdgoods_input 的缓存，
     * 不经过 LVGL indev；不同步的话合成点按对这类 app 完全不可见。还原路径
     * （tap_syn_restore → 真实 touchpad_read）自己会刷缓存，无需在此处理。 */
    sdgoods_touch_synth_report(data->state, data->point.x, data->point.y);
    s_syn_frame++;
}

void sdgoods_tap_synth_now(int x, int y, int dx, int dy, int frames)
{
    if (frames < 1) {
        frames = 1;
    }
    if (!tap_syn_ensure_drv()) {
        return;
    }

    s_syn_start.x = (lv_coord_t)x;
    s_syn_start.y = (lv_coord_t)y;
    s_syn_dx      = dx;
    s_syn_dy      = dy;
    s_syn_frames  = frames;
    s_syn_frame   = 0;
    s_syn_drv->read_cb = tap_syn_read;

    ESP_LOGI(TAG_SYN, "synth: %d frames, step=(%d,%d), from (%d,%d)",
             frames, dx, dy, x, y);
}

void sdgoods_tap_synth_hold_now(int x, int y, int hold_ms)
{
    if (hold_ms <= 0) {
        hold_ms = 800;   /* 默认 800ms = LVGL 长按阈值(400ms) 的 2 倍 */
    }
    if (!tap_syn_ensure_drv()) {
        return;
    }

    s_syn_start.x = (lv_coord_t)x;
    s_syn_start.y = (lv_coord_t)y;
    s_syn_hold_pend_ms = hold_ms;
    s_syn_hold_t0 = esp_timer_get_time();
    s_syn_hold_rel = false;
    s_syn_hold_ms = hold_ms;
    s_syn_drv->read_cb = tap_syn_read;

    ESP_LOGI(TAG_SYN, "synth: hold %d ms at (%d,%d) (lvgl long-press threshold = driver->long_press_time)",
             hold_ms, x, y);
}

static void tap_syn_async(void *p)
{
    (void)p;
    sdgoods_tap_synth_now((int)s_syn_pend_start.x, (int)s_syn_pend_start.y,
                          s_syn_pend_dx, s_syn_pend_dy, s_syn_pend_frames);
}

static void tap_syn_hold_async(void *p)
{
    (void)p;
    sdgoods_tap_synth_hold_now((int)s_syn_hold_pend.x, (int)s_syn_hold_pend.y,
                               s_syn_hold_pend_ms);
}

void sdgoods_tap_synth(int x, int y, int dx, int dy, int frames)
{
    /* 串口命令来自 console RX 任务：这里只填纯整数参数（不含任何 LVGL 访问），
     * 真正的执行投递到 LVGL 线程。
     * ⚠️ 必须用 sdgoods_lvgl_post()，**不能**用 lv_async_call()：后者的
     *    lv_timer_create() 会与 LVGL 线程并发改同一条无锁定时器链表 ⇒ 崩溃。
     *    （真机定位过程见 sdgoods_lvgl.h 的契约说明。） */
    s_syn_pend_start.x = (lv_coord_t)x;
    s_syn_pend_start.y = (lv_coord_t)y;
    s_syn_pend_dx      = dx;
    s_syn_pend_dy      = dy;
    s_syn_pend_frames  = frames;
    sdgoods_lvgl_post(tap_syn_async, NULL);
}

void sdgoods_tap_synth_hold(int x, int y, int hold_ms)
{
    s_syn_hold_pend.x  = (lv_coord_t)x;
    s_syn_hold_pend.y  = (lv_coord_t)y;
    s_syn_hold_pend_ms = hold_ms;
    sdgoods_lvgl_post(tap_syn_hold_async, NULL);
}
