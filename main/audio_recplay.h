#pragma once

#include "esp_err.h"

#define AUDIO_RECPLAY_SEC  5u

esp_err_t audio_recplay_init(void);

esp_err_t audio_recplay_record(void);

esp_err_t audio_recplay_play(void);

void audio_recplay_abort(void);

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

esp_err_t audio_bgm_start(audio_bgm_theme_t theme);   /* 启动指定主题 BGM（同时打开功放） */
void      audio_bgm_stop(void);    /* 停止 BGM 任务并关闭功放 */
void      audio_sfx_flap(void);    /* 触发一次“拍翅”音效（仅在 BGM 运行时混合输出） */
void      audio_set_volume(int pct); /* 设置全局音量（0~100），背景音与音效同步缩放 */
int       audio_get_volume(void);    /* 读取当前音量（0~100） */
