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

/*
 * 启动器开机动画「跳过」标志。
 *
 * 置位时机（都发生在「要回到启动器」的瞬间）：
 *   1. 从 app 点「返回」→ launch_slot(-1) 软复位前；
 *   2. 在 app 内关机 → sdgoods_power_off() 硬断电前。
 * 消费时机：启动器开机，sdgoods_boot_show() 读取并清除（消费一次）。
 *
 * 用 NVS 存（掉电保留），因此同时覆盖「软复位」与「硬断电再上电」两条返回路径。
 * 注意 RTC/rtc_noinit 在硬断电时会被清掉，无法用于关机路径，故不用。
 *
 * 取不到 NVS（未初始化等）时：设置端静默放弃，读取端返回 false（即播放动画），
 *   不会崩溃；最坏情况只是「该跳过的那次仍播动画」，不影响设备启动。
 */
void sdgoods_boot_set_skip_next(bool skip);
bool sdgoods_boot_should_skip(void);
