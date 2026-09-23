/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（板级支持包 BSP）· 多应用启动器
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

/*
 * sdgoods_slot_cover.h —— 槽内「主页封面图」的读取
 *
 * 为什么需要它
 * ------------
 * 启动器主页的每个 app 图标原本只画「中性灰圆盘 + app_id 前两个字符」。
 * 作者在平台上架时会传封面图（市场卡片用的那张首图），把这张图也送到设备上，
 * 主页就是真正的封面而不是字母占位。
 *
 * 封面存在哪（**这是本文件唯一的格式出处，生成端照它实现**）
 * -----------------------------------------------------------
 * 每个 **app 槽（ota_N）的末尾 56 KiB** 是启动器的保留区：槽物理 3 MiB、app 上限
 * 2.9 MiB（`SDGOODS_SLOT_SAFE_BYTES`），这段谁也够不到 —— 所以封面不占 app 体积、
 * 不需要动 app 镜像本身（`esp_ota_end` / bootloader 校验的仍是原样的 app 镜像）。
 *
 * 块布局（全部小端；块起点 = 槽起始 + 槽大小 - `SDGOODS_SLOT_COVER_OFFSET_FROM_END`）：
 *
 * | 偏移 | 长度 | 字段 | 说明 |
 * |---|---|---|---|
 * | 0x00 | 8  | `magic`    | ASCII `"SDGCOVER"`（无 NUL）|
 * | 0x08 | 1  | `version`  | = 1 |
 * | 0x09 | 1  | `format`   | = 1 = RGB565A8：先 RGB565 平面（2B/px）再 A8 平面（1B/px）|
 * | 0x0A | 2  | `width`    | 必须 = `SDGOODS_SLOT_COVER_EDGE` |
 * | 0x0C | 2  | `height`   | 同上 |
 * | 0x0E | 2  | `reserved` | 0 |
 * | 0x10 | 4  | `data_len` | = w×h×3 |
 * | 0x14 | 4  | `crc32`    | `zlib.crc32(data)`（= `esp_rom_crc32_le`，同一套 CRC-32）|
 * | 0x18 | 40 | `app_id`   | = 槽内 app 的 `esp_app_desc_t.project_name`，NUL 结尾 |
 * | 0x40 | …  | `data`     | RGB565 平面 ‖ A8 平面 |
 *
 * ⚠️ RGB565 平面**不带** `LV_COLOR_16_SWAP` 的字节序处理 —— 本设备
 *    `CONFIG_LV_COLOR_16_SWAP=y`，所以生成端必须把 RGB565 的高低位字节对调后再落盘
 *    （LVGL 内置解码器对 VARIABLE 源是「直接给指针」，不会替你做这个转换）。
 *    生成端一律以真机截屏核对，不要靠推理。
 *
 * 为什么块里要带 `app_id`
 * ------------------------
 * 槽会被反复复用：同一个槽今天装 A、明天装 B。若只认 magic，B 就会顶着 A 的封面。
 * 所以读取时**必须**拿块里的 `app_id` 与槽内 app 镜头上实读的 `project_name` 逐字比对
 * （`esp_app_desc_t`，flash 才是真相源），不一致就当「没有封面」。
 * ⚠️ 不要拿 `sdgoods_slot_entry_t.app_id` 比：那份 manifest 里的 app_id 在「平台 OTA
 *    安装」路径上会被写成平台的 Firmware.id，与 project_name 不是同一个标识
 *    （见 slot_manifest.c 的「以 flash 为准回填 manifest」注释），比了会误判。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
/* 块头的 app_id 字段长度 = 启动器对 app 身份的长度定义（SDGOODS_SLOT_APPID_MAX）。 */
#include "sdgoods_launcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 每个 app 槽末尾为封面保留的字节数（56 KiB = 14 个 4 KiB 扇区，便于整块擦写）。
 *
 * 为什么要 56 KiB：封面边长 132 时一块 = 64 + 132×132×3 = 52336 字节，48 KiB 不够
 * （历代：96→32 KiB / 112→40 KiB / 120→48 KiB / 132→56 KiB）。
 * 抬高保留区**不影响 app 上限**：槽 0x300000 − 0xE000 = 0x2F2000（3088384 字节），
 * 仍大于 SDGOODS_SLOT_SAFE_BYTES（2.9 MB = 3040870），两者相差 47514 字节。
 * ⚠️ 改这个值 = 改**分区布局约定**：`docs/MULTI_APP_DYNAMIC_SLOTS.md` §1 的封面地址表、
 *    `tools/make_cover.py` 的 RESERVE_BYTES、以及平台侧第二期的写入地址必须同步改。 */
