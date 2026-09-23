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

/* sdgoods_device_mode.c —— 单/多应用启动模式检测。判定口径见 sdgoods_device_mode.h。 */

#include "sdgoods_device_mode.h"

#include <string.h>

#include "esp_app_desc.h"     /* esp_app_get_description()：本固件 project_name */
#include "esp_ota_ops.h"      /* esp_ota_get_running_partition()：运行分区 */
#include "esp_partition.h"    /* ESP_PARTITION_SUBTYPE_APP_FACTORY：判「是不是出厂区」 */
#include "esp_log.h"

#include "sdgoods_hooks.h"    /* 强符号覆盖 board 的弱默认 sdgoods_device_is_multi_app_mode */
#include "sdgoods_launcher.h" /* sdgoods_launcher_return_to_launcher()：写 otadata 回 factory 并重启 */

static const char *TAG = "device_mode";

/* 判定结果在本次启动内恒定（运行分区与固件身份都不会中途变），故缓存。
 * 用 -1 表示「还没判定过」，避免把 0（SINGLE）当成未初始化。 */
static int s_cached = -1;

static const esp_partition_t *running_part(void)
{
    return esp_ota_get_running_partition();
}

static const char *boot_label(void)
{
    const esp_partition_t *p = running_part();
    return p ? p->label : "?";
}

/* 本固件是否运行在**出厂区**。
 * ⚠️ 不能用 label 判断：本工程的 factory 分区**标签就叫 "launcher"**
 *    （partitions.csv：`launcher, app, factory, 0x10000, 0x300000`），
 *    拿 label 去比 "factory" 会永远为假 —— 这正是「启动器里第 5 个按钮
 *    显示成 Exit 而不是 Power」的原因（已踩）。权威判据只有 subtype：
 *    app/factory = 0x00，ota_0..N = 0x10..0x1F。 */
static bool running_is_factory(void)
{
    const esp_partition_t *p = running_part();
    return p && (p->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY);
}

static bool desc_is_launcher(const esp_app_desc_t *d)
{
    return d && d->project_name[0] && strcmp(d->project_name, SDGOODS_LAUNCHER_APP_ID) == 0;
}

/* 本固件（运行分区里那份镜像）是不是启动器。 */
static bool running_app_is_launcher(void)
{
    return desc_is_launcher(esp_app_get_description());
}

/* 出厂区里装的那份固件是不是启动器。
 * 这是「这台设备到底有没有『被启动器管理的 app』」的唯一前提 —— 见 mode_probe()。
 * 读的是**别的分区**上的 esp_app_desc_t（自己那份用 esp_app_get_description()）。 */
static bool factory_holds_launcher(void)
{
    const esp_partition_t *f = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!f) {
        /* 连 factory 分区都没有（某些单应用固件用纯 A/B 分区表）⇒ 不可能由启动器管理 */
        return false;
    }
    esp_app_desc_t d;
    if (esp_ota_get_partition_description(f, &d) != ESP_OK) {
        return false;   /* 出厂区没有有效镜像（魔数不符等）⇒ 不认作启动器设备 */
    }
    return desc_is_launcher(&d);
}

