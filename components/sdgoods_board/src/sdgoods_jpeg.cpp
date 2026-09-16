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

/* 设备端 JPEG 编码：把 LVGL 快照得到的 RGB565 直接编码成 JPEG，供串口截屏走
 * 高压缩通道（典型 360x360 UI 画面 20~50KB，相较原始 259KB 省 ~6~13 倍）。
 * 编码器来自 vendored 组件 jpegenc（bitbank2/JPEGENC，Apache-2.0），原生支持 RGB565。 */

#include "JPEGENC.h"
#include "sdgoods_jpeg.h"

extern "C" int sdgoods_jpeg_encode_rgb565(const void *rgb565, int w, int h, void *out, int out_cap)
{
    JPEGENC j;
    if (j.open((uint8_t *)out, out_cap) != JPEGE_SUCCESS) {
        return -1;
    }
    JPEGENCODE e;
    /* JPEGE_PIXEL_RGB565：编码器直接读 2 字节/像素，无需先转 RGB888（省内存省时间）。
     * JPEGE_SUBSAMPLE_444：UI 文字偏小，444 比 420 更能保住彩色边缘清晰度。
     * JPEGE_Q_BEST：最高质量（截屏对清晰度敏感）。 */
    if (j.encodeBegin(&e, w, h, JPEGE_PIXEL_RGB565, JPEGE_SUBSAMPLE_444, JPEGE_Q_BEST) != JPEGE_SUCCESS) {
        return -1;
    }
    if (j.addFrame(&e, (uint8_t *)rgb565, w * 2) != JPEGE_SUCCESS) {
        return -1;
    }
    int sz = j.close();
    return (sz > 0) ? sz : -1;
}
