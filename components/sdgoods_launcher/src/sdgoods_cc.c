/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（板级支持包 BSP）· 多应用启动器 · 控制中心
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

/* 控制中心（Control Center）实现 —— **设备级统一系统浮层**，启动器与所有 app 共用。
 *
 * 顶部下滑唤出 → 5 个圆形图标按钮：上排 Volume / Brightness / Data，
 * 下排左对齐 Battery / 第 5 个按钮。点击 Volume、Brightness 进二级滑块页（可滑动实时调节）；
 * Data 进数据页；Battery 进电量页；第 5 个按钮按 sdgoods_device_is_managed_app() 二选一：
 *   · 被管理 app（从 ota_N 启动）        → Exit ：返回启动器（重启才回得去）
 *   · 启动器宿主 / 单应用主机固件（factory）→ Power：关机
 * 一级页底部另有小字显示设备启动模式（`SINGLE` / `MULTI`，不带 "Mode" 前缀）。
 *
 * 圆屏约束：所有内容落在 360 直径圆内；浮层本身做 360×360 方块，四角天然不可见。
 * 圆形按钮的图标用 LVGL 画布在运行时绘制（白色线条 + 透明底），按钮内不放任何文字；
 * 下方 caption 仍用内置字体，与首页「图标→名称」间距保持一致。
 */

#include "sdgoods_cc.h"
#include "sdgoods_device_mode.h"

#include "esp_app_desc.h"      /* esp_app_get_description：单应用模式下报「当前 app 名」 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h" /* vTaskDelay：关机前短暂停留，让 "Power Off" 文字可见 */
#include "freertos/task.h"
#include "nvs.h"               /* 音量 / 亮度掉电保存 */

#include "lvgl.h"
#include "sdgoods_lcd.h"       /* sdgoods_lcd_set/get_backlight */
#include "sdgoods_lvgl.h"      /* sdgoods_lvgl_post：跨任务投递到 LVGL 线程的唯一入口 */
#include "sdgoods_audio.h"     /* sdgoods_audio_set/get_volume */
#include "sdgoods_power.h"     /* sdgoods_power_off */
#include "sdgoods_swipe_up.h"   /* sdgoods_swipe_up_bind：底部上滑返回主页 */
#include "sdgoods_swipe_back.h"  /* sdgoods_swipe_back_bind：左→右返回上一级 */

#include "esp_heap_caps.h"       /* heap_caps_get_free_size：RAM 统计 */
#include "esp_vfs_fat.h"         /* esp_vfs_fat_info：读 appdata 空闲容量 */
#include "sdgoods_launcher.h"    /* 插槽数 / 已装 app 枚举 */
#include "sdgoods_app_sdk.h"     /* sdgoods_appdata_mount / unmount */
#include "sdgoods_hw_info.h"     /* sdgoods_hw_bat_v：电池电压（ADC1_CH7） */
#include "sdgoods_tap.h"         /* 全局手势策略：PRESS_LOCK + 点按位移守卫 */
#include "sdgoods_app_shell.h"   /* sdgoods_app_shell_notify_overlay：浮层开/关 → app 暂停/恢复 */
#include "sdgoods_nvs.h"        /* sdgoods_nvs_ensure：平台自己保证 NVS 可用（最小 app 可能不初始化） */

#include "esp_ota_ops.h"        /* esp_ota_get_running_partition：运行分区（取槽编号 / 镜像大小） */
#include "esp_partition.h"      /* esp_partition_find / read：枚举 ota 槽、读运行镜像头 */
#include "sdgoods_app_info.h"   /* SDGOODS_APP_VERSION：透传 version.txt（与弹框读的 esp_app_desc.version 同源，不再 +1） */
#ifndef SDGOODS_APP_VERSION
#define SDGOODS_APP_VERSION "1.0.0"   /* 头未生成时的兜底（正常由 gen_app_info.py 注入） */
#endif

LV_FONT_DECLARE(si_yuan_black_icon_16);
LV_FONT_DECLARE(si_yuan_black_icon_14);

/* 控制中心各按钮的图标：用 LVGL 画布在运行时绘制（白色线条 + 透明底），
 * 不放任何文字字形——满足「按钮内不要用文字显示」。画布缓冲用固定大小的池子，
 * 每次打开控制中心前 s_cc_icon_n 归零，每个按钮各占一个槽位，关闭/重开复用。 */
#define CC_ICON_SZ  48
typedef enum {
    CC_ICON_NONE = 0,
    CC_ICON_VOL,   /* 声音 */
    CC_ICON_BRI,   /* 亮度 */
    CC_ICON_PWR,   /* 电源 */
    CC_ICON_DATA,  /* 数据 */
    CC_ICON_BAT,   /* 电量（电池电压） */
    CC_ICON_INFO,  /* 关于 */
    CC_ICON_HOME,  /* 返回主页（小鸟游戏上下文下的第 5 键） */
} cc_icon_t;

static lv_color32_t s_cc_icon_buf[6][CC_ICON_SZ * CC_ICON_SZ];
static int s_cc_icon_n = 0;

/* 控制中心的应用上下文：由应用层在进入 / 离开某 app 时设置（如小鸟游戏隐藏部分按钮、
 * 改写第 5 键语义）。默认 DEFAULT = 标准 6 按钮控制中心。 */
static sdgoods_cc_app_ctx_t s_app_ctx = SDGOODS_CC_CTX_DEFAULT;

void sdgoods_cc_set_app_ctx(sdgoods_cc_app_ctx_t ctx)
{
    s_app_ctx = ctx;
}

/* 第 5 键在「小鸟游戏」上下文下的语义：返回主页（而非关机 / 退出启动器）。
 * 先关掉控制中心浮层，再走应用外壳的 leave(false) → 调回主页（apps_show = ui_home_show），
 * 并发 close_to_app 清掉小鸟屏。顺序必须是「关 CC 在前」：leave 会删掉当前活动屏，
 * 而 CC 是活动屏的子对象，先 leave 再关 CC 会变成对野指针 lv_obj_del。 */
static void home_to_home_async(void *p)
{
    (void)p;
    sdgoods_cc_close();
    sdgoods_app_shell_leave(false);
}
static void tap_home(void *ud)
{
    (void)ud;
    ESP_LOGI("cc", "tap: Home -> back to home");
    lv_async_call(home_to_home_async, NULL);
}

