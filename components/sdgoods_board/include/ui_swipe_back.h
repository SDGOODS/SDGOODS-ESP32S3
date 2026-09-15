#pragma once
#include "lvgl.h"

/* 在页面屏幕上挂一个全屏透明“返回捕获层”：空白处从左滑到右 → 调用 on_back 返回。
 * 捕获层置于控件之下，因此页面上的按钮仍可正常点击；直接从按钮上起手的滑动不触发返回（符合预期）。 */
void ui_swipe_back_bind(lv_obj_t *scr, void (*on_back)(void));
