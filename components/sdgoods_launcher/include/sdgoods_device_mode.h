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
 * sdgoods_device_mode.h —— 设备启动模式检测（单应用 / 多应用）
 *
 * 背景（见 docs/APP_SDK.md §0 与 docs/MULTI_APP_DYNAMIC_SLOTS.md）
 * ----------------------------------------------------------------
 *   · 多应用模式（MULTI）：设备由「启动器（launcher）」作宿主，app 是**独立固件镜像**，
 *     被装进 `ota_N` 槽、由启动器拉起。app 重启后由 bootloader 直接进 `ota_N`，
 *     **不会自动回启动器**。
 *   · 单应用模式（SINGLE）：设备只跑一个 app，该 app 就是**主机固件**，启动器不介入。
 *
 * 判定口径（不依赖平台侧配合，设备本地自证）
 * ------------------------------------------
 *   1) 运行分区**是出厂区**：这块里的固件就是本固件，看它是谁：
 *      · 本固件 project_name == SDGOODS_LAUNCHER_APP_ID ⇒ 本固件是启动器宿主 ⇒ **MULTI**；
 *      · 否则 ⇒ 本固件是被当成主机固件直接刷的普通 app ⇒ **SINGLE**。
 *   2) 运行分区**不是出厂区**（从 `ota_N` 启动）：这里有两种**完全不同**的来路，
 *      光看「不是 factory」是分不开的：
 *      (a) 启动器把一个 app 装进 `ota_N` 并拉起它 ⇒ 被管理 app ⇒ **MULTI**；
 *      (b) **单应用固件自己走标准 A/B 自更新**，把自己写进了 `ota_N`
 *          ⇒ 它仍是单应用主机固件（出厂区里躺着它的旧版本）⇒ **SINGLE**。
 *      区分依据 = **出厂区里装的是谁**：只有「factory = 启动器」的设备才存在
 *      「被启动器管理的 app」。(b) 的 factory 里是该 app 自己，不是启动器。
 *
 * ⚠️ 判据 2) 里那句「factory 是不是启动器」**必须有**（2026-09-19 真机实测）：
 *    只看「运行分区不是 factory」的话，(b) 会被判成 MULTI —— 控制中心第 5 个按钮
 *    从 `Power` 变 `Exit`，用户一点就 `esp_ota_set_boot_partition(factory)` + 重启
 *    ⇒ **回滚到更新前的旧版本**（现象：「我刚更新的固件被退回去了」），
 *    并且底部错显 `Mode MULTI`、app 若以模式决定是否显示自更新入口就再也不更新。
 *    ⇒ 单应用固件做 OTA **必须**沿用「factory 里不是启动器」的事实，不要给
 *      单应用固件也刷一个启动器到 factory。
 *
 * ⚠️ 「是不是出厂区」必须用**分区 subtype**（`ESP_PARTITION_SUBTYPE_APP_FACTORY`）判断，
 *    **不能拿分区 label 去比 "factory"**：本工程的 factory 分区标签就叫 `launcher`
 *    （partitions.csv：`launcher, app, factory, 0x10000, ...`），比 label 会永远为假 ——
 *    这会直接导致「启动器里第 5 个按钮显示成 Exit 而不是 Power」（已踩过）。
 *
 * ⚠️ 三个概念不要混（这是本头文件最容易用错的地方）：
 *   `sdgoods_device_mode()`     —— **设备**处于哪种模式（用于展示/上报）。
 *   `sdgoods_device_is_managed_app()` —— **本固件**是不是「被启动器管理的 app」。
 *     二者的差别在启动器自己身上：启动器跑在 factory、设备是多应用模式（MULTI），
 *     但它**不是**被管理的 app（它就是宿主）。所以：
 *     · 控制中心的第 5 个按钮用它来选语义 —— 被管理 app 显示 `Exit`（返回启动器，因为
 *       重启才回得去）；宿主/单应用 app 显示 `Power`（关机）。
 *   `sdgoods_device_is_launcher_host()` —— 本固件是不是**启动器宿主**。
 *     凡是「只有启动器该做的事」（装/卸/启动别的槽、改写 otadata），都应当 gate 在它上面；
 *     在这之前外部只能靠 `is_managed_app() == false` 反推，而 false 同时包含
 *     「单应用主机固件」和「宿主」两种，分不开。
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 启动器宿主的 app_id：等于 SDGOODS_LAUNCHER 工程 CMakeLists 里的 project() 名。
 * 「从 factory 启动时本固件是不是启动器」就靠它判定 —— 改动启动器工程名必须同步改这里。 */
#define SDGOODS_LAUNCHER_APP_ID  "SDGOODS_LAUNCHER"

/* 设备启动模式 */
typedef enum {
    SDGOODS_MODE_SINGLE = 0,   /* 单应用模式：设备只跑一个 app（主机固件），启动器不介入 */
    SDGOODS_MODE_MULTI  = 1,   /* 多应用模式：由启动器管理动态插槽（本固件是启动器或它装的 app）*/
} sdgoods_device_mode_t;

/* 当前设备处于哪种模式。结果在本次启动内恒定，内部缓存（首次调用时打一条日志，
 * 含运行分区 label 与判定依据，便于真机核验）。 */
sdgoods_device_mode_t sdgoods_device_mode(void);

/* "SINGLE" / "MULTI"（用于界面文案与日志）。 */
const char *sdgoods_device_mode_str(void);

/* 本固件是不是「由启动器装进 ota_N 并拉起的 app」。
 * ⇒ true 时重启才回得去启动器，界面应提供 Exit（返回启动器）；
 *   false 时本固件是宿主（启动器）或单应用主机固件（含自更新后运行在 ota_N 的情形），
 *   界面提供 Power（关机）。 */
bool sdgoods_device_is_managed_app(void);

/* 本固件是不是**启动器宿主**（本固件 = 启动器，且跑在 factory）。
 * 用于 gate「只有启动器该做的事」：装/卸/启动别的槽、改写 otadata。 */
bool sdgoods_device_is_launcher_host(void);

/* 本固件运行所在分区的 label（"factory" 或 "ota_0".."ota_N"）。诊断用。 */
const char *sdgoods_device_boot_partition(void);

#ifdef __cplusplus
}
#endif
