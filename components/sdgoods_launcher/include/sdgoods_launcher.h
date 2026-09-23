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
 * sdgoods_launcher.h —— 多应用动态插槽管理（固定分区表 + 动态指派）
 *
 * 设计依据见 docs/MULTI_APP_DYNAMIC_SLOTS.md。本组件只负责「槽管理 + 状态 + 切换」，
 * 不含具体 UI（启动器菜单的渲染由 UI 层照本头文件提供的列表接口自行绘制）。
 *
 * 关键约定
 * --------
 * · 设备的 flash 才是槽真相源：每槽起始 +0x20 处的 esp_app_desc_t。
 *   NVS「sdgoods_slots」里的 manifest 只是缓存，开机自校验以 flash 为准。
 * · otadata 管「下次启哪个槽」；manifest 管「每槽是什么 app」。
 * · 切换 = esp_ota_set_boot_partition + esp_restart；app 是完整固件镜像，不能热加载。
 * · 返回启动器 = 把 boot 指回 factory(launcher) 再重启。
 * · app 数据落高 16M 的 appdata 分区（data/fat），按 app_id 隔离；卸载即清、开机孤儿清理。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "sdgoods_device_mode.h"   /* 单/多应用启动模式检测（本组件同目录公开头） */

#ifdef __cplusplus
extern "C" {
#endif

#define SDGOODS_SLOT_APPID_MAX  40
#define SDGOODS_SLOT_PKGID_MAX  40
#define SDGOODS_SLOT_VER_MAX    24
#define SDGOODS_SLOT_COUNT_MAX  16

/* app 镜像的**安全体积上限**（2.9MB）。
 * 单槽 3MB 是分区的硬边界，但两侧必须同口径：平台侧 `server/src/routes/devices.ts` 的
 * `SAFE_APP_BYTES` 就是 2.9MB（`Math.floor(2.9*1024*1024) = 3040870`）。
 * 同口径的含义是「平台放行的包设备一定装得下；设备拒绝的包平台也不会放行」——
 * 以前设备只看 3MB，于是平台放行、设备也照收，贴边写到分区最后一个扇区，
 * 一旦将来槽尺寸或分区表微调就连「下不去」和「看起来装成功了」都分不清。 */
#define SDGOODS_SLOT_SAFE_BYTES 3040870u

/* 单槽状态（与平台 DeviceAppSlot.status 对齐：EMPTY/CORRUPTED/UPDATING 为设备侧扩展） */
typedef enum {
    SDG_SLOT_EMPTY     = 0,   /* 空槽 */
    SDG_SLOT_INSTALLED = 1,   /* 已装且 desc 校验通过 */
    SDG_SLOT_CORRUPTED = 2,   /* 槽里有数据但镜像损坏 / 描述不符 */
    SDG_SLOT_UPDATING  = 3,   /* 安装中（掉电残留，下次开机标 corrupted） */
} sdgoods_slot_state_t;

/* 单槽记录（持久化到 NVS，作为 flash 的缓存） */
typedef struct {
    uint8_t  slot_idx;                                  /* 0..N-1 */
    char     app_id[SDGOODS_SLOT_APPID_MAX];            /* 平台 Firmware.id（空串 = 空槽） */
    char     package_id[SDGOODS_SLOT_PKGID_MAX];
    char     version[SDGOODS_SLOT_VER_MAX];
    uint8_t  sha256[32];
    sdgoods_slot_state_t state;
    uint32_t last_launch;                               /* 最近启动时间戳（秒） */
} sdgoods_slot_entry_t;

/* 当前槽数：运行时按实际 ota 分区数，受 CONFIG_SDGOODS_APP_SLOTS 上限约束。 */
int  sdgoods_launcher_slot_count(void);

/* 开机自检：扫描各槽 flash 上的 esp_app_desc_t，以 flash 为准校正 manifest（NVS 缓存）。
   同时兜底：若 manifest 记录的已装 app 在 flash 上找不到，标 CORRUPTED。
   ⚠️ 只有**启动器宿主**可调（否则 ESP_ERR_INVALID_STATE）。 */
esp_err_t sdgoods_launcher_self_check(void);

/* 开机孤儿清理：删除高 16M 的 appdata 中，已没有任何已装 app 对应的 appdata/<app_id>/。
   详见设计稿 §2「appdata 孤儿清理」。
   ⚠️ 只有**启动器宿主**可调 —— 而且这个闸门是**必须**的：非启动器设备上没有任何已装槽，
      全部 appdata 目录都会被判成「孤儿」⇒ 每次开机把 app 自己的数据删光。 */
esp_err_t sdgoods_launcher_orphan_appdata_cleanup(void);

/* 启动早期调用：完成槽自校验 + 孤儿清理（建议 main.c 在 sdgoods_boot_show() 之后调用一次）。
   ⚠️ 只在**启动器宿主**上有意义；app 在单应用模式下不要调（会返回 ESP_ERR_INVALID_STATE）。 */
esp_err_t sdgoods_launcher_boot_check(void);

/* 枚举已装 app（state==INSTALLED 且 desc 校验通过）。返回写入 out 的数量 *count。
 * 只读查询，**任何固件都可调用**（不加权限闸门）。 */
esp_err_t sdgoods_launcher_list_installed(sdgoods_slot_entry_t *out, size_t cap, size_t *count);

/* 直接读某槽 flash 上的 esp_app_desc_t（设备真相源），填入 app_id / version / elf_sha。
 * 与 NVS manifest 缓存（list_installed 返回的那份）不同：这里永远以「flash 上实际烧进去的
 * 镜像」为准。any of app_id/version/elf_sha 传 NULL 表示不取该项。
 * 只读查询，**任何固件都可调用**（不加权限闸门）。
 * ⚠️ 用途：弹框等需要「展示真实版本号」的地方应调它而不是信任 manifest 的 version 缓存
 *   （manifest 的 version 可能来自平台下发的安装参数，与固件二进制里 baked-in 的
 *   esp_app_desc.version 不一致 —— 见 ui_app_menu.c 的长按弹框）。 */
bool sdgoods_slot_read_desc(int idx, char *app_id, char *version, uint8_t *elf_sha);

/* 切换：把下次启动指到 slot_idx 并重启。idx == -1 表示回启动器(factory)。
 *
 * ⚠️ 权限闸门（2026-09-19）：
 *   idx >= 0（启动某个槽）—— **只有启动器宿主**（`sdgoods_device_is_launcher_host()`）
 *                            可调；其它固件返回 ESP_ERR_INVALID_STATE 并打 refusal 日志。
 *   idx <  0（回 factory）—— **只有被启动器管理的 app**（`sdgoods_device_is_managed_app()`）
 *                            可调；单应用固件/宿主调它会被拒绝（否则单应用固件自更新后
 *                            调它 = 指向自己 + 重启，表现为「莫名重启/固件被退回去」）。
 * 判据不能用「运行分区不是 factory」代替 —— 见 sdgoods_device_mode.h。 */
esp_err_t sdgoods_launcher_launch_slot(int idx);

/* 返回启动器（factory）并重启。app 通过 sdgoods_app_sdk.h 的 sdgoods_return_to_launcher() 调用。
 * 闸门与 launch_slot(-1) 相同：非「被启动器管理的 app」调用会返回 ESP_ERR_INVALID_STATE 且不重启。 */
esp_err_t sdgoods_launcher_return_to_launcher(void);

/* 安装：把下载好的 app.bin 写入 slot_idx，校验 desc + sha256，更新 manifest。
   bin 必须是「不带地址」的纯应用镜像（pack_app.py 校验通过的产物）。
   内部就是 install_begin/write/end 的薄封装 —— 只是调用方手里已经有整包时更省事。
   ⚠️ 只有**启动器宿主**可调（否则 ESP_ERR_INVALID_STATE）：槽与 manifest 是启动器的簿记对象。 */
esp_err_t sdgoods_launcher_install(int slot_idx, const uint8_t *bin, size_t len,
                                  const char *app_id, const char *version,
                                  const uint8_t sha256[32]);

/* ---- 流式安装（网络拉取 / 分块写入用）----
 * 为什么需要它：`install()` 要求整包常驻内存（最大 2.9MB），而这块 PSRAM 还要同时供 LVGL
 * 双缓冲与 GIF 画布用；HTTP 下载天然是「收一块写一块」，也不该为了先攒齐整包而多占一份内存。
 * 另外这两个函数顺带解决了两件以前做不到的事：**精确擦除**与**真实的安装中状态**。
 *
 * 用法（三步严格按序，中途任何一步失败都要继续调 end 做收尾）：
 *   esp_err_t r = sdgoods_launcher_install_begin(slot, total_len, app_id, version, sha256);
 *   while (还有数据) { r = sdgoods_launcher_install_write(chunk, chunk_len); }
 *   r = sdgoods_launcher_install_end();
 *
 * 实现要点
 * --------
 * · `esp_ota_begin(..., OTA_WITH_SEQUENTIAL_WRITES)` ⇒ **边写边擦**。以前用
 *   `OTA_SIZE_UNKNOWN` 会在 begin 时把整槽 3MB 一次擦掉（实测阻塞数秒），
 *   装一个 200KB 的 app 也要付这个代价。
 * · sha256 边写边算，直到 `..._end()` 才比对 ⇒ 流式也能做完整性校验。
 * · 期间 manifest 标 `SDG_SLOT_UPDATING`（**写 flash 之前**就标），成功转 INSTALLED，
 *   失败回滚成进入前的状态并整槽擦净 —— 不留「半个镜像」给下一次启动。
 * · 掉电残留（UPDATING + 部分镜像）由下次开机的 `sdgoods_launcher_self_check()` 修正。
 *
 * ⚠️ 只有**启动器宿主**可调；同一时刻只允许一个会话（重复 begin 返回 ESP_ERR_INVALID_STATE）。
 * ⚠️ 调用方保证 chunk 按顺序、不重叠、累计恰好等于 begin 声明的 total_len。
 * ⚠️ total_len 超过 `SDGOODS_SLOT_SAFE_BYTES` 或槽大小时在 begin 就被拒（不写 flash）。 */
esp_err_t sdgoods_launcher_install_begin(int slot_idx, size_t total_len,
                                        const char *app_id, const char *version,
                                        const uint8_t sha256[32]);
/* 送一块数据。成功返回 ESP_OK；失败后不要再继续 write（继续也在 end 时被回滚）。 */
esp_err_t sdgoods_launcher_install_write(const void *data, size_t len);
/* 收尾：校验长度与 sha256、`esp_ota_end()` 校验镜像、写 manifest。
   任何失败都会把该槽擦净并回滚 manifest（返回的是**失败原因**，不是回滚结果）。 */
esp_err_t sdgoods_launcher_install_end(void);

/* 卸载：擦槽 + 清 appdata/<app_id>/ + 清 manifest。
   ⚠️ 只有**启动器宿主**可调（否则 ESP_ERR_INVALID_STATE）。 */
esp_err_t sdgoods_launcher_uninstall(int slot_idx);

/* 找一个空闲槽，返回索引；没有返回 -1（调用方据此提示「槽已满，请删除一个 app」）。
 * 只读查询，任何固件都可调用。 */
int sdgoods_launcher_find_free_slot(void);

#ifdef __cplusplus
}
#endif
