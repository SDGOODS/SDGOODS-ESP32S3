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

#include "sdgoods_hooks.h"

#include "esp_log.h"

/*
 * 应用层回调的注册表。见 include/sdgoods_hooks.h 的说明。
 * 全部为「未注册即空操作」——平台层可以独立跑起来（比如只做屏点亮测试），
 * 不会因为应用层缺席而空指针崩溃。
 */

static const char *TAG = "sdgoods";

static sdgoods_cb_t s_apps_poll = NULL;
static sdgoods_nav_t s_nav = { 0 };
static sdgoods_power_short_cb_t s_power_short = NULL;
static sdgoods_slot_event_cb_t  s_slot_event = NULL;

void sdgoods_apps_set_poll(sdgoods_cb_t fn)
{
    s_apps_poll = fn;
}

void sdgoods_set_slot_event_handler(sdgoods_slot_event_cb_t fn)
{
    s_slot_event = fn;
}

void sdgoods_slot_event_notify(sdgoods_slot_event_t ev)
{
    /* 注意调用上下文：串口接收任务（见 sdgoods_hooks.h 第 4 节）——实现方自己投递 UI。 */
    if (s_slot_event) {
        s_slot_event(ev);
    }
}

void sdgoods_set_power_short_handler(sdgoods_power_short_cb_t fn)
{
    s_power_short = fn;
}

bool sdgoods_ui_power_short(void)
{
    if (s_power_short) {
        return s_power_short();
    }
    return false;
}

/* 弱默认：非多应用模式（不含启动器、或单应用固件）。
 * 平台层 components/sdgoods_launcher 提供强符号覆盖（见 device_mode.c）。 */
__attribute__((weak)) bool sdgoods_device_is_multi_app_mode(void)
{
    return false;
}

/* 弱默认：无「退出到启动器」能力（不含启动器组件，或不是被管理的 app）。
 * 强符号覆盖见 components/sdgoods_launcher/src/device_mode.c。 */
__attribute__((weak)) bool sdgoods_multi_app_exit_to_launcher(void)
{
    return false;
}

void sdgoods_ui_set_nav(const sdgoods_nav_t *nav)
{
    if (nav) {
        s_nav = *nav;
    }
}

/* ---- 平台层内部调用 ------------------------------------------------------- */

void sdgoods_apps_poll(void)
{
    if (s_apps_poll) {
        s_apps_poll();
    }
}

void sdgoods_ui_home_create_show(void)
{
    if (s_nav.home_create_show) {
        s_nav.home_create_show();
    } else {
        ESP_LOGW(TAG, "no home_create_show registered: 系统已启动但没有任何首屏");
    }
}

void sdgoods_ui_home_show(void)
{
    if (s_nav.home_show) {
        s_nav.home_show();
    }
}

void sdgoods_ui_apps_show(void)
{
    if (s_nav.apps_show) {
        s_nav.apps_show();
    }
}
