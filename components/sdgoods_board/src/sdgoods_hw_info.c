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

#include "sdgoods_hw_info.h"

#include "board_pins.h"
#include "driver/i2c.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_image_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hw_info";

#define QMI_ADDR     0x6B
#define QMI_CTRL1    0x02
#define QMI_CTRL2    0x03
#define QMI_CTRL3    0x04
#define QMI_CTRL7    0x08
#define QMI_GX_L     0x3B
#define GYRO_SCALE   (64.0f / 32768.0f)

static adc_oneshot_unit_handle_t s_adc;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
static adc_cali_handle_t s_cali;
#endif
static bool s_cali_ok;
static bool s_imu_ok;

static esp_err_t i2c_wr(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device((i2c_port_t)BOARD_I2C_PORT, QMI_ADDR,
                                      buf, 2, pdMS_TO_TICKS(50));
}

static esp_err_t i2c_rd(uint8_t reg, uint8_t *data, size_t n)
{
    return i2c_master_write_read_device((i2c_port_t)BOARD_I2C_PORT, QMI_ADDR,
                                        &reg, 1, data, n, pdMS_TO_TICKS(50));
}

esp_err_t sdgoods_hw_info_init(void)
{
    /* ⚠️ 两条硬约束（2026-09-19 改，都踩过）：
     *
     * ① **必须幂等**。现在有两个调用点会走到这里：
     *    · 平台层的应用外壳 sdgoods_app_shell_init()（让**每个** app 自动拿到电池读数）；
     *    · 部分固件自己的 app_main（启动器 launcher_main.c、app0 main.c 都显式调过）。
     *    旧实现用 `ESP_ERROR_CHECK(adc_oneshot_new_unit(...))`，第二次调用会因
     *    ESP_ERR_INVALID_STATE 直接 abort() ⇒ 开机即挂。所以先判 s_adc。
     *
     * ② **必须不致命**。ADC 起不来只影响「电压 / 电量百分比」这一项显示
     *    （sdgoods_hw_bat_v() 会返回 0.f，UI 显示 `Voltage --V`）。
     *    为一项装饰性读数把整机 abort 掉，代价完全不对等 —— 改成记日志 + 返回错误，
     *    调用方可以忽略。 */
    if (s_adc) {
        return ESP_OK;   /* 已初始化过：幂等返回，不重复建 ADC 单元 */
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s (battery will read 0.00V)",
                 esp_err_to_name(err));
        s_adc = NULL;
        return err;
    }
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, ADC_CHANNEL_7, &ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc config channel 7 (GPIO8) failed: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return err;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_7,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali, &s_cali) == ESP_OK);
#endif

    /* IMU 走 I2C：失败只意味着「陀螺仪不可用」（sdgoods_hw_gyro 返回 false），
     * 不报错也不阻断 —— 与电池一样属于「没它设备照样能用」的外设。 */
    s_imu_ok = (i2c_wr(QMI_CTRL1, 0x40) == ESP_OK)
               && (i2c_wr(QMI_CTRL2, 0x15) == ESP_OK)
               && (i2c_wr(QMI_CTRL3, 0x25) == ESP_OK)
               && (i2c_wr(QMI_CTRL7, 0x43) == ESP_OK);
    ESP_LOGI(TAG, "hw_info ready: adc=ok cali=%d imu=%d", (int)s_cali_ok, (int)s_imu_ok);
    return ESP_OK;
}

float sdgoods_hw_bat_v(void)
{
    int raw = 0;
    if (!s_adc || adc_oneshot_read(s_adc, ADC_CHANNEL_7, &raw) != ESP_OK) {
        return 0.f;
    }
    int mv = 0;
    if (s_cali_ok) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        (void)adc_cali_raw_to_voltage(s_cali, raw, &mv);
#else
        mv = (raw * 3300) / 4095;
#endif
    } else {
        mv = (raw * 3300) / 4095;
    }
    return (float)mv * 3.f / 1000.f;
}

bool sdgoods_hw_gyro(int *x, int *y, int *z)
{
    uint8_t b[6] = {0};
    if (!s_imu_ok || i2c_rd(QMI_GX_L, b, sizeof(b)) != ESP_OK) {
        return false;
    }
    const int16_t gx = (int16_t)((uint16_t)b[1] << 8 | b[0]);
    const int16_t gy = (int16_t)((uint16_t)b[3] << 8 | b[2]);
    const int16_t gz = (int16_t)((uint16_t)b[5] << 8 | b[4]);
    if (x) {
        *x = (int)(gx * GYRO_SCALE);
    }
    if (y) {
        *y = (int)(gy * GYRO_SCALE);
    }
    if (z) {
        *z = (int)(gz * GYRO_SCALE);
    }
    return true;
}

float sdgoods_hw_space_mb(void)
{
    /* 可用空间 = 当前运行 app(factory) 分区大小 - 已烧录固件镜像实际占用 */
    const esp_partition_t *run = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!run) {
        return 0.f;
    }
    esp_partition_pos_t pos = { .offset = run->address, .size = run->size };
    esp_image_metadata_t meta;
    if (esp_image_get_metadata(&pos, &meta) != ESP_OK) {
        return 0.f;
    }
    const uint32_t used = meta.image_len;   /* 当前固件镜像实际长度（含 hash、按扇区对齐） */
    if (run->size <= used) {
        return 0.f;
    }
    return (float)(run->size - used) / (1024.0f * 1024.0f);
}
