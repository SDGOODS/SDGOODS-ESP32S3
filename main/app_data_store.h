/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于应用层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* app_data_store —— 「持久数据该写哪儿」的参考实现（供二次开发照抄）。
 *
 * 背景：平台的高 16M `appdata`(data/fat) 分区**不是每块固件都有**。
 *   多应用固件（本工程 / 启动器）有；**单应用固件若自带精简分区表就可能没有**。
 *   所以每个 app 都必须能「没有 appdata 也照常跑」——本模块就是那条兜底路线：
 *
 *     appdata 可挂载  → 文件写在  /appdata/<app_id>/<file>   （大对象 / 多文件 / 频繁写）
 *     否则            → 键值退到 NVS namespace "appstor"      （小配置 / 计数 / 状态）
 *
 * 界面可用 app_data_store_backend() 把「真实用了哪一种」显示出来（别写死）。
 */

/* 开机调用一次：探测后端 → 读/写一次计数（顺便验证写路径真的通）→ 打日志。 */
void app_data_store_init(void);

/* 实际生效的后端："appdata" / "NVS"（初始化前调用返回 "--"）。 */
const char *app_data_store_backend(void);

/* 本 app 私有数据目录（appdata 可用时形如 /appdata/<app_id>）；不可用时返回 ""。 */
const char *app_data_store_dir(void);

/* 开机次数（本模块每次启动 +1，用于证明数据真的跨重启留存了）。 */
unsigned app_data_store_boot_count(void);

/* appdata 的空闲容量（MB）。没有 appdata 或读取失败返回 0。 */
float app_data_store_free_mb(void);

#ifdef __cplusplus
}
#endif
