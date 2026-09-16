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

#include "esp_err.h"

#define AUDIO_RECPLAY_SEC  5u

esp_err_t sdgoods_audio_init(void);

esp_err_t sdgoods_audio_record(void);

esp_err_t sdgoods_audio_play(void);

void sdgoods_audio_abort(void);

/* ---------------------------------------------------------------------------
 * 游戏音频：背景音乐（BGM）循环 + 拍翅音效（SFX）
 * 复用本模块已初始化的 I2S 发送通道（s_tx），避免重复占用 I2S0。
 * ------------------------------------------------------------------------- */
typedef enum {
    AUDIO_BGM_THEME_FLAPPY = 0,   /* 欢快 C 大调 */
    AUDIO_BGM_THEME_PLANE,        /* 激昂上行 */
    AUDIO_BGM_THEME_TETRIS,       /* 经典俄方块旋律 */
    AUDIO_BGM_THEME_DEFAULT = AUDIO_BGM_THEME_FLAPPY,
} audio_bgm_theme_t;

esp_err_t sdgoods_audio_bgm_start(audio_bgm_theme_t theme);   /* 启动指定主题 BGM（同时打开功放） */
void      sdgoods_audio_bgm_stop(void);    /* 停止 BGM 任务并关闭功放 */
void      sdgoods_audio_sfx_flap(void);    /* 触发一次“拍翅”音效（仅在 BGM 运行时混合输出） */
void      sdgoods_audio_set_volume(int pct); /* 设置全局音量（0~100），背景音与音效同步缩放 */
int       sdgoods_audio_get_volume(void);    /* 读取当前音量（0~100） */
