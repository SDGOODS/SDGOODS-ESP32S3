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

/* DEMO 入口页：圆形按钮启动器，承载 录音 / WIFI / 蓝牙 / 其他 四个演示功能。
 * 进入其中任一子页时，该子页关闭会通过 ui_nav_back() 回到本页（而非主页）。 */

void ui_demo_page_show(void);
void ui_demo_page_poll(void);
