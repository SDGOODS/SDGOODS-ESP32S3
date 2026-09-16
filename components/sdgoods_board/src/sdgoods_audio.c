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

#include "sdgoods_audio.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RATE_HZ      16000u
#define SAMPLES      (RATE_HZ * AUDIO_RECPLAY_SEC)
#define MIC_SHIFT    14
#define PLAY_VOL     70
#define WARMUP_MS    200
#define PREFILL_MS   80
#define PA_SETTLE_MS 50
#define TAIL_MS      60
#define HPF_A_Q15    31800
#define NORM_RMS     3500
#define LEVEL_SKIP_MS 300
#define FADE_MS      60
#define GAIN_Q10_MAX (2 << 10)

static int16_t *s_pcm;
static size_t s_pcm_n;
static i2s_chan_handle_t s_rx;
static i2s_chan_handle_t s_tx;
static bool s_rx_on;
static bool s_tx_on;
static volatile bool s_abort;
static bool s_ready;
static int32_t s_mic_raw[512];
static int16_t s_spk_stereo[512];
static int16_t s_silence[128];

/* ---------------------------------------------------------------------------
 * BGM（背景音乐）循环 + 拍翅音效（SFX）
 * ------------------------------------------------------------------------- */
#define BGM_RATE      16000u
#define BGM_BEAT_MS   200u
#define BGM_NOTES     16
#define BGM_FLAP_MS   120u
#define BGM_FADE_STEPS 4u   /* 淡出块数：每块 256 帧 @16kHz ≈ 16ms，共约 64ms */

static int16_t     *s_bgm;          /* 预生成的 BGM 立体声 PCM（循环播放） */
static size_t       s_bgm_frames;   /* 帧数（每帧 = 2 个 int16） */
static TaskHandle_t s_bgm_task;
static bool                s_bgm_run;
static audio_bgm_theme_t   s_bgm_theme = AUDIO_BGM_THEME_DEFAULT;
static volatile bool       s_bgm_fade_req;  /* 请求渐出（由 sdgoods_audio_bgm_stop 设置） */
static volatile bool s_bgm_stopped;   /* BGM 任务已完全退出（PA/SPK 已关、I2S 空闲） */
static volatile int s_flap_rem;     /* 拍翅音效剩余采样数（>0 时混合输出） */
static float        s_flap_ph;      /* 拍翅音效相位累加器 */
static bool         s_spk_inited;   /* 扬声器硬件（PA GPIO + I2S 通道）是否已初始化 */
static volatile int s_vol_pct = 70; /* 全局音量（0~100，默认 70）；volatile：UI 线程改、BGM 任务读，防止编译器提升出循环 */

/* 三套 8-bit 风格旋律；lead = 主旋律，bass = 低音伴奏 */
static const float g_flappy_lead_f[BGM_NOTES] = {
    329.63f, 392.00f, 523.25f, 392.00f,
    329.63f, 440.00f, 523.25f, 440.00f,
    293.66f, 392.00f, 493.88f, 392.00f,
    523.25f, 392.00f, 329.63f, 523.25f
};
static const float g_flappy_bass_f[BGM_NOTES] = {
    130.81f, 196.00f, 130.81f, 196.00f,
    130.81f, 196.00f, 130.81f, 196.00f,
    130.81f, 196.00f, 130.81f, 196.00f,
    130.81f, 196.00f, 130.81f, 196.00f
};

/* 飞机：E 小调激昂上行 */
static const float g_plane_lead_f[BGM_NOTES] = {
    329.63f, 329.63f, 392.00f, 440.00f,
    493.88f, 493.88f, 440.00f, 392.00f,
    659.25f, 587.33f, 493.88f, 440.00f,
    392.00f, 440.00f, 493.88f, 659.25f
};
static const float g_plane_bass_f[BGM_NOTES] = {
    82.41f, 123.47f, 82.41f, 123.47f,
    82.41f, 123.47f, 82.41f, 123.47f,
    82.41f, 123.47f, 82.41f, 123.47f,
    82.41f, 123.47f, 82.41f, 123.47f
};

