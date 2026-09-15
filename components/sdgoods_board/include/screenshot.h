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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 「一键截屏」：把当前活动屏幕用 LVGL 快照渲染成 RGB565 全屏位图，
 * 再以 base64 分块经 USB 串口（console）发给电脑端脚本 screenshot_recv.py。
 *
 * 触发方式（两种都可用）：
 *   1) 设备端：顶部下滑打开「菜单」-> 点「截屏」；
 *   2) 电脑端：向串口写一个字符 's'（`python3 screenshot_recv.py -t` 会自动发）。
 *
 * 电脑端：
 *   python3 screenshot_recv.py                       # 只监听，等设备端点「截屏」
 *   python3 screenshot_recv.py -t                    # 自动发 's' 触发并接收
 *   python3 screenshot_recv.py -p /dev/cu.usbmodem21301 -n 3
 *
 * 说明：
 *   - 位图缓存在 PSRAM（360x360x2 ≈ 253KB），首次截屏时分配一次并复用。
 *   - 传输期间会临时静音所有日志，避免日志与数据行交错。
 *   - 截屏数据是「当前活动屏幕的渲染结果」，不含设备菜单浮层（点按钮时会先关菜单）。
 */

/** 初始化：创建 150ms 轮询定时器（LVGL 线程）+ 串口触发任务。建议开机调用一次。 */
void screenshot_init(void);

/** 请求截屏（任意线程可调用；只置标志，实际抓帧由 LVGL 线程完成）。 */
void screenshot_capture_async(void);

#ifdef __cplusplus
}
#endif
