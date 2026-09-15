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

/**
 * Play the SDGOODS boot animation, then create and show the home screen.
 *
 * This function blocks for about 2 seconds while the animation plays.
 * It turns on the backlight, creates the boot screen, runs the LVGL
 * timer loop, deletes the boot screen and finally loads the home screen.
 */
void ui_boot_show(void);
