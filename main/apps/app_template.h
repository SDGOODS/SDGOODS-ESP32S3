/*
 * SDGOODS 开放平台基础工程 · 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Required Notice: Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 *
 * 个人学习、研究与业余项目免费使用；商业用途需事前书面授权，见 LICENSING.md。
 * 分发时必须完整保留本声明 —— 这是许可条款，不是建议。
 */

#pragma once

/*
 * 新应用骨架（模板）—— 同时也是一个真实可运行的示例应用
 *
 * 这个应用是「照着抄」用的参考实现：完整演示了建屏、接入标准应用框架
 * （顶部下滑菜单 / 音量 / 退出 / 截屏）、退出清理、poll 推进。
 *
 * 用 tools/new_app.py 生成自己的应用时，就是复制这一对 .c/.h 并改名替换：
 *
 *     python3 tools/new_app.py 应用名 "按钮文字"
 */

/* 进入 / 显示本应用（注册在 main/apps/apps_registry.c 的 s_apps[] 表里） */
void ui_app_template_show(void);

/* 每帧推进（由 apps_registry.c 汇总后交给平台主循环；不在前台时立即返回） */
void ui_app_template_poll(void);
