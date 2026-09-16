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

/**
 * @file sdgoods_caps.h
 * @brief 次元屏 BSP 基础能力登记表。
 *
 * 平台层各模块在初始化时调用 sdgoods_caps_add() 登记自己提供的能力；
 * 网页端通过串口发送 '?' 查询 SDGOODS-CAPS: 行，据此判断当前固件是否支持
 * 某个能力（例如截屏 SHOT）。若不支持，网页提示开发者从 BSP（sdgoods_board
 * 组件）把对应能力加进固件、重新编译并烧录。
 *
 * 这样「截屏」等就作为 BSP 的**可选基础能力**存在：默认开启，固件也可按需关闭，
 * 而网页端总是能先探明能力再决定如何提示，而不是静默失败。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef uint32_t sdgoods_caps_t;

#define SDGOODS_CAP_NONE        (0u)
#define SDGOODS_CAP_SCREENSHOT  (1u << 0)   /* 串口 's' 截屏（sdgoods_screenshot） */

/* 后续可扩展：SDGOODS_CAP_BLE_PAIR / SDGOODS_CAP_WIFI_PROVISION / ... */

/** 登记 / 撤销一项能力（初始化时调用一次） */
void sdgoods_caps_add(sdgoods_caps_t caps);
void sdgoods_caps_remove(sdgoods_caps_t caps);

/** 当前固件已登记的能力位掩码 */
sdgoods_caps_t sdgoods_board_caps(void);

/** 是否支持某项能力 */
bool sdgoods_caps_has(sdgoods_caps_t cap);

/** 逗号分隔的能力名，如 "SHOT"；无任何能力时返回空串。供串口 '?' 查询回传。 */
const char *sdgoods_caps_names(void);
