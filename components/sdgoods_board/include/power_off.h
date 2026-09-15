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
 * Cut device power and turn the badge off.
 *
 * Releases the self-holding battery latch (BOARD_BAT_CONTROL_GPIO) so the
 * power MOSFET opens and the whole board loses power. If power is not actually
 * cut (e.g. the physical power button is still held), it falls back to deep
 * sleep and wakes on the physical key press.
 *
 * Only call this when the user explicitly requests shutdown.
 */
void system_power_off(void);