static void cc_icon_draw(lv_obj_t *canvas, cc_icon_t kind)
{
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_white();
    line.width = 4;
    line.opa = LV_OPA_COVER;

    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = lv_color_white();
    arc.width = 4;
    arc.opa = LV_OPA_COVER;
    arc.rounded = 0;

    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_white();
    fill.bg_opa = LV_OPA_COVER;
    fill.radius = 5;
    fill.border_width = 0;

    lv_point_t p[2];

    switch (kind) {
    case CC_ICON_PWR:
        /* 圆环 + 顶部竖线（常见电源符号） */
        lv_canvas_draw_arc(canvas, 24, 24, 15, 0, 360, &arc);
        p[0].x = 24; p[0].y = 5;  p[1].x = 24; p[1].y = 17;
        lv_canvas_draw_line(canvas, p, 2, &line);
        break;

    case CC_ICON_BRI: {
        /* 中心实心圆（r=6）+ 八向「圆头」光芒：线段两端各叠一枚 6×6 小圆点，
         * 拼成胶囊形光芒，比旧版（r=5 方头光芒）更饱满圆润。
         * ⚠️ 不用 line.round_start/end：canvas 假显示环境下 draw_sw_line 的
         * 圆头路径（内部转 lv_draw_rect）画不出来，实测光芒整体消失，
         * 故用「线段 + 端点小圆」手工拼出圆头。 */
        lv_draw_rect_dsc_t dot;
        lv_draw_rect_dsc_init(&dot);
        dot.bg_color = lv_color_white();
        dot.bg_opa = LV_OPA_COVER;
        dot.radius = LV_RADIUS_CIRCLE;
        dot.border_width = 0;

        lv_canvas_draw_rect(canvas, 18, 18, 12, 12, &fill);   /* radius=半边 -> 即 r=6 实心圆 */
        for (int i = 0; i < 8; i++) {
            int32_t a = i * 45;
            int32_t s = lv_trigo_sin(a);
            int32_t c = lv_trigo_cos(a);
            /* ⚠️ lv_trigo_sin/cos 返回 ±32767（LV_TRIGO_SIN_MAX），必须 >> LV_TRIGO_SHIFT(15)
             * 归一化，否则半径会放大 32 倍（11*32767/1024≈352px），光芒画到 48×48 画布之外
             * 整体不可见（实测踩坑：光芒全部消失只剩中心圆）。 */
            lv_coord_t x0 = (lv_coord_t)(24 + ((11 * c) >> LV_TRIGO_SHIFT));
            lv_coord_t y0 = (lv_coord_t)(24 + ((11 * s) >> LV_TRIGO_SHIFT));
            lv_coord_t x1 = (lv_coord_t)(24 + ((16 * c) >> LV_TRIGO_SHIFT));
            lv_coord_t y1 = (lv_coord_t)(24 + ((16 * s) >> LV_TRIGO_SHIFT));
            p[0].x = x0;
            p[0].y = y0;
            p[1].x = x1;
            p[1].y = y1;
            lv_canvas_draw_line(canvas, p, 2, &line);
            lv_canvas_draw_rect(canvas, x0 - 3, y0 - 3, 6, 6, &dot);   /* 内端圆头 */
            lv_canvas_draw_rect(canvas, x1 - 3, y1 - 3, 6, 6, &dot);   /* 外端圆头 */
        }
        break;
    }

    case CC_ICON_VOL: {
        /* 音箱主体（圆角矩形）+ 锥盆（梯形）+ 两道声波弧 */
        lv_draw_rect_dsc_t spk;
        lv_draw_rect_dsc_init(&spk);
        spk.bg_color = lv_color_white();
        spk.bg_opa = LV_OPA_COVER;
        spk.radius = 2;
        spk.border_width = 0;
        lv_canvas_draw_rect(canvas, 8, 17, 9, 14, &spk);

        lv_point_t tri[4];
        tri[0].x = 17; tri[0].y = 18;
        tri[1].x = 17; tri[1].y = 30;
        tri[2].x = 27; tri[2].y = 36;
        tri[3].x = 27; tri[3].y = 12;
        lv_canvas_draw_polygon(canvas, tri, 4, &spk);

        arc.rounded = 1;
        lv_canvas_draw_arc(canvas, 27, 24, 13, 320, 40, &arc);
        lv_canvas_draw_arc(canvas, 27, 24, 19, 315, 45, &arc);
        break;
    }

    case CC_ICON_DATA: {
        /* 三条递增竖条（柱状图）：表示「数据 / 存储」 */
        lv_draw_rect_dsc_t bar;
        lv_draw_rect_dsc_init(&bar);
        bar.bg_color = lv_color_white();
        bar.bg_opa = LV_OPA_COVER;
        bar.radius = 2;
        bar.border_width = 0;
        /* 基线 y=38，三条柱宽 8、高 10/18/26，左起 x=11/21/31 */
        lv_canvas_draw_rect(canvas, 11, 28, 8, 10, &bar);
        lv_canvas_draw_rect(canvas, 21, 20, 8, 18, &bar);
        lv_canvas_draw_rect(canvas, 31, 12, 8, 26, &bar);
        break;
    }

    case CC_ICON_BAT: {
        /* 电池：外框用 4 条矩形拼（不依赖边框绘制路径，稳）+ 正极凸点 + 内部电量条。
         * 外框 x6..36 / y14..34，凸点 x36..42，整体中心恰好落在 (24,24)。 */
        lv_canvas_draw_rect(canvas, 6,  14, 30, 3,  &fill);   /* 上边 */
        lv_canvas_draw_rect(canvas, 6,  31, 30, 3,  &fill);   /* 下边 */
        lv_canvas_draw_rect(canvas, 6,  14, 3,  20, &fill);   /* 左边 */
        lv_canvas_draw_rect(canvas, 33, 14, 3,  20, &fill);   /* 右边 */
        lv_canvas_draw_rect(canvas, 36, 20, 6,  8,  &fill);   /* 正极凸点 */
        lv_canvas_draw_rect(canvas, 11, 19, 18, 9,  &fill);   /* 电量条 */
        break;
    }

    case CC_ICON_INFO: {
        /* 信息符号「i」：顶部实心圆点 + 下方竖线（关于页图标） */
        lv_canvas_draw_rect(canvas, 21, 9, 6, 6, &fill);    /* 圆点 */
        lv_canvas_draw_rect(canvas, 21, 20, 6, 16, &fill);  /* 竖线 */
        break;
    }

    case CC_ICON_HOME: {
        /* 房子：三角屋顶 + 方身 + 黑色小门（「返回主页」语义） */
        lv_draw_line_dsc_t roof;
        lv_draw_line_dsc_init(&roof);
        roof.color = lv_color_white();
        roof.width = 4;
        roof.opa = LV_OPA_COVER;
        lv_point_t r[2];
        r[0].x = 9;  r[0].y = 23;  r[1].x = 24; r[1].y = 9;
        lv_canvas_draw_line(canvas, r, 2, &roof);
        r[0].x = 24; r[0].y = 9;   r[1].x = 39; r[1].y = 23;
        lv_canvas_draw_line(canvas, r, 2, &roof);
        /* 方身（复用 fill：白底圆角方块） */
        lv_canvas_draw_rect(canvas, 13, 23, 22, 16, &fill);
        /* 门（黑矩形挖空） */
        lv_draw_rect_dsc_t door;
        lv_draw_rect_dsc_init(&door);
        door.bg_color = lv_color_black();
        door.bg_opa = LV_OPA_COVER;
        door.radius = 0;
        door.border_width = 0;
        lv_canvas_draw_rect(canvas, 20, 30, 8, 9, &door);
        break;
    }
    default:
        break;
    }
}

static const char *TAG = "cc";

/* 一级浮层（控制中心）与二级浮层（滑块页 / 数据页）；二者可同时存在（覆盖在控制中心之上）。 */
static lv_obj_t *s_cc = NULL;
static lv_obj_t *s_slider = NULL;
/* 当前滑块页的标题（"Volume" / "Brightness"）。只为日志服务：两个滑块页的
 * VALUE_CHANGED 日志原本都是同一句 `slider value -> N%`，串口上**分不出是哪一页**
 * ⇒ 「亮度的断言」实际可能被音量那行满足（假阳性）。带上下标就没有歧义了。 */
static const char *s_slider_title = "?";
static lv_obj_t *s_data = NULL;   /* 数据二级页（与滑块页平级，可同时存在） */
static lv_obj_t *s_volt = NULL;   /* 电量二级页（同上） */
static lv_obj_t *s_about = NULL;  /* 关于二级页 */

/* 滑块页的当前应用到回调与数值标签（同一时刻仅一个滑块页，用静态量足够） */
static void (*s_apply)(int) = NULL;
static lv_obj_t *s_val_label = NULL;

/* 前向声明：二级页从底部小横条上滑直接返回主页时调用 */
void sdgoods_cc_close(void);

/* 点按守卫上下文池：每个按钮一份（静态存储，不做动态分配 ⇒ 没有失败路径）。
 * 每次重建控制中心时把 s_btn_ctx_n 归零重新分配。 */
#define CC_BTN_MAX 6
static sdgoods_tap_ctx_t s_btn_ctx[CC_BTN_MAX];
static int               s_btn_ctx_n = 0;

/* 圆形图标按钮：在 parent 上以 (cx,cy) 为圆心画直径 d 的圆形按钮，
 * 中心用画布画图标（无文字），下方放 caption 说明；点击触发 cb。 */
static lv_obj_t *make_round_btn(lv_obj_t *parent, int cx, int cy, int d,
                               cc_icon_t icon, const char *caption,
                               sdgoods_tap_cb_t cb, void *ud)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, d, d);
    lv_obj_set_pos(btn, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3A3A3C), LV_STATE_PRESSED);

    if (icon != CC_ICON_NONE) {
        int slot = s_cc_icon_n;
        if (slot >= 6) slot = 5;   /* 最多 6 个图标缓冲槽（VOL/BRI/DATA/BAT/PWR/INFO） */
        s_cc_icon_n++;
        lv_obj_t *cv = lv_canvas_create(btn);
        lv_canvas_set_buffer(cv, s_cc_icon_buf[slot], CC_ICON_SZ, CC_ICON_SZ,
                             LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_obj_set_style_bg_opa(cv, LV_OPA_TRANSP, 0);   /* 画布本身透明，只显图标 */
        cc_icon_draw(cv, icon);
        lv_obj_center(cv);
        /* 图标相对画布整体缩小（画布仍是 48×48，按钮直径不变）：canvas 继承 lv_img，
         * zoom 256=100%，这里取 ~81% 让线条图形在圆里更透气（用户要求：按钮大小不变、
         * 图标小一点）。pivot 锁在画布中心，缩放向心收缩；开抗锯齿避免边缘锯齿。 */
        lv_img_set_pivot(cv, CC_ICON_SZ / 2, CC_ICON_SZ / 2);
        lv_img_set_antialias(cv, true);
        lv_img_set_zoom(cv, 208);
        /* 关键：关掉画布的可点击，否则点击会被画布吃掉、按钮的 CLICKED 收不到
         * （与 ui_launcher.c 的圆形图标 ic 处理一致：清掉 CLICKABLE 让命中穿透到按钮）。 */
        lv_obj_clear_flag(cv, LV_OBJ_FLAG_CLICKABLE);
    }

    if (caption) {
        lv_obj_t *cap = lv_label_create(parent);
        lv_label_set_text(cap, caption);
        lv_obj_set_style_text_font(cap, &si_yuan_black_icon_14, 0);
        lv_obj_set_style_text_color(cap, lv_color_hex(0xE0E0E5), 0);
        /* 图标→文字间距 6px（比首页 pad_row=14 更紧凑，圆屏下方更省空间） */
        lv_obj_align_to(cap, btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    }

    if (cb) {
        /* 走点按守卫：只有「按下期间位移 ≤ 24px」才真的触发动作。
         * 这样「按在按钮上滑走再松手」（无论滑到别的按钮还是页面空白）都不会误触。 */
        int slot = (s_btn_ctx_n < CC_BTN_MAX) ? s_btn_ctx_n : (CC_BTN_MAX - 1);
        s_btn_ctx_n++;
        sdgoods_tap_bind(btn, &s_btn_ctx[slot], cb, ud);
    }
    return btn;
}

/* 底部小横条（home 指示）：提示「从底部往上滑返回主页」。
 * 短而灰，纯指示用；非点击性（CLICKABLE 关掉），让起手于底部的上滑穿透到捕获层触发返回。 */
static void make_bottom_hint(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 36, 5);
    lv_obj_set_pos(bar, 180 - 18, 336 - 2);   /* 居中对齐 (180,336)，落在圆内 */
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x9A9A9E), 0);   /* 灰色指示条 */
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
}

