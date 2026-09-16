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

#include "sdgoods_swipe_back.h"

#include "lvgl.h"

#define SWIPE_MIN_DX 50   /* 左→右滑动判定阈值（像素） */

/* 单线程：同一时刻仅一个前台屏、单触摸点，按下与松开连续发生，全局 px/py 安全。
 * 旧实现把 on_back 存在全局单例里：从子页返回父页后，父页走「已存在」分支只
 * lv_scr_load 不重新 bind，导致全局 on_back 仍指向子页的 close_page，父页滑动返回
 * 失效（调用子页已销毁的屏 -> no-op）。
 * 现把 on_back 直接挂到捕获层的事件 user_data 上，各页独立互不干扰。 */
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
    void (*on_back)(void) = (void (*)(void))lv_event_get_user_data(e);
    if (!on_back) {
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
    if (dx > SWIPE_MIN_DX && dx > LV_ABS(dy)) {
        /* 手从左滑动到右：返回。用 async 避免在输入事件回调里删除对象导致崩溃 */
        lv_async_call((lv_async_cb_t)on_back, NULL);
    }
}

void sdgoods_swipe_back_bind(lv_obj_t *scr, void (*on_back)(void))
{
    if (!scr || !on_back) {
        return;
    }
    lv_obj_t *cat = lv_obj_create(scr);
    lv_obj_remove_style_all(cat);
    lv_obj_set_size(cat, 360, 360);
    lv_obj_set_pos(cat, 0, 0);
    lv_obj_clear_flag(cat, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cat, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(cat);   /* 置于其它控件之下，避免遮挡按钮 */

    lv_obj_add_event_cb(cat, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(cat, on_released, LV_EVENT_RELEASED, (void *)on_back);
}
