#pragma once

/* 贪吃蛇小游戏：像素风网格 + 触摸滑动控制方向
 * 触摸操作：点击屏幕开始；游戏中向某方向滑动 = 蛇往该方向走；
 *           电源键返回应用页；游戏结束后点击屏幕重玩
 * 计分：每吃一个食物 +10，蛇越长下落越快 */

void ui_snake_start(void);
void ui_snake_poll(void);
