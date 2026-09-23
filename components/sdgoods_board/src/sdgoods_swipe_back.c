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
#include "esp_log.h"   /* 诊断日志：滑块页「左缘右滑」与「滑块拖动」互相误触时靠它定位 */

static const char *TAG = "swipe_back";

/* 判定口径（2026-09-19 收紧，目的：与页面内的按钮/滑块彻底互不干扰）
 *   - 起手必须真的在「最左边这一窄条」内（EDGE_X）；
 *   - 水平位移要够长（MIN_DX）且**明显**以横向为主（> 2 倍纵向），
 *     斜着划一下不再被当成返回；
 *   - 捕获层本身也只覆盖最左边一条窄带（STRIP_W），这样页面中部的
 *     拖动（例如拖动滑块）根本不会进入返回手势的判定范围。 */
#define SWIPE_MIN_DX  64   /* 左→右滑动判定阈值（像素） */
#define SWIPE_EDGE_X  24   /* 必须从最左边这段起手才算「从左滑回」 */
#define SWIPE_STRIP_W 56   /* 捕获层宽度：只覆盖最左边这条带 */

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
    /* 必须从最左边这段起手才算「从左滑回上一级」；方向以横向为主。
     * 用 async 避免在输入事件回调里删除对象导致崩溃。 */
    const bool edge_ok = (s_px <= SWIPE_EDGE_X);
    const bool hit = (edge_ok && dx > SWIPE_MIN_DX && dx > 2 * LV_ABS(dy));
    ESP_LOGI(TAG, "start=(%d,%d) dx=%d dy=%d edge(<=%d)=%s -> %s",
             (int)s_px, (int)s_py, dx, dy, SWIPE_EDGE_X,
             edge_ok ? "yes" : "no", hit ? "BACK" : "ignore");
    if (hit) {
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
    /* ⚠️ 只覆盖最左边一条窄带（旧版做 360x360 全屏）：全屏捕获层会让「在页面中部
     * 起手、往右拖」也被判成返回 —— 滑块页上这正是「拖滑块却触发返回」的元凶。 */
    lv_obj_set_size(cat, SWIPE_STRIP_W, 360);
    lv_obj_set_pos(cat, 0, 0);
    lv_obj_clear_flag(cat, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cat, LV_OBJ_FLAG_CLICKABLE);
    /* ⚠️ PRESS_LOCK：左→右滑返回时手指会移出原按下对象，锁死按下避免 PRESS_LOST
     * 导致 RELEASED 丢失（与 swipe_up 同理）。 */
    lv_obj_add_flag(cat, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_move_background(cat);   /* 置于其它控件之下，避免遮挡按钮 */

    lv_obj_add_event_cb(cat, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(cat, on_released, LV_EVENT_RELEASED, (void *)on_back);
}
