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

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "lvgl.h"

esp_err_t sdgoods_touch_init(void);
void sdgoods_key_init(void);
void sdgoods_power_key_poll(void);   /* 主循环轮询电源键，短按返回/关菜单，长按松手后关机 */
void sdgoods_power_key_short_action(void);   /* 电源键「短按」的统一动作（真实短按松手与串口调试键 'P' 共用，保证路径一致） */
bool sdgoods_power_key_long_hold(void);
lv_indev_t *sdgoods_touch_get_indev(void);  /* 返回已注册的指针 indev 句柄（备用） */
lv_indev_state_t sdgoods_touch_get_state(void);  /* 最近一次触摸读取的状态（RELEASED/PRESSED） */
void sdgoods_touch_get_point(lv_point_t *p);     /* 最近一次触摸读取的坐标 */
void sdgoods_touch_synth_report(lv_indev_state_t st, lv_coord_t x, lv_coord_t y);
/* 合成触摸（sdgoods_tap.c）同步更新上面的轮询缓存。为什么要暴露：轮询型 app
 * （如飞机大战 READY 界面的 ready_tap_poll）读的是这里缓存的真实触摸状态，
 * 不经过 LVGL indev —— 合成点按若不同步写缓存，对这类 app 完全不可见，
 * 串口调试键 '1'..'5' 的「任何 app 都能验证手势」承诺就落空了。 */
