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
 * ⚠️ 这一条判据只属于**槽尾封面**。平台下发给「推送列表」的那种封面还没有归属的槽
 *    （见下面 `_check()` / `_adopt()`），没有可比的对象 —— 别照抄过去。
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

/* 校验**内存里**的一整块 SDGCOVER 数据（上面那张表逐字节：头 + 像素 + crc32）。
 * 不改动 blob、不分配内存 ⇒ 调用方仍拥有 blob。
 *
 * 为什么需要它：推送列表里那些「还没装进槽」的 app 也要显示封面，而平台给的正是
 * 同一个格式的块（`GET /api/firmwares/:id/cover.raw`，见 PUSH_INSTALL.md §6.3）。
 * 校验必须与 `sdgoods_slot_cover_load()` **同一个实现**，否则两条路的容错度会漂移
 * （典型症状：能装进槽的封面在推送列表里显示不出来，或反过来）。
 *
 * ⚠️ 与 load() 的区别：**不比对 app_id**。槽尾封面必须比对是因为槽会被复用（同一个槽
 *    今天装 A 明天装 B，旧封面会顶着新 app）；而这里那份封面还没有归属的槽，没有可比的
 *    对象。将来收编进槽时（§5.5.4）才由安装链把那 40 字节改成 flash 里的 project_name。
 *
 * ESP_OK
 * ESP_ERR_INVALID_ARG —— 空指针、长度不足、data_len 与实长不符、crc32 不符
 * ESP_ERR_NOT_FOUND —— magic / 版本 / 格式 / 边长 / data_len 有一项不符
 */
esp_err_t sdgoods_slot_cover_check(const void *blob, size_t len);

/* 校验一块内存数据并**接管它的所有权**（成功后 `out->owner == blob`）。
 *
 * `blob` 必须是堆分配（`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` 之类）且可 free 的指针：
 * 成功之后释放只需 `sdgoods_slot_cover_free(&out)` 一次，它会连 blob 一起 free。
 * 失败时**不接管**，blob 仍归调用方（记得自己 free），out 被清零。
 *
 * 为什么是「接管」而不是「拷贝」：一张封面 52336 字节，8 个推送项重建一次主页就是
 * 400KB+ 的额外拷贝；而这份缓冲读进来之后除了显示没有第二个消费者。
 */
esp_err_t sdgoods_slot_cover_adopt(void *blob, size_t len, sdgoods_slot_cover_t *out);

/* 把封面**就地在 RGB565 平面上灰化**（A8 平面不动），用于「推送列表里还没装」的灰封面。
 *
 * 为什么灰化必须在数据层做、不能靠样式：
 *   · `lv_obj_set_style_img_recolor()` 对 `LV_IMG_CF_RGB565A8` **确实生效**
 *     （`lv_draw_sw_img.c` 逐像素 `lv_color_mix_premult`），但它是**向黑混色 = 整体变暗**，
 *     不是去饱和 —— 彩色封面变暗之后仍然是彩的（红还是红）。
 *   · 数据层 `y = (77R + 150G + 29B) >> 8` 才是真正的去饱和，确定性最强、零 LVGL 依赖。
 * 代价：52272 字节一次线性遍历（其中 RGB565 平面 34848 字节），相对一次 flash 读取可忽略。
 *
 * 🔴 字节序：本设备 `CONFIG_LV_COLOR_16_SWAP=y`，块里的 RGB565 平面是**高低字节对调过**的
 *    （见本文件顶部的警告）。算法必须先把 16 位值还原成 RGB565 再拆 R/G/B，算完再对调写回；
 *    漏了这一步会把 R/B 权重套错通道。真机判据：灰封面必须是**中性的灰**，不偏黄也不偏蓝。
 */
void sdgoods_slot_cover_grayscale(sdgoods_slot_cover_t *cover);

/* 把一整块 SDGCOVER 写进**槽尾保留区**（安装流程把推送项那块封面收编进槽时用）。
 *
 * 与 `_load()` 是严格的一对：写进去的东西必须能被 `_load()` 读回来，否则这个函数等于
 * 把 52 KB 悄悄写进了一个没人看的地方。为此它替调用方多做了两件事：
 *
 *   ① **把块头里的 app_id 改成槽内 app 的 `project_name`**
 *      （调 `sdgoods_slot_read_desc()` 从 flash 实读，不是拿 manifest 缓存）。
 *      平台下发的块里带的是平台自己的标识，原样落盘会被 `_load()` 的 app_id 比对拒掉 ⇒
 *      症状是「封面明明写了、槽里也有，图标却是字母占位」，极难倒推。
 *      改的是**本地头副本**，不动调用方的缓冲（它可能还在被显示用）。
 *      ⚠️ 这一改**不会**让 CRC 失效：crc32 只覆盖像素平面（见文件顶部的格式表）。
 *   ② **落盘之前先校验整块**（含 CRC）—— 坏块绝不入库，否则每次读都是一次白读。
 *
 * 🔴 擦的是 `part->size − 0xE000` 起的 0xE000 字节，而 app 最多到
 *    `0x300000 − 0xE000 = 0x2F2000 = 3088384`，平台闸门 `SDGOODS_SLOT_SAFE_BYTES`
 *    是 3040870 ⇒ 余量 47514 字节（与文件顶部「为什么是 56 KiB」那段的算法一致）。
 *    **一旦有人放宽体积闸门，这里就会擦掉 app 的尾部**。
 *
 * 写完后会回读 magic 做一次确认：抓「erase/write 都返回 OK 但内容没落地」这种最贵的
 * 静默失败（只读 8 字节，成本可忽略）。
 *
 * ⚠️ 只应由**启动器宿主**的安装流程调用（内部有 `sdgoods_device_is_launcher_host()`
 *    闸门，非宿主返回 ESP_ERR_INVALID_STATE 且不碰 flash）。
 *
 * ESP_OK
 * ESP_ERR_INVALID_ARG   —— 空指针；或块内容非法（头/长度/CRC 任一项不符）
 * ESP_ERR_NOT_FOUND     —— 槽号非法；或槽内读不出 app desc（不知道该署谁的名）
 * ESP_ERR_INVALID_STATE —— 非启动器宿主；或写完后回读不到 magic
 * 其它 —— 底层 flash 驱动返回的错误
 */
esp_err_t sdgoods_slot_cover_save(int slot_idx, const void *blob, size_t len);

/* 释放 sdgoods_slot_cover_load / _adopt 拿到的缓冲。可重复调用（内部指针置空）。
 * ⚠️ 对只经过 `_check()` 的那块缓冲**不要**调它（那个接口不接管所有权）。 */
void sdgoods_slot_cover_free(sdgoods_slot_cover_t *cover);

#ifdef __cplusplus
}
#endif