/* ---- 音量 / 亮度掉电保存（NVS） ------------------------------------------ */

#define CC_NVS_NS      "sdg_cc"
#define CC_NVS_KEY_VOL "vol"
#define CC_NVS_KEY_BRI "bri"

/* 亮度**用户意图值**（0~100）：落盘存的是它，不是背光 PWM 的瞬时值。
 *
 * 为什么不直接存 `sdgoods_lcd_get_backlight()`：那是**瞬时值**，会被三种「临时熄灭」
 * 改掉 —— ①开机动画期间（sdgoods_boot.c 刻意先保持 0，动画播完才点亮）；
 * ②息屏低功耗（sdgoods_power_suspend_toggle 压 0）；③关机前（power_off 压 0）。
 * 任何一次落盘撞上这三种状态，就会把 0 写进 NVS ⇒ 下次开机 `restore` 直接把背光
 * 按成 0 ⇒ **用户看到黑屏**。而息屏状态本身是 RAM-only、重启就没了，
 * 所以那个 0 纯粹是「临时态泄漏」，不是任何人的偏好。
 *
 * 只有**用户显式调节**（亮度滑块 → apply_backlight）和**NVS 恢复**才更新它。
 * 默认 60（用户要求：派生应用 / 控制中心默认亮度 60%）。 */
static uint8_t s_bri_user = 60;

/* 持久化亮度的下限（滑块本身仍是 0~100 全范围，只是**不把 0~4 记成偏好**）。
 *
 * 为什么必须有个下限：数值上 0 与「临时熄灭」完全一样（开机动画期 / 息屏 / 关机前），
 * 而**用户拖滑块经过最左端**也是 0 —— 那一刻若防抖定时器正好落盘，0 就进了 NVS。
 * 下次开机 restore 把背光按成 0 ⇒ 用户看到**黑屏**，并且**在屏上无法自救**
 * （连亮度滑块都看不见，只能靠重新烧写/清 NVS）。这是不可恢复的坏状态，
 * 所以宁可牺牲「把亮度存成 0」这个没人真正需要的偏好。
 *
 * 规则两条（写侧 + 读侧配套，缺一不可）：
 *   · 写：s_bri_user < 下限 ⇒ **跳过不写**，NVS 里保留上一次正常值；
 *   · 读：NVS 里的值 < 下限 ⇒ 视为**无效记录**（旧版污染留下的 0），保持默认不覆盖。
 * 5% 的亮度仍然很暗（夜里看着像黑），但屏上内容可辨 ⇒ 用户能自己调回来。 */
#define CC_BRI_PERSIST_MIN 5

/* 把当前音量 / 亮度写入 NVS（关机断电前必须已落盘，故变更后防抖 + 关页/关机即时落盘）。
 * ⚠️ 必须先 sdgoods_nvs_ensure()：控制中心现在是**每个 app 默认自带**的能力，而最小 app
 * 可能从不 nvs_flash_init（不碰 wifi/ble）⇒ nvs_open 直接失败、音量/亮度静默不保存
 * （实测过：app 里点 Exit 时打出 `W cc: nvs_open save failed`）。 */
static void cc_settings_save(void)
{
    sdgoods_nvs_ensure();
    nvs_handle_t h;
    if (nvs_open(CC_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open save failed");
        return;
    }
    nvs_set_u8(h, CC_NVS_KEY_VOL, (uint8_t)sdgoods_audio_get_volume());
    /* ⚠️ 存 s_bri_user（用户意图值）而不是 sdgoods_lcd_get_backlight()（瞬时值）——
     *    后者在开机动画 / 息屏 / 关机前都是 0，会把「临时熄灭」当成用户偏好写进 NVS
     *    ⇒ 下次开机 restore 成 0 ⇒ **黑屏**。详见 s_bri_user / CC_BRI_PERSIST_MIN。
     * ⚠️ 低于下限时**跳过写入**（而不是钳到下限）：钳位会凭空造出一个 5% 的「偏好」，
     *    而跳过能让 NVS 里保留的更早的正常值继续生效 —— 那才是用户真正的设置。 */
    const bool bri_ok = (s_bri_user >= CC_BRI_PERSIST_MIN);
    if (bri_ok) {
        nvs_set_u8(h, CC_NVS_KEY_BRI, s_bri_user);
    }
    nvs_commit(h);
    nvs_close(h);
    /* 与 sdgoods_cc_settings_restore() 的 `restore: …` 成对：写入侧也留一行日志，
     * 这样「某个 app 里音量/亮度不对」可以直接用
     *   grep -E "restore:|save:" 串起「谁写的 → 谁读的」整条链。
     * ⚠️ 打的是**真正落盘的结果**，不是 s_bri_user 原值 —— 否则日志写 bri=0 而 NVS
     *    里根本没被改（还是上一个值），拿日志对 NVS 会对不上。
     * ⚠️ 额外带上「息屏中」标记：息屏（短按电源键）会把背光强制压到 0，
     *    这行标记是定位这类「亮度神秘变 0」的唯一线索。 */
    char bri_txt[48];
    if (bri_ok) {
        snprintf(bri_txt, sizeof(bri_txt), "%d", (int)s_bri_user);
    } else {
        snprintf(bri_txt, sizeof(bri_txt), "unchanged (live %d < floor %d, skipped)",
                 (int)s_bri_user, CC_BRI_PERSIST_MIN);
    }
    ESP_LOGI(TAG, "save: vol=%d%% bri=%s%s -> NVS ns='%s'",
             sdgoods_audio_get_volume(), bri_txt,
             sdgoods_power_is_suspended() ? " (screen suspended!)" : "", CC_NVS_NS);
}

/* 防抖定时器：滑块拖动会高频触发 VALUE_CHANGED，不能每次都写 NVS（磨损 flash）。
 * 停止调节 800ms 后才真正落盘一次。 */
static lv_timer_t *s_save_timer = NULL;

static void save_timer_cb(lv_timer_t *t)
{
    s_save_timer = NULL;
    cc_settings_save();
    /* 一次性：本回调执行完就停掉这个定时器。
     *
     * ⚠️ 必须设 **0**，不能设 1（旧版设 1，注释还写着「执行完即自动删除」，是错的）。
     *    `lv_timer_create()` 给的初值是 **-1（无限）**，而 lv_timer_exec 的顺序是
     *      original = repeat_count;  if (repeat_count > 0) repeat_count--;
     *      cb();                     if (repeat_count == 0) lv_timer_del();
     *    设 1 ⇒ 本次执行完 repeat_count=1 ≠ 0 ⇒ **不删**；800ms 后再来一次（先减到 0，
     *    回调里又被设回 1）⇒ 永久循环。
     * 真机症状：`cc: save:` 每 800ms 连刷（实测一轮出现 23 行）—— 等于每 800ms 写一次
     *    NVS，永不停歇（磨损 flash，也让日志里的「谁写的→谁读的」链条淹没在噪声里）。
     * 设 0 ⇒ 第 320 行 `repeat_count == 0` 成立 ⇒ 本轮回调结束后被删除。
     * （源码：components/lvgl/src/misc/lv_timer.c:300-327） */
    lv_timer_set_repeat_count(t, 0);
}

static void save_timer_arm(void)
{
    if (s_save_timer) {
        lv_timer_del(s_save_timer);
    }
    s_save_timer = lv_timer_create(save_timer_cb, 800, NULL);
}

/* 强制立即落盘（进 app / 关机前调用，因为之后会立即重启断电）：
 * 取消挂起的防抖定时器并马上写一次 NVS。 */
void sdgoods_cc_flush(void)
{
    if (s_save_timer) {
        lv_timer_del(s_save_timer);
        s_save_timer = NULL;
    }
    cc_settings_save();
}

/* 开机恢复（由启动器在主页创建后调用、以及 app 侧在 sdgoods_app_shell_init() 里调用；
 * NVS 无记录则保持出厂默认）。
 * 同样先 ensure：启动器不调 sdgoods_app_shell_init()，这条路径就是它唯一的 NVS 保障点。
 *
 * ★ 这是音量/亮度**跨 app 同步**的唯一入口：每次「进 app」都是重启，能跨固件传递
 *   状态的只有 NVS，所以每个固件启动都必须走一遍这里。
 * ⚠️ 必须打日志（哪怕只是 INFO）：这条路径以前完全无声，于是「在 A 里调好的音量，
 *   进 B 又变回去了」这种问题在串口上**看不出任何迹象**，只能靠肉眼比对屏幕。
 *   现在两侧都会打 `restore: vol=.. bri=..`，跨 app 的一致性可以直接用日志断言。 */
void sdgoods_cc_settings_restore(void)
{
    sdgoods_nvs_ensure();
    nvs_handle_t h;
    if (nvs_open(CC_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "restore: no NVS record yet (keep factory defaults: vol=%d bri=%d)",
                 sdgoods_audio_get_volume(), (int)s_bri_user);
        return;   /* 首次开机尚无记录 */
    }
    uint8_t vol = 0, bri = 0;
    const bool has_vol = (nvs_get_u8(h, CC_NVS_KEY_VOL, &vol) == ESP_OK);
    const bool has_bri = (nvs_get_u8(h, CC_NVS_KEY_BRI, &bri) == ESP_OK);
    nvs_close(h);

    if (has_vol) {
        sdgoods_audio_set_volume(vol);
    }
    if (has_bri) {
        if (bri >= CC_BRI_PERSIST_MIN) {
            s_bri_user = bri;          /* 同步「用户意图值」：后续落盘以它为准 */
            sdgoods_lcd_set_backlight(bri);
        } else {
            /* 低于下限 ⇒ **无效记录**，不是用户偏好（见 CC_BRI_PERSIST_MIN）。
             * 这条分支是给「旧版每次软复位都把 bri=0 写进 NVS」那台设备准备的修复路径：
             * 不修的话用户开机即黑屏，而屏上没有任何手段能调回来。
             * 保持默认（s_bri_user，出厂 80）并把面板点亮，让下一次落盘把它彻底修好。 */
            ESP_LOGW(TAG, "restore: NVS bri=%u < floor %d -> invalid record, keep %u (screen stays visible)",
                     (unsigned)bri, CC_BRI_PERSIST_MIN, (unsigned)s_bri_user);
            sdgoods_lcd_set_backlight(s_bri_user);
        }
    }
    ESP_LOGI(TAG, "restore: vol=%d%%%s bri=%d%s (from NVS ns='%s')",
             sdgoods_audio_get_volume(), has_vol ? "" : "(no record)",
             (int)s_bri_user, has_bri ? "" : "(no record)", CC_NVS_NS);
}

/* 「用户设定的亮度」的公开访问点 —— 给**启动流程**用（sdgoods_boot.c）。
 *
 * 为什么需要它、以及为什么返回 s_bri_user 而不是 sdgoods_lcd_get_backlight()：
 *   软复位（含「从 app 回启动器」）的快路径顺序是「先建主页 → 再亮屏」，而建主页内部
 *   会调 sdgoods_cc_settings_restore() **把用户亮度应用上去了**；启动流程如果紧接着
 *   再 set_backlight(80)，就把刚恢复的用户值又覆盖掉 ⇒ 表现为「每次回启动器亮度都是 80%」。
 *   冷启动走的是开机动画路径，restore 在 set_backlight(80) **之后**，顺序相反 ⇒ 看起来正常，
 *   这正是该 bug 长期没被发现的原因。
 *   直接问「用户设定的亮度是多少」再点亮，两条路径就都对了。
 *
 * ⚠️ 不能改用 sdgoods_lcd_get_backlight()：那是**瞬时值**，开机动画期 / 息屏期会是 0，
 *   拿它去点屏等于把「临时熄灭」当成用户偏好（黑屏事故的另一条路径）。
 *   无 NVS 记录时返回出厂默认 80（与 sdgoods_lcd_get_backlight() 为 0 不同）。 */
uint8_t sdgoods_cc_brightness_get(void)
{
    return s_bri_user;
}

/* ---- 二级滑块页 ----------------------------------------------------------- */

static void slider_event_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", v);
    lv_label_set_text(s_val_label, buf);
    /* 日志：既便于串口离线核验「拖动真的改到了滑块」，也是「拖动被返回手势抢走」
     * 这类问题的判定依据（拖动却没这行 ⇒ 手势根本没到滑块手里）。 */
    ESP_LOGI(TAG, "slider[%s] value -> %d%%", s_slider_title, v);
    if (s_apply) {
        s_apply(v);
    }
    save_timer_arm();   /* 防抖落盘 */
}

