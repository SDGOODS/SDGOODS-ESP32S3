#pragma once

/* 俄罗斯方块小游戏：像素风（PICO-8 调色板 + 深色描边方块）
 * 触摸操作：左 1/3 屏 = 左移，右 1/3 屏 = 右移，中间 = 顺时针旋转，下滑 = 加速下落
 * 电源键 = 返回应用页；游戏结束后点屏重玩 */

void ui_tetris_start(void);
void ui_tetris_poll(void);
