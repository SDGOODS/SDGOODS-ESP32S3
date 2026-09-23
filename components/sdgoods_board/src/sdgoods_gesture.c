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

#include "sdgoods_gesture.h"

#include "sdgoods_swipe_up.h"
#include "sdgoods_swipe_back.h"

void sdgoods_app_on_gesture(lv_obj_t *scr, sdgoods_gesture_t g, void (*cb)(void))
{
    if (!scr || !cb) {
        return;
    }
    switch (g) {
        case SDGOODS_GESTURE_UP:
            sdgoods_swipe_up_bind(scr, cb);
            break;
        case SDGOODS_GESTURE_BACK:
            sdgoods_swipe_back_bind(scr, cb);
            break;
        default:
            break;
    }
}

void sdgoods_app_on_tap(lv_obj_t *scr, sdgoods_tap_ctx_t *ctx,
                        sdgoods_tap_cb_t on_tap, void *user_data)
{
    if (!scr || !ctx || !on_tap) {
        return;
    }
    sdgoods_tap_bind(scr, ctx, on_tap, user_data);
}
