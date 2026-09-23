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

#pragma once

#include <stdbool.h>   /* sdgoods_ui_in_circle / sdgoods_ui_in_safe_area 用到 bool */

/*
 * sdgoods_ui.h —— 圆屏通用 UI 栅格（平台层）
 *
 * 这块 360x360 圆屏的可用区域比同尺寸矩形屏小得多：四角会被圆边切掉。
 * 下面这套「3 列 x 2 行 + 标题 + 页脚」的栅格是主页 / 应用页 / DEMO 页共用的，
 * 坐标为各控件的**左上角**：
 *
 *            BTN1    BTN2    BTN3          <- 第 1 行 y = 93
 *            BTN4    BTN5    BTN6          <- 第 2 行 y = 191
 *
 *  标题在 y = 35 居中；页脚在 y = 296 居中。
 *
 * ⚠️ 圆边裁切：第 1 / 3 列（x=48 / 236）与第 2 行（y=191）已经比较靠近圆边。
 *    把 BTN_SIZE 调大或把坐标继续外移，按钮会被圆边切掉一块。
 *    改完请用 tools/screenshot_recv.py 截屏确认。
 */

#define SDG_UI_BTN_SIZE   76

#define SDG_UI_BTN1_X     48
#define SDG_UI_BTN1_Y     93
#define SDG_UI_BTN2_X     142
#define SDG_UI_BTN2_Y     93
#define SDG_UI_BTN3_X     236
#define SDG_UI_BTN3_Y     93
#define SDG_UI_BTN4_X     48
#define SDG_UI_BTN4_Y     191
#define SDG_UI_BTN5_X     142
#define SDG_UI_BTN5_Y     191
#define SDG_UI_BTN6_X     236
#define SDG_UI_BTN6_Y     191

#define SDG_UI_TITLE_Y    35
#define SDG_UI_FOOTER_Y   296

/* ---- 派生量：给「按数量自适应排布」用（启动台 / DEMO 页靠它们算坐标）----
 * 有了这几个量，往应用清单里加应用就不用回来手改坐标了：
 *   一行的总宽 = n*BTN_SIZE + (n-1)*间距；左起第一个按钮的 x = 圆屏中心 - 总宽/2
 * 行高取栅格原值，所以 1 行按钮落在两行之间的正中（142），与主页按钮行同高。 */
#define SDG_UI_BTN_PITCH   (SDG_UI_BTN2_X - SDG_UI_BTN1_X)        /* 94：相邻按钮左上角的 x 差 */
#define SDG_UI_ROW1_Y      SDG_UI_BTN1_Y                          /* 93 */
#define SDG_UI_ROW2_Y      SDG_UI_BTN4_Y                          /* 191 */
#define SDG_UI_ROW_MID_Y   ((SDG_UI_ROW1_Y + SDG_UI_ROW2_Y) / 2)  /* 142：单行时的垂直居中高度 */
#define SDG_UI_CENTER_X    (SDG_UI_BTN2_X + SDG_UI_BTN_SIZE / 2)  /* 180：圆屏水平中心 */

/* ===========================================================================
 * 圆屏安全区（给 AI / 应用放控件用）
 * ---------------------------------------------------------------------------
 * 这块屏是 360×360 的**圆形**显示区，四角会被圆边切掉。任何**非全屏**的控件、
 * 文字、图片都应落在下面的「安全矩形」内，否则会被圆边裁掉一块。
 *   - 安全矩形：以圆心 (180,180) 为中心、边长 240 的方（四周留 60px 余量）。
 *   - 全屏背景 / 圆形遮罩才允许超出安全区，贴住圆边。
 * ⚠️ 这些是几何参考值；不同批次屏的可见半径略有差异，**改完务必用
 *    tools/screenshot_recv.py 截屏核验**，尤其贴边内容。
 * ========================================================================= */
#define SDG_UI_W          360        /* 屏宽（像素） */
#define SDG_UI_H          360        /* 屏高（像素） */
#define SDG_UI_CX         180        /* 圆心 x */
#define SDG_UI_CY         180        /* 圆心 y */
#define SDG_UI_RADIUS     180        /* 圆屏几何半径（像素） */

/* 安全矩形：控件左上角 (x,y) 与尺寸 (w,h) 都应满足
 *   x >= SDG_UI_SAFE_X && x + w <= SDG_UI_SAFE_X + SDG_UI_SAFE_W
 *   y >= SDG_UI_SAFE_Y && y + h <= SDG_UI_SAFE_Y + SDG_UI_SAFE_H
 * 即内容完全落在圆内、不碰圆边。 */
#define SDG_UI_SAFE_X     60
#define SDG_UI_SAFE_Y     60
#define SDG_UI_SAFE_W     240
#define SDG_UI_SAFE_H     240
#define SDG_UI_SAFE_R     120        /* 安全区半边长；点离圆心距离 > 此值即越界 */

/* 点 (x,y) 是否落在圆屏内（距圆心 <= 半径）。用于裁剪 / 命中判断。 */
static inline bool sdgoods_ui_in_circle(int x, int y)
{
    int dx = x - SDG_UI_CX;
    int dy = y - SDG_UI_CY;
    return (dx * dx + dy * dy) <= (SDG_UI_RADIUS * SDG_UI_RADIUS);
}

/* 点 (x,y) 是否落在安全矩形内（不碰圆边）。用于放置文本 / 按钮 / 图片。 */
static inline bool sdgoods_ui_in_safe_area(int x, int y)
{
    return (x >= SDG_UI_SAFE_X && x < SDG_UI_SAFE_X + SDG_UI_SAFE_W &&
            y >= SDG_UI_SAFE_Y && y < SDG_UI_SAFE_Y + SDG_UI_SAFE_H);
}