static void slider_back(void)
{
    if (s_slider) {
        lv_obj_del(s_slider);
        s_slider = NULL;
    }
}

static void open_slider(const char *title, int init, void (*apply)(int))
{
    if (s_slider) {
        lv_obj_del(s_slider);
        s_slider = NULL;
    }
    s_apply = apply;
    s_slider_title = title ? title : "?";
    /* 打开时的**实时值**（亮度页传的是 sdgoods_lcd_get_backlight()）。
     * 为什么要打这一行：`slider[...] value -> N%` 只在**拖动时**触发，所以「当前实际亮度
     * 是多少」在串口上是取不到的 —— 而「从 app 返回启动器后亮度变成 80」这类问题，
     * 唯一的机器可读判据就是它。没有它就只能靠肉眼看截图。 */
    ESP_LOGI(TAG, "slider page '%s' opened, init=%d%% (live)", s_slider_title, init);

    lv_obj_t *scr = lv_scr_act();
    s_slider = lv_obj_create(scr);
    lv_obj_set_size(s_slider, 360, 360);
    lv_obj_set_pos(s_slider, 0, 0);
    lv_obj_set_style_bg_color(s_slider, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_slider, LV_OPA_COVER, 0);   /* 不透明：独立二级页 */
    lv_obj_set_style_border_width(s_slider, 0, 0);
    lv_obj_set_style_pad_all(s_slider, 0, 0);
    lv_obj_clear_flag(s_slider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_slider);

    lv_obj_t *t = lv_label_create(s_slider);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);   /* 与首页标题同高 */

    s_val_label = lv_label_create(s_slider);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", init);
    lv_label_set_text(s_val_label, buf);
    lv_obj_set_style_text_font(s_val_label, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(s_val_label, lv_color_white(), 0);
    lv_obj_align(s_val_label, LV_ALIGN_TOP_MID, 0, 72);   /* 维持与标题的间距 */

    /* 横向滑块（left→right）：0 在左、100 在右 */
    lv_obj_t *sl = lv_slider_create(s_slider);
    lv_obj_set_size(sl, 240, 24);
    lv_obj_align(sl, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(sl, 0, 100);
    lv_slider_set_value(sl, init, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 底部小横条：提示「从底部往上滑返回主页」 */
    make_bottom_hint(s_slider);

    /* 从底部横条处上滑 → 直接返回主页（关闭滑块与一级控制中心） */
    sdgoods_swipe_up_bind(s_slider, sdgoods_cc_close);

    /* 从最左边起手左→右滑 → 返回上一级（一级控制中心） */
    sdgoods_swipe_back_bind(s_slider, slider_back);

    /* 全局手势策略：页面与滑块都锁定按下所有权 ⇒ 在滑块旁边按下拖动不会让滑块
     * 突然「跳值」，在页面上起手拖动也不会被别人接管（见 sdgoods_tap.h）。 */
    sdgoods_tap_normalize(s_slider);
}

/* ---- 一级控制中心 --------------------------------------------------------- */

static void apply_backlight(int v)
{
    if (v < 0) {
        v = 0;
    } else if (v > 100) {
        v = 100;
    }
    /* 用户显式调节 ⇒ 更新「意图值」（落盘以它为准），再驱动 PWM。
     * 顺序无所谓，但先更新意图值是刻意的：即使 ledc 调用失败，NVS 里记的仍是
     * 用户想要的值，而不是一个中间态。 */
    s_bri_user = (uint8_t)v;
    sdgoods_lcd_set_backlight((uint8_t)v);
}

/* 三个按钮的点击实际工作（打开整屏二级浮层 / 关机）都较重：会创建整屏对象树并绑定手势
 * 事件回调。若直接在当前 LVGL 输入事件回调里同步执行，相当于在 indev 的事件分发过程中
 * 改动对象树与事件链表，极易使 LVGL 内部 indev 状态错乱 → 崩溃/死机。
 * 本项目 swipe 手势已统一用 lv_async_call 规避同一坑；此处三个点击处理同样延后到
 * 下一个 lv_timer_handler 节拍执行（此时当前输入手势已完全结束、indev 状态干净）。
 *
 * ⚠️ 这里用 lv_async_call 是**对的**，别"顺手统一"成 sdgoods_lvgl_post。两者解决的是
 * 两个不同问题，判据只有一条 —— **调用方是不是 LVGL 线程**：
 *   · 调用方 = LVGL 线程（本处三个点击处理、swipe_up/back 的 RELEASED 回调）：
 *       lv_async_call 只是把执行挪到下一拍，全程单线程 ⇒ 不碰并发问题，代价最小。
 *   · 调用方 ≠ LVGL 线程（console RX 任务、Wi-Fi/BLE 事件任务、esp_timer 回调）：
 *       必须 sdgoods_lvgl_post()。此时 lv_async_call 内部会与 LVGL 线程并发改同一条
 *       无锁定时器链表 ⇒ 链表指向已释放内存 ⇒ InstructionFetchError（真机已复现）。
 *   详见 components/sdgoods_board/include/sdgoods_lvgl.h。 */
static void open_vol_async(void *p)
{
    (void)p;
    open_slider("Volume", sdgoods_audio_get_volume(), sdgoods_audio_set_volume);
}

static void open_bri_async(void *p)
{
    (void)p;
    open_slider("Brightness", sdgoods_lcd_get_backlight(), apply_backlight);
}

static void pwr_off_async(void *p)
{
    (void)p;
    /* 先静音：避免「正在播放的 BGM / 音效」在断电瞬间被硬切产生「啪」的杂声。
     * sdgoods_audio_bgm_stop 会先淡出 BGM 再把功放(PA)/喇叭(spk)使能拉低，
     * 确保后续 sdgoods_power_off() 切断电池锁存前声音已彻底安静（根因修复）。 */
    sdgoods_audio_bgm_stop();
    cc_settings_save();      /* 关机即断电：先把音量/亮度落盘 */

    /* 宿主固件（单应用 / 启动器）关机：sdgoods_power_off() 内部已绘制 "Power Off"
     * 覆盖层（背光拉亮 + 立即刷新 + 延时 800ms）再压暗背光、切断电池锁存；
     * 此处不再重复绘制，否则会与内部那次叠成两次显示（用户反馈：出现 2 次 Power Off）。 */
    sdgoods_power_off();                      /* 显示 "Power Off" → 压暗背光 → 延时 → 切断电池锁存（深度睡眠） */
}

/* 点按守卫的适配器：守卫回调签名是 void(*)(void*)，这里转调原本的处理函数。
 * 原处理函数不使用事件参数，传 NULL 安全。
 * ⚠️ 这些处理函数在本文件里定义得较靠后（分别靠近各自的页面实现），必须先前向声明，
 *    否则适配器调用它们会触发 implicit-function-declaration（-Werror=all 直接编译失败）。 */
static void on_vol_click(lv_event_t *e);
static void on_bri_click(lv_event_t *e);
static void on_pwr_click(lv_event_t *e);
static void on_bat_click(lv_event_t *e);
static void on_data_click(lv_event_t *e);

static void tap_vol(void *ud)  { (void)ud; ESP_LOGI(TAG, "tap: Volume");     on_vol_click(NULL); }
static void tap_bri(void *ud)  { (void)ud; ESP_LOGI(TAG, "tap: Brightness"); on_bri_click(NULL); }
static void tap_pwr(void *ud)  { (void)ud; ESP_LOGI(TAG, "tap: Power");      on_pwr_click(NULL); }

/* 「Exit」= 返回启动器。只有「被启动器装进 ota_N 并拉起的 app」才需要它：
 * 那类 app 是独立固件，重启后 bootloader 直接进该槽，不会自动回启动器，
 * 不给出口用户就「出不去」。与 tap_pwr 由第 5 个按钮的构建处二选一。 */
static void exit_to_launcher_async(void *p)
{
    (void)p;
    /* 先静音：从「正在播放声音的 app」跳转回启动器时，硬切会产生杂声（与关机同理）。
     * 多应用模式下这一路直接跳启动器、不打任何文字（用户要求），故只做静音。 */
    sdgoods_audio_bgm_stop();
    cc_settings_save();            /* 重启即断电：先把音量/亮度落盘 */
    /* 重启进启动器，正常路径不返回。
     * ⚠️ 该调用有**模式闸门**（2026-09-19）：只有「被启动器管理的 app」才会真的重启，
     *    否则返回 ESP_ERR_INVALID_STATE 且**不重启**。本按钮只在
     *    sdgoods_device_is_managed_app() 为真时显示（见第 5 个按钮的构建处），所以
     *    理论上到不了失败分支；真到了就留一条日志 —— 此时浮层还在，用户不会「按了黑屏」。 */
    esp_err_t r = sdgoods_return_to_launcher();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "Exit refused: %s (mode=%s, boot=%s) -- staying in app",
                 esp_err_to_name(r), sdgoods_device_mode_str(), sdgoods_device_boot_partition());
    }
}

