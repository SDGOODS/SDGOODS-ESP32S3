/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于应用层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

#pragma once

/*
 * 新应用骨架（模板）—— 同时也是一个真实可运行的示例应用
 *
 * 这个应用是「照着抄」用的参考实现：完整演示了建屏、接入标准应用框架
 * （顶部下滑菜单 / 音量 / 退出 / 截屏）、退出清理、poll 推进。
 *
 * 用 tools/new_app_project.py 派生新应用时，就是复制这一对 .c/.h 并改名替换：
 *
 *     python3 tools/new_app_project.py 应用名
 */

/* 进入 / 显示本应用（注册在 main/apps/apps_registry.c 的 s_apps[] 表里） */
void ui_app_template_show(void);

/* 每帧推进（由 apps_registry.c 汇总后交给平台主循环；不在前台时立即返回） */
void ui_app_template_poll(void);
