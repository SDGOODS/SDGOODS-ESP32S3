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

#include "sdgoods_swipe_up.h"

#include "lvgl.h"
#include "esp_log.h"   /* 诊断日志：上滑返回主页是否被判定（阈值/方向一目了然） */

static const char *TAG = "swipe_up";

#define SWIPE_MIN_DY  50   /* 上滑判定阈值（像素） */
#define SWIPE_UP_W    360  /* 捕获区宽度：整屏宽，接住底部任意起手点 */
/* 捕获区高度：底部 110px 带（y=250..360）。从 90 加大到 110：控制中心下排按钮底缘
 * 是 252、caption 到约 275，90px 带（y≥270）与 caption 之间有一小段「谁都不管」的
 * 死区 —— 用户在那里起手上滑什么都不会发生（既不算上滑也不点按钮）。
 * 捕获层始终 move_background，按钮仍在其上方，所以加大高度不会抢按钮的点击。 */
#define SWIPE_UP_H    110
/* 底部小横条中心约在 (180,336)；捕获区扩成整屏宽、底部 90px 的带，
 * 用户从底部任意位置起手往上滑都能被接住（旧版 60×30 太小、必须精准命中，
 * 真机上几乎接不住 -> 二级页上滑回主页失效）。捕获层置于控件之下，
 * 页面按钮 / 滑块仍正常点按、拖动，不会误触。 */

static lv_coord_t s_px;
static lv_coord_t s_py;

static void on_pressed(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_px = p.x;
    s_py = p.y;
}

static void on_released(lv_event_t *e)
{
    void (*on_up)(void) = (void (*)(void))lv_event_get_user_data(e);
    if (!on_up) {
        return;
    }
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dx = (int)p.x - (int)s_px;
    int dy = (int)p.y - (int)s_py;
    /* 仅在底部小横条区域起手、且纵向位移明显大于横向 → 返回主页 */
    const bool hit = (dy < -SWIPE_MIN_DY && LV_ABS(dy) > LV_ABS(dx));
    ESP_LOGI(TAG, "start=(%d,%d) dx=%d dy=%d (need dy<-%d) -> %s",
             (int)s_px, (int)s_py, dx, dy, SWIPE_MIN_DY, hit ? "HOME" : "ignore");
    if (hit) {
        /* 用 async 避免在输入事件回调里删除对象导致崩溃 */
        lv_async_call((lv_async_cb_t)on_up, NULL);
    }
}

void sdgoods_swipe_up_bind(lv_obj_t *scr, void (*on_up)(void))
{
    if (!scr || !on_up) {
        return;
    }
    lv_obj_t *cat = lv_obj_create(scr);
    lv_obj_remove_style_all(cat);
    lv_obj_set_size(cat, SWIPE_UP_W, SWIPE_UP_H);
    lv_obj_set_pos(cat, 0, 360 - SWIPE_UP_H);   /* 贴底整条带：y = 270..360 */
    lv_obj_clear_flag(cat, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cat, LV_OBJ_FLAG_CLICKABLE);
    /* ⚠️ PRESS_LOCK 必须加：LVGL 在按下期间每个输入周期都会重新命中测试，
     * 手指一旦滑出捕获区，act_obj 会被切走 -> PRESS_LOST -> RELEASED 永不触发
     * -> 上滑返回主页失效。加 PRESS_LOCK 后，lv_indev.c 的重命中分支
     * （scroll_obj==NULL && !PRESS_LOCK）被跳过，act_obj 锁定在本捕获层，
     * 松手时 RELEASED 一定投递到这里，再按起点/终点位移判定是否算上滑。 */
    lv_obj_add_flag(cat, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_move_background(cat);   /* 置于其它控件之下，避免遮挡按钮 / 滑块 */

    lv_obj_add_event_cb(cat, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(cat, on_released, LV_EVENT_RELEASED, (void *)on_up);
}