static void tap_exit(void *ud)
{
    (void)ud;
    ESP_LOGI(TAG, "tap: Exit -> launcher");
    lv_async_call(exit_to_launcher_async, NULL);
}

static void on_vol_click(lv_event_t *e)
{
    (void)e;
    lv_async_call(open_vol_async, NULL);
}

static void on_bri_click(lv_event_t *e)
{
    (void)e;
    lv_async_call(open_bri_async, NULL);
}

static void on_pwr_click(lv_event_t *e)
{
    (void)e;
    lv_async_call(pwr_off_async, NULL);
}

/* ---- 数据二级页 ----------------------------------------------------------- */

static void data_back(void)
{
    if (s_data) {
        lv_obj_del(s_data);
        s_data = NULL;
    }
}

/* 低区未使用的 ota 槽空闲字节：枚举所有 app/ota 分区，累加其大小。
 * 单应用模式下这些槽一个 app 都没装 ⇒ 全部算空闲。运行分区本身排除。 */
static uint64_t sdgoods_unused_slot_bytes(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    uint64_t sum = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP,
                                                    ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p == run) continue;   /* 正在运行的那个分区本身不算空闲 */
        if (p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
            p->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_15) {
            sum += p->size;
        }
    }
    esp_partition_iterator_release(it);
    return sum;
}

/* 运行时解析「当前运行分区」的 app 镜像，得到 .bin 大小（含尾部 16B SHA）。
 * 直接读镜像头（魔数 0xE9，segment_count 在偏移 1）+ 各段头（8B: load_addr + data_len），
 * 不依赖 bootloader_support 的 esp_image_get_metadata。 */
static uint32_t sdgoods_app_image_size(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    if (!p) return 0;
    uint8_t hdr[24];
    if (esp_partition_read(p, 0, hdr, sizeof(hdr)) != ESP_OK) return 0;
    if (hdr[0] != 0xE9) return 0;                 /* ESP 镜像魔数校验 */
    int seg = hdr[1];
    if (seg <= 0 || seg > 32) return 0;
    uint32_t off   = 24;                           /* 镜像头固定 24 字节 */
    uint32_t total = 24;
    for (int i = 0; i < seg; i++) {
        uint8_t sh[8];
        if (esp_partition_read(p, off, sh, 8) != ESP_OK) return 0;
        /* 段头 8 字节 = [load_addr 4B][data_len 4B]，data_len 在偏移 4..7 */
        uint32_t data_len = (uint32_t)sh[4] | ((uint32_t)sh[5] << 8) |
                            ((uint32_t)sh[6] << 16) | ((uint32_t)sh[7] << 24);
        total += 8 + data_len;
        off   += 8 + data_len;
    }
    total += 16;                                   /* 追加的 SHA256（16B）计入 .bin 大小 */
    return total;
}

