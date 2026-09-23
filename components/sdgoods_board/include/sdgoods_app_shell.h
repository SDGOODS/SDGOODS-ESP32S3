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

/* ---- 设备级控制中心（顶部下滑唤出）----------------------------------------
 * 实现在平台层 components/sdgoods_launcher（见 sdgoods_cc.h）：音量 / 亮度 / 数据 /
 * 电量 / 第 5 个按钮（app 内为 Exit=返回启动器，宿主/单应用为 Power=关机）。
 *
 * ⚠️ 为什么这里只声明：sdgoods_launcher 是**依赖** sdgoods_board 的下游组件，BSP 反向
 * 直接依赖它会形成循环依赖。故 app_shell 内提供一个**弱默认空实现**，由 sdgoods_launcher
 * 的强符号覆盖（与本工程 `sdgoods_console_ext_cmd` 的既有惯例一致）。
 * 应用工程都带 sdgoods_launcher（返回启动器 / appdata 就在里面）⇒ 实际上总能生效。
 *
 * app 侧由 sdgoods_app_shell_bind() 的顶部下滑手势自动调用，无需自己接。 */
void sdgoods_cc_open(void);

/* 系统浮层（控制中心）开 / 关通知：由控制中心在打开 / 关闭时回调，
 * 外壳据此触发 app 注册的 pause_cb / resume_cb（游戏类 app 在浮层打开时暂停）。
 * 由平台层控制中心调用；app 不需要直接调它。 */
void sdgoods_app_shell_notify_overlay(bool open);
