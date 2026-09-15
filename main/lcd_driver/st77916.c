#include "st77916.h"
#include "lvgl.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "soc/soc_caps.h"
#if CONFIG_SPIRAM && SOC_CACHE_WRITEBACK_SUPPORTED
#include "esp_cache.h"
#endif
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "lcd";

esp_lcd_panel_handle_t panel_handle;
static SemaphoreHandle_t s_panel_mutex;
static SemaphoreHandle_t s_spi_color_done_sem;
static ledc_channel_config_t s_ledc_ch;
static bool s_backlight_ready;
static uint8_t s_backlight_pct;

/* 异步 LVGL flush 完成回调需要通知 LVGL 哪块已刷好；由 lvgl_port 在 drv 注册后赋值。
 * 注意：开机清屏(lcd_clear_black)调用 draw_bitmap_safe 时本指针尚为 NULL，
 * 故其完成只会给同步信号量、不会误触发 lv_disp_flush_ready。 */
static lv_disp_drv_t *s_lvgl_drv = NULL;

void lcd_set_lvgl_drv(lv_disp_drv_t *drv)
{
    s_lvgl_drv = drv;
}

#include "st77916_vendor_init.inc"

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t hp = pdFALSE;
    /* 异步 LVGL flush：本块 DMA 完成，通知 LVGL 释放该缓冲给下次渲染
     * （此刻 CPU 已并行渲染另一块，渲染与传输重叠 -> 无串行卡顿）。 */
    if (s_lvgl_drv) {
        lv_disp_flush_ready(s_lvgl_drv);
        hp = pdTRUE;
    }
    /* 同步调用方（如开机清屏）等待完成信号 */
    if (s_spi_color_done_sem) {
        xSemaphoreGiveFromISR(s_spi_color_done_sem, &hp);
    }
    return hp == pdTRUE;
}

void lcd_panel_apply_vendor_madctl(void)
{
    if (!panel_handle) {
        return;
    }
    esp_err_t err = esp_lcd_panel_st77916_set_madctl(panel_handle, BOARD_ST77916_VENDOR_MADCTL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MADCTL 0x%02x: %s", BOARD_ST77916_VENDOR_MADCTL, esp_err_to_name(err));
    }
}