/* 俄罗斯方块：Korobeiniki 经典片段简化 */
static const float g_tetris_lead_f[BGM_NOTES] = {
    329.63f, 246.94f, 261.63f, 293.66f,
    261.63f, 246.94f, 220.00f, 220.00f,
    261.63f, 329.63f, 293.66f, 261.63f,
    246.94f, 261.63f, 293.66f, 329.63f
};
static const float g_tetris_bass_f[BGM_NOTES] = {
    82.41f, 82.41f, 82.41f, 82.41f,
    82.41f, 82.41f, 82.41f, 82.41f,
    65.41f, 65.41f, 65.41f, 65.41f,
    65.41f, 65.41f, 65.41f, 65.41f
};

static void pa_set(bool on)
{
    const int want = on ? BOARD_AUDIO_PA_EN_ACTIVE_LEVEL : (1 - BOARD_AUDIO_PA_EN_ACTIVE_LEVEL);
    gpio_set_level(BOARD_AUDIO_PA_EN_GPIO, want);
}

static inline int16_t sat16(int32_t s)
{
    if (s > 32767) {
        return 32767;
    }
    if (s < -32768) {
        return -32768;
    }
    return (int16_t)s;
}

static void rf_quiet_for_mic(bool quiet)
{
    if (quiet) {
        (void)esp_wifi_stop();
    } else {
        (void)esp_wifi_start();
    }
}

static int32_t isqrt64(int64_t x)
{
    if (x <= 0) {
        return 0;
    }
    int64_t y = x;
    for (int k = 0; k < 10; k++) {
        y = (y + x / y) / 2;
        if (y <= 0) {
            return 0;
        }
    }
    return (int32_t)y;
}

static esp_err_t mic_ensure(void)
{
    if (s_rx) {
        return ESP_OK;
    }
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    chan.dma_desc_num = 8;
    chan.dma_frame_num = 256;
    esp_err_t err = i2s_new_channel(&chan, NULL, &s_rx);
    if (err != ESP_OK) {
        return err;
    }
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE_HZ),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = BOARD_MIC_I2S_GPIO_BCLK,
            .ws = BOARD_MIC_I2S_GPIO_WS,
            .dout = GPIO_NUM_NC,
            .din = BOARD_MIC_I2S_GPIO_DIN,
            .invert_flags = { false, false, false },
        },
    };
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    err = i2s_channel_init_std_mode(s_rx, &std);
    if (err != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return err;
    }
    return ESP_OK;
}

static esp_err_t spk_ensure(void)
{
    if (s_tx) {
        return ESP_OK;
    }
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    chan.dma_desc_num = 6;
    chan.dma_frame_num = 240;
    esp_err_t err = i2s_new_channel(&chan, &s_tx, NULL);
    if (err != ESP_OK) {
        return err;
    }
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE_HZ),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_SPK_I2S_GPIO_MCLK,
            .bclk = BOARD_SPK_I2S_GPIO_BCLK,
            .ws = BOARD_SPK_I2S_GPIO_WS,
            .dout = BOARD_SPK_I2S_GPIO_DOUT,
            .din = GPIO_NUM_NC,
            .invert_flags = { false, false, false },
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    err = i2s_channel_init_std_mode(s_tx, &std);
    if (err != ESP_OK) {
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }
    return ESP_OK;
}

static void mic_set(bool on)
{
    if (!s_rx || s_rx_on == on) {
        return;
    }
    if (on) {
        (void)i2s_channel_enable(s_rx);
    } else {
        (void)i2s_channel_disable(s_rx);
    }
    s_rx_on = on;
}

static void spk_set(bool on)
{
    if (!s_tx || s_tx_on == on) {
        return;
    }
    if (on) {
        (void)i2s_channel_enable(s_tx);
    } else {
        (void)i2s_channel_disable(s_tx);
    }
    s_tx_on = on;
}

static void write_silence_ms(uint32_t ms)
{
    if (!s_tx_on || ms == 0) {
        return;
    }
    memset(s_silence, 0, sizeof(s_silence));
    const size_t frames = (RATE_HZ * ms) / 1000u;
    size_t left = frames;
    while (left > 0 && !s_abort) {
        size_t n = left > 64 ? 64 : left;
        size_t bw = 0;
        (void)i2s_channel_write(s_tx, s_silence, n * 2 * sizeof(int16_t), &bw, pdMS_TO_TICKS(200));
        left -= n;
    }
}

