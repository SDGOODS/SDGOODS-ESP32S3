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

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 把 w*h 的**标准 RGB565** 位图（2 字节/像素、R 在 16 位值的高 5 位，即小端内存里
 * 低字节在前）编码成 JPEG，写入 out（容量 out_cap 字节）。
 *
 * 注意：LVGL 快照（LV_COLOR_16_SWAP=1）的内存字节序是**交换过的**（高字节在前），
 * 不能直接喂进来——调用方需先按 16 位逐像素交换字节，否则 R/B 通道错乱（灰底变绿）。
 *
 * @return 成功返回 JPEG 字节数（>0）；缓冲区不足或编码失败返回 <=0。
 *         调用方据此决定是发送 JPEG 还是退回原始 RGB565。
 */
int sdgoods_jpeg_encode_rgb565(const void *rgb565, int w, int h, void *out, int out_cap);

#ifdef __cplusplus
}
#endif
