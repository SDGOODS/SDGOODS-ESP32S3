/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（板级支持包 BSP）· 应用 SDK（提供给上架 app 的极薄库）
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于平台层，以 Apache-2.0 发布：可自由商用、可闭源分发。
 *
 * 第三方 / 第一方 app 在上架到启动器时，必须提供「返回启动器」入口，
 * 统一调用 sdgoods_return_to_launcher()。持久数据一律走 sdgoods_appdata_*，
 * 落在高 16M 的 appdata 分区、按 app_id 隔离（设计稿 §1 硬约束）。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

/* 设备启动模式（单应用 / 多应用）检测：app 常用它来决定「返回启动器」入口是否存在、
 * 以及界面该给 Power（关机）还是 Exit（返回启动器）。判定口径见该头文件。 */
#include "sdgoods_device_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 返回启动器（factory）并重启。所有上架（多应用模式）app 的「返回」按钮统一调它。
 *
 * 返回值（v2，2026-09-19）：
 *   ESP_OK                —— 已把下次启动指到 factory 并重启（本函数的正常路径**不返回**，
 *                            因为 esp_restart() 在内部；返回 ESP_OK 只出现在极端竞态下）。
 *   ESP_ERR_INVALID_STATE —— **本固件不是「被启动器管理的 app」**（单应用主机固件，
 *                            或启动器宿主自己），此时本函数**不重启、不改 otadata**，
 *                            只打一条 `return-to-launcher refused: ...` 的 ERROR 日志。
 * ⇒ 界面上的「返回启动器」入口请先用 `sdgoods_device_is_managed_app()` 判断再显示
 *   （平台外壳/控制中心的第 5 个按钮已经这么做了）；直接调也不会出事故，但会「按了没反应」，
 *   日志里有 refusal 原因可查。
 * ⚠️ 单应用固件**不要**靠它做 OTA 后的重启：它的标准 A/B 自更新有自己的重启路径，
 *    调本函数只会被拒（见 docs/APP_SDK.md §3.3）。
 *
 * 说明：返回值从 void 改为 esp_err_t 是**向后兼容**的 —— C 允许忽略返回值，
 *      老代码原样调用照常通过编译。 */
esp_err_t sdgoods_return_to_launcher(void);

/* ---- appdata：按 app_id 隔离的持久数据目录（高 16M 的 appdata 分区）----
 * 每个 app 用自己唯一的 app_id 当目录名，互不越界；卸载/开机孤儿清理会整目录回收。
 *
 * ⚠️⚠️ 平台分区表**不一定有** appdata 分区（2026-09-19 补记，SDK 文档此前没写）：
 *   多应用固件（含本平台的 app0 / 启动器）都有；**单应用固件若自带精简分区表，
 *   很可能只有 nvs + factory**。此时 sdgoods_appdata_mount() 返回 ESP_ERR_NOT_FOUND，
 *   而路径根本不存在 —— **拿着拼出来的路径直接 fopen/mkdir 会失败，甚至写坏别处**。
 *   官方建议：单应用固件沿用平台分区表（见 docs/SINGLE_APP_FIRMWARE.md），
 *   但 app 代码仍必须写兜底分支，因为**第三方固件不受你控制**：
 *
 *     char dir[64];
 *     const bool fat_ok = (sdgoods_appdata_begin(SDG_APP_ID, dir, sizeof(dir)) == ESP_OK);
 *     if (fat_ok) {
 *         // 大对象（日志、音频、缓存、多文件）走 FAT 文件，快且不磨损 NVS
 *         ...
 *     } else {
 *         // 退 NVS：适合几百字节以内的键值（设置、计数、上次状态）
 *         sdgoods_nvs_ensure();          // 平台层统一入口，别自己 nvs_flash_init
 *         nvs_open("myapp", NVS_READWRITE, &h); ...
 *     }
 *
 * 一句话判据：**「appdata 拿不到」是正常返回值，不是异常** —— 必须能跑通第二条路。
 * 想让界面/日志反映真实存储位置，用 sdgoods_appdata_available() 一句话判断。 */

/* 本固件的分区表里是否存在 appdata(data/fat) 分区。**只查找、不挂载**，随时可调。 */
bool sdgoods_appdata_available(void);

/* ★ 推荐入口：挂载 appdata → 建好本 app 的私有目录 → 返回可用的绝对路径。
 *   out 收到形如 "/appdata/<app_id>/"，之后直接 fopen/snprintf 拼子文件即可。
 *
 *   ESP_OK                 —— 目录已就绪，out 有效
 *   ESP_ERR_NOT_FOUND      —— 本固件没有 appdata 分区（单应用精简分区表）⇒ **退 NVS**
 *   ESP_ERR_INVALID_ARG / ESP_ERR_INVALID_SIZE —— 参数问题（app_id 为空 / out 太小）
 *   其他（ESP_ERR_NO_MEM、ESP_FAIL…）—— 挂载或建目录失败 ⇒ 同样退 NVS
 *   ⚠️ 失败时 out 的内容不可用，**不要**用它的半成品路径去 fopen。 */
esp_err_t sdgoods_appdata_begin(const char *app_id, char *out, size_t out_len);

/* 挂载 appdata 分区（幂等：重复调用安全）。返回 ESP_OK 表示可用。
 * 一般不用直接调 —— 用 sdgoods_appdata_begin() 更省事（它连目录一起建好）。 */
esp_err_t sdgoods_appdata_mount(void);

/* 卸载 appdata 分区。 */
void sdgoods_appdata_unmount(void);

/* 取得某 app 的私有目录绝对路径（如 /appdata/<app_id>/）。out 至少 64 字节。 */
esp_err_t sdgoods_appdata_path_for(const char *app_id, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