static void pcm_remove_dc(void)
{
    if (s_pcm_n == 0) {
        return;
    }
    int64_t sum = 0;
    for (size_t i = 0; i < s_pcm_n; i++) {
        sum += s_pcm[i];
    }
    const int16_t dc = (int16_t)(sum / (int64_t)s_pcm_n);
    if (dc == 0) {
        return;
    }
    for (size_t i = 0; i < s_pcm_n; i++) {
        s_pcm[i] = sat16((int32_t)s_pcm[i] - dc);
    }
}

static void pcm_hpf(void)
{
    if (s_pcm_n == 0) {
        return;
    }
    int32_t x1 = s_pcm[0];
    int32_t y1 = 0;
    s_pcm[0] = 0;
    for (size_t i = 1; i < s_pcm_n; i++) {
        const int32_t x0 = s_pcm[i];
        const int32_t y0 = (HPF_A_Q15 * (y1 + x0 - x1)) >> 15;
        s_pcm[i] = sat16(y0);
        x1 = x0;
        y1 = y0;
    }
}

static void pcm_normalize_body(void)
{
    size_t skip = (RATE_HZ * LEVEL_SKIP_MS) / 1000u;
    if (skip + 1000u > s_pcm_n) {
        skip = s_pcm_n / 5u;
    }
    int64_t sum_sq = 0;
    int32_t peak = 0;
    size_t n = 0;
    for (size_t i = skip; i < s_pcm_n; i++) {
        const int32_t s = s_pcm[i];
        const int32_t a = s < 0 ? -s : s;
        if (a > peak) {
            peak = a;
        }
        sum_sq += (int64_t)s * (int64_t)s;
        n++;
    }
    if (n == 0 || peak < 40) {
        return;
    }
    const int32_t rms = isqrt64(sum_sq / (int64_t)n);
    int32_t gain_q10 = (rms > 0) ? ((NORM_RMS << 10) / rms) : 1024;
    if (gain_q10 > GAIN_Q10_MAX) {
        gain_q10 = GAIN_Q10_MAX;
    }
    if (gain_q10 < 512) {
        gain_q10 = 512;
    }
    if (peak > 0) {
        const int32_t max_g = (20000 << 10) / peak;
        if (gain_q10 > max_g) {
            gain_q10 = max_g;
        }
    }
    for (size_t i = 0; i < s_pcm_n; i++) {
        s_pcm[i] = sat16(((int32_t)s_pcm[i] * gain_q10) >> 10);
    }
}

static void pcm_fade_in(void)
{
    const size_t fade = (RATE_HZ * FADE_MS) / 1000u;
    if (fade == 0 || fade > s_pcm_n) {
        return;
    }
    for (size_t i = 0; i < fade; i++) {
        s_pcm[i] = (int16_t)(((int32_t)s_pcm[i] * (int32_t)i) / (int32_t)fade);
    }
}

static void pcm_fade_out(void)
{
    const size_t fade = (RATE_HZ * FADE_MS) / 1000u;
    if (fade == 0 || fade > s_pcm_n) {
        return;
    }
    for (size_t i = 0; i < fade; i++) {
        const size_t idx = s_pcm_n - fade + i;
        s_pcm[idx] = (int16_t)(((int32_t)s_pcm[idx] * (int32_t)(fade - 1 - i)) / (int32_t)fade);
    }
}

/* 扬声器硬件初始化：PA 功放 GPIO + I2S 发送通道（幂等，可被录音/BGM 共用） */
static void spk_hw_init(void)
{
    if (s_spk_inited) {
        return;
    }
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_AUDIO_PA_EN_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    pa_set(false);
    spk_ensure();
    s_spk_inited = true;
}

