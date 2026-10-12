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

/* ---- 🔴 捕获区必须覆盖「手指从最底部起手、且面板真的会上报坐标」的那一整块
 *      （2026-09-27 第四版；前三版的口径沿革都是真机反馈逼出来的，别退回去）----
 *   ① 整屏宽的宽带       —— 与键盘 / 按钮抢事件；
 *   ② 104×44 @ (128,306) —— 「必须从小横条起手」的窄条、贴屏幕最下沿。
 *      真机几乎按不出来（用户：「上滑返回主页的手势也很难触发了」）。病根是**物理**：
 *      这块面板的有效上报区比屏面小得多，
 *        · 有效圆 **R ≈ 137**：键心 r=132.8 能点中、r=136.8「很难点中」；
 *        · **下方硬边界 y ≈ 269**：功能行键体压在 y 260..295 时下半截按不中，
 *          压到 239..269 就整行可用 ⇒ **y ≳ 270 那一带基本不上报**
 *          （详见 sdgoods_cc.c 的 KB_H/KB_W 事故表）。
 *      ② 的上沿 306 比「第一次能被上报的 y（≈270）」还低 ⇒ **整条带都在死区里**，
 *      用户在那儿滑，模组根本不产生坐标 ⇒ 判定再准也是零命中。
 *      （网络页原先那排 y 286..314 的按钮踩的是同一个坑。合成触摸注入坐标**绕过面板**，
 *        所以那种自检「全通过」并不能证明手指按得上。）
 *   ③ 212×34 @ (74,232)  —— 挪进可报区了，但**把小横条一起拽了上来**（HINT_CY_CC=249），
 *      用户看到的是「上滑的小横条怎么跑到上面去了」（2026-09-27 反馈）。
 *      提示条画在按不到的地方是最坏的提示，可反过来把提示条挪到中间也不对。
 *
 * 本版（④）：横条回到**屏幕最底部**（HINT_CY_CC = 336，视觉如旧），
 *   捕获带改成**从 y 232 一直铺到屏幕最下沿**（H = 360-232 = 128）。
 *   🔴 关键认识（决定了「铺到最下沿」是有效的，而不是白铺）：
 *      面板在 y≳270 不产生坐标，但手指从底边往上滑时，**进入可报区的第一帧才被 LVGL
 *      当成 PRESSED** —— lv_indev 只在「当前 state=PRESSED 且上一态=RELEASED」时派发
 *      PRESSED，用的就是当时那个点。所以只要带覆盖到屏幕最下沿，从底边起手的那一滑，
 *      PRESSED 必然落在带内（≈y 270），之后继续上行 dy 照常累计 ⇒ 判定成立。
 *      ② 之所以完全没反应，正是因为它的上沿 306 比 ≈270 还低 ⇒ PRESSED 派发在带外。
 *   ⚠️ 代价（用户 2026-09-27 明确接受）：从底边起手要滑得更长 —— 最下面 ≈270..360
 *      那一段不上报，等于白滑约 90px，总滑程约 120px 才触发。想更灵敏就得把横条
 *      往上挪（③ 版的位置），用户选了「横条贴底」。**别再擅自把横条挪上去。**
 *
 * ⚠️ 本层**不可见**（remove_style_all、无背景/边框、只吃事件），所以它的外角允许超出内切圆：
 *   (74,360) / (285,360) 到圆心 (180,180) 是 208.9 / 208.4 px > 180。
 *   这**不是**「控件切出圆」—— 圆外那几 px 玻璃本身不显示、面板也不会在那儿上报，纯占位。
 *   可见控件仍然必须 < 180（判据见 skill sdgoods-screenshot「圆内布局」一节）。
 * ⚠️ 与键盘的关系：靠**层级**，不是靠几何 —— bind 末尾 lv_obj_move_background() 把本层
 *   压到最底，键盘的键与兜底垫（y 141..266）始终在本层之上。第三版把它缩成小横条是想
 *   躲这个冲突，但躲错了方向：让两者不打架的是层级。
 * ⚠️ 与左缘返回手势（swipe_back：捕获层 x 0..55、起手需 x≤24）不重叠：本区左沿 74。
 * ⚠️ CC 各页底部的开关 / Scan 等按钮都在本 bind **之前**建 ⇒ 压在本层之上（见 open_wifi 注释）。
 * ⚠️ 纵向判定仍只要 dy < -50（不改）：手指可以从区内起手后滑出捕获区，PRESS_LOCK
 *    保证 RELEASED 一定回到捕获层（见下面 bind 里的说明）。 */
#define SWIPE_UP_W    212
#define SWIPE_UP_H    128   /* 360 - 232：铺到屏幕最下沿（含面板不上报的 ≈270..360 死区） */
#define SWIPE_UP_X     74   /* 左沿 = 180 - W/2 - 1（居中，右沿 285） */
#define SWIPE_UP_Y    232   /* 上沿比可报区下沿（≈269）再留 37px 余量 */

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
