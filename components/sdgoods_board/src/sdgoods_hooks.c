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

void sdgoods_apps_set_poll(sdgoods_cb_t fn)
{
    s_apps_poll = fn;
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