/* 预生成一段循环 BGM 到 PSRAM（方波主旋律 + 低音，带轻 ADSR 包络消除爆音） */
static esp_err_t gen_bgm(audio_bgm_theme_t theme)
{
    if (s_bgm && s_bgm_theme == theme) {
        return ESP_OK;
    }
    if (s_bgm) {
        heap_caps_free(s_bgm);
        s_bgm = NULL;
        s_bgm_frames = 0;
    }
    s_bgm_theme = theme;
    const float *lead_f = g_flappy_lead_f;
    const float *bass_f = g_flappy_bass_f;
    switch (theme) {
    case AUDIO_BGM_THEME_PLANE:
        lead_f = g_plane_lead_f;
        bass_f = g_plane_bass_f;
        break;
    case AUDIO_BGM_THEME_TETRIS:
        lead_f = g_tetris_lead_f;
        bass_f = g_tetris_bass_f;
        break;
    case AUDIO_BGM_THEME_FLAPPY:
    default:
        lead_f = g_flappy_lead_f;
        bass_f = g_flappy_bass_f;
        break;
    }

    const size_t frames = BGM_NOTES * ((BGM_RATE * BGM_BEAT_MS) / 1000u);
    s_bgm = heap_caps_malloc(frames * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_bgm) {
        return ESP_ERR_NO_MEM;
    }
    s_bgm_frames = frames;

    const float at  = (BGM_RATE * 12u) / 1000.0f;   /* 起音 12ms */
    const float rel = (BGM_RATE * 30u) / 1000.0f;   /* 释音 30ms */
    float lead_ph = 0.0f;
    float bass_ph = 0.0f;
    const size_t beat = (BGM_RATE * BGM_BEAT_MS) / 1000u;

    for (int n = 0; n < BGM_NOTES; n++) {
        for (size_t s = 0; s < beat; s++) {
            lead_ph += 2.0f * 3.14159265f * lead_f[n] / (float)BGM_RATE;
            if (lead_ph > 3.14159265f * 2.0f) {
                lead_ph -= 3.14159265f * 2.0f;
            }
            bass_ph += 2.0f * 3.14159265f * bass_f[n] / (float)BGM_RATE;
            if (bass_ph > 3.14159265f * 2.0f) {
                bass_ph -= 3.14159265f * 2.0f;
            }
            float env = 1.0f;
            if (s < at) {
                env = (float)s / at;
            } else if (s > beat - rel) {
                env = (float)(beat - s) / rel;
            }
            const float lead = (sinf(lead_ph) >= 0.0f) ? 1.0f : -1.0f;
            const float bass = (sinf(bass_ph) >= 0.0f) ? 1.0f : -1.0f;
            int32_t v = (int32_t)(lead * 5000.0f * env + bass * 3000.0f * env);
            if (v > 32767) {
                v = 32767;
            }
            if (v < -32768) {
                v = -32768;
            }
            const int16_t sv = (int16_t)v;
            s_bgm[n * beat * 2 + s * 2]     = sv;
            s_bgm[n * beat * 2 + s * 2 + 1] = sv;
        }
    }
    return ESP_OK;
}