static void open_data(void)
{
    if (s_data) {
        lv_obj_del(s_data);
        s_data = NULL;
    }
    lv_obj_t *scr = lv_scr_act();
    s_data = lv_obj_create(scr);
    lv_obj_set_size(s_data, 360, 360);
    lv_obj_set_pos(s_data, 0, 0);
    lv_obj_set_style_bg_color(s_data, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_data, LV_OPA_COVER, 0);   /* 不透明：独立二级页 */
    lv_obj_set_style_border_width(s_data, 0, 0);
    lv_obj_set_style_pad_all(s_data, 0, 0);
    lv_obj_clear_flag(s_data, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_data);

    lv_obj_t *t = lv_label_create(s_data);
    lv_label_set_text(t, "Data");
    lv_obj_set_style_text_font(t, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);   /* 与首页 / 控制中枢标题同高 */

    /* 三项设备数据：插槽 / RAM / 存储空闲 */
    int total = sdgoods_launcher_slot_count();
    size_t ninst = 0;
    sdgoods_slot_entry_t apps[SDGOODS_SLOT_COUNT_MAX];
    sdgoods_launcher_list_installed(apps, SDGOODS_SLOT_COUNT_MAX, &ninst);

    /* RAM：free 与 total 都由 heap_caps 实测（内部 SRAM 堆），不再使用任何写死常量。
     * ⚠️ 旧版 total 写死成「物理 512KB」标称值 —— 那是个常量、测不出来，属于假数据；
     * 已按用户要求（「data 里面的数据要是真实的」）改为实测堆总量。
     * 口径说明：MALLOC_CAP_INTERNAL 的 total 只统计「可分配的堆区」，会排除 ROM 映像、
     * .data/.bss 静态占用与 DMA 保留区，故略小于芯片标称 SRAM。free/total 同为实测口径，
     * 二者相减即「当前已用堆」，比值才有意义。 */
    uint32_t ram_free  = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    uint32_t ram_total = (uint32_t)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024);

    /* 存储：appdata FAT 分区空闲容量（挂载失败或读取失败则显示 --） */
    uint64_t st_total = 0, st_free = 0;
    int st_ok = 0;
    if (sdgoods_appdata_mount() == ESP_OK) {
        st_ok = (esp_vfs_fat_info("/appdata", &st_total, &st_free) == ESP_OK);
        sdgoods_appdata_unmount();
    }

    /* 单应用模式没有槽：低区 ota_0..3 分区（约 12MB）全部空闲，应计入「存储空闲」；
     * 多应用模式下 ota 槽装着 app，不计入。运行分区本身（无论 factory 还是 ota_N）排除。 */
    if (sdgoods_device_mode() == SDGOODS_MODE_SINGLE) {
        uint64_t extra = sdgoods_unused_slot_bytes();
        st_free  += extra;
        st_total += extra;
    }

    /* 行内容按启动模式区分（2026-09-22 用户口径）：
     *   MULTI  —— 三行：`Slot 已装 / 总槽`（启动器真实的簿记状态）+ RAM + MEM
     *   SINGLE —— 两行：RAM + MEM。「当前跑的是哪个 app」这行删掉 ——
     *             主页本身就在展示这个 app，再报一遍属于重复信息。 */
    char l1[40], l2[40], l3[40];
    const bool single = (sdgoods_device_mode() != SDGOODS_MODE_MULTI);
    if (!single) {
        snprintf(l1, sizeof(l1), "Slot %u / %d", (unsigned)ninst, total);
    }
    snprintf(l2, sizeof(l2), "RAM Free %u / %u KB", (unsigned)ram_free, (unsigned)ram_total);
    if (st_ok) {
        snprintf(l3, sizeof(l3), "MEM Free %.1f MB", (double)st_free / (1024.0 * 1024.0));
    } else {
        snprintf(l3, sizeof(l3), "MEM Free -- MB");
    }

    /* 日志同时打出未取整的原始值：便于把屏上数字与运行时真值逐字节核对，
     * 确认页面上每一个数都是实测的、没有被常量替换掉。 */
    ESP_LOGI(TAG, "data page (%s): %s | %s | %s", sdgoods_device_mode_str(),
             single ? "(app name hidden)" : l1, l2, l3);
    ESP_LOGI(TAG, "data raw: ram_free=%u ram_total=%u bytes | fat_free=%llu fat_total=%llu bytes | installed=%u/%d",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
             (unsigned long long)st_free, (unsigned long long)st_total,
             (unsigned)ninst, total);

    if (single) {
        /* 单应用：两行（RAM / MEM），行距与电量页一致（40px），整块视觉居中 */
        const char *lines2[2] = { l2, l3 };
        for (int i = 0; i < 2; i++) {
            lv_obj_t *l = lv_label_create(s_data);
            lv_label_set_text(l, lines2[i]);
            lv_obj_set_style_text_font(l, &si_yuan_black_icon_16, 0);
            lv_obj_set_style_text_color(l, lv_color_white(), 0);
            lv_obj_align(l, LV_ALIGN_CENTER, 0, -28 + i * 40);
        }
    } else {
        const char *lines[3] = { l1, l2, l3 };
        for (int i = 0; i < 3; i++) {
            lv_obj_t *l = lv_label_create(s_data);
            lv_label_set_text(l, lines[i]);
            lv_obj_set_style_text_font(l, &si_yuan_black_icon_16, 0);
            lv_obj_set_style_text_color(l, lv_color_white(), 0);
            lv_obj_align(l, LV_ALIGN_CENTER, 0, -48 + i * 40);
        }
    }

    /* 底部小横条：提示「从底部往上滑返回主页」 */
    make_bottom_hint(s_data);

    /* 从底部横条处上滑 → 直接返回主页（关闭数据页 + 一级控制中心） */
    sdgoods_swipe_up_bind(s_data, sdgoods_cc_close);

    /* 从最左边起手左→右滑 → 返回上一级（控制中心） */
    sdgoods_swipe_back_bind(s_data, data_back);

    /* 全局手势策略（文字穿透 + 按下所有权锁定） */
    sdgoods_tap_normalize(s_data);
}

static void open_data_async(void *p)
{
    (void)p;
    open_data();
}

/* ---- 电量二级页（电池电压 + 电量百分比） ------------------------------------- */

/* 电压 -> 百分比 的换算端点（用户给定）：最小 3.00V = 0%，最大 4.28V = 100%。
 * ⚠️ 锂电真实放电曲线并不线性（3.7~4.0V 段最平坦，同一电压对应的剩余电量跨度很大），
 * 这里按用户给的两点做线性换算，只作粗略指示，不做库仑计式的精确电量。 */
#define SDG_BAT_V_MIN  3.00f
#define SDG_BAT_V_MAX  4.28f

/* 电池读数的**唯一**换算口径：控制中心「电量」页与启动器主页底部状态行共用。
 * 两处各写一份阈值迟早会漂移，届时同一块电池在两个界面显示不同百分比，很难查。
 * 返回 0~100 的百分比；读数失败（平台层返 0.f）返回 -1，并回填 *v_out = 0。
 * v_out 可传 NULL（只关心百分比时）。 */
int sdgoods_cc_bat_read(float *v_out)
{
    float v = sdgoods_hw_bat_v();
    if (v_out) {
        *v_out = v;
    }
    if (!(v > 0.3f)) {
        return -1;   /* ADC 未就绪 / 读数异常 ⇒ 让调用方显示 "--"，不要显示 0% */
    }
    float p = (v - SDG_BAT_V_MIN) / (SDG_BAT_V_MAX - SDG_BAT_V_MIN) * 100.0f;
    if (p < 0.0f)   p = 0.0f;
    if (p > 100.0f) p = 100.0f;   /* 充电时瞬时电压会 >4.28V */
    return (int)(p + 0.5f);
}

static void volt_back(void)
{
    if (s_volt) {
        lv_obj_del(s_volt);
        s_volt = NULL;
    }
}

static void open_volt(void)
{
    volt_back();
    lv_obj_t *scr = lv_scr_act();
    s_volt = lv_obj_create(scr);
    lv_obj_set_size(s_volt, 360, 360);
    lv_obj_set_pos(s_volt, 0, 0);
    lv_obj_set_style_bg_color(s_volt, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_volt, LV_OPA_COVER, 0);   /* 不透明：独立二级页 */
    lv_obj_set_style_border_width(s_volt, 0, 0);
    lv_obj_set_style_pad_all(s_volt, 0, 0);
    lv_obj_clear_flag(s_volt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_volt);

    lv_obj_t *t = lv_label_create(s_volt);
    lv_label_set_text(t, "Battery");   /* 正文两行：Voltage x.xxV / Level nn% */
    lv_obj_set_style_text_font(t, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);   /* 与首页 / 控制中心标题同高 */

    /* 电池电压：平台层 sdgoods_hw_bat_v() 读 ADC1_CH7（含 x3 分压换算）返回伏特，
     * 打开本页时采样一次即可（ADC 单次转换，无需连续刷新）。
     * 换算走共用函数，与启动器主页底部状态行同口径。
     * 注意：读数失败时返回 -1，此时显示 "--" 而不是 0%，避免被误读成没电。 */
    float v = 0.0f;
    int pct = sdgoods_cc_bat_read(&v);
    const bool bat_ok = (pct >= 0);
    char vbuf[32], pbuf[32];
    if (bat_ok) {
        snprintf(vbuf, sizeof(vbuf), "Voltage %.2fV", (double)v);
        snprintf(pbuf, sizeof(pbuf), "Level %d%%", pct);
    } else {
        snprintf(vbuf, sizeof(vbuf), "Voltage --V");
        snprintf(pbuf, sizeof(pbuf), "Level --%%");
    }
    ESP_LOGI(TAG, "battery page: %s | %s  (raw v=%.3f)", vbuf, pbuf, (double)v);

    /* 两行（电压 / 百分比）以 40px 行距排布，整块视觉居中 */
    const char *vlines[2] = { vbuf, pbuf };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = lv_label_create(s_volt);
        lv_label_set_text(l, vlines[i]);
        lv_obj_set_style_text_font(l, &si_yuan_black_icon_16, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -28 + i * 40);   /* 与数据页行距口径一致 */
    }

    /* 底部小横条：提示「从底部往上滑返回主页」 */
    make_bottom_hint(s_volt);

    /* 从底部横条处上滑 -> 直接返回主页（关闭电量页 + 一级控制中心） */
    sdgoods_swipe_up_bind(s_volt, sdgoods_cc_close);

    /* 从最左边起手左->右滑 -> 返回上一级（控制中心） */
    sdgoods_swipe_back_bind(s_volt, volt_back);

    /* 全局手势策略（文字穿透 + 按下所有权锁定） */
    sdgoods_tap_normalize(s_volt);
}

static void open_volt_async(void *p)
{
    (void)p;
    open_volt();
}

static void tap_bat(void *ud)  { (void)ud; ESP_LOGI(TAG, "tap: Battery");    on_bat_click(NULL); }

static void on_bat_click(lv_event_t *e)
{
    (void)e;
    lv_async_call(open_volt_async, NULL);
}


/* 串口调试钩子：从 console_rx_task 调用，必须 marshalling 到 LVGL 线程。
 * ⚠️ 必须先 close 再 open：否则屏上已开着 CC/二级页时 cc_open() 会因 s_cc != NULL 直接返回，
 * 截回来的就是二级页（幻触经常把设备停在这个状态，会让人误判「布局没生效」）。 */
