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
 * @file sdgoods_console.h
 * @brief BSP 串口控制台：始终编译。轮询 USB-Serial-JTAG 的 RX FIFO，分发命令：
 *   '?'      -> 回传一行 "SDGOODS-CAPS:<能力名逗号列表>"（网页端据此检测固件能力）
 *   's'/'S'  -> 若 SDGOODS_CAP_SCREENSHOT 已登记，触发截屏（sdgoods_screenshot_capture）
 *   'X'      -> 进入二进制注入模式（把 app 镜像写进槽；见 sdgoods_console.c 文件头）
 *   'R'      -> 立即重启（esp_restart）。平台级命令，**任何固件**（启动器 / app /
 *               单应用）都有，与具体应用无关。
 *               ⚠️ 存在的理由：本机 USB-Serial-JTAG 的 DTR/RTS 脉冲复位并非每次都生效，
 *               验证脚本靠它才能把「重启」变成确定性动作（如启动失败回滚的验证）。
 *   'r'/'L' 等 -> 交给 sdgoods_console_ext_cmd（弱符号，应用层可覆盖）
 *
 * 控制台常驻，是「能力查询」的可靠应答方：即使某个具体能力（如截屏）未编入固件，
 * '?' 仍能回传（只是列表里没有该项），网页端即可明确提示「该功能未启用」。
 */
#pragma once

void sdgoods_console_init(void);