/* BGM 播放任务：循环读 BGM 缓冲，并实时混合“拍翅”音效，写入 I2S */
static void bgm_task(void *arg)
{
    (void)arg;
    size_t p = 0;
    const int flap_n = (int)((BGM_RATE * BGM_FLAP_MS) / 1000u);
    int16_t buf[256 * 2];
    bool fading = false;
    unsigned fade_idx = 0;

    while (s_bgm_run) {
        /* 收到停止请求 -> 进入渐出：音量按块线性降到 0，避免直接静音/关功放产生爆音 */
        if (s_bgm_fade_req) {
            s_bgm_fade_req = false;
            fading = true;
            fade_idx = 0;
        }
        int gain = 100;
        if (fading) {
            fade_idx++;
            if (fade_idx > BGM_FADE_STEPS) {
                break;   /* 已降到 0，退出循环做收尾 */
            }
            gain = (int)(100u * (BGM_FADE_STEPS - fade_idx) / BGM_FADE_STEPS);
        }

        for (size_t i = 0; i < 256; i++) {
            if (p >= s_bgm_frames) {
                p = 0;
            }
            int32_t l = s_bgm[p * 2];
            int32_t r = s_bgm[p * 2 + 1];
            p++;

            int32_t flap = 0;
            if (s_flap_rem > 0) {
                const float t = 1.0f - (float)s_flap_rem / (float)flap_n;   /* 0→1 频率上扫 */
                const float f = 500.0f + 500.0f * t;
                s_flap_ph += 2.0f * 3.14159265f * f / (float)BGM_RATE;
                if (s_flap_ph > 3.14159265f * 2.0f) {
                    s_flap_ph -= 3.14159265f * 2.0f;
                }
                const float e = (float)s_flap_rem / (float)flap_n;          /* 指数衰减包络 */
                flap = (int32_t)(sinf(s_flap_ph) * e * 6500.0f);
                s_flap_rem--;
            }

            /* 真正按全局音量缩放：之前写死 *7/10（70%），导致调音量无效 */
            const int vol = s_vol_pct;   /* 读一次 volatile（0=静音；背景音与拍翅音效同步缩放） */
            int32_t ol = (int32_t)((int32_t)l * vol / 100) + (int32_t)((int32_t)flap * vol / 100);
            int32_t or = (int32_t)((int32_t)r * vol / 100) + (int32_t)((int32_t)flap * vol / 100);
            ol = ol * gain / 100;   /* 渐出增益 */
            or = or * gain / 100;
            if (ol > 32767) {
                ol = 32767;
            } else if (ol < -32768) {
                ol = -32768;
            }
            if (or > 32767) {
                or = 32767;
            } else if (or < -32768) {
                or = -32768;
            }
            buf[i * 2]     = (int16_t)ol;
            buf[i * 2 + 1] = (int16_t)or;
        }
        size_t bw = 0;
        (void)i2s_channel_write(s_tx, buf, sizeof(buf), &bw, pdMS_TO_TICKS(200));
    }

    /* 收尾：补一段静音把 DMA 里残留的波形冲干净，再关功放，杜绝断电“啪”声 */
    memset(buf, 0, sizeof(buf));
    {
        size_t bw = 0;
        (void)i2s_channel_write(s_tx, buf, sizeof(buf), &bw, pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(8));

    pa_set(false);
    spk_set(false);
    s_bgm_run = false;
    s_bgm_stopped = true;
    s_bgm_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t sdgoods_audio_bgm_start(audio_bgm_theme_t theme)
{
    spk_hw_init();
    if (!s_tx) {
        return ESP_FAIL;
    }
    if (gen_bgm(theme) != ESP_OK) {
        return ESP_FAIL;
    }
    if (s_bgm_run) {
        return ESP_OK;
    }
    s_bgm_run = true;
    s_bgm_fade_req = false;
    s_bgm_stopped = false;
    s_flap_rem = 0;
    s_flap_ph = 0.0f;
    spk_set(true);
    pa_set(true);
    if (xTaskCreate(bgm_task, "bgm", 2048, NULL, 5, &s_bgm_task) != pdPASS) {
        s_bgm_run = false;
        pa_set(false);
        spk_set(false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void sdgoods_audio_bgm_stop(void)
{
    if (!s_bgm_run && !s_bgm_task) {
        if (s_spk_inited) {     /* 硬件未初始化时不要动 PA GPIO */
            pa_set(false);      /* 未在运行：确保功放/扬声器处于关闭态 */
            spk_set(false);
        }
        return;
    }
    s_bgm_fade_req = true;  /* 请求渐出，任务降到 0 后自行关功放并退出 */

    /* 等待任务真正退出（最长 500ms），保证返回后 I2S 发送通道空闲，
       录音/回放路径随后复用该通道不会发生竞争。用 vTaskDelay 让出 CPU。 */
    const TickType_t t0 = xTaskGetTickCount();
    while (!s_bgm_stopped && (xTaskGetTickCount() - t0) < pdMS_TO_TICKS(500)) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (!s_bgm_stopped) {
        s_bgm_run = false;              /* 超时兜底 */
        vTaskDelay(pdMS_TO_TICKS(40));
        pa_set(false);
        spk_set(false);
    }
    s_bgm_stopped = false;
}

void sdgoods_audio_sfx_flap(void)
{
    if (!s_bgm_run) {
        return;   /* 仅在 BGM 运行时混合输出 */
    }
    s_flap_rem = (int)((BGM_RATE * BGM_FLAP_MS) / 1000u);
    s_flap_ph = 0.0f;
}

/* 设置/读取全局音量（0~100）。背景音与拍翅音效都按此比例缩放；0 = 静音 */
void sdgoods_audio_set_volume(int pct)
{
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    s_vol_pct = pct;
}

int sdgoods_audio_get_volume(void)
{
    return s_vol_pct;
}

esp_err_t sdgoods_audio_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    spk_hw_init();

    s_pcm = heap_caps_malloc(SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pcm) {
        return ESP_ERR_NO_MEM;
    }
    memset(s_pcm, 0, SAMPLES * sizeof(int16_t));

    ESP_ERROR_CHECK(mic_ensure());
    s_rx_on = false;
    s_tx_on = false;

    s_ready = true;
    return ESP_OK;
}

void sdgoods_audio_abort(void)
{
    s_abort = true;
}

esp_err_t sdgoods_audio_record(void)
{
    if (!s_ready && sdgoods_audio_init() != ESP_OK) {
        return ESP_FAIL;
    }
    sdgoods_audio_bgm_stop();   /* 避免与游戏 BGM 抢占同一 I2S 发送通道 */
    s_abort = false;
    s_pcm_n = 0;

    pa_set(false);
    spk_set(false);
    rf_quiet_for_mic(true);
    mic_set(true);

    {
        const size_t drop = (RATE_HZ * WARMUP_MS) / 1000u;
        size_t got = 0;
        while (got < drop && !s_abort) {
            size_t br = 0;
            if (i2s_channel_read(s_rx, s_mic_raw, sizeof(s_mic_raw), &br, pdMS_TO_TICKS(500)) != ESP_OK || br == 0) {
                continue;
            }
            got += br / sizeof(int32_t);
        }
    }

    while (s_pcm_n < SAMPLES && !s_abort) {
        size_t br = 0;
        if (i2s_channel_read(s_rx, s_mic_raw, sizeof(s_mic_raw), &br, pdMS_TO_TICKS(1000)) != ESP_OK || br == 0) {
            continue;
        }
        const size_t n = br / sizeof(int32_t);
        for (size_t i = 0; i < n && s_pcm_n < SAMPLES; i++) {
            s_pcm[s_pcm_n++] = sat16(s_mic_raw[i] >> MIC_SHIFT);
        }
    }

    mic_set(false);
    rf_quiet_for_mic(false);
    if (s_abort) {
        return ESP_ERR_INVALID_STATE;
    }
    pcm_remove_dc();
    pcm_hpf();
    pcm_normalize_body();
    pcm_fade_in();
    pcm_fade_out();
    return ESP_OK;
}

esp_err_t sdgoods_audio_play(void)
{
    if (!s_ready || s_pcm_n == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    sdgoods_audio_bgm_stop();   /* 避免与游戏 BGM 抢占同一 I2S 发送通道 */
    s_abort = false;

    mic_set(false);
    
    spk_set(true);
    write_silence_ms(PREFILL_MS);
    pa_set(true);
    write_silence_ms(PA_SETTLE_MS);

    size_t off = 0;
    while (off < s_pcm_n && !s_abort) {
        size_t n = s_pcm_n - off;
        if (n > 256) {
            n = 256;
        }
        for (size_t i = 0; i < n; i++) {
            const int32_t s = ((int32_t)s_pcm[off + i] * PLAY_VOL) / 100;
            s_spk_stereo[i * 2] = (int16_t)s;
            s_spk_stereo[i * 2 + 1] = (int16_t)s;
        }
        const size_t need = n * 2 * sizeof(int16_t);
        size_t bw = 0;
        (void)i2s_channel_write(s_tx, s_spk_stereo, need, &bw, pdMS_TO_TICKS(500));
        off += n;
    }

    write_silence_ms(TAIL_MS);
    pa_set(false);
    write_silence_ms(20);
    spk_set(false);

    if (s_abort) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
