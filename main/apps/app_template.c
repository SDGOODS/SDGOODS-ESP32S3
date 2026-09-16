/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Required Notice: Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 *
 * 个人学习、研究与业余项目免费使用；商业用途需事前书面授权，见 LICENSING.md。
 * 分发时必须完整保留本声明 —— 这是许可条款，不是建议。
 */

/*
 * 新应用骨架（模板）—— 复制改名后就是你的应用
 *
 * ---------------------------------------------------------------------------
 * 最快上手方式（推荐）：
 *
 *     python3 tools/new_app.py 应用名 "按钮文字" "Button text"
 *
 * 它会复制本文件成 main/apps/ui_应用名.c/.h，替换掉里面的应用名与按钮文字，
 * 并自动帮你注册进启动台（apps_registry.c）与构建（CMakeLists.txt）。
 *
 * 手动接一个应用也可以，只需 3 步（见 apps_registry.c 顶部注释）。
 *
 * 界面文案请写成 SDG_T("中文", "English") —— 固件默认英文显示，
 * 用户可在 DEMO 页切换语言（见 sdgoods_i18n.h）。英文不需要动字体子集，
 * 新增中文才需要重跑 tools/gen_fonts.py。
 * ---------------------------------------------------------------------------
 *
 * 这个骨架演示了写一个应用要做的全部事情：
 *   · 建屏、画界面（用平台提供的圆屏栅格常量 SDG_UI_*）
 *   · 用 sdgoods_app_shell 接入标准交互（顶部下滑菜单 / 音量 / 退出）
 *   · 退出时清理资源
 *   · 提供 poll 函数供主循环推进（本例用不上，留了空实现）
 *
 * ★★★ 最重要的一条 ★★★
 *   界面里只要出现**新的中文文案**，就必须重跑字体工具重新生成字体子集：
 *
 *       python3 tools/fetch_fonts.py    # 首次需要：下载 OFL 源字体
 *       python3 tools/gen_fonts.py
 *
 *   否则那些新字在屏上是方框（tofu）。因为固件用的是「子集字体」，
 *   只有源码里出现过的字才会被烘进字体文件。
 */

#include "app_template.h"

#include <stdio.h>

#include "lvgl.h"
#include "esp_log.h"
#include "sdgoods_board.h"   /* 平台层全部能力：屏 / 触摸 / 音频 / 框架 / 栅格常量 */

LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(si_yuan_black_icon_14);

static const char *TAG = "app_template";

static lv_obj_t *s_scr;       /* 本应用的屏幕（非 NULL 表示已创建） */
static lv_obj_t *s_cnt_lbl;   /* 演示用：显示点击次数 */
static int       s_count;

/* ---------------------------------------------------------------------------
 * 1) 退出清理
 *    sdgoods_app_shell 在「已经切到目标屏之后」才回调这里，所以可以放心删自己的资源：
 *      · 用 lv_timer_create 建的定时器 → lv_timer_del
 *      · 自己 heap_caps_malloc 的大缓冲 → heap_caps_free
 *      · 屏幕对象交给 LVGL 自动回收（lv_scr_load 换走后即可删）
 *    没资源要放的话，留空也行；但**必须**把 s_scr 置 NULL，
 *    否则下次进来会走「已创建」分支、直接切回一个已死的屏。
 * ------------------------------------------------------------------------- */
/* ⚠️ 名字别叫 on_exit —— libc 里有同名函数，会报 conflicting types */
static void on_menu_exit(void)
{
    /* 若有定时器：if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; } */
    s_scr = NULL;
    s_cnt_lbl = NULL;
    s_count = 0;
    ESP_LOGI(TAG, "exit");
}

/* ---------------------------------------------------------------------------
 * 2) 暂停 / 恢复
 *    顶部下滑弹出菜单时调用 on_pause（菜单盖住了画面），关闭菜单时 on_resume。
 *    游戏类应用在这里冻结计时器；纯展示类应用可以不做。
 * ------------------------------------------------------------------------- */
static void on_pause(void)  { ESP_LOGI(TAG, "pause"); }
static void on_resume(void) { ESP_LOGI(TAG, "resume"); }

/* ---------------------------------------------------------------------------
 * 3) 界面
 * ------------------------------------------------------------------------- */
static void on_btn_click(lv_event_t *e)
{
    (void)e;
    s_count++;
    if (s_cnt_lbl) {
        char buf[32];
        snprintf(buf, sizeof(buf), SDG_T("点了 %d 次", "%d taps"), s_count);
        lv_label_set_text(s_cnt_lbl, buf);
    }
    sdgoods_audio_sfx_flap();   /* 平台音效：短促提示音（BGM 未播放时是静默的，安全） */
}

void ui_app_template_show(void)
{
    if (s_scr) {
        /* 已创建过：直接切回来，不重建（重建会浪费一次整屏绘制） */
        lv_scr_load(s_scr);
        return;
    }

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题（用 SDG_UI_TITLE_Y 与其它页面保持同一水平线） */
    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, SDG_T("我的应用", "My App"));
    lv_obj_set_style_text_font(title, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SDG_UI_TITLE_Y);

    /* 中间：一个显示状态的标签 */
    s_cnt_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_cnt_lbl, SDG_T("点了 0 次", "0 taps"));
    lv_obj_set_style_text_font(s_cnt_lbl, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(s_cnt_lbl, lv_color_hex(0xFFEC27), 0);
    lv_obj_align(s_cnt_lbl, LV_ALIGN_CENTER, 0, -50);

    /* 一个圆形按钮（样式与主页按钮一致，放屏幕中央） */
    lv_obj_t *btn = lv_btn_create(s_scr);
    lv_obj_set_size(btn, SDG_UI_BTN_SIZE, SDG_UI_BTN_SIZE);
    lv_obj_set_style_radius(btn, SDG_UI_BTN_SIZE / 2, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(btn, on_btn_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *blbl = lv_label_create(btn);
    lv_label_set_text(blbl, SDG_T("点我", "Tap me"));
    lv_obj_set_style_text_font(blbl, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(blbl, lv_color_white(), 0);
    lv_obj_center(blbl);

    lv_scr_load(s_scr);

    /* ★★★ 接入标准应用框架（三行，顺序别改）★★★
       bind 必须在 lv_scr_load 之后 —— 它要往当前屏上挂手势捕获层。 */
    sdgoods_app_shell_bind(s_scr);                 /* 1. 顶部下滑出菜单（音量+/-/退出/截屏） */
    sdgoods_app_shell_set_exit_cb(on_menu_exit);        /* 2. 退出时清理（切屏后才回调） */
    sdgoods_app_shell_set_pause_cb(on_pause);      /* 3. 菜单打开/关闭（可省，不做就传 NULL） */
    sdgoods_app_shell_set_resume_cb(on_resume);

    ESP_LOGI(TAG, "show");
}

/* ---------------------------------------------------------------------------
 * 4) 每帧推进
 *    平台主循环每轮（约 2ms）调用一次。硬性要求：
 *      · 不在前台就立刻 return（否则白白占用主循环时间）
 *      · 内部绝不做阻塞操作（vTaskDelay / 等信号量 / 阻塞读）
 *    需要周期性逻辑又不想占用主循环的，用 lv_timer_create 或独立 FreeRTOS 任务。
 * ------------------------------------------------------------------------- */
void ui_app_template_poll(void)
{
    if (!s_scr) {
        return;   /* 不在前台 */
    }
    /* 例：s_tick++; if (s_tick % 50 == 0) { 每 100ms 做点事 } */
}
