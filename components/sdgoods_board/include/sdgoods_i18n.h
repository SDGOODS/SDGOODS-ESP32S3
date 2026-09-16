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

#include <stdbool.h>
#include <stdint.h>

/*
 * sdgoods_i18n.h —— 界面语言（中 / 英）
 *
 * 设计要点
 * --------
 * · **默认英文**（出厂状态），用户可在 DEMO 页点「语言」按钮切到中文；
 *   选择存 NVS，重启后保留。
 * · 取词用 SDG_T(中文, 英文) 内联写，不做字符串表 —— 少一层间接，
 *   改文案时中英就在同一行，不会漏掉另一语言。
 * · 界面**重建**：切换语言后已创建好的 LVGL 屏上的旧文案不会自己更新，
 *   所以每个带屏缓存的页面在 *_show() 里比一下 sdg_i18n_seq()，
 *   变了就先删旧屏再重建。三行代码，别偷懒省掉（否则切完语言旧页还是旧文字）。
 *     static uint32_t s_lang_seq;
 *     if (s_scr && s_lang_seq != sdg_i18n_seq()) { lv_obj_del(s_scr); s_scr = NULL; }
 *     if (s_scr) { lv_scr_load(s_scr); return; }
 *     s_lang_seq = sdg_i18n_seq();
 * · 品牌名与公司名（谷仓共创计划 / 谷仓 SDGOODS 开放平台 / 谷仓次元屏 /
 *   深圳希德创新网络有限公司）是商标与法定名称，**不做翻译**，任何语言下
 *   都按原样显示（见关于页）。
 */

typedef enum {
    SDG_LANG_EN = 0,   /* English（默认） */
    SDG_LANG_ZH = 1,   /* 简体中文 */
} sdg_lang_t;

/* 从 NVS 读取语言设置；无记录时用默认英文。main.c 在创建任何界面前调用一次。 */
void sdg_i18n_init(void);

/* 当前语言 */
sdg_lang_t sdg_i18n_get(void);
bool       sdg_i18n_is_zh(void);

/* 切换语言（写 NVS；NVS 不可用时仅本次运行生效，不影响功能） */
void sdg_i18n_set(sdg_lang_t lang);
void sdg_i18n_toggle(void);

/* 语言版本号：每切换一次 +1。页面用它判断「手上的屏是不是旧语言的」。 */
uint32_t sdg_i18n_seq(void);

/* 按当前语言取词。一般直接用下面的 SDG_T 宏。 */
const char *sdg_i18n_pick(const char *zh, const char *en);

/* 取词宏：SDG_T("中文", "English")。返回值生命周期同字符串字面量。 */
#define SDG_T(zh, en) sdg_i18n_pick((zh), (en))

/* 语言代码（"en" / "zh"），给日志和 sdgoods_hw_info 类用途 */
const char *sdg_i18n_code(void);
