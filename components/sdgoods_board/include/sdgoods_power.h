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

#pragma once

#include <stdbool.h>

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
void sdgoods_power_off(void);

/**
 * DEBUG ONLY: draw the exact "Power Off" overlay on the LVGL thread (same visual
 * as sdgoods_power_off) but do NOT cut board power. Lets a serial console command
 * trigger the shutdown screen so it can be captured by the screenshot tool.
 * Implemented via sdgoods_lvgl_post so it runs on the LVGL thread (safe).
 */
void sdgoods_power_off_preview(void);

/**
 * Quick "screen off / low-power" toggle for the home screen.
 *
 * First call saves the current backlight level, turns the backlight off and
 * asks the LVGL loop to pause rendering (see sdgoods_lvgl_loop). The next call
 * restores the saved brightness. Intended for the launcher home screen:
 * short-press power -> sleep, short-press again -> wake.
 *
 * This is a light, instantly-resumable low-power state (CPU mostly idle, no
 * render work); it does NOT cut board power. Use sdgoods_power_off() for that.
 */
void sdgoods_power_suspend_toggle(void);

/**
 * Enter ultra-low-power light sleep from the home screen (power-key short press).
 *
 * Turns the backlight off and puts the SoC into light sleep. Wake-up is wired to
 * the power key via ext0, so a single press wakes the device and lights the
 * screen again — WITHOUT a reboot: LVGL / peripherals / app state are all
 * preserved and execution resumes right after this call returns. (sdgoods_power_off()
 * is the real deep-sleep path where wake == cold boot.) Use this for "screen off,
 * tap power to resume".
 */
void sdgoods_power_enter_light_sleep(void);

/**
 * Enter deep sleep from the home screen (power-key short press).
 *
 * The lowest-power state: the SoC is fully powered down and wakes ONLY on the
 * physical power key (ext0). Wake == a cold boot (app_main runs again), so it is
 * slower to come back than light sleep but consumes far less.
 *
 * The battery self-latching MOSFET (BOARD_BAT_CONTROL_GPIO) is held across the
 * deep sleep via gpio_hold_en + gpio_deep_sleep_hold_en so the board stays
 * powered and can actually be woken. On wake, the device cold-boots straight
 * back to the home screen (apps go directly to their home; the launcher host
 * skips its boot GIF for this wakeup -- flagged via sdgoods_power_set_wake_skip_gif(true)
 * before entering deep sleep, consumed in sdgoods_boot_show). A CC "Power Off"
 * uses a different flag value so its (rare) deep-sleep fallback still replays the GIF.
 *
 * Use this for "screen off, lowest power"; use sdgoods_power_enter_light_sleep()
 * when a fast, in-place wake (no reboot) is preferred.
 */
void sdgoods_power_enter_deep_sleep(void);

/**
 * Mark whether the NEXT deep-sleep wake should skip the launcher boot GIF.
 *
 * The launcher host skips its boot animation on a home-short-press deep-sleep
 * wake (the user wants "wake from deep sleep -> straight to home, no boot GIF").
 * But a CC "Power Off" can also land in deep sleep (its power-cut fallback) and
 * wakes on the same EXT0 power key -- that path is a real shutdown and MUST
 * replay the boot GIF. A bare `wakeup_cause == EXT0` test cannot tell the two
 * apart, so an explicit RTC flag is used instead.
 *
 * Call sdgoods_power_set_wake_skip_gif(true) right before sdgoods_power_enter_deep_sleep()
 * (home short-press) and sdgoods_power_set_wake_skip_gif(false) before the
 * sdgoods_power_off() power-cut/fallback (CC shutdown). Stored in RTC_DATA_ATTR
 * so it survives the deep sleep; a true power cut clears it (cold boot defaults
 * to replaying the GIF).
 *
 * @param skip  true -> next EXT0 wake skips the boot GIF (home short-press),
 *              false -> next EXT0 wake replays it (CC shutdown / default).
 */
void sdgoods_power_set_wake_skip_gif(bool skip);

/**
 * Consume the wake-skip-GIF flag (read-and-clear).
 *
 * Returns the value last set by sdgoods_power_set_wake_skip_gif() and clears it
 * so a stale flag can never leak into a later boot. Call it once, guarded by
 * `esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0`, inside sdgoods_boot_show().
 *
 * @return true if the wake should skip the boot GIF.
 */
bool sdgoods_power_consume_wake_skip_gif(void);

/** True while the screen is in the suspended (low-power) state. */
bool sdgoods_power_is_suspended(void);
