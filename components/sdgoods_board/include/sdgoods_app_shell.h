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

#include "lvgl.h"

void sdgoods_app_shell_init(void);
void sdgoods_app_shell_bind(lv_obj_t *scr);
void sdgoods_app_shell_set_exit_cb(void (*cb)(void));
void sdgoods_app_shell_set_pause_cb(void (*cb)(void));
void sdgoods_app_shell_set_resume_cb(void (*cb)(void));

void sdgoods_app_shell_menu_open(void);
void sdgoods_app_shell_menu_close(void);
bool sdgoods_app_shell_menu_is_open(void);
bool sdgoods_app_shell_is_app_active(void);

void sdgoods_app_shell_leave(bool to_home);

void sdgoods_app_volume_up(void);
void sdgoods_app_volume_down(void);
int  sdgoods_app_volume_get(void);
