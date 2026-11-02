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

/* ---- 🔴 捕获区必须覆盖「手指从最底部起手」的那一整块
 *      （2026-09-27 第四版成形；2026-10-23 按触摸标定更正了立论，几何未变）----
 *   ① 整屏宽的宽带       —— 与键盘 / 按钮抢事件；
 *   ② 104×44 @ (128,306) —— 「必须从小横条起手」的窄条、贴屏幕最下沿。
 *      真机几乎按不出来（用户：「上滑返回主页的手势也很难触发了」）。
 *   ③ 212×34 @ (74,232)  —— 挪上去了，但**把小横条一起拽了上来**（HINT_CY_CC=249），
 *      用户看到的是「上滑的小横条怎么跑到上面去了」（2026-09-27 反馈）。
 *
 * 本版（④）：横条回到**屏幕最底部**（HINT_CY_CC = 336，视觉如旧），
 *   捕获带从 y 232 一直铺到屏幕最下沿（H = 360-232 = 128）。
 *
 * 🔴🔴 2026-10-23 更正立论（**这段以前写错了，别再照抄**）：
 *   旧注释说「面板有效上报区比屏面小：有效圆 R≈137、y≳270 不上报」，并据此解释
 *   ②为什么按不出来。那条结论 **2026-10-04 已被 9 靶心真机标定证伪**：
 *     · raw 能顶到量程两端（display_x→359、display_y→358）⇒ **面板覆盖整块圆屏**；
 *     · `map_raw_to_lvgl()` 是纯旋转+镜像、**没有缩放**（增益必须保持 1000）。
 *   证据与复核步骤：`tools/verify_shots/touch_calib_2026-10-04/README.md`。
 *   ⇒ ② 的真实病因是**目标太小**：那条带只有 104×44，而指尖接触面 ≈63~79px、
 *     瞄准噪声 ±10~35px ⇒ "按在带里"这件事本身就不成立（不是面板不产生坐标）。
 *   ⇒ 同理，「y 232 是为了给可报区下沿 269 留 37px 余量」这句话**作废**。
 *     232 保留下来的理由变成纯交互：它在屏心（180）以下 52px，既不与页面中部的
 *     内容抢起手，又给"手指落点抖一下"留出足够余量。
 *
 *   ⚠️ 仍成立的那半句：**PRESSED 只派发一次**（lv_indev 只在
 *      「当前 state=PRESSED 且上一态=RELEASED」时派发），所以带必须覆盖到屏底 ——
 *      否则"从最底边起手"的手指要等进入带内才被算作按下，白白丢掉一段行程。
 *      ⚠️ 代价（用户 2026-09-27 明确接受）：从底边起手时，带外那段行程不计入 dy。
 *      想更灵敏就得把横条往上挪（③ 版的位置），用户选了「横条贴底」。
 *      **别再擅自把横条挪上去。**
 *
 * ⚠️ 本层**不可见**（remove_style_all、无背景/边框、只吃事件），所以它的外角允许超出内切圆：
 *   (74,360) / (285,360) 到圆心 (180,180) 是 208.9 / 208.4 px > 180。
 *   这**不是**「控件切出圆」—— 圆外那几 px 玻璃本身不显示、面板也不会在那儿上报，纯占位。
 *   可见控件仍然必须 < 176（判据见 `CC_GEO_*` 与 skill sdgoods-screenshot「圆内布局」）。
 * ⚠️ 与键盘的关系：靠**层级**，不是靠几何 —— bind 末尾 lv_obj_move_background() 把本层
 *   压到最底，键盘的键与兜底垫（y 141..360）始终在本层之上。
 * ⚠️ 与左缘返回手势（swipe_back：捕获层 x 0..55、起手需 x≤24）不重叠：本区左沿 74。
 * ⚠️ CC 各页底部的开关 / Scan 等按钮都在本 bind **之前**建 ⇒ 压在本层之上（见 open_wifi 注释）。
 * ⚠️ 纵向判定仍只要 dy < -50（不改）：手指可以从区内起手后滑出捕获区，PRESS_LOCK
 *    保证 RELEASED 一定回到捕获层（见下面 bind 里的说明）。 */
#define SWIPE_UP_W    212
#define SWIPE_UP_H    128   /* 360 - 232：铺到屏幕最下沿（起手必须在带内才收到 PRESSED） */
#define SWIPE_UP_X     74   /* 左沿 = 180 - W/2 - 1（居中，右沿 285） */
#define SWIPE_UP_Y    232   /* 屏心以下 52px（**不是**为了躲 269，那条已作废，见上） */

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
    /* 仅在底部那一条捕获带内起手、且纵向位移明显大于横向 → 返回主页
     * ⚠️ 起点可能远高于「手指真正落下的位置」：底边那 90px 面板不上报，
     *    PRESSED 是手指进入可报区的那一帧才派发的（见宏头）。 */
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
    lv_obj_set_pos(cat, SWIPE_UP_X, SWIPE_UP_Y);   /* 从可报区里一路铺到屏幕最下沿（见宏头推导） */
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