esp_err_t lcd_panel_draw_bitmap_safe(int x1, int y1, int x2, int y2, const void *color_data)
{
    if (!panel_handle || !color_data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_panel_mutex) {
        s_panel_mutex = xSemaphoreCreateMutex();
    }
    if (!s_spi_color_done_sem) {
        s_spi_color_done_sem = xSemaphoreCreateBinary();
    }
    if (xSemaphoreTake(s_panel_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

#if CONFIG_SPIRAM && SOC_CACHE_WRITEBACK_SUPPORTED
    if (esp_ptr_external_ram(color_data)) {
        size_t bytes = (size_t)(x2 - x1) * (size_t)(y2 - y1) * sizeof(uint16_t);
        (void)esp_cache_msync((void *)color_data, bytes,
                              ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
#endif

    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 12; i++) {
        err = esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, color_data);
        if (err == ESP_OK) {
            /* 同步等待本次传输真正完成（on_color_trans_done 仅在最后一个分片完成时给信号量）。
             * 关键：保证任意时刻只有“本事务”在飞，DMA 描述符池不会被整屏连续刷新的
             * 多个事务同时占满 -> 否则 setup_priv_desc 分配描述符失败返回 NO_MEM，
             * 整块区域被丢弃，表现为固定的水平黑带。 */
            if (s_spi_color_done_sem) {
                (void)xSemaphoreTake(s_spi_color_done_sem, 0);   /* 先清空可能残留的旧信号 */
                if (xSemaphoreTake(s_spi_color_done_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
                    ESP_LOGW(TAG, "draw_bitmap done sem timeout x:%d y:%d", x1, y1);
                }
            }
            break;
        }
        if (err == ESP_ERR_NO_MEM) {
            /* 描述符池/队列暂满：让出 CPU 让 SPI DMA 把已排队事务跑完、腾出资源，再重试 */
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        ESP_LOGW(TAG, "draw_bitmap retry %d x:%d y:%d w:%d h:%d err=0x%x",
                 i, x1, y1, x2 - x1, y2 - y1, err);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap FAILED x:%d y:%d w:%d h:%d err=0x%x ext=%d bytes=%d",
                 x1, y1, x2 - x1, y2 - y1, err,
                 (int)esp_ptr_external_ram(color_data),
                 (int)((size_t)(x2 - x1) * (size_t)(y2 - y1) * 2));
    }
    xSemaphoreGive(s_panel_mutex);
    return err;
}

static void backlight_init(void)
{
    if (s_backlight_ready) {
        return;
    }
    ledc_timer_config_t timer = {
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 5000,
        .speed_mode = LEDC_LS_MODE,
        .timer_num = LEDC_HS_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    s_ledc_ch.gpio_num = BOARD_LCD_GPIO_BACKLIGHT;
    s_ledc_ch.speed_mode = LEDC_LS_MODE;
    s_ledc_ch.channel = LEDC_HS_CH0_CHANNEL;
    s_ledc_ch.intr_type = LEDC_INTR_DISABLE;
    s_ledc_ch.timer_sel = LEDC_HS_TIMER;
    s_ledc_ch.duty = 0;
    s_ledc_ch.hpoint = 0;
#if defined(BOARD_LCD_BACKLIGHT_LEDC_INVERT) && BOARD_LCD_BACKLIGHT_LEDC_INVERT
    s_ledc_ch.flags.output_invert = 1;
#endif
    ESP_ERROR_CHECK(ledc_channel_config(&s_ledc_ch));
    s_backlight_ready = true;
}

void Set_Backlight(uint8_t light)
{
    if (!s_backlight_ready) {
        backlight_init();
    }
    if (light > 100) {
        light = 100;
    }
    s_backlight_pct = light;
    uint32_t duty = (LEDC_MAX_Duty * light) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(s_ledc_ch.speed_mode, s_ledc_ch.channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(s_ledc_ch.speed_mode, s_ledc_ch.channel));
}

uint8_t Get_Backlight(void)
{
    return s_backlight_pct;
}

static void lcd_pow_on(void)
{
#if BOARD_LCD_POW_EN_GPIO >= 0
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_LCD_POW_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(BOARD_LCD_POW_EN_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
#endif
}

static void lcd_hw_reset(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_LCD_GPIO_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(BOARD_LCD_GPIO_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_LCD_GPIO_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static int lcd_qspi_bus_init(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    const int pins[] = {
        BOARD_LCD_SPI_IO_SCK, BOARD_LCD_SPI_IO_DATA0, BOARD_LCD_SPI_IO_DATA1,
        BOARD_LCD_SPI_IO_DATA2, BOARD_LCD_SPI_IO_DATA3, BOARD_LCD_SPI_IO_CS,
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        if (pins[i] >= 0) {
            gpio_reset_pin((gpio_num_t)pins[i]);
        }
    }
#endif

    spi_bus_config_t bus = {
        .data0_io_num = BOARD_LCD_SPI_IO_DATA0,
        .data1_io_num = BOARD_LCD_SPI_IO_DATA1,
        .sclk_io_num = BOARD_LCD_SPI_IO_SCK,
        .data2_io_num = BOARD_LCD_SPI_IO_DATA2,
        .data3_io_num = BOARD_LCD_SPI_IO_DATA3,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = LCD_SPI_MAX_TRANSFER_SIZE,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    if (spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed");
        return 0;
    }

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_SPI_IO_CS,
        .dc_gpio_num = -1,
        .spi_mode = LCD_SPI_MODE,
        .pclk_hz = BOARD_LCD_SPI_PCLK_HZ,
        .trans_queue_depth = LCD_SPI_TRANS_QUEUE_SZ,
        .lcd_cmd_bits = LCD_SPI_CMD_BITS,
        .lcd_param_bits = LCD_SPI_PARAM_BITS,
        .flags = {
            .quad_mode = 1,
        },
    };
    esp_lcd_panel_io_handle_t io = NULL;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io) != ESP_OK) {
        ESP_LOGE(TAG, "panel_io failed");
        return 0;
    }

    if (!s_spi_color_done_sem) {
        s_spi_color_done_sem = xSemaphoreCreateBinary();
    }
    esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = on_color_done };
    (void)esp_lcd_panel_io_register_event_callbacks(io, &cbs, NULL);

    st77916_vendor_config_t vendor = {
        .init_cmds = vendor_specific_init_vendor,
        .init_cmds_size = sizeof(vendor_specific_init_vendor) / sizeof(vendor_specific_init_vendor[0]),
        .flags = {
            .use_qspi_interface = 1,
            .init_madctl_valid = 1,
        },
        .init_madctl = BOARD_ST77916_VENDOR_MADCTL,
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_GPIO_RST,
        .rgb_ele_order = BOARD_LCD_RGB_ORDER_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_COLOR_BITS,
        .vendor_config = &vendor,
    };
    if (esp_lcd_new_panel_st77916(io, &panel_cfg, &panel_handle) != ESP_OK) {
        ESP_LOGE(TAG, "new_panel_st77916 failed");
        return 0;
    }

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    lcd_panel_apply_vendor_madctl();
    esp_lcd_panel_disp_on_off(panel_handle, true);
    return 1;
}

static void lcd_clear_black(void)
{
    const size_t pixels = (size_t)LCD_WIDTH * LCD_HEIGHT;
    uint16_t *buf = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < pixels; i++) {
        buf[i] = 0;
    }
#if CONFIG_SPIRAM && SOC_CACHE_WRITEBACK_SUPPORTED
    (void)esp_cache_msync(buf, pixels * sizeof(uint16_t),
                          ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
#endif
    (void)lcd_panel_draw_bitmap_safe(0, 0, LCD_WIDTH, LCD_HEIGHT, buf);
    heap_caps_free(buf);
}

void LCD_Init_Panel(void)
{
    lcd_pow_on();
    lcd_hw_reset();
    if (!lcd_qspi_bus_init()) {
        ESP_LOGE(TAG, "LCD init failed");
        return;
    }
    lcd_clear_black();
    backlight_init();
    Set_Backlight(0);
}