static void debug_open_cc_async(void *p)
{
    (void)p;
    sdgoods_cc_close();
    sdgoods_cc_open();
}

static void debug_open_data_async(void *p)
{
    (void)p;
    sdgoods_cc_close();
    sdgoods_cc_open();
    open_data();
}

static void debug_open_volt_async(void *p)
{
    (void)p;
    sdgoods_cc_close();
    sdgoods_cc_open();
    open_volt();
}

/* which=3：音量滑块页。滑块页的手势冲突（左缘右滑返回 vs 滑块左右拖动）
 * 只能在这一页上验证，所以必须能离线打开它。 */
static void debug_open_slider_async(void *p)
{
    (void)p;
    sdgoods_cc_close();
    sdgoods_cc_open();
    open_slider("Volume", sdgoods_audio_get_volume(), sdgoods_audio_set_volume);
}

/* which=4：**亮度**滑块页（2026-09-19 新增）。
 * 为什么要有它：音量能离线开（which=3），亮度却是「音量/亮度跨 app 同步」里
 * **同样需要被显式改一次再断言**的那一半 —— 没有入口就只能断言「恢复值等于一个
 * 从没被改过的值」，那种断言即使功能全坏也能通过（假阳性）。 */
static void debug_open_bri_async(void *p)
{
    (void)p;
    sdgoods_cc_close();
    sdgoods_cc_open();
    open_slider("Brightness", sdgoods_lcd_get_backlight(), apply_backlight);
}

void sdgoods_cc_debug_open(int which)
{
    /* 串口命令来自 console RX 任务 ⇒ 必须投递到 LVGL 线程执行。
     * ⚠️ 用 sdgoods_lvgl_post()（不是 lv_async_call）：后者会与 LVGL 线程并发改
     *    同一条无锁定时器链表 ⇒ 崩溃，详见 sdgoods_lvgl.h。 */
    sdgoods_lvgl_post(which == 1 ? debug_open_data_async
                      : which == 2 ? debug_open_volt_async
                      : which == 3 ? debug_open_slider_async
                      : which == 4 ? debug_open_bri_async
                                   : debug_open_cc_async, NULL);
}

static void tap_data(void *ud) { (void)ud; ESP_LOGI(TAG, "tap: Data");       on_data_click(NULL); }

static void on_data_click(lv_event_t *e)
{
    (void)e;
    lv_async_call(open_data_async, NULL);
}

/* 系统浮层开 / 关状态：用于给应用外壳发 pause / resume 通知，并避免重复通知。 */
static bool s_overlay_open = false;

void sdgoods_cc_close(void)
{
    /* ⚠️ 只有**真的关掉了一个开着的浮层**才落盘（2026-09-19 修）。
     *
     * 旧实现无条件 `sdgoods_cc_flush()`。但本函数还有一个「非用户动作」的调用点：
     * ui_launcher_create() 在删旧屏前必须先关浮层（否则 CC 的静态句柄成野指针），
     * 而它**每次开机都会跑**。于是每次开机都会在「开机动画期间背光本来就是 0」的
     * 时刻落盘一次 ⇒ NVS 里的亮度被写成 0 ⇒ 紧接着 `sdgoods_cc_settings_restore()`
     * 又把这个 0 读回来 apply ⇒ **背光 0、屏幕全黑**（用户可见症状：开机黑屏 /
     * 亮度在 app 之间「不同步」——因为它压根不是用户的亮度）。
     *
     * 真机日志特征（改前必现，可当作回归探针）：
     *   I (1252) cc: save: vol=70% bri=0 -> NVS ns='sdg_cc'
     *   I (1272) cc: restore: vol=70% bri=0 (from NVS ns='sdg_cc')
     *   —— 开机后约 1.2 秒、用户还没碰过任何东西。
     *
     * 语义上也本该如此：落盘的目的是「把用户在 CC 里挂起的调节先存下来」，
     * 没有开着的浮层就说明没有任何挂起调节。 */
    if (s_cc || s_slider || s_data || s_volt) {
        sdgoods_cc_flush();   /* 返回主页前落盘（避免随后立即断电丢最近调节） */
    }
    if (s_volt) {
        lv_obj_del(s_volt);
        s_volt = NULL;
    }
    if (s_data) {
        lv_obj_del(s_data);
        s_data = NULL;
    }
    if (s_slider) {
        lv_obj_del(s_slider);
        s_slider = NULL;
    }
    if (s_about) {
        lv_obj_del(s_about);
        s_about = NULL;
    }
    if (s_cc) {
        lv_obj_del(s_cc);
        s_cc = NULL;
    }
    /* 通知应用外壳：系统浮层已关闭 ⇒ app 恢复推进（游戏类继续）。 */
    if (s_overlay_open) {
        s_overlay_open = false;
        sdgoods_app_shell_notify_overlay(false);
    }
}

/* ---- 关于页（About）------------------------------------------------------ */

static void about_back(void)
{
    if (s_about) {
        lv_obj_del(s_about);
        s_about = NULL;
    }
}

static void open_about(void)
{
    about_back();
    lv_obj_t *scr = lv_scr_act();
    s_about = lv_obj_create(scr);
    lv_obj_set_size(s_about, 360, 360);
    lv_obj_set_pos(s_about, 0, 0);
    lv_obj_set_style_bg_color(s_about, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_about, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_about, 0, 0);
    lv_obj_set_style_pad_all(s_about, 0, 0);
    lv_obj_clear_flag(s_about, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_about);

    lv_obj_t *t = lv_label_create(s_about);
    lv_label_set_text(t, "About");
    lv_obj_set_style_text_font(t, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);

    /* 运行中的 app 描述：名称 / 编译时间 取自 esp_app_desc_t */
    const esp_app_desc_t *d = esp_app_get_description();
    const char *name  = (d && d->project_name[0]) ? d->project_name : "?";
    const char *bdate = (d && d->date[0])  ? d->date  : "--";
    const char *btime = (d && d->time[0])  ? d->time  : "--";
    /* 版本：SDGOODS_APP_VERSION 透传 version.txt，与弹框读的 esp_app_desc.version 同源（不再 +1） */
    const char *ver = SDGOODS_APP_VERSION;

    /* app 包大小：运行时解析运行分区镜像 */
    uint32_t img_len = sdgoods_app_image_size();
    char szbuf[40];
    if (img_len > 0) {
        if (img_len >= 1024 * 1024)
            snprintf(szbuf, sizeof(szbuf), "%.2f MB", (double)img_len / (1024.0 * 1024.0));
        else
            snprintf(szbuf, sizeof(szbuf), "%.1f KB", (double)img_len / 1024.0);
    } else {
        snprintf(szbuf, sizeof(szbuf), "--");
    }

    /* 安装槽编号：仅多应用（从 ota_N 启动）显示；单应用不显示 */
    char slotbuf[40];
    bool has_slot = false;
    const esp_partition_t *rp = esp_ota_get_running_partition();
    if (rp && rp->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
        rp->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_15) {
        int idx = rp->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;
        snprintf(slotbuf, sizeof(slotbuf), "Slot %d (%s)", idx, rp->label);
        has_slot = true;
    }

    /* ⚠️ 行缓冲放大到 64：l3/l4 由运行时值填充、长度不可静态推断，
     * 在 -Werror=format-truncation 下会判为「可能截断」而报错；加大到 64 即可消除。
     * 四行只显示值，不带 App/Ver/Build/Size 标签（2026-09-22 用户口径：删的是
     * 前缀词，数值行保留——单/多应用一视同仁；Slot 行仍仅多应用显示）。 */
    char l1[64], l2[64], l3[64], l4[64];
    snprintf(l1, sizeof(l1), "%.20s", name);
    snprintf(l2, sizeof(l2), "v%s",   ver);
    snprintf(l3, sizeof(l3), "%s %s", bdate, btime);
    snprintf(l4, sizeof(l4), "%s",    szbuf);

    const char *lines[5];
    int n = 0;
    lines[n++] = l1;
    lines[n++] = l2;
    lines[n++] = l3;
    lines[n++] = l4;
    if (has_slot) lines[n++] = slotbuf;

    for (int i = 0; i < n; i++) {
        lv_obj_t *l = lv_label_create(s_about);
        lv_label_set_text(l, lines[i]);
        lv_obj_set_style_text_font(l, &si_yuan_black_icon_16, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -64 + i * 34);
    }

    ESP_LOGI(TAG, "about page: %s | v%s | %s %s | %s | %s",
             name, ver, bdate, btime, szbuf, has_slot ? slotbuf : "(no slot)");

    make_bottom_hint(s_about);
    sdgoods_swipe_up_bind(s_about, sdgoods_cc_close);   /* 上滑直接回主页 */
    sdgoods_swipe_back_bind(s_about, about_back);       /* 左滑回控制中心 */
    sdgoods_tap_normalize(s_about);
}

static void open_about_async(void *p) { (void)p; open_about(); }

static void on_about_click(lv_event_t *e) { (void)e; lv_async_call(open_about_async, NULL); }

static void tap_about(void *ud) { (void)ud; ESP_LOGI(TAG, "tap: About"); on_about_click(NULL); }

