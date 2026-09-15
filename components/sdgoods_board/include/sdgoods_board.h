/*
 * SDGOODS 开放平台基础工程 · 平台层（BSP）
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于平台层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

#pragma once

/*
 * sdgoods_board.h —— 谷仓次元屏 / 谷仓电子徽章 板级支持包（BSP）总入口
 *
 * 设备：ESP32-S3-R8 + 360x360 圆形 ST77916 QSPI 屏 + CST816 电容触摸
 *
 * 二次开发建议
 * ------------
 * · 应用代码请放在 main/apps/ 下，用 tools/new_app.py 生成骨架。
 * · 这个目录（components/sdgoods_board）是平台层：引脚、屏驱动、LVGL 移植、
 *   触摸、音频、应用框架。**一般不需要改**，除非你要换硬件或改平台行为。
 * · 唯一鼓励改的文件是 include/board_pins.h（引脚定义）。
 * · 平台大缓冲要显式申请 PSRAM（heap_caps_malloc + MALLOC_CAP_SPIRAM）；
 *   但 LVGL 的绘制缓冲**不能**放 PSRAM（QSPI DMA 弹跳会出黑条），
 *   详见 src/lvgl_port.c 的注释。
 */

/* 给应用层的接口（导航钩子 + UI 栅格） */
#include "sdgoods_ui.h"
#include "sdgoods_hooks.h"

/* 硬件与显示 */
#include "board_pins.h"
#include "st77916.h"
#include "lvgl_port.h"
#include "touch_input.h"
#include "power_off.h"

/* 板载外设与服务 */
#include "audio_recplay.h"
#include "hw_info.h"
#include "wifi_scan.h"
#include "ble_scan.h"

/* 调试能力（改完 UI 用它截屏自证） */
#include "screenshot.h"

/* 应用框架：所有应用统一「顶部下滑菜单 / 退出 / 暂停」体验 */
#include "ui_app_shell.h"
#include "ui_swipe_back.h"
#include "ui_boot.h"
