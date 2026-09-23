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

/*
 * sdgoods_gesture.h —— 给应用（和写应用的 AI）用的「应用级手势回调」薄封装
 *
 * 为什么要有这一层：平台底层已经有经过实战打磨的手势原语
 *   - sdgoods_swipe_up_bind(scr, on_up)    底部空白区上滑 → on_up
 *   - sdgoods_swipe_back_bind(scr, on_back) 空白处左滑到右 → on_back
 *   - sdgoods_tap_bind(obj, ctx, on_tap, ud) 带位移守卫的点按（见 sdgoods_tap.h）
 * 但它们都是「屏幕级」的，app 作者（尤其 AI）要自己拿 scr、自己理解捕获层语义。
 *
 * 本文件把它们收敛成一个**统一、可发现**的入口：
 *   - sdgoods_app_on_gesture(scr, 手势类型, cb)  —— 一行绑定「上滑 / 返回 / 点按」。
 *   - 内部全部复用上面的成熟原语，**不新写任何手势检测逻辑**（避免重蹈 LVGL 接管 / 误触的坑）。
 *
 * ⚠️ 设备级导航（顶部下滑出控制中心、上滑回主页）由 sdgoods_app_shell_bind(scr)
 *    自动接管，不要在 app 里再绑一遍。本文件只管「app 自己想要的手势」。
 *
 * 用法（在 app 的 show() 里、建好 s_scr 之后调）：
 *   sdgoods_app_on_gesture(s_scr, SDGOODS_GESTURE_BACK, my_back);   // 左滑返回
 *   sdgoods_app_on_tap(s_scr, &s_tap_ctx, my_tap_cb);               // 点按（带位移守卫）
 */

#pragma once

#include "lvgl.h"
#include "sdgoods_tap.h"   /* sdgoods_tap_ctx_t / sdgoods_tap_cb_t */

#ifdef __cplusplus
extern "C" {
#endif

/* 应用常用的手势类型。BACK = 空白处左滑到右（返回上级）；UP = 底部上滑。 */
typedef enum {
    SDGOODS_GESTURE_UP = 0,     /* 从底部空白区上滑（默认语义：回主页 / 下一页） */
    SDGOODS_GESTURE_BACK,       /* 空白处左滑到右（默认语义：返回上级 / 关闭浮层） */
} sdgoods_gesture_t;

/* 绑定一个「屏幕级」手势到 scr。cb 在对应手势发生时被调用（无参）。
 * 实现直接转发到底层 sdgoods_swipe_up_bind / sdgoods_swipe_back_bind，
 * 因此保留它们全部的行为与日志（含顶部下滑被控制中心拦截等）。
 * scr / cb 任一为空则静默忽略。可重复调用（每次新建一个透明捕获层）。 */
void sdgoods_app_on_gesture(lv_obj_t *scr, sdgoods_gesture_t g, void (*cb)(void));

/* 绑定「点按」到整个屏幕（带位移守卫：滑动掠过不算点按）。
 * ctx 由调用方提供（建议 static），生命周期需覆盖该屏 —— 直接复用底层 sdgoods_tap_bind。
 * on_tap 形如 void my_tap_cb(void *user_data)；ud 透传给它。
 * ⚠️ 不要在底层之外再用 lv_obj_add_event_cb(..., LV_EVENT_CLICKED, ...) 直接绑动作，
 *    否则会绕过位移守卫（按下滑走再松手也会触发）。 */
void sdgoods_app_on_tap(lv_obj_t *scr, sdgoods_tap_ctx_t *ctx,
                        sdgoods_tap_cb_t on_tap, void *user_data);

#ifdef __cplusplus
}
#endif
