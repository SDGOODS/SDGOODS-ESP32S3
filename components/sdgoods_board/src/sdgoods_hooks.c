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
