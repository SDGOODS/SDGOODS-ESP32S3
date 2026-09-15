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

/* 贪吃蛇小游戏：像素风网格 + 触摸滑动控制方向
 * 触摸操作：点击屏幕开始；游戏中向某方向滑动 = 蛇往该方向走；
 *           电源键返回应用页；游戏结束后点击屏幕重玩
 * 计分：每吃一个食物 +10，蛇越长下落越快 */

void ui_snake_start(void);
void ui_snake_poll(void);