void sdgoods_cc_open(void)
{
    if (s_cc) {
        return;   /* 已打开，避免重复创建 */
    }
    s_btn_ctx_n = 0;   /* 重建一级页：守卫上下文池重新分配 */
    lv_obj_t *scr = lv_scr_act();
    s_cc = lv_obj_create(scr);
    lv_obj_set_size(s_cc, 360, 360);
    lv_obj_set_pos(s_cc, 0, 0);
    lv_obj_set_style_bg_color(s_cc, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_cc, LV_OPA_COVER, 0);   /* 不透明：菜单页不漏出主屏 */
    lv_obj_set_style_border_width(s_cc, 0, 0);
    lv_obj_set_style_pad_all(s_cc, 0, 0);
    lv_obj_clear_flag(s_cc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_cc);

    lv_obj_t *t = lv_label_create(s_cc);
    lv_label_set_text(t, "Control Center");
    lv_obj_set_style_text_font(t, &si_yuan_black_icon_16, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);   /* 与首页标题同高 */

    s_cc_icon_n = 0;   /* 重置图标缓冲槽位，每个按钮各占一个 */

    /* 应用上下文：小鸟游戏下隐藏 Data / Battery / About，第 5 键改成「返回主页」。 */
    const bool bird = (s_app_ctx == SDGOODS_CC_CTX_BIRD);

    if (bird) {
        /* 小鸟上下文：三个按钮一行居中（cy=169），专注游戏、减少误触。 */
        make_round_btn(s_cc, 84,  169, 68, CC_ICON_VOL,  "Volume",  tap_vol,  NULL);
        make_round_btn(s_cc, 180, 169, 68, CC_ICON_BRI,  "Brightness", tap_bri, NULL);
        make_round_btn(s_cc, 276, 169, 68, CC_ICON_HOME, "Home",    tap_home, NULL);
    } else {
        /* 上排 3 个 + 下排 2 个【左对齐】，整块（图标 + caption）在圆内左右、上下都居中：
         *   上排：Volume / Brightness / Data（x = 84 / 180 / 276，cy = 118）
         *   下排：Battery / Power（x = 84 / 180，与首列对齐；「关机」固定排最后，误触代价最大）
         * 整块包围盒：y 84..275（上留白 84 / 下留白 85，上下居中）；
         *             x 50..310（中心 180，左右居中）。直径 68（较上一版 76 收小）。
         * ⚠️ 行距必须留出 caption 高度：caption 用 OUT_BOTTOM_MID +6 挂按钮下缘（约 17px 高），
         * 上排 caption 底 175，下排按钮上缘 184 -> 9px 净空，互不遮挡（曾因行距过小被盖住）。
         * 圆内校验（贴 360² 圆屏，别靠肉眼）：上排最远点 |OC|+r = 114.3+34 = 148.3 < 180 ✓；
         * 下排 (84,218) 137.2 ✓、(180,218) 72.0 ✓；"Battery" caption 左下角 (59,275) 距圆心 153.8 ✓。 */
        make_round_btn(s_cc, 84,  118, 68, CC_ICON_VOL,  "Volume",     tap_vol,  NULL);
        make_round_btn(s_cc, 180, 118, 68, CC_ICON_BRI,  "Brightness", tap_bri,  NULL);
        make_round_btn(s_cc, 276, 118, 68, CC_ICON_DATA, "Data",       tap_data, NULL);
        make_round_btn(s_cc, 84,  218, 68, CC_ICON_BAT,  "Battery",    tap_bat,  NULL);
        /* ⚠️ 第 5 个按钮不能用「设备模式 == MULTI」来选语义：启动器自己也是 MULTI，
         *    但它要的是关机。判据必须是「本固件是不是被启动器管理的 app」：
         *    从 ota_N 启动 ⇒ 被管理 ⇒ 提供 Exit（重启才回得去启动器）；
         *    从 factory 启动（启动器宿主 / 单应用主机固件）⇒ 提供 Power（关机）。 */
        const bool managed_app = sdgoods_device_is_managed_app();
        make_round_btn(s_cc, 276, 218, 68, CC_ICON_PWR, managed_app ? "Exit" : "Power",
                       managed_app ? tap_exit : tap_pwr, NULL);
        /* 第 6 个按钮：About（关于）——显示 app 名称 / 版本 / 编译时间 / 包大小 / 安装槽（多应用）。
         * 与 Power 互换了位置：About 在中间(180,218)，Power 在最右(276,218)。 */
        make_round_btn(s_cc, 180, 218, 68, CC_ICON_INFO, "About", tap_about, NULL);
    }

    /* 底部小横条：提示「从底部往上滑返回主页」 */
    make_bottom_hint(s_cc);

    /* 设备启动模式（单应用 / 多应用）：一级页底部小字，现场一眼确认本机跑在哪种模式。
     * 判定口径见 sdgoods_device_mode.h（运行分区 + factory 内固件的身份，本地自证）。
     * 位置：底部提示横条在 y=334，这里贴在它上方（y≈316），仍在 360 圆内
     * （y=316 时可用半宽 ≈118px，文案约 70px 宽，安全）。 */
    lv_obj_t *mode_lbl = lv_label_create(s_cc);
    /* 只显示模式值本身（SINGLE / MULTI），不带 "Mode" 前缀 */
    lv_label_set_text(mode_lbl, sdgoods_device_mode_str());
    lv_obj_set_style_text_font(mode_lbl, &si_yuan_black_icon_14, 0);
    lv_obj_set_style_text_color(mode_lbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_align(mode_lbl, LV_ALIGN_BOTTOM_MID, 0, -36);

    /* 从底部横条处上滑 → 关闭控制中心（返回主页） */
    sdgoods_swipe_up_bind(s_cc, sdgoods_cc_close);

    /* 从最左边起手左→右滑 → 也返回主页（控制中心之上即主页） */
    sdgoods_swipe_back_bind(s_cc, sdgoods_cc_close);

    /* 全局手势策略（放在最后：按钮 / caption / 底部横条都已创建）：
     *   - caption 等文字变触摸穿透 ⇒ 从底部上滑时按到文字也能落到捕获带上；
     *   - 页面与按钮锁定按下所有权 ⇒ 从底部上滑（起手点略高于捕获带）不会再
     *     被中途接管成「点了上面的按钮」（用户报的误触）。 */
    sdgoods_tap_normalize(s_cc);

    /* 通知应用外壳：系统浮层已打开 ⇒ app 暂停推进（游戏类停止）。
     * 与旧的应用外壳菜单 pause/resume 语义一致，避免换成控制中心后丢掉暂停能力。 */
    if (!s_overlay_open) {
        s_overlay_open = true;
        sdgoods_app_shell_notify_overlay(true);
    }
}

bool sdgoods_cc_is_open(void)
{
    return s_cc != NULL || s_slider != NULL || s_data != NULL || s_volt != NULL;
}

bool sdgoods_cc_power_short(void)
{
    if (sdgoods_cc_is_open()) {
        sdgoods_cc_close();
        return true;   /* 已消费：关闭浮层即“回到主页” */
    }
    return false;
}

/* ---- 主页顶部下滑手势 ----------------------------------------------------- */

#define CC_TOP_ZONE  70   /* 顶部手势区高度（覆盖标题区，更宽容地接住「从上往下滑」） */
#define CC_SWIPE_DY  50   /* 下滑判定阈值 */

static lv_coord_t s_cc_start_y = 0;

static void on_cc_pressed(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_cc_start_y = p.y;
}

static void on_cc_released(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dy = (int)p.y - (int)s_cc_start_y;
    ESP_LOGI(TAG, "swipe-down start_y=%d dy=%d", (int)s_cc_start_y, dy);
    if (s_cc_start_y <= CC_TOP_ZONE && dy >= CC_SWIPE_DY) {
        /* async：避免在输入事件回调里同步创建整屏浮层（indev 状态错乱坑） */
        lv_async_call((lv_async_cb_t)sdgoods_cc_open, NULL);
    }
}

void sdgoods_cc_bind(lv_obj_t *home_scr)
{
    if (!home_scr) {
        return;
    }
    lv_obj_t *cat = lv_obj_create(home_scr);
    lv_obj_remove_style_all(cat);
    lv_obj_set_size(cat, 360, CC_TOP_ZONE);
    lv_obj_set_pos(cat, 0, 0);
    lv_obj_set_style_bg_opa(cat, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(cat, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cat, LV_OBJ_FLAG_CLICKABLE);
    /* ⚠️ PRESS_LOCK 必须加：LVGL 按下期间每个输入周期都会重新命中测试，
     * 手指滑出捕获区（落到 app 图标上）会把按下对象切走 -> PRESS_LOST ->
     * RELEASED 永不触发（下滑手势失效）且松手时 CLICKED 落到图标上 -> 误启动 app。
     * PRESS_LOCK 让整个滑动期间按下状态锁死在本捕获区上。 */
    lv_obj_add_flag(cat, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_move_foreground(cat);   /* 置顶，捕获顶部下滑手势 */
    lv_obj_add_event_cb(cat, on_cc_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(cat, on_cc_released, LV_EVENT_RELEASED, NULL);
}
