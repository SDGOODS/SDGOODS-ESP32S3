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

esp_err_t touch_input_init(void);
void key_input_init(void);
void power_key_poll(void);   /* 主循环轮询电源键，短按返回/关菜单，长按松手后关机 */
bool power_key_long_hold_active(void);
lv_indev_t *touch_input_get_indev(void);  /* 返回已注册的指针 indev 句柄（备用） */
lv_indev_state_t touch_input_get_state(void);  /* 最近一次触摸读取的状态（RELEASED/PRESSED） */
void touch_input_get_point(lv_point_t *p);     /* 最近一次触摸读取的坐标 */