#define SDGOODS_SLOT_COVER_RESERVE_BYTES 0xE000u

/* 封面块相对**槽末尾**的偏移：块起点 = 槽起始 + 槽大小 - 本值。 */
#define SDGOODS_SLOT_COVER_OFFSET_FROM_END SDGOODS_SLOT_COVER_RESERVE_BYTES

/* 块头长度（像素数据紧跟其后）。 */
#define SDGOODS_SLOT_COVER_HDR_BYTES 0x40u

/* 主页图标直径。封面必须正好是这个边长；读取端严格校验，不符即当没有封面
 *（宁可回退「灰色圆盘 + 字母」，也不要画出一张溢出圆屏的图）。
 *
 * 132 是按**圆屏排布**定的（用户 2026-09-21 三次要求加大：图标要大、间距也要大）：
 *   - 居中图标占屏宽 132/360 = 37%（96 时 27%、120 时 33%），一眼看过去更醒目；
 *   - 图标间距 28px（原来 8 → 16 → 22 → 28），相邻圆盘之间拉得开；
 *   - 屏是圆的 ⇒ 左右邻居会被圆弧裁掉，**可见面积约 65%**（120/22 时是 85%）——
 *     邻居仍露出一大半，天然的「左右还有更多」暗示还在。
 * ⚠️ 代价：一排连 3 个都放不下（3×132 + 2×28 = 452 > 360）。2 个以上必然横向滑动查看，
 *    这是「图标大 + 间距大」在同一块 360 圆屏上的必然取舍；`ui_launcher.c` 的滚动阈值
 *    因此是 `n >= 2` 而不是「放不下才滚」，漏了会让第 3 个图标被裁掉且够不到。
 * ⚠️ 改这里必须**成组**改：`tools/make_cover.py` 的 DEFAULT_EDGE 与 RESERVE_BYTES、
 *    `ui_launcher.c` 的 GAP、`docs/MULTI_APP_DYNAMIC_SLOTS.md` §1 的地址表，
 *    以及**板上已有的封面全部重生成**（旧边长的块会被读取端判失败 ⇒ 图标退回字母）。 */
#define SDGOODS_SLOT_COVER_EDGE 132

/* 封面格式标识（format 字段）。 */
#define SDGOODS_SLOT_COVER_FMT_RGB565A8 1
#define SDGOODS_SLOT_COVER_VERSION      1

/* 已载入内存的一张封面。`data` 指向 PSRAM 缓冲，直接喂给 lv_img 的
 * `LV_IMG_CF_RGB565A8` 描述符（LVGL 对 VARIABLE 源不拷贝、不转换）。 */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t data_len;
    const void *data;
    void   *owner;      /* 内部私有：缓冲头，用于 free。调用方不要碰 */
} sdgoods_slot_cover_t;

/* 读槽 slot_idx 的封面。成功返回 ESP_OK 且 out 可用（用完必须 sdgoods_slot_cover_free）。
 *
 * ESP_ERR_NOT_FOUND —— 该槽没有封面，或封面不属于当前槽内的 app（正常返回值，不是异常）
 * ESP_ERR_INVALID_ARG / ESP_ERR_INVALID_STATE —— 参数 / 槽号不合法
 * ESP_ERR_NO_MEM    —— PSRAM 分配失败
 *
 * 只读查询，**任何固件都可调用**（不加权限闸门）。 */
esp_err_t sdgoods_slot_cover_load(int slot_idx, sdgoods_slot_cover_t *out);

/* 释放 sdgoods_slot_cover_load 拿到的缓冲。可重复调用（内部指针置空）。 */
void sdgoods_slot_cover_free(sdgoods_slot_cover_t *cover);

#ifdef __cplusplus
}
#endif
