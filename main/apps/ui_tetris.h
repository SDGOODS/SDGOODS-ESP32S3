/*
 * SDGOODS 开放平台基础工程 · 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Required Notice: Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 *
 * 个人学习、研究与业余项目免费使用；商业用途需事前书面授权，见 LICENSING.md。
 * 分发时必须完整保留本声明 —— 这是许可条款，不是建议。
 */

#pragma once

/* 俄罗斯方块小游戏：像素风（PICO-8 调色板 + 深色描边方块）
 * 触摸操作：左 1/3 屏 = 左移，右 1/3 屏 = 右移，中间 = 顺时针旋转，下滑 = 加速下落
 * 电源键 = 返回应用页；游戏结束后点屏重玩 */

void ui_tetris_start(void);
void ui_tetris_poll(void);
