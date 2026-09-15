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

#include "ui_boot.h"

#include "lvgl.h"
#include <stdio.h>
#include "extra/libs/gif/lv_gif.h"
#include "st77916.h"
#include "sdgoods_hooks.h"   /* sdgoods_ui_home_create_show：首屏由应用层提供 */
#include "boot_anim_gif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static lv_obj_t *s_boot_scr = NULL;
static volatile bool s_done = false;

static void finish_boot(void)
{
    if (s_done) {
        return;
    }
    /* 首屏由应用层提供（main/apps/apps_registry.c 注册的 home_create_show），
       平台层不关心首屏是什么 —— 换一套 UI 也不用改这里。 */
    sdgoods_ui_home_create_show();
    if (s_boot_scr) {
        lv_obj_del(s_boot_scr);
        s_boot_scr = NULL;
    }
    s_done = true;
}

/* 开机动画时长：与转换后的 GIF 时长一致（360x360 / 15fps / 约 4s 循环） */
#define BOOT_GIF_MS 4000

void ui_boot_show(void)
{
    s_done = false;

    s_boot_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_boot_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_boot_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_boot_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_scr_load(s_boot_scr);
    lv_refr_now(NULL);   /* 先铺一帧纯黑底，帧缓冲就绪（此时背光仍关，不可见） */

    /* 全屏播放由开机动画.mp4 转换的开机 GIF。
       lv_gif 的帧推进按真实时钟 lv_tick_get() 累计；首帧 LZW 解码发生在第一次
       lv_timer_handler()。为避免“亮屏瞬间正好撞上一次重解码 + 其它子系统启动突发”
       而卡在开头，这里把“首帧解码”放到背光开启之前完成（不可见），并在背光点亮前
       重置 gif 计时器，使下一次解码对齐到亮屏之后系统已平稳的时刻。 */
    lv_obj_t *gif = lv_gif_create(s_boot_scr);
    lv_gif_set_src(gif, &boot_anim_gif);
    /* 源 GIF 是 240x240。要做到真正流畅的 15fps（帧间隔 66ms），
       解码必须 < 66ms，只有 240x240（~58ms）满足；360x360 解码 ~95ms 只能 10fps。
       LVGL 的实时缩放（zoom/VIRTUAL 放大）在 PSRAM 上做 360x360 插值极慢
       （~90ms/帧），会让整体退化到 ~6fps，且 VIRTUAL 模式还会平铺成 4 个。
       因此用 REAL 模式：对象大小 = 图片原始大小，无缩放、无平铺，居中显示
       （四周黑边，圆形屏边缘本就显示不全）。240x240 在 360x360 屏上约占中央 2/3。 */
    lv_img_set_size_mode(gif, LV_IMG_SIZE_MODE_REAL);
    lv_obj_center(gif);

    vTaskDelay(pdMS_TO_TICKS(150));   /* 背光仍关：推进并解码首帧（不可见） */
    lv_timer_handler();

    vTaskDelay(pdMS_TO_TICKS(200));   /* 等 touch/hw_info/wifi/ble/audio 等启动突发结束 */
    lv_gif_restart(gif);              /* 回帧0 + 清零计时器（画布仍是帧0，last_call 归零） */

    Set_Backlight(80);
    lv_refr_now(NULL);   /* 首帧已就绪，立即干净显示；下一帧约 70ms 后才解码 */

    /* 若 PSRAM canvas 分配失败，lv_img_get_src 会返回 NULL。 */
    const void *src = lv_img_get_src(gif);
    printf("BOOT GIF src=%p (%s)\n", src, src ? "OK" : "FAILED");

    /* 阻塞驱动 GIF 播放；到时间后直接切到主页，不做淡入淡出。
       15fps 帧间隔 66ms；单帧 LZW 解码必须 < 66ms 才不丢帧。
       统计实际解码次数与每帧耗时。 */
    const TickType_t t0 = xTaskGetTickCount();
    int64_t max_dt = 0;
    int slow = 0, measured = 0, decodes_seen = 0;
    while (!s_done && (xTaskGetTickCount() - t0) < pdMS_TO_TICKS(BOOT_GIF_MS)) {
        int64_t ta = esp_timer_get_time();
        lv_timer_handler();
        int64_t dt = esp_timer_get_time() - ta;
        if (dt > 5000) {  /* 一次真正的解码 */
            decodes_seen++;
            if (dt > 120000) printf("  slow frame decode=%lld us\n", (long long)dt);
            if (dt > max_dt) max_dt = dt;
            if (dt > 66000) slow++;
        }
        measured++;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    printf("BOOT gif: loop_iters=%d decodes=%d steady_max=%lld us slow(>66ms)=%d (target<66000)\n",
           measured, decodes_seen, (long long)max_dt, slow);

    if (!s_done) {
        finish_boot();
    }

    while (!s_done) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
