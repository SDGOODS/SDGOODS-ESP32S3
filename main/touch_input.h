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