static int mode_probe(void)
{
    const esp_partition_t *p = running_part();
    const char *label = boot_label();
    const int    sub   = p ? (int)p->subtype : -1;

    /* ① 从 ota_N 启动。这里有两种**完全不同**的来路，光看「不是 factory」分不开：
     *      (a) 启动器把一个 app 装进 ota_N 并拉起它  ⇒ 被管理 app ⇒ MULTI；
     *      (b) **单应用固件自己走标准 A/B 自更新**，把自己写进了 ota_N ⇒ 它仍是
     *          单应用主机固件（factory 里躺着的是它的旧版本）⇒ SINGLE。
     *    区分依据 = **出厂区里装的是谁**：只有「factory = 启动器」的设备才存在
     *    被管理 app。(b) 的 factory 里是该 app 自己，不是启动器。
     *
     * ⚠️ 2026-09-19 真机实测（别把这条判据退回去）：
     *    没有下面这个 factory 判断时，(b) 会被判成 MULTI —— 控制中心第 5 按钮
     *    从 Power 变 Exit，用户一点就 set_boot_partition(factory) + 重启
     *    ⇒ **回滚到更新前的旧版本**（现象：「我刚更新的固件被退回去了」），
     *    底部还错显 `Mode MULTI`。 */
    if (!running_is_factory()) {
        if (factory_holds_launcher()) {
            ESP_LOGI(TAG, "running '%s' (subtype=0x%02x, ota slot), factory holds the launcher"
                          " -> MULTI (managed by launcher)", label, sub);
            return SDGOODS_MODE_MULTI;
        }
        ESP_LOGI(TAG, "running '%s' (subtype=0x%02x, ota slot), factory does NOT hold the launcher"
                      " -> SINGLE (single-app firmware running from an ota slot, e.g. after self-update)",
                 label, sub);
        return SDGOODS_MODE_SINGLE;
    }

    /* ② 从出厂区启动：这块里的固件就是本固件，看它是不是启动器宿主。 */
    const esp_app_desc_t *d = esp_app_get_description();
    const char *name = (d && d->project_name[0]) ? d->project_name : "";
    if (desc_is_launcher(d)) {
        ESP_LOGI(TAG, "running '%s' (subtype=0x%02x, factory), app='%s' -> MULTI (launcher host)",
                 label, sub, name);
        return SDGOODS_MODE_MULTI;
    }

    ESP_LOGI(TAG, "running '%s' (subtype=0x%02x, factory), app='%s' (not the launcher) -> SINGLE",
             label, sub, name[0] ? name : "unknown");
    return SDGOODS_MODE_SINGLE;
}

sdgoods_device_mode_t sdgoods_device_mode(void)
{
    if (s_cached < 0) {
        s_cached = mode_probe();
    }
    return (sdgoods_device_mode_t)s_cached;
}

const char *sdgoods_device_mode_str(void)
{
    return (sdgoods_device_mode() == SDGOODS_MODE_MULTI) ? "MULTI" : "SINGLE";
}

bool sdgoods_device_is_managed_app(void)
{
    /* 「被启动器管理的 app」= 从 ota_N 启动 **且** 这台设备的 factory 里是启动器。
     * ⚠️ 不能只用「不是从 factory 启动」判断：单应用固件自更新后也运行在 ota_N 上，
     *    但它不是被管理的 app（详见 mode_probe() 的长注释与实测记录）。 */
    return !running_is_factory() && factory_holds_launcher();
}

bool sdgoods_device_is_launcher_host(void)
{
    /* 宿主 = 本固件就是启动器，且它跑在 factory 上。 */
    return running_is_factory() && running_app_is_launcher();
}

const char *sdgoods_device_boot_partition(void)
{
    return boot_label();
}

/* 强符号：覆盖 board 的弱默认（返回 false）。
 * 仅「启动器宿主」或「被启动器管理的 app」才算多应用模式 —— 这两种固件里
 * 电源键在「应用主页」短按应当返回启动器，而不是熄屏。详见 sdgoods_hooks.h。 */
bool sdgoods_device_is_multi_app_mode(void)
{
    return sdgoods_device_mode() == SDGOODS_MODE_MULTI;
}

/* 强符号：覆盖 board 的弱默认（返回 false = 无回启动器能力）。
 * 只有「被启动器管理的 app」才执行真正的退出（launch_slot(-1) 闸门内再判一次）：
 * 写 otadata 指回 factory(launcher) + 重启，OK 路径**不返回**。
 * 返回 false（含被闸门拒绝的边界）⇒ 调用方（电源键主页分支）继续走熄屏 + 深睡。 */
bool sdgoods_multi_app_exit_to_launcher(void)
{
    if (!sdgoods_device_is_managed_app()) {
        return false;
    }
    return sdgoods_launcher_return_to_launcher() == ESP_OK;
}
