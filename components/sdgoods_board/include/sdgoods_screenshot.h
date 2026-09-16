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
 * 在板端用 vendored 的 jpegenc 组件编码成 JPEG（典型 20~50KB），再以**二进制 blob**
 * 经 USB 串口发给电脑端脚本 screenshot_recv.py（直接落 .jpg）。
 *
 * 触发方式：**向串口写一个字符 's'**（应用外壳菜单里的「截屏」按钮已移除，
 * 设备端没有别的触发入口）——`python3 screenshot_recv.py -t` 会自动发。
 * 也可以在任意应用里调用 sdgoods_screenshot_capture() 自己触发。
 *
 * 传输协议（详见 sdgoods_screenshot.c）：
 *   ===SHOT-BEGIN w=360 h=360 bpp=16 fmt=1 swap=0 bytes=24567===
 *   <bytes 个 JPEG 字节（fmt=1）或原始 RGB565 字节（fmt=0），无换行分帧>
 *   ===SHOT-END===
 * 不再用 base64；像素部分靠 BEGIN 头里的 bytes 字段「精确读取这么多字节」，不依赖换行分帧。
 * fmt=1 时 PC 直接落 .jpg；fmt=0（JPEG 缓冲分配失败兜底）时 PC 转 PNG。旧固件无 fmt 字段按 0 处理。
 *
 * 电脑端：
 *   python3 screenshot_recv.py -t                    # 自动发 's' 触发并接收
 *   python3 screenshot_recv.py -p /dev/cu.usbmodem21301 -n 3
 *
 * 说明：
 *   - 位图缓存在 PSRAM（360x360x2 ≈ 253KB），首次截屏时分配一次并复用；JPEG 输出缓冲同样在 PSRAM。
 *   - 传输期间会临时静音所有日志，避免日志与数据交错。
 *   - 截屏数据是「当前活动屏幕的渲染结果」；抓帧完成**之后**才显示「截图中」浮层
 *     （顶层，带进度条），所以存下来的图里不会带上这个浮层。
 *   - 抓帧 + JPEG 编码 + 浮层 + 进度条都在 LVGL 线程完成；串口任务只读编码结果灌进 TX FIFO。
 *   - 实测单张 JPEG 约 2~5 秒（USB-Serial-JTAG 链路上限约 10.5 KB/s，物理瓶颈）；
 *     退回原始 RGB565 时约 25 秒。
 */

/** 初始化：创建 150ms 轮询定时器（LVGL 线程）+ 串口触发任务。建议开机调用一次。 */
void sdgoods_screenshot_init(void);

/** 请求截屏（任意线程可调用；只置标志，实际抓帧由 LVGL 线程完成）。 */
void sdgoods_screenshot_capture(void);

#ifdef __cplusplus
}
#endif
