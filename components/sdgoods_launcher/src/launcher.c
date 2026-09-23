/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（BSP）· 多应用启动器 · 菜单骨架
 *
 * 设计稿 §0 明确「本稿不含 launcher 的具体 UI / 渲染实现，只定槽管理 + 协议 + 状态」，
 * 因此这里只给一个**最小可运行**的菜单：列出已装 app、点击即切到该槽（重启）。
 * 真实的启动器界面（图标、网格、商店入口、槽满弹窗让用户删 app）由 UI 层在此之上绘制，
 * 数据接口全部来自 sdgoods_launcher.h（list_installed / find_free_slot / install / uninstall）。
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS). SPDX-License-Identifier: Apache-2.0
 */

#include "sdgoods_launcher.h"

#include <stdint.h>
#include <stdio.h>

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "sdg_launcher";
static lv_obj_t *s_menu_scr;

/* 点击某个已装 app → 切到对应槽并重启（esp_ota_set_boot_partition + esp_restart）。 */
static void on_app_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "launch slot %d", idx);
    sdgoods_launcher_launch_slot(idx);   /* 不返回 */
}

/*
 * 展示已装 app 列表。UI 层可在此基础上替换成图标网格 / 添加「应用商店」「删除」按钮。
 * 注意：本骨架的文案用 ASCII（app_id 即 project_name，为 ASCII）；真实界面请改用
 * SDG_T("中文", "English") 并补字体子集（见 tools/gen_fonts.py）。
 */
void sdgoods_launcher_show_menu(void)
{
    /* 确保 manifest 与 flash 一致（首次进菜单前也跑过 boot_check，这里再保险一次） */
    sdgoods_launcher_self_check();

    s_menu_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_menu_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_menu_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_menu_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_menu_scr);
    lv_label_set_text(title, "Installed Apps");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

    sdgoods_slot_entry_t apps[SDGOODS_SLOT_COUNT_MAX];
    size_t n = 0;
    sdgoods_launcher_list_installed(apps, SDGOODS_SLOT_COUNT_MAX, &n);

    if (n == 0) {
        lv_obj_t *empty = lv_label_create(s_menu_scr);
        lv_label_set_text(empty, "No apps installed");
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
    } else {
        for (size_t i = 0; i < n; i++) {
            lv_obj_t *btn = lv_btn_create(s_menu_scr);
            lv_obj_set_size(btn, 240, 48);
            lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 80 + (int)i * 56);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, apps[i].app_id);
            lv_obj_center(lbl);
            lv_obj_add_event_cb(btn, on_app_click, LV_EVENT_CLICKED,
                               (void *)(intptr_t)apps[i].slot_idx);
        }
    }

    lv_scr_load(s_menu_scr);
}
